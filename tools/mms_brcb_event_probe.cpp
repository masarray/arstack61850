// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/live_discovery.hpp"
#include "ariec61850/mms/reporting.hpp"
#include "ariec61850/mms/services.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace mms = ar::iec61850::mms;

[[nodiscard]] std::size_t parse_size(
    const std::string& option,
    const std::string& text,
    const std::size_t maximum) {
    std::size_t consumed{};
    const auto value = std::stoull(text, &consumed, 10);
    if (consumed != text.size() || value == 0U || value > maximum) {
        throw std::invalid_argument(option + " is outside the supported range.");
    }
    return static_cast<std::size_t>(value);
}

[[nodiscard]] std::span<const std::uint8_t> response_payload(
    const mms::MmsConfirmedExchangeResult& exchange) {
    return exchange.presentation_payload.empty()
        ? exchange.envelope.mms_payload
        : std::span<const std::uint8_t>{exchange.presentation_payload};
}

void require_boolean_write(
    mms::MmsAssociationRuntime& association,
    const std::string& domain,
    const std::string& item,
    const bool value) {
    const auto invoke_id = association.next_invoke_id();
    mms::MmsWriteRequest request;
    request.invoke_id = invoke_id;
    request.variables.push_back(mms::MmsObjectName::domain_specific(domain, item));
    request.values.push_back(mms::MmsDataValue::boolean(value));

    const auto encoded = mms::MmsServiceCodec::encode_write_request_p_data(
        request, association.negotiated().presentation_context_id);
    const auto exchange = association.exchange_confirmed(encoded, invoke_id);
    if (exchange.envelope.kind != mms::MmsPduKind::confirmed_response) {
        throw std::runtime_error("BRCB Boolean write did not return Confirmed-Response.");
    }
    const auto response = mms::MmsServiceCodec::decode_write_response(
        response_payload(exchange), invoke_id);
    if (response.results.size() != 1U || !response.results.front().success) {
        const std::optional<std::uint32_t> failure = response.results.size() == 1U
            ? response.results.front().failure_code
            : std::nullopt;
        throw std::runtime_error(
            "BRCB Boolean write was rejected" +
            (failure ? " (DataAccessError=" + std::to_string(*failure) + ")." : "."));
    }
}

void print_usage() {
    std::cout
        << "Usage: ariec61850_mms_brcb_event_probe <host> [port] --domain NAME --rcb ITEM [options]\n\n"
        << "Options:\n"
        << "  --timeout-ms N  Connect/request/report timeout (default 7000).\n"
        << "  -h, --help      Show this help.\n\n"
        << "The probe enables one BRCB, announces readiness, then requires an\n"
        << "unsolicited buffered InformationReport with a non-empty EntryID.\n";
}

} // namespace

int main(const int argc, char** argv) {
    try {
        if (argc < 2 || std::string_view{argv[1]} == "--help" ||
            std::string_view{argv[1]} == "-h") {
            print_usage();
            return argc < 2 ? 2 : 0;
        }

        mms::MmsEndpoint endpoint;
        endpoint.host = argv[1];
        endpoint.port = 102U;
        int argument = 2;
        if (argument < argc && std::string_view{argv[argument]}.rfind("--", 0U) != 0U) {
            endpoint.port = static_cast<std::uint16_t>(parse_size(
                "port", argv[argument++], 65'535U));
        }

        std::string domain;
        std::string rcb;
        std::chrono::milliseconds timeout{7'000};
        while (argument < argc) {
            const std::string option = argv[argument++];
            if (option == "--help" || option == "-h") {
                print_usage();
                return 0;
            }
            if (argument >= argc) {
                throw std::invalid_argument(option + " requires a value.");
            }
            const std::string value = argv[argument++];
            if (option == "--domain") {
                domain = value;
            } else if (option == "--rcb") {
                rcb = value;
            } else if (option == "--timeout-ms") {
                timeout = std::chrono::milliseconds{static_cast<std::int64_t>(
                    parse_size(option, value, 120'000U))};
            } else {
                throw std::invalid_argument("Unknown option: " + option);
            }
        }
        if (domain.empty() || rcb.empty()) {
            throw std::invalid_argument("--domain and --rcb are required.");
        }

        mms::MmsAssociationOptions association_options;
        association_options.connect_timeout = timeout;
        association_options.request_timeout = timeout;
        mms::MmsTcpLiveDiscoverySession session{{}, association_options};
        session.connect(endpoint);

        const auto rpt_ena = rcb + "$RptEna";
        require_boolean_write(session.association(), domain, rpt_ena, true);
        std::cout << "MMS_BRCB_EVENT_READY reference=" << domain << '/' << rcb << '\n';
        std::cout.flush();

        std::vector<std::uint8_t> report_payload;
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline && report_payload.empty()) {
            if (session.association().try_pop_information_report(report_payload)) break;
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            if (remaining <= std::chrono::milliseconds::zero()) break;
            mms::MmsPduEnvelope envelope;
            static_cast<void>(session.association().try_poll_once_for(
                std::min(remaining, std::chrono::milliseconds{500}), envelope));
            static_cast<void>(session.association().try_pop_information_report(report_payload));
        }
        if (report_payload.empty()) {
            throw std::runtime_error("BRCB did not produce an MMS InformationReport.");
        }

        mms::MmsInformationReport report;
        std::string decode_error;
        if (!mms::MmsInformationReportCodec::try_decode(
                report_payload, report, &decode_error)) {
            throw std::runtime_error(
                "BRCB InformationReport could not be decoded: " + decode_error);
        }
        const auto header = mms::MmsReportFrameMapper::decode_header(report);
        if (header.report_id.empty()) {
            throw std::runtime_error("BRCB InformationReport has no ReportID.");
        }
        if (header.entry_id.empty()) {
            throw std::runtime_error("BRCB InformationReport has no EntryID.");
        }
        if (!header.optional_fields.has("entry-id")) {
            throw std::runtime_error("BRCB InformationReport OptFlds does not select EntryID.");
        }

        require_boolean_write(session.association(), domain, rpt_ena, false);
        session.disconnect();
        std::cout << "MMS_BRCB_EVENT_PASS reference=" << domain << '/' << rcb
                  << " rptid=" << header.report_id
                  << " entry_id_bytes=" << header.entry_id.size()
                  << " access_results=" << report.items.size() << '\n';
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "MMS BRCB event probe failed: " << exception.what() << '\n';
        return 1;
    }
}
