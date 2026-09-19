// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/mms/live_discovery.hpp"
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
#include <variant>
#include <vector>

namespace {
namespace mms = ar::iec61850::mms;

[[nodiscard]] std::span<const std::uint8_t> response_payload(
    const mms::MmsConfirmedExchangeResult& exchange) {
    return exchange.presentation_payload.empty()
        ? exchange.envelope.mms_payload
        : std::span<const std::uint8_t>{exchange.presentation_payload};
}

[[nodiscard]] std::uint64_t parse_u64(
    const std::string& option,
    const std::string& text,
    const std::uint64_t maximum) {
    std::size_t consumed{};
    const auto value = std::stoull(text, &consumed, 10);
    if (consumed != text.size() || value > maximum) {
        throw std::invalid_argument(option + " is outside the supported range.");
    }
    return value;
}

[[nodiscard]] mms::MmsDataValue read_one(
    mms::MmsAssociationRuntime& association,
    const std::string& domain,
    const std::string& item) {
    mms::MmsReadRequest request;
    request.invoke_id = association.next_invoke_id();
    request.variables.push_back(mms::MmsObjectName::domain_specific(domain, item));
    const auto encoded = mms::MmsServiceCodec::encode_read_request_p_data(
        request, association.negotiated().presentation_context_id);
    const auto exchange = association.exchange_confirmed(encoded, request.invoke_id);
    if (exchange.envelope.kind != mms::MmsPduKind::confirmed_response) {
        throw std::runtime_error("SGCB Read did not return Confirmed-Response.");
    }
    const auto response = mms::MmsServiceCodec::decode_read_response(
        response_payload(exchange), request.invoke_id);
    if (response.results.size() != 1U || !response.results.front().success()) {
        throw std::runtime_error("SGCB Read returned failed AccessResult for " + item + '.');
    }
    return *response.results.front().value;
}

[[nodiscard]] std::uint64_t as_unsigned(
    const mms::MmsDataValue& value,
    const std::string_view label) {
    if (value.kind() == mms::MmsDataKind::unsigned_integer) {
        return std::get<std::uint64_t>(value.value());
    }
    if (value.kind() == mms::MmsDataKind::integer) {
        const auto parsed = std::get<std::int64_t>(value.value());
        if (parsed >= 0) return static_cast<std::uint64_t>(parsed);
    }
    throw std::runtime_error(std::string{label} + " is not an unsigned/integer scalar.");
}

[[nodiscard]] bool as_bool(const mms::MmsDataValue& value, const std::string_view label) {
    if (value.kind() != mms::MmsDataKind::boolean) {
        throw std::runtime_error(std::string{label} + " is not Boolean.");
    }
    return std::get<bool>(value.value());
}

[[nodiscard]] bool write_actsg(
    mms::MmsAssociationRuntime& association,
    const std::string& domain,
    const std::string& item,
    const std::uint64_t group) {
    mms::MmsWriteRequest request;
    request.invoke_id = association.next_invoke_id();
    request.variables.push_back(mms::MmsObjectName::domain_specific(domain, item));
    request.values.push_back(mms::MmsDataValue::unsigned_integer(group));
    const auto encoded = mms::MmsServiceCodec::encode_write_request_p_data(
        request, association.negotiated().presentation_context_id);
    const auto exchange = association.exchange_confirmed(encoded, request.invoke_id);
    if (exchange.envelope.kind != mms::MmsPduKind::confirmed_response) {
        throw std::runtime_error("ActSG Write did not return Confirmed-Response.");
    }
    const auto response = mms::MmsServiceCodec::decode_write_response(
        response_payload(exchange), request.invoke_id);
    if (response.results.size() != 1U) {
        throw std::runtime_error("ActSG Write response cardinality mismatch.");
    }
    return response.all_success();
}

void usage() {
    std::cout
        << "Usage: ariec61850_mms_sgcb_probe <host> [port] --domain D --root ITEM [options]\n"
        << "  --expect-num N   Require NumOfSG.\n"
        << "  --expect-act N   Require initial ActSG.\n"
        << "  --activate N     Write ActSG=N and verify it.\n"
        << "  --reject N       Require ActSG=N to be rejected and state unchanged.\n"
        << "  --timeout-ms N   Connect/request timeout, default 5000.\n";
}
} // namespace

