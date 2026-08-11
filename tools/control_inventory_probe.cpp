// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/control/control_session.hpp"
#include "ariec61850/mms/live_discovery.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace ar::iec61850;
using namespace ar::iec61850::control;
using namespace ar::iec61850::mms;

struct Options final {
    MmsEndpoint endpoint{};
    std::chrono::milliseconds timeout{5'000};
    std::string evidence_path;
    bool self_test{};
};

struct ControlCandidate final {
    std::string domain;
    std::string object_reference;
    std::string ctl_model_item;
};

struct CandidateEvidence final {
    ControlCandidate candidate;
    bool operationally_ready{};
    std::string ctl_model;
    std::string cdc;
    std::string status_object;
    std::string diagnostic;
};

[[nodiscard]] std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const char ch) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    });
    return value;
}

[[nodiscard]] std::vector<std::string> split_dollar(const std::string& value) {
    std::vector<std::string> parts;
    std::size_t begin{};
    while (begin <= value.size()) {
        const auto end = value.find('$', begin);
        parts.push_back(value.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin));
        if (end == std::string::npos) break;
        begin = end + 1U;
    }
    return parts;
}

[[nodiscard]] std::vector<ControlCandidate> extract_control_candidates(
    const MmsDiscoverySnapshot& snapshot) {
    std::vector<ControlCandidate> candidates;
    std::set<std::string, std::less<>> seen;

    for (const auto& [domain, variables] : snapshot.domain_variables) {
        for (const auto& item : variables) {
            const auto parts = split_dollar(item);
            if (parts.size() < 4U || parts.front().empty()) continue;
            if (lower_ascii(parts[1]) != "cf" ||
                lower_ascii(parts.back()) != "ctlmodel") {
                continue;
            }

            std::string data_object_path;
            for (std::size_t index = 2U; index + 1U < parts.size(); ++index) {
                if (parts[index].empty()) {
                    data_object_path.clear();
                    break;
                }
                if (!data_object_path.empty()) data_object_path.push_back('.');
                data_object_path += parts[index];
            }
            if (data_object_path.empty()) continue;

            ControlCandidate candidate;
            candidate.domain = domain;
            candidate.object_reference = domain + "/" + parts.front() + "." + data_object_path;
            candidate.ctl_model_item = item;

            const auto identity = lower_ascii(candidate.object_reference);
            if (seen.insert(identity).second) {
                candidates.push_back(std::move(candidate));
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        return left.object_reference < right.object_reference;
    });
    return candidates;
}

[[nodiscard]] const char* model_name(const ControlModel model) noexcept {
    switch (model) {
    case ControlModel::status_only: return "status-only";
    case ControlModel::direct_normal: return "direct-normal";
    case ControlModel::select_before_operate_normal: return "sbo-normal";
    case ControlModel::direct_enhanced: return "direct-enhanced";
    case ControlModel::select_before_operate_enhanced: return "sbo-enhanced";
    case ControlModel::unknown: return "unknown";
    }
    return "unknown";
}

[[nodiscard]] std::uint16_t parse_port(const std::string& value) {
    std::size_t consumed{};
    const auto parsed = std::stoul(value, &consumed, 10);
    if (consumed != value.size() || parsed == 0UL || parsed > 65'535UL) {
        throw std::invalid_argument("MMS TCP port must be in the range 1..65535.");
    }
    return static_cast<std::uint16_t>(parsed);
}

[[nodiscard]] std::chrono::milliseconds parse_timeout(const std::string& value) {
    std::size_t consumed{};
    const auto parsed = std::stoull(value, &consumed, 10);
    if (consumed != value.size() || parsed == 0ULL ||
        parsed > static_cast<unsigned long long>(std::numeric_limits<std::int64_t>::max())) {
        throw std::invalid_argument("--timeout-ms requires a positive millisecond value.");
    }
    return std::chrono::milliseconds{static_cast<std::int64_t>(parsed)};
}

[[nodiscard]] std::string json_escape(const std::string& value) {
    std::ostringstream output;
    for (const auto ch : value) {
        switch (ch) {
        case '\\': output << "\\\\"; break;
        case '"': output << "\\\""; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20U) output << '?';
            else output << ch;
        }
    }
    return output.str();
}

