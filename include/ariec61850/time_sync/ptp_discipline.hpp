// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/time_sync/ptp.hpp"
#include "ariec61850/time_sync/ptp_monitor.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

namespace ar::iec61850::time_sync {

inline constexpr std::uint16_t ptp_flag_current_utc_offset_valid = 0x0004U;
inline constexpr std::uint16_t ptp_flag_ptp_timescale = 0x0008U;
inline constexpr std::uint16_t ptp_flag_time_traceable = 0x0010U;
inline constexpr std::uint16_t ptp_flag_frequency_traceable = 0x0020U;

/** Convert a PTP timestamp to signed nanoseconds when it fits in int64. */
[[nodiscard]] std::optional<std::int64_t> ptp_timestamp_nanoseconds(
    const PtpTimestamp& timestamp) noexcept;

/** Return left - right in nanoseconds when both timestamps fit safely. */
[[nodiscard]] std::optional<std::int64_t> ptp_timestamp_delta_nanoseconds(
    const PtpTimestamp& left,
    const PtpTimestamp& right) noexcept;

/**
 * Convert the IEEE 1588 correctionField from scaled nanoseconds (2^-16 ns)
 * to integral nanoseconds. Fractional nanoseconds are truncated toward zero.
 */
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

/** Compute peer mean path delay from a complete two-step Pdelay exchange. */
[[nodiscard]] std::optional<PtpPeerDelayMeasurement> calculate_peer_delay(
    const PtpPeerDelayExchange& exchange) noexcept;

struct PtpSyncExchange final {
    PtpTimestamp master_origin_timestamp{};
    PtpTimestamp local_receive_timestamp{};
    std::int64_t mean_path_delay_ns{};
    std::int64_t sync_correction_field{};
    std::int64_t follow_up_correction_field{};
};

/** Compute offsetFromMaster = local - master - pathDelay - correction. */
[[nodiscard]] std::optional<std::int64_t> calculate_offset_from_master(
    const PtpSyncExchange& exchange) noexcept;

/**
 * Conservative traceability classification for AUTO smpSynch promotion.
 * This is intentionally stricter than merely receiving valid Announce traffic.
 */
[[nodiscard]] bool ptp_announce_is_globally_traceable(const PtpFrame& frame) noexcept;

enum class PtpDisciplineState : std::uint8_t {
    unlocked,
    acquiring,
    locked,
    holdover,
    fault,
};

[[nodiscard]] const char* ptp_discipline_state_name(PtpDisciplineState state) noexcept;

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
    /** Add this signed amount to the local hardware clock. */
    std::int64_t phase_step_ns{};
    /** Absolute desired frequency correction relative to nominal, in ppb. */
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

/**
 * Portable phase/frequency discipline state machine.
 *
 * It owns no hardware. A platform adapter applies the returned phase/frequency
 * commands and reports actuation failures. LOCKED therefore means the measured
 * timestamp stream meets configured thresholds while clock commands are being
 * accepted by the platform; it is not a certification or GPS claim.
 */
class PtpClockDiscipline final {
public:
    explicit PtpClockDiscipline(PtpDisciplineOptions options = {}) noexcept;

    [[nodiscard]] static bool validate_options(const PtpDisciplineOptions& options) noexcept;

    void reset(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) noexcept;

    [[nodiscard]] PtpClockCommand observe(
        const PtpOffsetMeasurement& measurement,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) noexcept;

    void tick(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) noexcept;
    void record_actuation_failure() noexcept;

    [[nodiscard]] const PtpDisciplineStatus& status() const noexcept { return status_; }
    [[nodiscard]] std::optional<SmpSynchValue> measured_smp_synch() const noexcept;

private:
    [[nodiscard]] PtpClockCommand frequency_command(
        std::int64_t offset_ns,
        std::chrono::steady_clock::time_point now) noexcept;
    void reject_sample() noexcept;

