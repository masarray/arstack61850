// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/time_sync/ptp.hpp"
#include "ariec61850/time_sync/ptp_monitor.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>

namespace ar::iec61850::time_sync {

inline constexpr std::uint16_t ptp_flag_current_utc_offset_valid = 0x0004U;
inline constexpr std::uint16_t ptp_flag_ptp_timescale = 0x0008U;
inline constexpr std::uint16_t ptp_flag_time_traceable = 0x0010U;
inline constexpr std::uint16_t ptp_flag_frequency_traceable = 0x0020U;
inline constexpr std::int64_t ptp_nanoseconds_per_second = 1'000'000'000LL;

namespace discipline_detail {

[[nodiscard]] inline bool checked_add(
    const std::int64_t left,
    const std::int64_t right,
    std::int64_t& result) noexcept {
    if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
        (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right)) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] inline bool checked_subtract(
    const std::int64_t left,
    const std::int64_t right,
    std::int64_t& result) noexcept {
    if (right == std::numeric_limits<std::int64_t>::min()) {
        if (left >= 0) return false;
        result = left - right;
        return true;
    }
    return checked_add(left, -right, result);
}

[[nodiscard]] inline std::uint64_t magnitude(const std::int64_t value) noexcept {
    if (value >= 0) return static_cast<std::uint64_t>(value);
    return static_cast<std::uint64_t>(-(value + 1LL)) + 1ULL;
}

[[nodiscard]] inline bool identity_is_zero(const PtpClockIdentity& identity) noexcept {
    return std::all_of(identity.begin(), identity.end(), [](const std::uint8_t value) {
        return value == 0U;
    });
}

} // namespace discipline_detail

[[nodiscard]] inline std::optional<std::int64_t> ptp_timestamp_nanoseconds(
    const PtpTimestamp& timestamp) noexcept {
    if (timestamp.nanoseconds >= static_cast<std::uint32_t>(ptp_nanoseconds_per_second)) {
        return std::nullopt;
    }
    const auto ns = static_cast<std::int64_t>(timestamp.nanoseconds);
    const auto max_seconds = static_cast<std::uint64_t>(
        (std::numeric_limits<std::int64_t>::max() - ns) / ptp_nanoseconds_per_second);
    if (timestamp.seconds > max_seconds) return std::nullopt;
    return static_cast<std::int64_t>(timestamp.seconds) * ptp_nanoseconds_per_second + ns;
}

[[nodiscard]] inline std::optional<std::int64_t> ptp_timestamp_delta_nanoseconds(
    const PtpTimestamp& left,
    const PtpTimestamp& right) noexcept {
    const auto left_ns = ptp_timestamp_nanoseconds(left);
    const auto right_ns = ptp_timestamp_nanoseconds(right);
    if (!left_ns.has_value() || !right_ns.has_value()) return std::nullopt;
    std::int64_t result{};
    if (!discipline_detail::checked_subtract(*left_ns, *right_ns, result)) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] constexpr std::int64_t ptp_correction_nanoseconds(
    const std::int64_t correction_field) noexcept {
    return correction_field / 65'536LL;
}

struct PtpPeerDelayExchange final {
    PtpTimestamp t1_request_tx{};
    PtpTimestamp t2_peer_request_rx{};
    PtpTimestamp t3_peer_response_tx{};
    PtpTimestamp t4_response_rx{};
    std::int64_t response_correction_field{};
    std::int64_t response_follow_up_correction_field{};
};

struct PtpPeerDelayMeasurement final {
    std::int64_t mean_path_delay_ns{};
    std::int64_t round_trip_path_ns{};
};

[[nodiscard]] inline std::optional<PtpPeerDelayMeasurement> calculate_peer_delay(
    const PtpPeerDelayExchange& exchange) noexcept {
    const auto t2_minus_t1 = ptp_timestamp_delta_nanoseconds(
        exchange.t2_peer_request_rx,
        exchange.t1_request_tx);
    const auto t4_minus_t3 = ptp_timestamp_delta_nanoseconds(
        exchange.t4_response_rx,
        exchange.t3_peer_response_tx);
    if (!t2_minus_t1.has_value() || !t4_minus_t3.has_value()) return std::nullopt;

    std::int64_t path_sum{};
    if (!discipline_detail::checked_add(*t2_minus_t1, *t4_minus_t3, path_sum)) {
        return std::nullopt;
    }
    if (!discipline_detail::checked_subtract(
            path_sum,
            ptp_correction_nanoseconds(exchange.response_correction_field),
            path_sum) ||
        !discipline_detail::checked_subtract(
            path_sum,
            ptp_correction_nanoseconds(exchange.response_follow_up_correction_field),
            path_sum) ||
        path_sum < 0) {
        return std::nullopt;
    }
    return PtpPeerDelayMeasurement{path_sum / 2LL, path_sum};
}

