// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/evidence/pcap_equivalence.hpp"

#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/goose/frame_codec.hpp"
#include "ariec61850/sampled_values/frame_codec.hpp"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ar::iec61850::evidence {
namespace {

std::chrono::microseconds canonical_timestamp(
    const std::chrono::system_clock::time_point timestamp) noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        timestamp.time_since_epoch());
}

std::string json_escape(const std::string& value) {
    std::ostringstream output;
    for (const char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
        switch (character) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20U) {
                output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned>(character) << std::dec;
            } else {
                output << static_cast<char>(character);
            }
            break;
        }
    }
    return output.str();
}

PacketEvidence analyze_packet(
    const std::size_t packet_index,
    const capture::PcapPacket& packet) {
    PacketEvidence result;
    result.packet_index = packet_index;

    ethernet::EthernetFrame ethernet_frame;
    if (!ethernet::EthernetFrameCodec::try_decode(packet.frame, ethernet_frame)) {
        result.protocol = ProcessBusProtocol::malformed;
        result.diagnostics.emplace_back("Ethernet frame decode failed.");
        return result;
    }

    result.ether_type = ethernet_frame.ether_type;
    if (ethernet_frame.ether_type == ethernet::goose_ethertype) {
        result.protocol = ProcessBusProtocol::goose;
        goose::GooseFrame decoded;
        if (!goose::GooseFrameCodec::try_decode(packet.frame, decoded)) {
            result.diagnostics.emplace_back("GOOSE process-bus frame decode failed.");
            return result;
        }
        result.decoded = true;
        result.app_id = decoded.app_id;
        result.stream_identifier = decoded.pdu.go_cb_ref;
        const auto reencoded = goose::GooseFrameCodec::encode(decoded);
        result.exact_reencode_match = reencoded == packet.frame;
        if (!result.exact_reencode_match) {
            result.diagnostics.emplace_back(
                "GOOSE decode/re-encode bytes differ from the captured frame.");
        }
        return result;
    }

    if (ethernet_frame.ether_type == ethernet::sampled_values_ethertype) {
        result.protocol = ProcessBusProtocol::sampled_values;
        sampled_values::SampledValuesFrame decoded;
        if (!sampled_values::SampledValuesFrameCodec::try_decode(packet.frame, decoded)) {
            result.diagnostics.emplace_back("Sampled Values process-bus frame decode failed.");
            return result;
        }
        result.decoded = true;
        result.app_id = decoded.app_id;
        if (!decoded.pdu.asdus.empty()) {
            result.stream_identifier = decoded.pdu.asdus.front().sv_id;
        }
        const auto reencoded = sampled_values::SampledValuesFrameCodec::encode(decoded);
        result.exact_reencode_match = reencoded == packet.frame;
        if (!result.exact_reencode_match) {
            result.diagnostics.emplace_back(
                "Sampled Values decode/re-encode bytes differ from the captured frame.");
        }
        return result;
    }

    result.protocol = ProcessBusProtocol::other_ethernet;
    result.decoded = true;
    result.exact_reencode_match =
        ethernet::EthernetFrameCodec::encode(ethernet_frame) == packet.frame;
    if (!result.exact_reencode_match) {
        result.diagnostics.emplace_back(
            "Generic Ethernet decode/re-encode bytes differ from the captured frame.");
    }
    return result;
}

} // namespace

bool PcapEquivalenceReport::passed() const noexcept {
    return process_bus_packet_count > 0U && malformed_packet_count == 0U &&
           reencode_mismatch_count == 0U && canonical_pcap_round_trip_match;
}

PcapEquivalenceReport PcapEquivalenceAnalyzer::analyze(
    const std::span<const capture::PcapPacket> packets) {
    PcapEquivalenceReport report;
    report.packet_count = packets.size();
    report.canonical_pcap_round_trip_match = canonical_pcap_round_trip_matches(packets);
    report.packets.reserve(packets.size());

    for (std::size_t index = 0U; index < packets.size(); ++index) {
        auto packet = analyze_packet(index, packets[index]);
        switch (packet.protocol) {
        case ProcessBusProtocol::goose:
            ++report.process_bus_packet_count;
            ++report.goose_packet_count;
            break;
        case ProcessBusProtocol::sampled_values:
            ++report.process_bus_packet_count;
            ++report.sampled_values_packet_count;
            break;
        case ProcessBusProtocol::other_ethernet:
            ++report.other_ethernet_packet_count;
            break;
        case ProcessBusProtocol::malformed:
            ++report.malformed_packet_count;
            break;
        }

        if ((packet.protocol == ProcessBusProtocol::goose ||
             packet.protocol == ProcessBusProtocol::sampled_values) &&
            !packet.decoded) {
            ++report.malformed_packet_count;
        } else if (packet.decoded && packet.exact_reencode_match) {
            ++report.exact_reencode_match_count;
        } else if (packet.decoded) {
            ++report.reencode_mismatch_count;
        }
        report.packets.push_back(std::move(packet));
    }
    return report;
}

