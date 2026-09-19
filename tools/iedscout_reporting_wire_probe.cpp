// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/live_discovery.hpp"
#include "ariec61850/mms/reporting.hpp"
#include "ariec61850/mms/services.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace mms = ar::iec61850::mms;

enum class Mode : std::uint8_t { urcb, brcb };

struct Options final {
    mms::MmsEndpoint endpoint;
    Mode mode{Mode::urcb};
    std::string domain;
    std::string rcb;
    std::string expected_rptid;
    std::string expected_dataset;
    std::size_t expected_members{};
    std::uint32_t expected_conf_rev{};
    std::uint8_t expected_opt_first{};
    std::uint8_t expected_opt_second{};
    std::chrono::milliseconds timeout{5'000};
};

[[nodiscard]] std::size_t parse_size(
    const std::string& option,
    const std::string& text,
    const std::size_t maximum) {
    std::size_t consumed{};
    const auto value = std::stoull(text, &consumed, 0);
    if (consumed != text.size() || value > maximum) {
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

void require_write(
    mms::MmsAssociationRuntime& association,
    std::vector<mms::MmsObjectName> variables,
    std::vector<mms::MmsDataValue> values,
    const std::string_view label) {
    const auto invoke_id = association.next_invoke_id();
    mms::MmsWriteRequest request;
    request.invoke_id = invoke_id;
    request.variables = std::move(variables);
    request.values = std::move(values);
    const auto encoded = mms::MmsServiceCodec::encode_write_request_p_data(
        request, association.negotiated().presentation_context_id);
    const auto exchange = association.exchange_confirmed(encoded, invoke_id);
    if (exchange.envelope.kind != mms::MmsPduKind::confirmed_response) {
        throw std::runtime_error(
            std::string{label} + " did not return Confirmed-Response.");
    }
    const auto response = mms::MmsServiceCodec::decode_write_response(
        response_payload(exchange), invoke_id);
    if (response.results.size() != request.variables.size() ||
        !response.all_success()) {
        std::string detail;
        for (std::size_t index = 0U; index < response.results.size(); ++index) {
            if (response.results[index].success) continue;
            detail += " result[" + std::to_string(index) + "]=" +
                (response.results[index].failure_code
                    ? std::to_string(*response.results[index].failure_code)
                    : std::string{"unknown"});
        }
        throw std::runtime_error(
            std::string{label} + " was rejected." + detail);
    }
}

void require_boolean_write(
    mms::MmsAssociationRuntime& association,
    const std::string& domain,
    const std::string& item,
    const bool value,
    const std::string_view label) {
    require_write(
        association,
        {mms::MmsObjectName::domain_specific(domain, item)},
        {mms::MmsDataValue::boolean(value)},
        label);
}

[[nodiscard]] std::vector<std::uint8_t> wait_report(
    mms::MmsAssociationRuntime& association,
    const std::chrono::milliseconds timeout) {
    std::vector<std::uint8_t> payload;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (association.try_pop_information_report(payload)) return payload;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining <= std::chrono::milliseconds::zero()) break;
        mms::MmsPduEnvelope envelope;
        static_cast<void>(association.try_poll_once_for(
            std::min(remaining, std::chrono::milliseconds{250}), envelope));
    }
    if (association.try_pop_information_report(payload)) return payload;
    throw std::runtime_error("RCB GI did not produce InformationReport.");
}

[[nodiscard]] bool exact_optflds(
    const mms::MmsReportBitField& field,
    const std::uint8_t first,
    const std::uint8_t second) {
    return field.raw == std::vector<std::uint8_t>{6U, first, second};
}

[[nodiscard]] bool all_gi(const mms::MmsReportFrame& frame) {
    return !frame.values.empty() &&
        std::all_of(
            frame.values.begin(), frame.values.end(),
            [](const mms::MmsReportValue& value) {
                return value.reason_for_inclusion.has("general-interrogation");
            });
}

[[nodiscard]] bool full_inclusion(
    const std::vector<std::size_t>& indexes,
    const std::size_t count) {
    if (indexes.size() != count) return false;
    for (std::size_t index = 0U; index < count; ++index) {
        if (indexes[index] != index) return false;
    }
    return true;
}

[[nodiscard]] bool entry_id_one(
    const std::vector<std::uint8_t>& entry_id) {
    if (entry_id.size() != 8U) return false;
    for (std::size_t index = 0U; index + 1U < entry_id.size(); ++index) {
        if (entry_id[index] != 0U) return false;
    }
    return entry_id.back() == 1U;
}

void print_usage() {
    std::cout
        << "Usage: ariec61850_iedscout_reporting_wire_probe <host> [port] [options]\n"
        << "  --mode urcb|brcb\n"
        << "  --domain DOMAIN\n"
        << "  --rcb ITEM\n"
        << "  --expected-rptid TEXT\n"
        << "  --expected-dataset DOMAIN/ITEM\n"
        << "  --expected-members N\n"
        << "  --expected-confrev N\n"
        << "  --expected-opt-first N\n"
        << "  --expected-opt-second N\n"
        << "  --timeout-ms N\n\n"
        << "Replays the static OMICRON IEDScout commissioning order: URCB Resv,\n"
        << "grouped TrgOps+RptEna, GI; BRCB grouped TrgOps+RptEna, GI.\n";
}

[[nodiscard]] Options parse_options(const int argc, char** argv) {
    if (argc < 2) throw std::invalid_argument("host is required.");
    Options options;
    options.endpoint.host = argv[1];
    options.endpoint.port = 102U;
    int argument = 2;
    if (argument < argc && std::string_view{argv[argument]}.rfind("--", 0U) != 0U) {
        options.endpoint.port = static_cast<std::uint16_t>(
            parse_size("port", argv[argument++], 65'535U));
    }
    while (argument < argc) {
        const std::string key = argv[argument++];
        if (key == "--help" || key == "-h") {
            print_usage();
            std::exit(0);
        }
        if (argument >= argc) {
            throw std::invalid_argument(key + " requires a value.");
        }
        const std::string value = argv[argument++];
        if (key == "--mode") {
            if (value == "urcb") options.mode = Mode::urcb;
            else if (value == "brcb") options.mode = Mode::brcb;
            else throw std::invalid_argument("--mode requires urcb or brcb.");
        } else if (key == "--domain") {
            options.domain = value;
        } else if (key == "--rcb") {
            options.rcb = value;
        } else if (key == "--expected-rptid") {
            options.expected_rptid = value;
        } else if (key == "--expected-dataset") {
            options.expected_dataset = value;
        } else if (key == "--expected-members") {
            options.expected_members = parse_size(key, value, 4096U);
        } else if (key == "--expected-confrev") {
            options.expected_conf_rev = static_cast<std::uint32_t>(
                parse_size(key, value, 0xFFFFFFFFU));
        } else if (key == "--expected-opt-first") {
            options.expected_opt_first = static_cast<std::uint8_t>(
                parse_size(key, value, 0xFFU));
        } else if (key == "--expected-opt-second") {
            options.expected_opt_second = static_cast<std::uint8_t>(
                parse_size(key, value, 0xFFU));
        } else if (key == "--timeout-ms") {
            options.timeout = std::chrono::milliseconds{
                static_cast<std::int64_t>(parse_size(key, value, 120'000U))};
        } else {
            throw std::invalid_argument("Unknown option: " + key);
        }
    }
    if (options.domain.empty() || options.rcb.empty() ||
        options.expected_rptid.empty() || options.expected_dataset.empty() ||
        options.expected_members == 0U) {
        throw std::invalid_argument("required reporting probe options are missing.");
    }
    return options;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc > 1 && (std::string_view{argv[1]} == "--help" ||
                         std::string_view{argv[1]} == "-h")) {
            print_usage();
            return 0;
        }
        const auto options = parse_options(argc, argv);

        mms::MmsAssociationOptions association_options;
        association_options.connect_timeout = options.timeout;
        association_options.request_timeout = options.timeout;
        mms::MmsTcpLiveDiscoverySession session{{}, association_options};
        session.connect(options.endpoint);

        const auto root = options.rcb + "$";
        if (options.mode == Mode::urcb) {
            require_boolean_write(
                session.association(), options.domain, root + "Resv", true,
                "URCB Resv=true");
        }

        const std::array<std::uint8_t, 1U> trigger_payload{0x7CU};
        require_write(
            session.association(),
            {
                mms::MmsObjectName::domain_specific(
                    options.domain, root + "TrgOps"),
                mms::MmsObjectName::domain_specific(
                    options.domain, root + "RptEna"),
            },
            {
                mms::MmsDataValue::bit_string(2U, trigger_payload),
                mms::MmsDataValue::boolean(true),
            },
            "grouped TrgOps+RptEna");

        require_boolean_write(
            session.association(), options.domain, root + "GI", true, "GI=true");

        const auto payload = wait_report(session.association(), options.timeout);
        mms::MmsInformationReport report;
        std::string error;
        if (!mms::MmsInformationReportCodec::try_decode(payload, report, &error)) {
            throw std::runtime_error(
                "InformationReport decode failed: " + error);
        }
        const auto frame = mms::MmsReportFrameMapper::map(report, {});
        if (frame.header.report_id != options.expected_rptid ||
            !exact_optflds(
                frame.header.optional_fields,
                options.expected_opt_first,
                options.expected_opt_second) ||
            frame.header.sequence_number != 1U ||
            !frame.header.time_of_entry ||
            frame.header.data_set_reference != options.expected_dataset ||
            frame.header.configuration_revision != options.expected_conf_rev ||
            !full_inclusion(
                frame.included_data_set_indexes, options.expected_members) ||
            frame.values.size() != options.expected_members ||
            !all_gi(frame)) {
            throw std::runtime_error(
                "InformationReport semantic golden mismatch.");
        }
        if (options.mode == Mode::urcb) {
            if (!frame.header.entry_id.empty()) {
                throw std::runtime_error("URCB GI unexpectedly exposed EntryID.");
            }
        } else if (!entry_id_one(frame.header.entry_id)) {
            throw std::runtime_error("BRCB GI EntryID is not 1.");
        }

        require_boolean_write(
            session.association(), options.domain, root + "RptEna", false,
            "RptEna=false");
        if (options.mode == Mode::urcb) {
            require_boolean_write(
                session.association(), options.domain, root + "Resv", false,
                "URCB Resv=false");
        }
        session.disconnect();

        std::cout
            << "IEDSCOUT_REPORTING_WIRE_PASS mode="
            << (options.mode == Mode::urcb ? "urcb" : "brcb")
            << " rcb=" << options.domain << '/' << options.rcb
            << " members=" << frame.values.size()
            << " sqNum=1 confRev=" << *frame.header.configuration_revision
            << " optFlds=" << std::hex
            << static_cast<unsigned>(options.expected_opt_first)
            << static_cast<unsigned>(options.expected_opt_second)
            << std::dec
            << " reason=general-interrogation"
            << " groupedWrite=TrgOps,RptEna"
            << " reservation="
            << (options.mode == Mode::urcb ? "urcb-only" : "none")
            << '\n';
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "IEDScout reporting wire probe failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