    PtpDisciplineOptions options_{};
    PtpDisciplineStatus status_{};
    std::optional<std::chrono::steady_clock::time_point> last_sample_time_;
    std::optional<std::chrono::steady_clock::time_point> previous_servo_time_;
    std::optional<std::int64_t> previous_servo_offset_ns_;
    std::optional<double> filtered_path_delay_ns_;
    double filtered_frequency_ppb_{};
};

struct PtpTimeReceiverOptions final {
    std::uint8_t domain_number{};
    std::uint8_t transport_specific{};
    PtpPortIdentity local_port_identity{};
    std::chrono::milliseconds source_timeout{3000};
    std::chrono::milliseconds exchange_timeout{2000};
};

struct PtpTimeReceiverStatus final {
    std::optional<PtpPortIdentity> selected_source;
    std::optional<PtpClockIdentity> selected_grandmaster;
    std::optional<std::int64_t> mean_path_delay_ns;
    bool selected_source_globally_traceable{};
    std::uint64_t announce_frames{};
    std::uint64_t sync_frames{};
    std::uint64_t follow_up_frames{};
    std::uint64_t pdelay_responses{};
    std::uint64_t completed_pdelay_exchanges{};
    std::uint64_t completed_sync_exchanges{};
    std::uint64_t rejected_exchanges{};
};

/**
 * Bounded single-port PTP time-receiver correlator.
 *
 * It intentionally implements deterministic source preference and timestamp
 * correlation, not full IEEE 1588 BMCA. A better Announce source may replace
 * the current source; stale sources are dropped. The class owns no NIC/threads.
 */
class PtpTimeReceiver final {
public:
    explicit PtpTimeReceiver(PtpTimeReceiverOptions options = {}) noexcept;

    void reset() noexcept;
    void reconfigure(PtpTimeReceiverOptions options) noexcept;

    /** Returns true when the selected source changed. */
    [[nodiscard]] bool observe_announce(
        const PtpFrame& frame,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) noexcept;

    void note_pdelay_request(
        std::uint16_t sequence_id,
        const PtpTimestamp& request_tx_timestamp,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) noexcept;

    [[nodiscard]] bool observe_pdelay_response(
        const PtpFrame& frame,
        const PtpTimestamp& local_rx_timestamp,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) noexcept;

    [[nodiscard]] std::optional<PtpPeerDelayMeasurement> observe_pdelay_response_follow_up(
        const PtpFrame& frame,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) noexcept;

    [[nodiscard]] std::optional<PtpOffsetMeasurement> observe_sync(
        const PtpFrame& frame,
        const PtpTimestamp& local_rx_timestamp,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) noexcept;

    [[nodiscard]] std::optional<PtpOffsetMeasurement> observe_follow_up(
        const PtpFrame& frame,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) noexcept;

    /** Returns true when a stale selected source was dropped. */
    [[nodiscard]] bool tick(
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) noexcept;

    [[nodiscard]] const PtpTimeReceiverStatus& status() const noexcept { return status_; }

private:
    struct AnnounceQuality final {
        std::uint8_t priority1{255U};
        std::uint8_t clock_class{255U};
        std::uint8_t clock_accuracy{255U};
        std::uint16_t variance{0xFFFFU};
        std::uint8_t priority2{255U};
        PtpClockIdentity grandmaster_identity{};
        std::uint16_t steps_removed{0xFFFFU};
    };

    struct PendingSync final {
        PtpPortIdentity source{};
        std::uint16_t sequence_id{};
        PtpTimestamp local_rx_timestamp{};
        std::int64_t sync_correction_field{};
        std::chrono::steady_clock::time_point observed_at{};
    };

    struct PendingPdelay final {
        std::uint16_t sequence_id{};
        PtpTimestamp t1{};
        PtpTimestamp t2{};
        PtpTimestamp t4{};
        std::int64_t response_correction_field{};
        bool response_received{};
        std::chrono::steady_clock::time_point started_at{};
    };

    [[nodiscard]] bool profile_matches(const PtpFrame& frame) const noexcept;
    [[nodiscard]] bool source_matches_selected(const PtpPortIdentity& source) const noexcept;
    [[nodiscard]] static bool better_quality(
        const AnnounceQuality& left,
        const AnnounceQuality& right) noexcept;
    void clear_exchanges() noexcept;
    [[nodiscard]] std::optional<PtpOffsetMeasurement> complete_sync(
        const PtpTimestamp& origin,
        std::int64_t follow_up_correction_field,
        std::uint16_t sequence_id) noexcept;

    PtpTimeReceiverOptions options_{};
    PtpTimeReceiverStatus status_{};
    std::optional<AnnounceQuality> selected_quality_;
    std::optional<std::chrono::steady_clock::time_point> selected_source_last_seen_;
    std::optional<PendingSync> pending_sync_;
    std::optional<PendingPdelay> pending_pdelay_;
};

} // namespace ar::iec61850::time_sync
