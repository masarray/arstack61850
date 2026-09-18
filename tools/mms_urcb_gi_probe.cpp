// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/live_discovery.hpp"
#include "ariec61850/mms/reporting.hpp"
#include "ariec61850/mms/services.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
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

[[nodiscard]] std::string_view data_kind_name(const mms::MmsDataKind kind) noexcept {
    switch (kind) {
    case mms::MmsDataKind::array: return "array";
    case mms::MmsDataKind::structure: return "structure";
    case mms::MmsDataKind::boolean: return "boolean";
    case mms::MmsDataKind::bit_string: return "bit-string";
    case mms::MmsDataKind::integer: return "integer";
    case mms::MmsDataKind::unsigned_integer: return "unsigned";
    case mms::MmsDataKind::floating_point: return "floating-point";
    case mms::MmsDataKind::octet_string: return "octet-string";
    case mms::MmsDataKind::visible_string: return "visible-string";
    case mms::MmsDataKind::binary_time: return "binary-time";
    case mms::MmsDataKind::bcd: return "bcd";
    case mms::MmsDataKind::boolean_array: return "boolean-array";
    case mms::MmsDataKind::mms_string: return "mms-string";
    case mms::MmsDataKind::utc_time: return "utc-time";
    case mms::MmsDataKind::unknown: return "unknown";
    }
    return "unknown";
}

[[nodiscard]] std::string data_shape(const mms::MmsDataValue& value) {
    std::string result{data_kind_name(value.kind())};
    if (value.kind() != mms::MmsDataKind::array &&
        value.kind() != mms::MmsDataKind::structure) {
        return result;
    }
    result.push_back('(');
    for (std::size_t index = 0U; index < value.children().size(); ++index) {
        if (index != 0U) result.push_back(',');
        result += data_shape(value.children()[index]);
    }
    result.push_back(')');
    return result;
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
        throw std::runtime_error("RCB Boolean write did not return Confirmed-Response.");
    }
    const auto response = mms::MmsServiceCodec::decode_write_response(
        response_payload(exchange), invoke_id);
    if (response.results.size() != 1U || !response.results.front().success) {
        const auto failure = response.results.size() == 1U
            ? response.results.front().failure_code
            : std::nullopt;
        throw std::runtime_error(
            "RCB Boolean write was rejected" +
            (failure ? " (DataAccessError=" + std::to_string(*failure) + ")." : "."));
    }
}

void print_usage() {
    std::cout
        << "Usage: ariec61850_mms_urcb_gi_probe <host> [port] --domain NAME --rcb ITEM [options]\n\n"
        << "Options:\n"
        << "  --timeout-ms N  Connect/request/report timeout (default 5000).\n"
        << "  -h, --help      Show this help.\n\n"
        << "The probe enables one URCB, requests GI on the same association, and\n"
        << "requires an unsolicited MMS InformationReport before disabling it.\n";
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
        std::chrono::milliseconds timeout{5'000};
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
        const auto gi = rcb + "$GI";
        require_boolean_write(session.association(), domain, rpt_ena, true);
        require_boolean_write(session.association(), domain, gi, true);

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
            throw std::runtime_error("GI did not produce an MMS InformationReport.");
        }

        mms::MmsInformationReport report;
        std::string decode_error;
        if (!mms::MmsInformationReportCodec::try_decode(
                report_payload, report, &decode_error)) {
            throw std::runtime_error(
                "GI InformationReport could not be decoded: " + decode_error);
        }
        if (report.items.empty()) {
            throw std::runtime_error("GI InformationReport contains no AccessResults.");
        }
        const auto frame = mms::MmsReportFrameMapper::map(report, {});
        if (frame.header.report_id.empty()) {
            throw std::runtime_error("GI InformationReport has no ReportID.");
        }
        if (frame.values.empty() || !frame.values.front().value) {
            throw std::runtime_error("GI InformationReport contains no process values.");
        }
        const auto first_value_shape = data_shape(*frame.values.front().value);
        const auto first_value_display =
            mms::MmsDataCodec::to_display_string(*frame.values.front().value);
        const auto uniform_value_shape = std::all_of(
            frame.values.begin(), frame.values.end(),
            [&first_value_shape](const mms::MmsReportValue& value) {
                return value.value.has_value() &&
                    data_shape(*value.value) == first_value_shape;
            });
        if (!uniform_value_shape) {
            throw std::runtime_error(
                "GI InformationReport process values do not share one MMS shape.");
        }

        require_boolean_write(session.association(), domain, rpt_ena, false);
        session.disconnect();
        std::cout << "MMS_URCB_GI_PASS reference=" << domain << '/' << rcb
                  << " rptid=" << frame.header.report_id
                  << " access_results=" << report.items.size()
                  << " report_values=" << frame.values.size()
                  << " first_value_shape=" << first_value_shape
                  << " first_value=" << first_value_display
                  << " uniform_value_shape=true" << '\n';
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "MMS URCB GI probe failed: " << exception.what() << '\n';
        return 1;
    }
}
