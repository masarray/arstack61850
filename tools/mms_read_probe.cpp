// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/mms/live_discovery.hpp"
#include "ariec61850/mms/services.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
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

void print_usage() {
    std::cout
        << "Usage: ariec61850_mms_read_probe <host> [port] --domain NAME --item NAME [options]\n\n"
        << "Options:\n"
        << "  --type          Read GetVariableAccessAttributes instead of the value.\n"
        << "  --write-bool V  Write a Boolean value (true/false).\n"
        << "  --write-int V   Write a signed integer value.\n"
        << "  --write-uint V  Write an unsigned integer value.\n"
        << "  --count N       Read repeatedly on one MMS association (default 1).\n"
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
        std::chrono::milliseconds delay{500};
        std::chrono::milliseconds timeout{5'000};
        bool type_only{};
        std::optional<mms::MmsDataValue> write_value;
        std::string write_display;
        while (argument < argc) {
            const std::string option = argv[argument++];
            if (option == "--help" || option == "-h") {
                print_usage();
                return 0;
            }
            if (option == "--type") {
                type_only = true;
                continue;
            }
            if (argument >= argc) throw std::invalid_argument(option + " requires a value.");
            const std::string value = argv[argument++];
            if (option == "--write-bool" || option == "--write-int" ||
                option == "--write-uint") {
                if (write_value.has_value()) {
                    throw std::invalid_argument("Only one write value may be specified.");
                }
                if (option == "--write-bool") {
                    if (value == "true" || value == "1") {
                        write_value = mms::MmsDataValue::boolean(true);
                        write_display = "true";
                    } else if (value == "false" || value == "0") {
                        write_value = mms::MmsDataValue::boolean(false);
                        write_display = "false";
                    } else {
                        throw std::invalid_argument("--write-bool expects true/false.");
                    }
                } else if (option == "--write-int") {
                    std::size_t consumed{};
                    const auto parsed = std::stoll(value, &consumed, 10);
                    if (consumed != value.size()) throw std::invalid_argument("Invalid signed integer.");
                    write_value = mms::MmsDataValue::integer(parsed);
                    write_display = value;
                } else {
                    std::size_t consumed{};
                    const auto parsed = std::stoull(value, &consumed, 10);
                    if (consumed != value.size()) throw std::invalid_argument("Invalid unsigned integer.");
                    write_value = mms::MmsDataValue::unsigned_integer(parsed);
                    write_display = value;
                }
                continue;
            }
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
        if (type_only && count != 1U) {
            throw std::invalid_argument("--type cannot be combined with --count.");
        }
        if (write_value.has_value() && (type_only || count != 1U)) {
            throw std::invalid_argument("Write mode cannot be combined with --type or --count.");
        }

        mms::MmsAssociationOptions association_options;
        association_options.connect_timeout = timeout;
        association_options.request_timeout = timeout;
        mms::MmsTcpLiveDiscoverySession session{{}, association_options};
        session.connect(endpoint);

        if (write_value.has_value()) {
            const auto invoke_id = session.association().next_invoke_id();
            mms::MmsWriteRequest request;
            request.invoke_id = invoke_id;
            request.variables.push_back(mms::MmsObjectName::domain_specific(domain, item));
            request.values.push_back(*write_value);
            const auto encoded = mms::MmsServiceCodec::encode_write_request_p_data(
                request, session.association().negotiated().presentation_context_id);
            const auto exchange = session.association().exchange_confirmed(encoded, invoke_id);
            if (exchange.envelope.kind != mms::MmsPduKind::confirmed_response) {
                throw std::runtime_error("Write did not return Confirmed-Response.");
            }
            const auto response = mms::MmsServiceCodec::decode_write_response(
                response_payload(exchange), invoke_id);
            if (!response.all_success()) {
                throw std::runtime_error("Write returned a failed AccessResult.");
            }
            std::cout << "MMS_WRITE reference=" << domain << '/' << item
                      << " value=" << write_display << '\n';
            session.disconnect();
            return 0;
        }

        if (type_only) {
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
            std::cout << "MMS_TYPE reference=" << domain << '/' << item
                      << " kind=" << response.type.mms_type_name()
                      << " size=";
            if (response.type.size) std::cout << *response.type.size;
            else std::cout << "none";
            std::cout << " exponent=";
            if (response.type.exponent_width) std::cout << *response.type.exponent_width;
            else std::cout << "none";
            std::cout << " signature=" << response.type.signature() << '\n';
            session.disconnect();
            return 0;
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
            std::cout << "MMS_READ index=" << (index + 1U)
                      << " reference=" << domain << '/' << item
                      << " value=" << mms::MmsDataCodec::to_display_string(
                             *response.results[0].value)
                      << '\n';
            std::cout.flush();
            if (index + 1U < count) std::this_thread::sleep_for(delay);
        }
        session.disconnect();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "MMS read/write probe failed: " << exception.what() << '\n';
        return 1;
    }
}