int main(const int argc, char** argv) {
    try {
        if (argc < 2 || std::string_view{argv[1]} == "--help" ||
            std::string_view{argv[1]} == "-h") {
            usage();
            return argc < 2 ? 2 : 0;
        }
        mms::MmsEndpoint endpoint;
        endpoint.host = argv[1];
        endpoint.port = 102U;
        int argument = 2;
        if (argument < argc && std::string_view{argv[argument]}.rfind("--", 0U) != 0U) {
            endpoint.port = static_cast<std::uint16_t>(
                parse_u64("port", argv[argument++], 65'535U));
        }
        std::string domain;
        std::string root;
        std::optional<std::uint64_t> expected_num;
        std::optional<std::uint64_t> expected_act;
        std::optional<std::uint64_t> activate;
        std::optional<std::uint64_t> reject;
        std::chrono::milliseconds timeout{5'000};
        while (argument < argc) {
            const std::string option = argv[argument++];
            if (argument >= argc) throw std::invalid_argument(option + " requires a value.");
            const std::string value = argv[argument++];
            if (option == "--domain") domain = value;
            else if (option == "--root") root = value;
            else if (option == "--expect-num") expected_num = parse_u64(option, value, 255U);
            else if (option == "--expect-act") expected_act = parse_u64(option, value, 255U);
            else if (option == "--activate") activate = parse_u64(option, value, 255U);
            else if (option == "--reject") reject = parse_u64(option, value, 255U);
            else if (option == "--timeout-ms") {
                timeout = std::chrono::milliseconds{
                    static_cast<std::int64_t>(parse_u64(option, value, 120'000U))};
            } else throw std::invalid_argument("Unknown option: " + option);
        }
        if (domain.empty() || root.empty()) {
            throw std::invalid_argument("--domain and --root are required.");
        }

        mms::MmsAssociationOptions association_options;
        association_options.connect_timeout = timeout;
        association_options.request_timeout = timeout;
        mms::MmsTcpLiveDiscoverySession session{{}, association_options};
        session.connect(endpoint);

        mms::MmsLiveDiscoveryOptions discovery_options;
        discovery_options.probe_variable_types = false;
        discovery_options.read_data_set_directories = false;
        discovery_options.probe_report_controls = false;
        const auto discovered = session.discover(discovery_options);
        const auto found_domain = discovered.names.domain_variables.find(domain);
        if (found_domain == discovered.names.domain_variables.end()) {
            throw std::runtime_error("SGCB domain missing from GetNameList discovery.");
        }
        const std::vector<std::string> suffixes{
            "ActSG", "CnfEdit", "EditSG", "LActTm", "NumOfSG"};
        for (const auto& suffix : suffixes) {
            const auto exact = root + '$' + suffix;
            if (std::find(found_domain->second.begin(), found_domain->second.end(), exact) ==
                found_domain->second.end()) {
                throw std::runtime_error("GetNameList missing exact SGCB attribute: " + exact);
            }
        }

        const auto act_item = root + "$ActSG";
        const auto act = as_unsigned(read_one(session.association(), domain, act_item), "ActSG");
        const auto count = as_unsigned(
            read_one(session.association(), domain, root + "$NumOfSG"), "NumOfSG");
        const auto edit = as_unsigned(
            read_one(session.association(), domain, root + "$EditSG"), "EditSG");
        const auto confirm = as_bool(
            read_one(session.association(), domain, root + "$CnfEdit"), "CnfEdit");
        const auto last = read_one(session.association(), domain, root + "$LActTm");
        if (last.kind() != mms::MmsDataKind::utc_time) {
            throw std::runtime_error("LActTm is not IEC 61850 UTC time.");
        }
        if (edit != 0U || confirm) {
            throw std::runtime_error("Simulator SGCB unexpectedly advertises active edit transaction.");
        }
        if (expected_num && count != *expected_num) {
            throw std::runtime_error("NumOfSG verification mismatch.");
        }
        if (expected_act && act != *expected_act) {
            throw std::runtime_error("Initial ActSG verification mismatch.");
        }

        std::uint64_t final_act = act;
        if (activate) {
            if (!write_actsg(session.association(), domain, act_item, *activate)) {
                throw std::runtime_error("Expected ActSG activation was rejected.");
            }
            final_act = as_unsigned(
                read_one(session.association(), domain, act_item), "ActSG verification");
            if (final_act != *activate) {
                throw std::runtime_error("ActSG verification Read did not observe requested group.");
            }
        }
        if (reject) {
            const auto before = as_unsigned(
                read_one(session.association(), domain, act_item), "ActSG before reject");
            if (write_actsg(session.association(), domain, act_item, *reject)) {
                throw std::runtime_error("Expected invalid ActSG Write to be rejected.");
            }
            final_act = as_unsigned(
                read_one(session.association(), domain, act_item), "ActSG after reject");
            if (final_act != before) {
                throw std::runtime_error("Rejected ActSG Write changed canonical state.");
            }
        }

        std::cout << "SGCB_PROBE_PASS discovery=5/5 num=" << count
                  << " initial=" << act << " final=" << final_act
                  << " edit=0 cnf=false"
                  << (activate ? " activation=verified" : "")
                  << (reject ? " rejection=verified" : "") << '\n';
        session.disconnect();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "SGCB_PROBE_FAIL " << error.what() << '\n';
        return 1;
    }
}
