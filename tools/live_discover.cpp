// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/live_discovery.hpp"
#include "ariec61850/mms/live_model.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace ar::iec61850;

struct IedIdentityResolution final {
    std::string name;
    std::string source;
    mms::MmsLiveModelConfidence confidence{mms::MmsLiveModelConfidence::unknown};
    bool ambiguous{};
    std::vector<std::string> candidates;
    std::vector<std::string> evidence;
};

[[nodiscard]] std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    return value;
}

[[nodiscard]] bool same(
    const std::string_view left,
    const std::string_view right) {
    return lower(std::string{left}) == lower(std::string{right});
}

[[nodiscard]] bool starts_with_ci(
    const std::string_view value,
    const std::string_view prefix) {
    return value.size() >= prefix.size() && same(value.substr(0U, prefix.size()), prefix);
}

[[nodiscard]] bool ends_with_ci(
    const std::string_view value,
    const std::string_view suffix) {
    return value.size() >= suffix.size() &&
        same(value.substr(value.size() - suffix.size()), suffix);
}

[[nodiscard]] std::string trim_boundary(std::string value) {
    while (!value.empty()) {
        const char character = value.back();
        if (character != '_' && character != '-' && character != '.' && character != ' ') {
            break;
        }
        value.pop_back();
    }
    return value;
}

[[nodiscard]] bool viable_ied_name(const std::string_view value) {
    return std::count_if(value.begin(), value.end(), [](const char character) {
        return std::isalnum(static_cast<unsigned char>(character)) != 0;
    }) >= 3;
}

void append_unique_ci(std::vector<std::string>& values, const std::string& value) {
    if (std::none_of(values.begin(), values.end(), [&value](const auto& existing) {
            return same(existing, value);
        })) {
        values.push_back(value);
    }
}

[[nodiscard]] bool try_extract_known_ld_prefix(
    const std::string_view domain,
    std::string& candidate) {
    static constexpr std::array<std::string_view, 14> stems{
        "PROT", "CTRL", "MEAS", "PQM", "MET", "ANN", "BCU",
        "SYS", "COM", "RLY", "BAY", "DR", "LD", "MU"};

    std::string trimmed{domain};
    std::size_t suffix_start = trimmed.size();
    while (suffix_start > 0U &&
           std::isdigit(static_cast<unsigned char>(trimmed[suffix_start - 1U])) != 0) {
        --suffix_start;
    }
    const std::string without_index = trimmed.substr(0U, suffix_start);
    for (const auto stem : stems) {
        if (!ends_with_ci(without_index, stem) || without_index.size() <= stem.size()) {
            continue;
        }
        auto prefix = trim_boundary(
            without_index.substr(0U, without_index.size() - stem.size()));
        if (!viable_ied_name(prefix)) {
            continue;
        }
        candidate = std::move(prefix);
        return true;
    }
    return false;
}

[[nodiscard]] std::string infer_common_prefix(
    const std::vector<std::string>& domains) {
    if (domains.size() < 2U) {
        return {};
    }

    std::string prefix = domains.front();
    for (std::size_t index = 1U; index < domains.size(); ++index) {
        const auto& domain = domains[index];
        std::size_t length = 0U;
        while (length < prefix.size() && length < domain.size() &&
               std::toupper(static_cast<unsigned char>(prefix[length])) ==
                   std::toupper(static_cast<unsigned char>(domain[length]))) {
            ++length;
        }
        prefix.resize(length);
        if (prefix.empty()) {
            return {};
        }
    }

    prefix = trim_boundary(std::move(prefix));
    static constexpr std::array<std::string_view, 14> stems{
        "PROT", "CTRL", "MEAS", "PQM", "MET", "ANN", "BCU",
        "SYS", "COM", "RLY", "BAY", "DR", "LD", "MU"};
    for (const auto stem : stems) {
        if (ends_with_ci(prefix, stem) && prefix.size() > stem.size()) {
            auto without_stem = trim_boundary(
                prefix.substr(0U, prefix.size() - stem.size()));
            if (viable_ied_name(without_stem)) {
                prefix = std::move(without_stem);
            }
            break;
        }
    }
    return viable_ied_name(prefix) ? prefix : std::string{};
}

