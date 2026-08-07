// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/live_discovery.hpp"
#include "ariec61850/mms/live_model.hpp"

#include <chrono>
#include <cstdint>
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
            if (static_cast<unsigned char>(character) < 0x20U) output << '?';
            else output << character;
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
        << "  --json                 Emit compact discovery summary JSON.\n"
        << "  --model-json           Emit C#-compatible live-ied-model-v1 JSON.\n"
        << "  --manifest             Emit deterministic parity manifest.\n"
        << "  --ied-name NAME        Explicit IED identity override.\n"
        << "  --no-types             Skip GetVariableAccessAttributes probes.\n"
        << "  --no-datasets          Skip DataSet directory reads.\n"
        << "  --no-rcb               Skip read-only RCB attribute probes.\n"
        << "  --max-types N          Bound all type probes.\n"
        << "  --max-datasets N       Bound DataSet directory reads.\n"
        << "  --max-rcb N            Bound RCB read probes.\n"
        << "  --timeout-ms N         Connect/request timeout in milliseconds.\n";
}

void print_human(
    const mms::MmsLiveDiscoveryResult& result,
    const mms::MmsLiveModelDocument& model) {
    std::cout << result.summary() << '\n'
              << model.summary << '\n'
              << "Model fingerprint: " << model.canonical_fingerprint_hex() << '\n';
    for (const auto& logical_device : model.logical_devices) {
        std::cout << "  LD " << logical_device.mms_domain
                  << " (alias=" << logical_device.instance << ")\n";
        for (const auto& logical_node : logical_device.logical_nodes) {
            std::cout << "    LN " << logical_node.name
                      << " class=" << logical_node.logical_node_class
                      << " DO=" << logical_node.data_objects.size() << '\n';
        }
    }
    if (!model.warnings.empty()) {
        std::cout << "Warnings:\n";
        for (const auto& warning : model.warnings) {
            std::cout << "  - [" << warning.code << "] " << warning.message << '\n';
        }
    }
}

void print_summary_json(
    const mms::MmsLiveDiscoveryResult& result,
    const mms::MmsLiveModelDocument& model) {
    std::cout << '{'
              << "\"endpoint\":\"" << json_escape(result.endpoint.host) << ':'
              << result.endpoint.port << "\","
              << "\"iedName\":\"" << json_escape(model.identity.ied_name) << "\","
              << "\"domains\":" << result.domain_count() << ','
              << "\"variables\":" << result.variable_count() << ','
              << "\"logicalDevices\":" << model.coverage.logical_device_count << ','
              << "\"logicalNodes\":" << model.coverage.logical_node_count << ','
              << "\"dataObjects\":" << model.coverage.data_object_count << ','
              << "\"dataAttributes\":" << model.coverage.data_attribute_count << ','
              << "\"dataSets\":" << model.coverage.data_set_count << ','
              << "\"reportControls\":" << model.coverage.report_control_count << ','
              << "\"exactMmsTypes\":" << model.coverage.exact_mms_type_count << ','
              << "\"fingerprint\":\"" << model.canonical_fingerprint_hex() << "\","
              << "\"partial\":" << (result.partial() ? "true" : "false") << ','
              << "\"summary\":\"" << json_escape(model.summary) << "\"}\n";
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

        enum class OutputMode { human, summary_json, model_json, manifest };
        OutputMode output_mode = OutputMode::human;
        std::chrono::milliseconds timeout{5'000};
        mms::MmsLiveDiscoveryOptions discovery_options;
        mms::MmsLiveModelBuildOptions model_options;

        while (argument < argc) {
            const std::string option = argv[argument++];
            if (option == "--json") output_mode = OutputMode::summary_json;
            else if (option == "--model-json") output_mode = OutputMode::model_json;
            else if (option == "--manifest") output_mode = OutputMode::manifest;
            else if (option == "--no-types") discovery_options.probe_variable_types = false;
            else if (option == "--no-datasets") {
                discovery_options.read_data_set_directories = false;
            } else if (option == "--no-rcb") {
                discovery_options.probe_report_controls = false;
            } else if (option == "--ied-name" || option == "--max-types" ||
                       option == "--max-datasets" || option == "--max-rcb" ||
                       option == "--timeout-ms") {
                if (argument >= argc) {
                    throw std::invalid_argument(option + " requires a value.");
                }
                const std::string value = argv[argument++];
                if (option == "--ied-name") {
                    model_options.explicit_ied_name = value;
                    continue;
                }
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
        const auto model = mms::MmsLiveModelBuilder::build(result, model_options);

        switch (output_mode) {
        case OutputMode::human: print_human(result, model); break;
        case OutputMode::summary_json: print_summary_json(result, model); break;
        case OutputMode::model_json: std::cout << model.to_json() << '\n'; break;
        case OutputMode::manifest: std::cout << model.canonical_manifest(); break;
        }
        return result.partial() ? 1 : 0;
    } catch (const std::exception& exception) {
        std::cerr << "Live MMS discovery failed: " << exception.what() << '\n';
        return 2;
    }
}
