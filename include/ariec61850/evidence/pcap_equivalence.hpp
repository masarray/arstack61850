// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/capture/pcap.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ar::iec61850::evidence {

enum class ProcessBusProtocol : std::uint8_t {
    goose,
    sampled_values,
    other_ethernet,
    malformed
};

struct PacketEvidence final {
    std::size_t packet_index{};
    ProcessBusProtocol protocol{ProcessBusProtocol::malformed};
    std::uint16_t ether_type{};
    std::optional<std::uint16_t> app_id;
    bool decoded{};
    bool exact_reencode_match{};
    std::string stream_identifier;
    std::vector<std::string> diagnostics;
};

struct PcapEquivalenceReport final {
    std::size_t packet_count{};
    std::size_t process_bus_packet_count{};
    std::size_t goose_packet_count{};
    std::size_t sampled_values_packet_count{};
    std::size_t other_ethernet_packet_count{};
    std::size_t malformed_packet_count{};
    std::size_t exact_reencode_match_count{};
    std::size_t reencode_mismatch_count{};
    bool canonical_pcap_round_trip_match{};
    std::vector<PacketEvidence> packets;

    [[nodiscard]] bool passed() const noexcept;
};

class PcapEquivalenceAnalyzer final {
public:
    [[nodiscard]] static PcapEquivalenceReport analyze(
        std::span<const capture::PcapPacket> packets);
    [[nodiscard]] static PcapEquivalenceReport analyze(
        const std::filesystem::path& pcap_path);
    [[nodiscard]] static bool canonical_pcap_round_trip_matches(
        std::span<const capture::PcapPacket> packets);
    [[nodiscard]] static std::string to_json(const PcapEquivalenceReport& report);
};

[[nodiscard]] std::string protocol_name(ProcessBusProtocol protocol);

} // namespace ar::iec61850::evidence