void print_usage() {
    std::cout
        << "Usage:\n"
        << "  ariec61850_control_inventory_probe <host> [port] [options]\n\n"
        << "Options:\n"
        << "  --timeout-ms N     MMS connect/request timeout (default 5000).\n"
        << "  --evidence FILE    Write JSON inventory evidence.\n"
        << "  --self-test        Offline parser/safety self-test; no network.\n\n"
        << "Safety:\n"
        << "  This executable is read-only. It has no control Write action and no arm token.\n"
        << "  It enumerates MMS domains/named variables, finds CF/.../ctlModel candidates,\n"
        << "  and validates candidates with Read/GVAA only.\n";
}

[[nodiscard]] Options parse_options(const int argc, char** argv) {
    Options options;
    options.endpoint.port = 102U;
    if (argc == 2 && std::string_view{argv[1]} == "--self-test") {
        options.self_test = true;
        return options;
    }
    if (argc < 2 || std::string_view{argv[1]} == "--help" ||
        std::string_view{argv[1]} == "-h") {
        return options;
    }

    options.endpoint.host = argv[1];
    int argument = 2;
    if (argument < argc && std::string_view{argv[argument]}.find("--") != 0U) {
        options.endpoint.port = parse_port(argv[argument]);
        ++argument;
    }
    while (argument < argc) {
        const std::string option = argv[argument++];
        if (option == "--help" || option == "-h") return Options{};
        if (argument >= argc) throw std::invalid_argument(option + " requires a value.");
        const std::string value = argv[argument++];
        if (option == "--timeout-ms") options.timeout = parse_timeout(value);
        else if (option == "--evidence") options.evidence_path = value;
        else throw std::invalid_argument("Unknown option: " + option);
    }
    return options;
}

void write_evidence(
    const Options& options,
    const MmsLiveDiscoveryResult& discovery,
    const std::vector<CandidateEvidence>& evidence) {
    if (options.evidence_path.empty()) return;
    std::ofstream output{options.evidence_path, std::ios::binary | std::ios::trunc};
    if (!output) {
        throw std::runtime_error("Cannot open evidence file: " + options.evidence_path);
    }

    std::size_t ready{};
    for (const auto& item : evidence) {
        if (item.operationally_ready) ++ready;
    }

    output << "{\n"
           << "  \"schema\": \"arstack61850-control-inventory-v1\",\n"
           << "  \"endpoint\": \"" << json_escape(options.endpoint.host) << ':'
           << options.endpoint.port << "\",\n"
           << "  \"readOnly\": true,\n"
           << "  \"mmsControlWriteCount\": 0,\n"
           << "  \"domainCount\": " << discovery.domain_count() << ",\n"
           << "  \"variableCount\": " << discovery.variable_count() << ",\n"
           << "  \"candidateCount\": " << evidence.size() << ",\n"
           << "  \"operationallyReadyCount\": " << ready << ",\n"
           << "  \"candidates\": [\n";
    for (std::size_t index = 0U; index < evidence.size(); ++index) {
        const auto& item = evidence[index];
        output << "    {\"object\": \"" << json_escape(item.candidate.object_reference)
               << "\", \"ctlModelItem\": \"" << json_escape(item.candidate.ctl_model_item)
               << "\", \"operationallyReady\": "
               << (item.operationally_ready ? "true" : "false")
               << ", \"ctlModel\": \"" << json_escape(item.ctl_model)
               << "\", \"cdc\": \"" << json_escape(item.cdc)
               << "\", \"statusObject\": \"" << json_escape(item.status_object)
               << "\", \"diagnostic\": \"" << json_escape(item.diagnostic) << "\"}";
        if (index + 1U != evidence.size()) output << ',';
        output << '\n';
    }
    output << "  ]\n}\n";
}

