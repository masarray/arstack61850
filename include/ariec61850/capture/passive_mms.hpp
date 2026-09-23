// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/capture/pcap.hpp"
#include "ariec61850/osi/tpkt.hpp"
#include "ariec61850/osi/cotp.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ar::iec61850::capture {

struct PassiveMmsEvent final {
    std::chrono::system_clock::time_point timestamp;
    std::string source;
    std::string destination;
    std::string kind;
    std::string service;
    std::string detail;
    std::optional<std::uint32_t> invoke_id;
    std::size_t wire_bytes{};
};

struct PassiveMmsCounters final {
    std::uint64_t ethernet_packets{};
    std::uint64_t tcp_packets{};
    std::uint64_t mms_messages{};
    std::uint64_t decoded_requests{};
    std::uint64_t decoded_responses{};
    std::uint64_t decoded_reports{};
    std::uint64_t retransmissions{};
    std::uint64_t out_of_order{};
    std::uint64_t gaps{};
    std::uint64_t malformed{};
    std::uint64_t dropped{};
};

class PassiveMmsDecoder final {
public:
    static constexpr std::size_t maximum_flows = 128;
    static constexpr std::size_t maximum_events = 4096;
    static constexpr std::size_t maximum_tcp_buffer = 1024U * 1024U;
    static constexpr std::size_t maximum_pending_segments = 64;
    static constexpr std::size_t maximum_packet_bytes = 65535;

    void ingest(const PcapPacket& packet);
    void ingest(std::span<const std::uint8_t> ethernet,
                std::chrono::system_clock::time_point timestamp);
    void reset() noexcept;

    [[nodiscard]] const std::vector<PassiveMmsEvent>& events() const noexcept {
        return events_;
    }
    [[nodiscard]] const PassiveMmsCounters& counters() const noexcept {
        return counters_;
    }

private:
    struct Direction final {
        bool sequence_initialized{};
        std::uint32_t next_sequence{};
        std::map<std::uint32_t, std::vector<std::uint8_t>> pending;
        osi::TpktStreamDecoder tpkt{maximum_tcp_buffer};
        osi::CotpDataReassembler cotp{
            maximum_tcp_buffer, maximum_pending_segments, 8};
    };
    struct Flow final {
        Direction directions[2];
        std::map<std::uint32_t, std::string> outstanding;
    };

    void process_segment(
        Flow& flow, int direction, std::uint32_t sequence,
        bool syn, bool fin, bool rst,
        std::span<const std::uint8_t> payload,
        std::chrono::system_clock::time_point timestamp,
        const std::string& source, const std::string& destination);
    void append_contiguous(
        Flow& flow, int direction,
        std::span<const std::uint8_t> payload,
        std::chrono::system_clock::time_point timestamp,
        const std::string& source, const std::string& destination);
    void record(PassiveMmsEvent event);

    std::map<std::string, Flow> flows_;
    std::vector<PassiveMmsEvent> events_;
    PassiveMmsCounters counters_{};
};

} // namespace ar::iec61850::capture
