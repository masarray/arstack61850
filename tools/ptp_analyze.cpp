// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/capture/pcap.hpp"
#include "ariec61850/time_sync/ptp.hpp"
#include "ariec61850/time_sync/ptp_monitor.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using ar::iec61850::time_sync::PtpHealthSeverity;
using ar::iec61850::time_sync::PtpMessageType;
using ar::iec61850::time_sync::PtpPortIdentity;

[[nodiscard]] const char* severity_text(const PtpHealthSeverity severity) noexcept {
    switch (severity) {
    case PtpHealthSeverity::ok: return "ok";
    case PtpHealthSeverity::warning: return "warning";
    case PtpHealthSeverity::fail: return "fail";
    }
    return "unknown";
}

[[nodiscard]] std::string port_identity_text(const PtpPortIdentity& identity) {
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t index = 0U; index < identity.clock_identity.size(); ++index) {
        if (index != 0U) stream << ':';
        stream << std::setw(2) << static_cast<unsigned>(identity.clock_identity[index]);
    }
    stream << std::dec << '/' << identity.port_number;
    return stream.str();
}

[[nodiscard]] std::uint8_t parse_domain(const std::string_view text) {
    std::size_t consumed{};
    const auto value = std::stoul(std::string{text}, &consumed, 0);
    if (consumed != text.size() || value > 255UL) {
        throw std::invalid_argument("PTP domain must be in the range 0..255.");
    }
    return static_cast<std::uint8_t>(value);
}

void print_help() {
    std::cout
        << "Usage: ariec61850_ptp_analyze <capture.pcap> [--domain N] [--json]\n"
        << "\n"
        << "Read-only PTPv2 Layer-2 analyzer for laboratory captures.\n"
        << "It reports source/domain/message visibility and sequence health;\n"
        << "it does not claim measured clock accuracy from host capture timestamps.\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            print_help();
            return 2;
        }

        std::filesystem::path capture_path;
        std::optional<std::uint8_t> expected_domain;
        bool json = false;

        for (int index = 1; index < argc; ++index) {
            const std::string_view argument{argv[index]};
            if (argument == "--help" || argument == "-h") {
                print_help();
                return 0;
            }
            if (argument == "--json") {
                json = true;
                continue;
            }
            if (argument == "--domain") {
                if (index + 1 >= argc) {
                    throw std::invalid_argument("--domain requires a value.");
                }
                expected_domain = parse_domain(argv[++index]);
                continue;
            }
            if (!capture_path.empty()) {
                throw std::invalid_argument("Unexpected argument: " + std::string{argument});
            }
            capture_path = std::filesystem::path{argument};
        }

        if (capture_path.empty()) {
            throw std::invalid_argument("A PCAP capture path is required.");
        }

        const auto packets = ar::iec61850::capture::PcapReader::read_all(capture_path);
        ar::iec61850::time_sync::PtpPassiveMonitor monitor;
        std::uint64_t ptp_frames = 0U;
        std::uint64_t other_frames = 0U;

        for (const auto& packet : packets) {
            ar::iec61850::time_sync::PtpFrame frame;
            if (ar::iec61850::time_sync::PtpCodec::try_parse_ethernet_frame(packet.frame, frame)) {
                monitor.observe(frame, packet.timestamp);
                ++ptp_frames;
            } else {
                ++other_frames;
            }
        }

        const auto captured_at = packets.empty()
            ? std::chrono::system_clock::now()
            : packets.back().timestamp;
        const auto snapshot = monitor.snapshot(captured_at);
        ar::iec61850::time_sync::PtpTimingHealthOptions options;
        options.expected_domain_number = expected_domain;
        const auto report = ar::iec61850::time_sync::PtpTimingHealthValidator::evaluate(snapshot, options);

        if (json) {
            std::cout << '{'
                      << "\"capturePackets\":" << packets.size() << ','
                      << "\"ptpFrames\":" << ptp_frames << ','
                      << "\"otherFrames\":" << other_frames << ','
                      << "\"health\":\"" << severity_text(report.severity) << "\","
                      << "\"sources\":[";
            for (std::size_t index = 0U; index < snapshot.sources.size(); ++index) {
                if (index != 0U) std::cout << ',';
                const auto& source = snapshot.sources[index];
                std::cout << '{'
                          << "\"domain\":" << static_cast<unsigned>(source.domain_number) << ','
                          << "\"portIdentity\":\"" << port_identity_text(source.source_port_identity) << "\","
                          << "\"announce\":" << source.count(PtpMessageType::announce) << ','
                          << "\"sync\":" << source.count(PtpMessageType::sync) << ','
                          << "\"followUp\":" << source.count(PtpMessageType::follow_up) << ','
                          << "\"pdelayReq\":" << source.count(PtpMessageType::pdelay_req) << ','
                          << "\"pdelayResp\":" << source.count(PtpMessageType::pdelay_resp) << ','
                          << "\"pdelayRespFollowUp\":" << source.count(PtpMessageType::pdelay_resp_follow_up) << ','
                          << "\"sequenceAnomalies\":" << source.sequence_anomaly_count;
                if (source.outer_vlan_id.has_value()) {
                    std::cout << ",\"outerVlan\":" << *source.outer_vlan_id;
                }
                if (source.vlan_id.has_value()) {
                    std::cout << ",\"vlan\":" << *source.vlan_id;
                }
                std::cout << '}';
            }
            std::cout << "],\"checks\":[";
            for (std::size_t index = 0U; index < report.checks.size(); ++index) {
                if (index != 0U) std::cout << ',';
                const auto& check = report.checks[index];
                std::cout << '{'
                          << "\"id\":\"" << check.id << "\","
                          << "\"severity\":\"" << severity_text(check.severity) << "\"";
                std::cout << '}';
            }
            std::cout << "]}\n";
            return report.severity == PtpHealthSeverity::fail ? 1 : 0;
        }

        std::cout << "PTP capture: packets=" << packets.size()
                  << " ptp=" << ptp_frames
                  << " other=" << other_frames
                  << " health=" << severity_text(report.severity) << '\n';
        for (const auto& source : snapshot.sources) {
            std::cout << "  domain=" << static_cast<unsigned>(source.domain_number)
                      << " source=" << port_identity_text(source.source_port_identity)
                      << " announce=" << source.count(PtpMessageType::announce)
                      << " sync=" << source.count(PtpMessageType::sync)
                      << " followUp=" << source.count(PtpMessageType::follow_up)
                      << " pdelayReq=" << source.count(PtpMessageType::pdelay_req)
                      << " pdelayResp=" << source.count(PtpMessageType::pdelay_resp)
                      << " seqAnomaly=" << source.sequence_anomaly_count
                      << '\n';
        }
        for (const auto& check : report.checks) {
            std::cout << "  [" << severity_text(check.severity) << "] "
                      << check.id << ": " << check.message << '\n';
        }
        return report.severity == PtpHealthSeverity::fail ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "PTP analyzer error: " << error.what() << '\n';
        return 2;
    }
}
