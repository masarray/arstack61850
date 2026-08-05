// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/evidence/pcap_equivalence.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " <capture.pcap> [--json]\n";
}

} // namespace

int main(const int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        print_usage(argv[0]);
        return 64;
    }

    const bool json = argc == 3 && std::string{argv[2]} == "--json";
    if (argc == 3 && !json) {
        print_usage(argv[0]);
        return 64;
    }

    try {
        const auto report = ar::iec61850::evidence::PcapEquivalenceAnalyzer::analyze(
            std::filesystem::path{argv[1]});
        if (json) {
            std::cout << ar::iec61850::evidence::PcapEquivalenceAnalyzer::to_json(report);
        } else {
            std::cout << "PCAP packets: " << report.packet_count << '\n'
                      << "GOOSE: " << report.goose_packet_count << '\n'
                      << "Sampled Values: " << report.sampled_values_packet_count << '\n'
                      << "Other Ethernet: " << report.other_ethernet_packet_count << '\n'
                      << "Malformed process-bus/Ethernet: "
                      << report.malformed_packet_count << '\n'
                      << "Exact decode/re-encode matches: "
                      << report.exact_reencode_match_count << '\n'
                      << "Decode/re-encode mismatches: "
                      << report.reencode_mismatch_count << '\n'
                      << "Canonical PCAP round trip: "
                      << (report.canonical_pcap_round_trip_match ? "PASS" : "FAIL") << '\n'
                      << "Overall evidence result: "
                      << (report.passed() ? "PASS" : "FAIL") << '\n';
        }
        return report.passed() ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "PCAP analysis failed: " << error.what() << '\n';
        return 1;
    }
}