struct PtpSyncExchange final {
    PtpTimestamp master_origin_timestamp{};
    PtpTimestamp local_receive_timestamp{};
    std::int64_t mean_path_delay_ns{};
    std::int64_t sync_correction_field{};
    std::int64_t follow_up_correction_field{};
};

[[nodiscard]] inline std::optional<std::int64_t> calculate_offset_from_master(
    const PtpSyncExchange& exchange) noexcept {
    if (exchange.mean_path_delay_ns < 0) return std::nullopt;
    const auto local_minus_master = ptp_timestamp_delta_nanoseconds(
        exchange.local_receive_timestamp,
        exchange.master_origin_timestamp);
    if (!local_minus_master.has_value()) return std::nullopt;

    std::int64_t result = *local_minus_master;
    if (!discipline_detail::checked_subtract(result, exchange.mean_path_delay_ns, result) ||
        !discipline_detail::checked_subtract(
            result,
            ptp_correction_nanoseconds(exchange.sync_correction_field),
            result) ||
        !discipline_detail::checked_subtract(
            result,
            ptp_correction_nanoseconds(exchange.follow_up_correction_field),
            result)) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] inline bool ptp_announce_is_globally_traceable(
    const PtpFrame& frame) noexcept {
    if (frame.header.message_type != PtpMessageType::announce || !frame.announce.has_value()) {
        return false;
    }
    const auto required_flags = static_cast<std::uint16_t>(
        ptp_flag_current_utc_offset_valid |
        ptp_flag_ptp_timescale |
        ptp_flag_time_traceable);
    if ((frame.header.flags & required_flags) != required_flags) return false;

    const auto& announce = *frame.announce;
    if (announce.clock_class >= 128U) return false;
    if (announce.time_source == PtpTimeSource::internal_oscillator ||
        announce.time_source == PtpTimeSource::hand_set) {
        return false;
    }
    return !discipline_detail::identity_is_zero(announce.grandmaster_identity);
}

enum class PtpDisciplineState : std::uint8_t {
    unlocked,
    acquiring,
    locked,
    holdover,
    fault,
};

[[nodiscard]] inline const char* ptp_discipline_state_name(
    const PtpDisciplineState state) noexcept {
    switch (state) {
    case PtpDisciplineState::unlocked: return "UNLOCKED";
    case PtpDisciplineState::acquiring: return "ACQUIRING";
    case PtpDisciplineState::locked: return "LOCKED";
    case PtpDisciplineState::holdover: return "HOLDOVER";
    case PtpDisciplineState::fault: return "FAULT";
    }
    return "FAULT";
}

struct PtpOffsetMeasurement final {
    PtpPortIdentity source_port_identity{};
    std::uint16_t sync_sequence_id{};
    std::int64_t offset_from_master_ns{};
    std::int64_t mean_path_delay_ns{};
    bool globally_traceable{};
};

enum class PtpClockCommandKind : std::uint8_t {
    none,
    step_phase,
    set_frequency,
};

struct PtpClockCommand final {
    PtpClockCommandKind kind{PtpClockCommandKind::none};
    std::int64_t phase_step_ns{};
    std::int32_t frequency_adjustment_ppb{};
};

struct PtpDisciplineOptions final {
    std::int64_t maximum_path_delay_ns{5'000'000LL};
    std::int64_t maximum_path_delay_jitter_ns{100'000LL};
    std::int64_t lock_offset_threshold_ns{2'000LL};
    std::int64_t unlock_offset_threshold_ns{20'000LL};
    std::int64_t phase_step_threshold_ns{1'000'000LL};
    std::int64_t fault_offset_threshold_ns{5'000'000'000LL};
    std::uint32_t lock_required_samples{8U};
    std::uint32_t unlock_required_samples{3U};
    std::uint32_t invalid_samples_before_fault{8U};
    std::int32_t maximum_frequency_adjustment_ppb{100'000};
    std::chrono::milliseconds phase_time_constant{2000};
    std::chrono::milliseconds sync_timeout{1000};
    std::chrono::milliseconds holdover_timeout{5000};
    double frequency_filter_alpha{0.25};
};

struct PtpDisciplineStatus final {
    PtpDisciplineState state{PtpDisciplineState::unlocked};
    std::optional<std::int64_t> offset_from_master_ns;
    std::optional<std::int64_t> mean_path_delay_ns;
    std::optional<std::int64_t> path_delay_jitter_ns;
    std::int32_t frequency_adjustment_ppb{};
    std::uint64_t accepted_samples{};
    std::uint64_t rejected_samples{};
    std::uint64_t phase_steps{};
    std::uint64_t frequency_updates{};
    std::uint32_t consecutive_qualified_samples{};
    std::uint32_t consecutive_bad_samples{};
    bool globally_traceable{};
};

} // namespace ar::iec61850::time_sync
