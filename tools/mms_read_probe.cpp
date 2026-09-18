// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/mms/live_discovery.hpp"
#include "ariec61850/mms/services.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

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
    case mms::MmsDataKind::object_id: return "object-id";
    case mms::MmsDataKind::mms_string: return "mms-string";
    case mms::MmsDataKind::utc_time: return "utc-time";
    case mms::MmsDataKind::unknown: return "unknown";
    }
    return "unknown";
}

[[nodiscard]] std::string_view type_kind_name(const mms::MmsTypeKind kind) noexcept {
    switch (kind) {
    case mms::MmsTypeKind::array: return "array";
    case mms::MmsTypeKind::structure: return "structure";
    case mms::MmsTypeKind::boolean: return "boolean";
    case mms::MmsTypeKind::bit_string: return "bit-string";
    case mms::MmsTypeKind::integer: return "integer";
    case mms::MmsTypeKind::unsigned_integer: return "unsigned";
    case mms::MmsTypeKind::floating_point: return "floating-point";
    case mms::MmsTypeKind::octet_string: return "octet-string";
    case mms::MmsTypeKind::visible_string: return "visible-string";
    case mms::MmsTypeKind::binary_time: return "binary-time";
    case mms::MmsTypeKind::bcd: return "bcd";
    case mms::MmsTypeKind::boolean_array: return "boolean-array";
    case mms::MmsTypeKind::object_id: return "object-id";
    case mms::MmsTypeKind::mms_string: return "mms-string";
    case mms::MmsTypeKind::utc_time: return "utc-time";
    case mms::MmsTypeKind::unknown: return "unknown";
    }
    return "unknown";
}

[[nodiscard]] std::string type_shape(const mms::MmsTypeSpecification& type) {
    std::string result{type_kind_name(type.kind)};
    if (type.kind != mms::MmsTypeKind::array &&
        type.kind != mms::MmsTypeKind::structure) {
        return result;
    }
    result.push_back('(');
    for (std::size_t index = 0U; index < type.children.size(); ++index) {
        if (index != 0U) result.push_back(',');
        result += type_shape(type.children[index]);
    }
    result.push_back(')');
    return result;
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

void print_usage() {
    std::cout
        << "Usage: ariec61850_mms_read_probe <host> [port] --domain NAME --item NAME [options]\n\n"
        << "Options:\n"
        << "  --count N       Read repeatedly on one MMS association (default 1).\n"
        << "  --with-type     Read GetVariableAccessAttributes and print type shape.\n"
        << "  --delay-ms N    Delay between reads (default 500).\n"
        << "  --timeout-ms N  Connect/request timeout (default 5000).\n"
        << "  -h, --help      Show this help.\n";
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
        std::string item;
        std::size_t count{1U};
        bool with_type{};
        std::chrono::milliseconds delay{500};
        std::chrono::milliseconds timeout{5'000};
        while (argument < argc) {
            const std::string option = argv[argument++];
            if (option == "--help" || option == "-h") {
                print_usage();
                return 0;
            }
            if (option == "--with-type") {
                with_type = true;
                continue;
            }
            if (argument >= argc) throw std::invalid_argument(option + " requires a value.");
            const std::string value = argv[argument++];
            if (option == "--domain") {
                domain = value;
            } else if (option == "--item") {
                item = value;
            } else if (option == "--count") {
                count = parse_size(option, value, 10'000U);
            } else if (option == "--delay-ms") {
                delay = std::chrono::milliseconds{static_cast<std::int64_t>(
                    parse_size(option, value, 60'000U))};
            } else if (option == "--timeout-ms") {
                timeout = std::chrono::milliseconds{static_cast<std::int64_t>(
                    parse_size(option, value, 120'000U))};
            } else {
                throw std::invalid_argument("Unknown option: " + option);
            }
        }
        if (domain.empty() || item.empty()) {
            throw std::invalid_argument("--domain and --item are required.");
        }

        mms::MmsAssociationOptions association_options;
        association_options.connect_timeout = timeout;
        association_options.request_timeout = timeout;
        mms::MmsTcpLiveDiscoverySession session{{}, association_options};
        session.connect(endpoint);

        std::string type_shape_text;
        if (with_type) {
            const auto invoke_id = session.association().next_invoke_id();
            mms::MmsVariableAccessAttributesRequest request;
            request.invoke_id = invoke_id;
            request.name = mms::MmsObjectName::domain_specific(domain, item);
            const auto encoded =
                mms::MmsServiceCodec::encode_variable_access_attributes_request_p_data(
                    request, session.association().negotiated().presentation_context_id);
            const auto exchange = session.association().exchange_confirmed(encoded, invoke_id);
            if (exchange.envelope.kind != mms::MmsPduKind::confirmed_response) {
                throw std::runtime_error(
                    "GetVariableAccessAttributes did not return Confirmed-Response.");
            }
            const auto response =
                mms::MmsServiceCodec::decode_variable_access_attributes_response(
                    response_payload(exchange), invoke_id);
            type_shape_text = type_shape(response.type);
        }

        for (std::size_t index = 0U; index < count; ++index) {
            const auto invoke_id = session.association().next_invoke_id();
            mms::MmsReadRequest request;
            request.invoke_id = invoke_id;
            request.variables.push_back(mms::MmsObjectName::domain_specific(domain, item));
            const auto encoded = mms::MmsServiceCodec::encode_read_request_p_data(
                request, session.association().negotiated().presentation_context_id);
            const auto exchange = session.association().exchange_confirmed(encoded, invoke_id);
            if (exchange.envelope.kind != mms::MmsPduKind::confirmed_response) {
                throw std::runtime_error("Read did not return Confirmed-Response.");
            }
            const auto response = mms::MmsServiceCodec::decode_read_response(
                response_payload(exchange), invoke_id);
            if (response.results.size() != 1U || !response.results[0].success()) {
                throw std::runtime_error("Read returned a failed AccessResult.");
            }
            const auto& value = *response.results[0].value;
            std::cout << "MMS_READ index=" << (index + 1U)
                      << " reference=" << domain << '/' << item;
            if (with_type) {
                std::cout << " type=" << type_shape_text;
            }
            std::cout << " shape=" << data_shape(value)
                      << " value=" << mms::MmsDataCodec::to_display_string(value)
                      << '\n';
            std::cout.flush();
            if (index + 1U < count) std::this_thread::sleep_for(delay);
        }
        session.disconnect();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "MMS read probe failed: " << exception.what() << '\n';
        return 1;
    }
}
