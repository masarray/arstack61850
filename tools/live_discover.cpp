// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/live_discovery.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace ar::iec61850;

[[nodiscard]] std::string json_escape(const std::string_view value) {
    std::ostringstream output;
    for (const char character : value) {
        switch (character) {
        case '\\': output << "\\\\"; break;
        case '"': output << "\\\""; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U) {
                output << "?";
            } else {
                output << character;
            }
            break;
        }
    }
    return output.str();
}

[[nodiscard]] std::uint16_t parse_port(const std::string& value) {
    std::size_t consumed = 0U;
    const auto parsed = std::stoul(value, &consumed, 10);
    if (consumed != value.size() || parsed == 0UL || parsed > 65'535UL) {
        throw std::invalid_argument("MMS TCP port must be in the range 1..65535.");
    }
    return static_cast<std::uint16_t>(parsed);
}

[[nodiscard]] std::size_t parse_limit(
    const std::string& option,
    const std::string& value) {
    std::size_t consumed = 0U;
    const auto parsed = std::stoull(value, &consumed, 10);
    if (consumed != value.size() || parsed == 0ULL ||
        parsed > static_cast<unsigned long long>(
            std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument(option + " requires a positive bounded integer.");
    }
    return static_cast<std::size_t>(parsed);
}

void print_usage() {
    std::cout
        << "Usage: ariec61850_live_discover <host> [port] [options]\n"
        << "Options:\n"
        << "  --json                 Emit a compact JSON summary.\n"
        << "  --no-types             Skip GetVariableAccessAttributes probes.\n"
        << "  --no-datasets          Skip DataSet directory reads.\n"
        << "  --no-rcb               Skip read-only RCB attribute probes.\n"
        << "  --max-types N          Bound variable type probes.\n"
        << "  --max-datasets N       Bound DataSet directory reads.\n"
        << "  --max-rcb N            Bound RCB read probes.\n"
        << "  --timeout-ms N         Connect/request timeout in milliseconds.\n";
}

void print_human(const mms::MmsLiveDiscoveryResult& result) {
    std::cout << result.summary() << '\n';
    for (const auto& [domain, variables] : result.names.domain_variables) {
        const auto lists = result.names.domain_variable_lists.find(domain);
        const std::size_t list_count = lists == result.names.domain_variable_lists.end()
            ? 0U
            : lists->second.size();
        std::cout << "  " << domain << ": variables=" << variables.size()
                  << ", DataSet names=" << list_count << '\n';
    }
    if (!result.diagnostics.empty()) {
        std::cout << "Diagnostics:\n";
        for (const auto& diagnostic : result.diagnostics) {
            std::cout << "  - " << diagnostic << '\n';
        }
    }
}

void print_json(const mms::MmsLiveDiscoveryResult& result) {
    std::cout << '{'
              << "\"endpoint\":\"" << json_escape(result.endpoint.host) << ':'
              << result.endpoint.port << "\","
              << "\"domains\":" << result.domain_count() << ','
              << "\"variables\":" << result.variable_count() << ','
              << "\"dataSets\":" << result.report_inventory.data_sets.size() << ','
              << "\"reportControls\":"
              << result.report_inventory.report_controls.size() << ','
              << "\"typeEvidence\":" << result.variable_types.size() << ','
              << "\"dataSetDirectories\":"
              << result.data_set_directories.size() << ','
              << "\"reportControlReads\":" << result.report_controls.size() << ','
              << "\"partial\":" << (result.partial() ? "true" : "false") << ','
              << "\"summary\":\"" << json_escape(result.summary()) << "\","
              << "\"diagnostics\":[";
    for (std::size_t index = 0U; index < result.diagnostics.size(); ++index) {
        if (index != 0U) {
            std::cout << ',';
        }
        std::cout << '"' << json_escape(result.diagnostics[index]) << '"';
    }
    std::cout << "]}\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            print_usage();
            return 2;
        }

        mms::MmsEndpoint endpoint;
        endpoint.host = argv[1];
        endpoint.port = 102U;

        int argument = 2;
        if (argument < argc && std::string_view{argv[argument]}.find("--") != 0U) {
            endpoint.port = parse_port(argv[argument]);
            ++argument;
        }

        bool json = false;
        std::chrono::milliseconds timeout{5'000};
        mms::MmsLiveDiscoveryOptions discovery_options;

        while (argument < argc) {
            const std::string option = argv[argument++];
            if (option == "--json") {
                json = true;
            } else if (option == "--no-types") {
                discovery_options.probe_variable_types = false;
            } else if (option == "--no-datasets") {
                discovery_options.read_data_set_directories = false;
            } else if (option == "--no-rcb") {
                discovery_options.probe_report_controls = false;
            } else if (option == "--max-types" ||
                       option == "--max-datasets" ||
                       option == "--max-rcb" ||
                       option == "--timeout-ms") {
                if (argument >= argc) {
                    throw std::invalid_argument(option + " requires a value.");
                }
                const std::string value = argv[argument++];
                const auto parsed = parse_limit(option, value);
                if (option == "--max-types") {
                    discovery_options.maximum_variable_type_probes = parsed;
                } else if (option == "--max-datasets") {
                    discovery_options.maximum_data_set_directories = parsed;
                } else if (option == "--max-rcb") {
                    discovery_options.maximum_report_control_probes = parsed;
                } else {
                    if (parsed > static_cast<std::size_t>(
                            std::numeric_limits<std::int64_t>::max())) {
                        throw std::invalid_argument("--timeout-ms is too large.");
                    }
                    timeout = std::chrono::milliseconds{
                        static_cast<std::int64_t>(parsed)};
                }
            } else if (option == "--help" || option == "-h") {
                print_usage();
                return 0;
            } else {
                throw std::invalid_argument("Unknown option: " + option);
            }
        }

        mms::MmsAssociationOptions association_options;
        association_options.connect_timeout = timeout;
        association_options.request_timeout = timeout;
        mms::MmsTcpLiveDiscoverySession session{{}, association_options};
        session.connect(endpoint);
        const auto result = session.discover(discovery_options);
        session.disconnect();

        if (json) {
            print_json(result);
        } else {
            print_human(result);
        }
        return result.partial() ? 1 : 0;
    } catch (const std::exception& exception) {
        std::cerr << "Live MMS discovery failed: " << exception.what() << '\n';
        return 2;
    }
}
