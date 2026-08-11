// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/ethernet/ethernet.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ar::iec61850::time_sync {

inline constexpr std::uint16_t ptp_ethertype = 0x88F7U;
inline constexpr std::uint16_t ptp_vlan_ethertype = 0x8100U;
inline constexpr std::uint16_t ptp_qinq_ethertype = 0x88A8U;
inline constexpr std::uint8_t ptp_version = 2U;
inline constexpr std::size_t ptp_header_length = 34U;
inline constexpr std::array<std::uint8_t, 6> ptp_general_multicast_mac{
    0x01U, 0x1BU, 0x19U, 0x00U, 0x00U, 0x00U};
inline constexpr std::array<std::uint8_t, 6> ptp_peer_delay_multicast_mac{
    0x01U, 0x80U, 0xC2U, 0x00U, 0x00U, 0x0EU};

enum class PtpMessageType : std::uint8_t {
    sync = 0x0U,
    delay_req = 0x1U,
    pdelay_req = 0x2U,
    pdelay_resp = 0x3U,
    follow_up = 0x8U,
    delay_resp = 0x9U,
    pdelay_resp_follow_up = 0xAU,
    announce = 0xBU,
    signaling = 0xCU,
    management = 0xDU,
};

enum class PtpClockAccuracy : std::uint8_t {
    within_25_ns = 0x20U,
    within_100_ns = 0x21U,
    within_250_ns = 0x22U,
    within_1_us = 0x23U,
    within_2_5_us = 0x24U,
    within_10_us = 0x25U,
    within_25_us = 0x26U,
    within_100_us = 0x27U,
    within_250_us = 0x28U,
    within_1_ms = 0x29U,
    within_2_5_ms = 0x2AU,
    within_10_ms = 0x2BU,
    greater_than_10_s = 0x31U,
    unknown = 0xFEU,
};

enum class PtpTimeSource : std::uint8_t {
    atomic_clock = 0x10U,
    gps = 0x20U,
    terrestrial_radio = 0x30U,
    ptp = 0x40U,
    ntp = 0x50U,
    hand_set = 0x60U,
    other = 0x90U,
    internal_oscillator = 0xA0U,
};

using PtpClockIdentity = std::array<std::uint8_t, 8>;

struct PtpPortIdentity final {
    PtpClockIdentity clock_identity{};
    std::uint16_t port_number{1U};

    friend bool operator==(const PtpPortIdentity&, const PtpPortIdentity&) = default;
};

struct PtpTimestamp final {
    std::uint64_t seconds{}; // PTP wire field is 48-bit; upper bits are rejected when encoded.
    std::uint32_t nanoseconds{};

    [[nodiscard]] static PtpTimestamp from_system_time(
        std::chrono::system_clock::time_point time) noexcept;
    [[nodiscard]] static PtpTimestamp now_utc() noexcept;
    [[nodiscard]] static bool try_read(std::span<const std::uint8_t> source, PtpTimestamp& timestamp) noexcept;
    [[nodiscard]] bool write(std::span<std::uint8_t> destination) const noexcept;

    friend bool operator==(const PtpTimestamp&, const PtpTimestamp&) = default;
};

struct PtpHeader final {
    std::uint8_t transport_specific{};
    PtpMessageType message_type{PtpMessageType::sync};
    std::uint8_t version{ptp_version};
    std::uint16_t message_length{};
    std::uint8_t domain_number{};
    std::uint16_t flags{};
    std::int64_t correction_field{};
    PtpPortIdentity source_port_identity{};
    std::uint16_t sequence_id{};
    std::uint8_t control_field{};
    std::int8_t log_message_interval{};

    [[nodiscard]] bool is_two_step() const noexcept { return (flags & 0x0200U) != 0U; }

    friend bool operator==(const PtpHeader&, const PtpHeader&) = default;
};

struct PtpAnnounceMessage final {
    PtpTimestamp origin_timestamp{};
    std::int16_t current_utc_offset{37};
    std::uint8_t priority1{128U};
    std::uint8_t clock_class{248U};
    PtpClockAccuracy clock_accuracy{PtpClockAccuracy::unknown};
    std::uint16_t offset_scaled_log_variance{0xFFFFU};
    std::uint8_t priority2{128U};
    PtpClockIdentity grandmaster_identity{};
    std::uint16_t steps_removed{};
    PtpTimeSource time_source{PtpTimeSource::internal_oscillator};

    friend bool operator==(const PtpAnnounceMessage&, const PtpAnnounceMessage&) = default;
};

struct PtpFrame final {
    PtpHeader header{};
    std::optional<PtpTimestamp> timestamp;
    std::optional<PtpAnnounceMessage> announce;
    std::vector<std::uint8_t> message;
    std::vector<std::uint8_t> body;
    std::optional<std::uint16_t> vlan_id;
    std::optional<std::uint16_t> outer_vlan_id;
    bool peer_delay_multicast{};
};

struct PtpBuildOptions final {
    std::uint8_t transport_specific{};
    std::uint8_t domain_number{};
    PtpPortIdentity source_port_identity{};
    std::uint16_t sequence_id{};
    std::int64_t correction_field{};
    std::int8_t log_message_interval{};
    bool two_step{true};
    std::uint8_t priority1{128U};
    std::uint8_t priority2{128U};
    std::uint8_t clock_class{248U};
    PtpClockAccuracy clock_accuracy{PtpClockAccuracy::unknown};
    std::uint16_t offset_scaled_log_variance{0xFFFFU};
    PtpClockIdentity grandmaster_identity{};
    std::uint16_t steps_removed{};
    PtpTimeSource time_source{PtpTimeSource::internal_oscillator};
    PtpTimestamp timestamp{};
};

class PtpCodec final {
public:
    [[nodiscard]] static std::vector<std::uint8_t> build_sync(const PtpBuildOptions& options);
    [[nodiscard]] static std::vector<std::uint8_t> build_follow_up(const PtpBuildOptions& options);
    [[nodiscard]] static std::vector<std::uint8_t> build_pdelay_req(const PtpBuildOptions& options);
    [[nodiscard]] static std::vector<std::uint8_t> build_pdelay_resp(
        const PtpBuildOptions& options,
        const PtpPortIdentity& requesting_port_identity);
    [[nodiscard]] static std::vector<std::uint8_t> build_pdelay_resp_follow_up(
        const PtpBuildOptions& options,
        const PtpPortIdentity& requesting_port_identity);
    [[nodiscard]] static std::vector<std::uint8_t> build_announce(
        const PtpBuildOptions& options,
        std::int16_t current_utc_offset = 37);

    [[nodiscard]] static std::vector<std::uint8_t> build_ethernet_frame(
        const std::array<std::uint8_t, 6>& destination_mac,
        const std::array<std::uint8_t, 6>& source_mac,
        std::span<const std::uint8_t> ptp_message,
        std::optional<std::uint16_t> vlan_id = std::nullopt,
        std::uint8_t vlan_priority = 0U);

    [[nodiscard]] static bool try_parse_message(
        std::span<const std::uint8_t> message,
        PtpFrame& frame) noexcept;
    [[nodiscard]] static bool try_parse_ethernet_frame(
        std::span<const std::uint8_t> ethernet_frame,
        PtpFrame& frame) noexcept;
};

} // namespace ar::iec61850::time_sync
