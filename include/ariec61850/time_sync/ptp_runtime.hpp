// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/time_sync/ptp.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ar::iec61850::time_sync {

[[nodiscard]] bool try_parse_ptp_clock_identity(
    std::string_view text,
    PtpClockIdentity& identity) noexcept;
[[nodiscard]] std::string format_ptp_clock_identity(const PtpClockIdentity& identity);
[[nodiscard]] PtpClockIdentity ptp_clock_identity_from_mac(
    const std::array<std::uint8_t, 6>& mac) noexcept;

struct PtpPublisherOptions final {
    std::uint8_t transport_specific{};
    std::uint8_t domain_number{};
    std::array<std::uint8_t, 6> source_mac{0x02U, 0x00U, 0x00U, 0xFFU, 0xFEU, 0x00U};
    std::optional<std::uint16_t> vlan_id;
    std::uint8_t vlan_priority{4U};
    PtpClockIdentity clock_identity{0x02U, 0x00U, 0x00U, 0xFFU, 0xFEU, 0x00U, 0x00U, 0x01U};
    std::uint16_t port_number{1U};
    std::chrono::milliseconds announce_interval{1000};
    std::chrono::milliseconds sync_interval{250};
    std::chrono::milliseconds follow_up_delay{};
    bool two_step_clock{true};
    bool respond_to_peer_delay{true};
    std::uint8_t priority1{128U};
    std::uint8_t priority2{128U};
    std::uint8_t clock_class{248U};
    PtpClockAccuracy clock_accuracy{PtpClockAccuracy::unknown};
    std::uint16_t offset_scaled_log_variance{0xFFFFU};
    PtpTimeSource time_source{PtpTimeSource::internal_oscillator};
    std::int16_t current_utc_offset{37};
    std::uint16_t steps_removed{};

    [[nodiscard]] PtpPortIdentity source_port_identity() const noexcept {
        return {clock_identity, port_number};
    }
};

struct PtpPublisherStatus final {
    bool is_running{};
    std::optional<std::chrono::system_clock::time_point> started_at;
    std::optional<std::chrono::system_clock::time_point> last_sent_at;
    std::uint64_t announce_sent{};
    std::uint64_t sync_sent{};
    std::uint64_t follow_up_sent{};
    std::uint64_t peer_delay_responses_sent{};
    std::string last_error;
};

struct PtpDueMessages final {
    bool announce{};
    bool sync{};
};

struct PtpPreparedFrame final {
    PtpMessageType message_type{PtpMessageType::sync};
    std::uint16_t sequence_id{};
    std::array<std::uint8_t, 6> destination_mac{};
    std::vector<std::uint8_t> ethernet_frame;
};

class PtpSequenceCounters final {
public:
    [[nodiscard]] std::uint16_t next(PtpMessageType message_type) noexcept;
    void reset() noexcept;

private:
    std::array<std::uint16_t, 16> counters_{};
};

/**
 * Portable PTPv2 publisher runtime state machine.
 *
 * This class owns profile/configuration, lifecycle, scheduling, sequence counters,
 * frame preparation, and status accounting. It intentionally does not own a NIC,
 * thread, or clock servo. Platform adapters are responsible for transport and for
 * supplying hardware timestamps when available.
 */
class PtpPublisherRuntime final {
public:
    explicit PtpPublisherRuntime(PtpPublisherOptions options = {});

    [[nodiscard]] static bool validate_options(
        const PtpPublisherOptions& options,
        std::string& error) noexcept;

    [[nodiscard]] const PtpPublisherOptions& options() const noexcept { return options_; }
    [[nodiscard]] PtpPublisherStatus status() const { return status_; }

    [[nodiscard]] bool reconfigure(PtpPublisherOptions options, std::string& error) noexcept;
    [[nodiscard]] bool start(
        std::chrono::system_clock::time_point wall_time = std::chrono::system_clock::now(),
        std::chrono::steady_clock::time_point monotonic_time = std::chrono::steady_clock::now()) noexcept;
    void stop() noexcept;

    [[nodiscard]] PtpDueMessages poll_due(
        std::chrono::steady_clock::time_point monotonic_time = std::chrono::steady_clock::now()) noexcept;

    [[nodiscard]] std::optional<PtpPreparedFrame> prepare_announce(PtpTimestamp origin_timestamp);
    [[nodiscard]] std::optional<PtpPreparedFrame> prepare_sync(PtpTimestamp origin_timestamp);
    [[nodiscard]] std::optional<PtpPreparedFrame> prepare_follow_up(
        std::uint16_t sync_sequence_id,
        PtpTimestamp precise_origin_timestamp);
    [[nodiscard]] std::optional<PtpPreparedFrame> prepare_pdelay_response(
        const PtpPortIdentity& requesting_port_identity,
        std::uint16_t request_sequence_id,
        std::int8_t request_log_message_interval,
        PtpTimestamp request_receipt_timestamp);
    [[nodiscard]] std::optional<PtpPreparedFrame> prepare_pdelay_response_follow_up(
        const PtpPortIdentity& requesting_port_identity,
        std::uint16_t request_sequence_id,
        std::int8_t request_log_message_interval,
        PtpTimestamp response_origin_timestamp);

    void record_sent(
        PtpMessageType message_type,
        std::chrono::system_clock::time_point wall_time = std::chrono::system_clock::now()) noexcept;
    void record_error(std::string error);
    void clear_error();

private:
    [[nodiscard]] static std::int8_t interval_to_log2(std::chrono::milliseconds interval) noexcept;
    [[nodiscard]] PtpBuildOptions make_build_options(
        std::uint16_t sequence_id,
        std::int8_t log_message_interval,
        bool two_step,
        PtpTimestamp timestamp) const noexcept;
    [[nodiscard]] std::optional<PtpPreparedFrame> wrap(
        PtpMessageType message_type,
        std::uint16_t sequence_id,
        const std::array<std::uint8_t, 6>& destination_mac,
        std::vector<std::uint8_t> ptp_message) const;

    PtpPublisherOptions options_;
    PtpPublisherStatus status_;
    PtpSequenceCounters sequences_;
    std::chrono::steady_clock::time_point next_announce_{};
    std::chrono::steady_clock::time_point next_sync_{};
};

} // namespace ar::iec61850::time_sync