PcapEquivalenceReport PcapEquivalenceAnalyzer::analyze(
    const std::filesystem::path& pcap_path) {
    return analyze(capture::PcapReader::read_all(pcap_path));
}

bool PcapEquivalenceAnalyzer::canonical_pcap_round_trip_matches(
    const std::span<const capture::PcapPacket> packets) {
    try {
        std::stringstream stream{
            std::ios::in | std::ios::out | std::ios::binary};
        capture::PcapWriter writer{stream};
        for (const auto& packet : packets) {
            writer.write_packet(packet.timestamp, packet.frame);
        }
        stream.seekg(0, std::ios::beg);
        const auto decoded = capture::PcapReader::read_all(stream);
        if (decoded.size() != packets.size()) {
            return false;
        }
        for (std::size_t index = 0U; index < packets.size(); ++index) {
            if (decoded[index].frame != packets[index].frame ||
                canonical_timestamp(decoded[index].timestamp) !=
                    canonical_timestamp(packets[index].timestamp)) {
                return false;
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::string PcapEquivalenceAnalyzer::to_json(
    const PcapEquivalenceReport& report) {
    std::ostringstream output;
    output << "{\n"
           << "  \"passed\": " << (report.passed() ? "true" : "false") << ",\n"
           << "  \"packet_count\": " << report.packet_count << ",\n"
           << "  \"process_bus_packet_count\": " << report.process_bus_packet_count << ",\n"
           << "  \"goose_packet_count\": " << report.goose_packet_count << ",\n"
           << "  \"sampled_values_packet_count\": "
           << report.sampled_values_packet_count << ",\n"
           << "  \"other_ethernet_packet_count\": "
           << report.other_ethernet_packet_count << ",\n"
           << "  \"malformed_packet_count\": " << report.malformed_packet_count << ",\n"
           << "  \"exact_reencode_match_count\": "
           << report.exact_reencode_match_count << ",\n"
           << "  \"reencode_mismatch_count\": "
           << report.reencode_mismatch_count << ",\n"
           << "  \"canonical_pcap_round_trip_match\": "
           << (report.canonical_pcap_round_trip_match ? "true" : "false") << ",\n"
           << "  \"packets\": [\n";

    for (std::size_t index = 0U; index < report.packets.size(); ++index) {
        const auto& packet = report.packets[index];
        output << "    {\"index\": " << packet.packet_index
               << ", \"protocol\": \"" << protocol_name(packet.protocol)
               << "\", \"ether_type\": " << packet.ether_type
               << ", \"decoded\": " << (packet.decoded ? "true" : "false")
               << ", \"exact_reencode_match\": "
               << (packet.exact_reencode_match ? "true" : "false");
        if (packet.app_id.has_value()) {
            output << ", \"app_id\": " << *packet.app_id;
        }
        if (!packet.stream_identifier.empty()) {
            output << ", \"stream_identifier\": \""
                   << json_escape(packet.stream_identifier) << "\"";
        }
        output << ", \"diagnostics\": [";
        for (std::size_t diagnostic = 0U;
             diagnostic < packet.diagnostics.size(); ++diagnostic) {
            if (diagnostic != 0U) {
                output << ", ";
            }
            output << "\"" << json_escape(packet.diagnostics[diagnostic]) << "\"";
        }
        output << "]}";
        if (index + 1U != report.packets.size()) {
            output << ',';
        }
        output << '\n';
    }
    output << "  ]\n}\n";
    return output.str();
}

std::string protocol_name(const ProcessBusProtocol protocol) {
    switch (protocol) {
    case ProcessBusProtocol::goose: return "GOOSE";
    case ProcessBusProtocol::sampled_values: return "Sampled Values";
    case ProcessBusProtocol::other_ethernet: return "Other Ethernet";
    case ProcessBusProtocol::malformed: return "Malformed";
    }
    return "Unknown";
}

} // namespace ar::iec61850::evidence