[[nodiscard]] IedIdentityResolution resolve_ied_identity(
    const mms::MmsLiveDiscoveryResult& discovery) {
    std::vector<std::string> domains;
    domains.reserve(discovery.names.domain_variables.size());
    for (const auto& [domain, _] : discovery.names.domain_variables) {
        if (!domain.empty()) {
            append_unique_ci(domains, domain);
        }
    }
    std::sort(domains.begin(), domains.end(), [](const auto& left, const auto& right) {
        return lower(left) < lower(right);
    });

    struct SuffixMatch final {
        std::string domain;
        std::string candidate;
    };
    std::vector<SuffixMatch> suffix_matches;
    std::vector<std::string> distinct_candidates;
    for (const auto& domain : domains) {
        std::string candidate;
        if (try_extract_known_ld_prefix(domain, candidate)) {
            suffix_matches.push_back({domain, candidate});
            append_unique_ci(distinct_candidates, candidate);
        }
    }
    std::sort(
        distinct_candidates.begin(), distinct_candidates.end(),
        [](const auto& left, const auto& right) { return lower(left) < lower(right); });

    if (!domains.empty() && suffix_matches.size() == domains.size()) {
        if (distinct_candidates.size() == 1U) {
            IedIdentityResolution resolution;
            resolution.name = distinct_candidates.front();
            resolution.source = "MmsDomainKnownLogicalDeviceSuffix";
            resolution.confidence = domains.size() > 1U
                ? mms::MmsLiveModelConfidence::high
                : mms::MmsLiveModelConfidence::medium;
            resolution.candidates = distinct_candidates;
            for (const auto& match : suffix_matches) {
                resolution.evidence.push_back(
                    "MMS domain '" + match.domain +
                    "' matched the logical-device suffix pattern for IED '" +
                    resolution.name + "'.");
            }
            return resolution;
        }
        if (distinct_candidates.size() > 1U) {
            return {
                discovery.endpoint.host.empty() ? "DISCOVERED_IED" : discovery.endpoint.host,
                "MmsDomainAmbiguous",
                mms::MmsLiveModelConfidence::low,
                true,
                distinct_candidates,
                {"MMS domains produced conflicting IED-name candidates."}};
        }
    }

    if (const auto common_prefix = infer_common_prefix(domains); !common_prefix.empty()) {
        return {
            common_prefix,
            "MmsDomainCommonPrefix",
            domains.size() >= 3U
                ? mms::MmsLiveModelConfidence::high
                : mms::MmsLiveModelConfidence::medium,
            false,
            {common_prefix},
            {"IED name was derived from the common prefix of " +
             std::to_string(domains.size()) + " MMS domain(s)."}};
    }

    if (distinct_candidates.size() == 1U) {
        const auto& candidate = distinct_candidates.front();
        const bool shared_prefix = suffix_matches.size() >= 2U &&
            std::all_of(domains.begin(), domains.end(), [&candidate](const auto& domain) {
                return starts_with_ci(domain, candidate);
            });
        if (domains.size() == 1U || shared_prefix) {
            IedIdentityResolution resolution;
            resolution.name = candidate;
            resolution.source = "MmsDomainKnownLogicalDeviceSuffix";
            resolution.confidence = domains.size() == 1U
                ? mms::MmsLiveModelConfidence::medium
                : mms::MmsLiveModelConfidence::high;
            resolution.candidates = distinct_candidates;
            for (const auto& match : suffix_matches) {
                resolution.evidence.push_back(
                    "MMS domain '" + match.domain +
                    "' matched the logical-device suffix pattern for IED '" +
                    candidate + "'.");
            }
            return resolution;
        }
    }

    if (distinct_candidates.size() > 1U) {
        return {
            discovery.endpoint.host.empty() ? "DISCOVERED_IED" : discovery.endpoint.host,
            "MmsDomainAmbiguous",
            mms::MmsLiveModelConfidence::low,
            true,
            distinct_candidates,
            {"MMS domains produced conflicting IED-name candidates."}};
    }

    return {
        discovery.endpoint.host.empty() ? "DISCOVERED_IED" : discovery.endpoint.host,
        "HostFallback",
        mms::MmsLiveModelConfidence::low,
        false,
        {},
        {"No safe IED-name candidate could be derived from the live MMS domains."}};
}

void apply_resolved_identity(
    mms::MmsLiveModelDocument& model,
    const mms::MmsLiveDiscoveryResult& discovery) {
    const auto resolution = resolve_ied_identity(discovery);
    model.identity.ied_name = resolution.name;
    model.identity.source = resolution.source;
    model.identity.confidence = resolution.confidence;
    model.identity.ambiguous = resolution.ambiguous;
    model.identity.candidate_names = resolution.candidates;
    model.identity.evidence = resolution.evidence;
    model.identity.logical_device_aliases.clear();

    for (auto& logical_device : model.logical_devices) {
        std::string alias = logical_device.mms_domain;
        if (starts_with_ci(logical_device.mms_domain, resolution.name) &&
            logical_device.mms_domain.size() > resolution.name.size()) {
            alias = logical_device.mms_domain.substr(resolution.name.size());
        }
        if (alias.empty()) {
            alias = logical_device.mms_domain;
        }
        logical_device.instance = alias;
        model.identity.logical_device_aliases.emplace(
            logical_device.mms_domain, std::move(alias));
    }
}

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
        << "  --ied-name NAME        Optional explicit IED identity override.\n"
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
              << "IED identity: " << model.identity.ied_name
              << " (source=" << model.identity.source << ")\n"
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
              << "\"iedNameSource\":\"" << json_escape(model.identity.source) << "\","
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
        auto model = mms::MmsLiveModelBuilder::build(result, model_options);
        if (model_options.explicit_ied_name.empty()) {
            apply_resolved_identity(model, result);
        }

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