int run_self_test() {
    MmsDiscoverySnapshot snapshot;
    snapshot.domain_variables["LD0"] = {
        "LLN0$ST$Mod$stVal",
        "CSWI1$CF$Pos$ctlModel",
        "CSWI1$CO$Pos$Oper",
        "CILO1$CF$EnaOpn$ctlModel",
        "MMXU1$MX$A$phsA$cVal$mag$f",
    };
    const auto candidates = extract_control_candidates(snapshot);
    if (candidates.size() != 2U ||
        candidates[0].object_reference != "LD0/CILO1.EnaOpn" ||
        candidates[1].object_reference != "LD0/CSWI1.Pos") {
        std::cerr << "CONTROL_INVENTORY_SELF_TEST FAIL candidate extraction mismatch.\n";
        return 1;
    }
    std::cout << "CONTROL_INVENTORY_SELF_TEST PASS readOnly=true controlWritePath=false candidates=2\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        if (options.self_test) return run_self_test();
        if (options.endpoint.host.empty()) {
            print_usage();
            return argc > 1 ? 0 : 2;
        }

        MmsAssociationOptions association_options;
        association_options.connect_timeout = options.timeout;
        association_options.request_timeout = options.timeout;
        MmsTcpLiveDiscoverySession live_session{{}, association_options};
        live_session.connect(options.endpoint);

        MmsLiveDiscoveryOptions discovery_options;
        discovery_options.probe_variable_types = false;
        discovery_options.read_data_set_directories = false;
        discovery_options.probe_report_controls = false;
        const auto discovery = live_session.discover(discovery_options);

        std::cout << "CONTROL_INVENTORY_ENDPOINT " << options.endpoint.host << ':'
                  << options.endpoint.port << '\n';
        std::cout << "MMS_INVENTORY domains=" << discovery.domain_count()
                  << " variables=" << discovery.variable_count() << '\n';

        const auto candidates = extract_control_candidates(discovery.names);
        MmsAssociationControlTransport transport{live_session.association()};
        std::vector<CandidateEvidence> evidence;
        evidence.reserve(candidates.size());

        for (const auto& candidate : candidates) {
            CandidateEvidence item;
            item.candidate = candidate;
            try {
                const auto descriptor = ControlDescriptorDiscovery::discover(
                    transport, candidate.object_reference);
                item.operationally_ready = descriptor.operationally_ready();
                item.ctl_model = model_name(descriptor.model);
                item.cdc = descriptor.cdc;
                if (descriptor.status_object) item.status_object = descriptor.status_object->reference();
                std::cout << "CONTROL_READY object=" << candidate.object_reference
                          << " ctlModel=" << item.ctl_model
                          << " cdc=" << item.cdc
                          << " requiresSelect=" << (descriptor.requires_select() ? "true" : "false")
                          << " enhanced=" << (descriptor.enhanced() ? "true" : "false");
                if (!item.status_object.empty()) {
                    std::cout << " status=" << item.status_object;
                }
                std::cout << '\n';
            } catch (const std::exception& exception) {
                item.diagnostic = exception.what();
                std::cout << "CONTROL_CANDIDATE object=" << candidate.object_reference
                          << " ctlModelItem=" << candidate.ctl_model_item
                          << " ready=false reason=\"" << item.diagnostic << "\"\n";
            }
            evidence.push_back(std::move(item));
        }

        std::size_t ready{};
        for (const auto& item : evidence) {
            if (item.operationally_ready) ++ready;
        }
        std::cout << "CONTROL_INVENTORY_RESULT readOnly=true mmsControlWriteCount=0"
                  << " candidates=" << evidence.size()
                  << " ready=" << ready << '\n';
        std::cout << "PCAP_HINT filter=\"tcp.port == " << options.endpoint.port
                  << " && ip.addr == " << options.endpoint.host << "\"\n";

        write_evidence(options, discovery, evidence);
        live_session.disconnect();
        return ready > 0U ? 0 : 3;
    } catch (const std::exception& exception) {
        std::cerr << "Control inventory probe failed: " << exception.what() << '\n';
        return 1;
    }
}
