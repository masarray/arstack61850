// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/live_discovery.hpp"
#include "ariec61850/mms/rcb_contention.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace ar::iec61850;

void print_usage() {
    std::cout
        << "Usage: ariec61850_rcb_contention_probe <host> [port] [options]\n"
        << "Options:\n"
        << "  --rcb REF              Probe this exact RCB reference; default is first discovered RCB.\n"
        << "  --probe-count N        Number of read-only probes (C# default 1).\n"
        << "  --probe-delay-ms N     Delay between probes (C# default 1000 ms).\n"
        << "  --cooldown-sec N       Evidence cooldown if contended (C# default 60 s).\n"
        << "  --timeout-ms N         Association/request timeout (default 5000 ms).\n"
        << "\n"
        << "This tool sends only discovery GetNameList plus repeated MMS Read requests.\n"
        << "It never sends Write, Resv, RptEna, GI, control, or DataSet mutation.\n";
}

[[nodiscard]] std::uint16_t parse_port(const std::string& value) {
    std::size_t consumed{};
    const auto parsed = std::stoul(value, &consumed, 10);
    if (consumed != value.size() || parsed == 0UL || parsed > 65'535UL) {
        throw std::invalid_argument("MMS TCP port must be in the range 1..65535.");
    }
    return static_cast<std::uint16_t>(parsed);
}

[[nodiscard]] std::size_t parse_positive_size(
    const std::string& option,
    const std::string& value) {
    std::size_t consumed{};
    const auto parsed = std::stoull(value, &consumed, 10);
    if (consumed != value.size() || parsed == 0ULL ||
        parsed > static_cast<unsigned long long>(
            std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument(option + " requires a positive bounded integer.");
    }
    return static_cast<std::size_t>(parsed);
}

[[nodiscard]] std::uint64_t parse_nonnegative_u64(
    const std::string& option,
    const std::string& value) {
    std::size_t consumed{};
    const auto parsed = std::stoull(value, &consumed, 10);
    if (consumed != value.size()) {
        throw std::invalid_argument(option + " requires a non-negative integer.");
    }
    return parsed;
}

[[nodiscard]] std::string trim(std::string value) {
    const auto not_space = [](const unsigned char character) {
        return std::isspace(character) == 0;
    };
    const auto first = std::find_if(
        value.begin(), value.end(), [&](const char character) {
            return not_space(static_cast<unsigned char>(character));
        });
    const auto last = std::find_if(
        value.rbegin(), value.rend(), [&](const char character) {
            return not_space(static_cast<unsigned char>(character));
        }).base();
    if (first >= last) {
        return {};
    }
    return std::string{first, last};
}

[[nodiscard]] std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const char character) {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    });
    return value;
}

[[nodiscard]] std::string normalize_reference(std::string value) {
    value = trim(std::move(value));
    std::replace(value.begin(), value.end(), '$', '.');
    return lower_ascii(std::move(value));
}

[[nodiscard]] const mms::MmsReportControlCandidate& select_candidate(
    const mms::MmsLiveDiscoveryResult& discovery,
    const std::string& requested_reference) {
    if (discovery.report_inventory.report_controls.empty()) {
        throw std::runtime_error("No RCB was discovered on the target IED.");
    }
    if (requested_reference.empty()) {
        return discovery.report_inventory.report_controls.front();
    }
    const auto requested = normalize_reference(requested_reference);
    const auto found = std::find_if(
        discovery.report_inventory.report_controls.begin(),
        discovery.report_inventory.report_controls.end(),
        [&](const auto& candidate) {
            return normalize_reference(candidate.reference) == requested;
        });
    if (found == discovery.report_inventory.report_controls.end()) {
        throw std::invalid_argument(
            "Requested RCB was not found in live inventory: " + requested_reference);
    }
    return *found;
}

[[nodiscard]] std::string_view text_or_dash(const std::string& value) noexcept {
    return value.empty() ? std::string_view{"-"} : std::string_view{value};
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

        std::string requested_rcb;
        mms::MmsRcbContentionProbeOptions probe_options;
        std::chrono::milliseconds timeout{5'000};

        while (argument < argc) {
            const std::string option = argv[argument++];
            if (option == "--help" || option == "-h") {
                print_usage();
                return 0;
            }
            if (option != "--rcb" && option != "--probe-count" &&
                option != "--probe-delay-ms" && option != "--cooldown-sec" &&
                option != "--timeout-ms") {
                throw std::invalid_argument("Unknown option: " + option);
            }
            if (argument >= argc) {
                throw std::invalid_argument("Missing value after " + option + '.');
            }
            const std::string value = argv[argument++];
            if (option == "--rcb") {
                requested_rcb = value;
            } else if (option == "--probe-count") {
                probe_options.probe_count = parse_positive_size(option, value);
            } else if (option == "--probe-delay-ms") {
                const auto parsed = parse_nonnegative_u64(option, value);
                if (parsed > static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max())) {
                    throw std::invalid_argument("--probe-delay-ms is too large.");
                }
                probe_options.probe_delay = std::chrono::milliseconds{
                    static_cast<std::int64_t>(parsed)};
            } else if (option == "--cooldown-sec") {
                const auto parsed = parse_nonnegative_u64(option, value);
                if (parsed > static_cast<std::uint64_t>(
                        std::numeric_limits<int>::max())) {
                    throw std::invalid_argument("--cooldown-sec is too large.");
                }
                probe_options.cooldown_seconds = static_cast<int>(parsed);
            } else {
                const auto parsed = parse_positive_size(option, value);
                if (parsed > static_cast<std::size_t>(
                        std::numeric_limits<std::int64_t>::max())) {
                    throw std::invalid_argument("--timeout-ms is too large.");
                }
                timeout = std::chrono::milliseconds{
                    static_cast<std::int64_t>(parsed)};
            }
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
        const auto discovery = session.discover(discovery_options);
        const auto& candidate = select_candidate(discovery, requested_rcb);

        mms::MmsRcbContentionProbeClient probe{session.association()};
        const auto result = probe.probe(candidate, probe_options);
        session.disconnect();

        std::cout << "Read-only RCB contention probe: endpoint="
                  << endpoint.host << ':' << endpoint.port
                  << ", associationProfile="
                  << (discovery.association_profile.empty()
                          ? std::string{"unknown"}
                          : discovery.association_profile)
                  << ", associationAttempts=" << discovery.association_attempts.size()
                  << ", RCB=" << result.rcb_reference
                  << ", probes=" << result.observations.size()
                  << ", busy=" << (result.is_busy_at_probe ? "true" : "false")
                  << ", flapping=" << (result.is_flapping ? "true" : "false")
                  << ", contended=" << (result.is_contended ? "true" : "false")
                  << ", decision=" << result.decision
                  << ", cooldownSec=" << result.cooldown_seconds << ".\n";

        for (const auto& observation : result.observations) {
            std::cout << "  probe=" << observation.probe_number
                      << " RptEna=" << text_or_dash(observation.rpt_ena)
                      << " Resv=" << text_or_dash(observation.resv)
                      << " ResvTms=" << text_or_dash(observation.resv_tms)
                      << " DatSet=" << text_or_dash(observation.data_set_reference)
                      << " ConfRev=" << text_or_dash(observation.conf_rev);
            if (!observation.message.empty()) {
                std::cout << " message=" << observation.message;
            }
            std::cout << '\n';
        }
        std::cout << "Reason: " << result.reason << '\n'
                  << "Recommended action: " << result.recommended_action << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RCB contention probe failed: " << error.what() << '\n';
        return 1;
    }
}
