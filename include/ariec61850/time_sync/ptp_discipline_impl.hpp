// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/time_sync/ptp.hpp"
#include "ariec61850/time_sync/ptp_monitor.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <tuple>
#include <utility>

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

[[nodiscard]] inline bool parse_pdelay_body(
    const PtpFrame& frame,
    PtpTimestamp& timestamp,
    PtpPortIdentity& requesting_port_identity) noexcept {
    if (frame.body.size() < 20U ||
        !PtpTimestamp::try_read(
            std::span<const std::uint8_t>{frame.body}.first(10U),
            timestamp)) {
        return false;
    }
    std::copy_n(
        frame.body.begin() + 10,
        8,
        requesting_port_identity.clock_identity.begin());
    requesting_port_identity.port_number = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(frame.body[18]) << 8U) |
        static_cast<std::uint16_t>(frame.body[19]));
    return true;
}

} // namespace discipline_detail

[[nodiscard]] inline std::optional<std::int64_t> ptp_timestamp_nanoseconds(
    const PtpTimestamp& timestamp) noexcept {
    if (timestamp.nanoseconds >= static_cast<std::uint32_t>(ptp_nanoseconds_per_second)) {
        return std::nullopt;
    }
    const auto ns = static_cast<std::int64_t>(timestamp.nanoseconds);
    const auto max_seconds = static_cast<std::uint64_t>(
        (std::numeric_limits<std::int64_t>::max() - ns) /
        ptp_nanoseconds_per_second);
    if (timestamp.seconds > max_seconds) return std::nullopt;
    return static_cast<std::int64_t>(timestamp.seconds) *
               ptp_nanoseconds_per_second +
           ns;
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
    if (!t2_minus_t1.has_value() || !t4_minus_t3.has_value()) {
        return std::nullopt;
    }

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
    if (!discipline_detail::checked_subtract(
            result,
            exchange.mean_path_delay_ns,
            result) ||
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
    if (frame.header.message_type != PtpMessageType::announce ||
        !frame.announce.has_value()) {
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
    case PtpDisciplineState::unlocked:
        return "UNLOCKED";
    case PtpDisciplineState::acquiring:
        return "ACQUIRING";
    case PtpDisciplineState::locked:
        return "LOCKED";
    case PtpDisciplineState::holdover:
        return "HOLDOVER";
    case PtpDisciplineState::fault:
        return "FAULT";
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

class PtpClockDiscipline final {
public:
    explicit PtpClockDiscipline(PtpDisciplineOptions options = {}) noexcept
        : options_(std::move(options)) {
        if (!validate_options(options_)) options_ = {};
    }

    [[nodiscard]] static bool validate_options(
        const PtpDisciplineOptions& options) noexcept {
        return options.maximum_path_delay_ns > 0 &&
               options.maximum_path_delay_jitter_ns >= 0 &&
               options.lock_offset_threshold_ns > 0 &&
               options.unlock_offset_threshold_ns >= options.lock_offset_threshold_ns &&
               options.phase_step_threshold_ns >= options.unlock_offset_threshold_ns &&
               options.fault_offset_threshold_ns >= options.phase_step_threshold_ns &&
               options.lock_required_samples != 0U &&
               options.unlock_required_samples != 0U &&
               options.invalid_samples_before_fault != 0U &&
               options.maximum_frequency_adjustment_ppb > 0 &&
               options.phase_time_constant > std::chrono::milliseconds::zero() &&
               options.sync_timeout > std::chrono::milliseconds::zero() &&
               options.holdover_timeout > std::chrono::milliseconds::zero() &&
               options.frequency_filter_alpha > 0.0 &&
               options.frequency_filter_alpha <= 1.0;
    }

    void reset(
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now()) noexcept {
        status_ = {};
        last_sample_time_.reset();
        previous_servo_time_.reset();
        previous_servo_offset_ns_.reset();
        filtered_path_delay_ns_.reset();
        frequency_bias_ppb_ = 0.0;
        static_cast<void>(now);
    }

    [[nodiscard]] PtpClockCommand observe(
        const PtpOffsetMeasurement& measurement,
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now()) noexcept {
        last_sample_time_ = now;
        status_.offset_from_master_ns = measurement.offset_from_master_ns;
        status_.mean_path_delay_ns = measurement.mean_path_delay_ns;

        const auto offset_magnitude =
            discipline_detail::magnitude(measurement.offset_from_master_ns);
        if (measurement.mean_path_delay_ns < 0 ||
            measurement.mean_path_delay_ns > options_.maximum_path_delay_ns ||
            offset_magnitude >
                static_cast<std::uint64_t>(options_.fault_offset_threshold_ns)) {
            reject_sample();
            if (offset_magnitude >
                static_cast<std::uint64_t>(options_.fault_offset_threshold_ns)) {
                status_.state = PtpDisciplineState::fault;
            }
            return {};
        }

        std::int64_t jitter_ns = 0LL;
        if (filtered_path_delay_ns_.has_value()) {
            const auto difference = std::fabs(
                static_cast<double>(measurement.mean_path_delay_ns) -
                *filtered_path_delay_ns_);
            const auto rounded = std::llround(difference);
            jitter_ns = rounded < 0LL ? 0LL : rounded;
        }
        status_.path_delay_jitter_ns = jitter_ns;
        if (jitter_ns > options_.maximum_path_delay_jitter_ns) {
            reject_sample();
            return {};
        }

        if (!filtered_path_delay_ns_.has_value()) {
            filtered_path_delay_ns_ =
                static_cast<double>(measurement.mean_path_delay_ns);
        } else {
            *filtered_path_delay_ns_ += 0.25 *
                (static_cast<double>(measurement.mean_path_delay_ns) -
                 *filtered_path_delay_ns_);
        }

        ++status_.accepted_samples;
        status_.consecutive_bad_samples = 0U;
        status_.globally_traceable = measurement.globally_traceable;
        if (status_.state == PtpDisciplineState::unlocked ||
            status_.state == PtpDisciplineState::holdover ||
            status_.state == PtpDisciplineState::fault) {
            status_.state = PtpDisciplineState::acquiring;
            status_.consecutive_qualified_samples = 0U;
        }

        if (status_.state != PtpDisciplineState::locked &&
            offset_magnitude >
                static_cast<std::uint64_t>(options_.phase_step_threshold_ns)) {
            status_.state = PtpDisciplineState::acquiring;
            status_.consecutive_qualified_samples = 0U;
            previous_servo_time_.reset();
            previous_servo_offset_ns_.reset();
            ++status_.phase_steps;
            return {
                PtpClockCommandKind::step_phase,
                -measurement.offset_from_master_ns,
                status_.frequency_adjustment_ppb,
            };
        }

        const auto command = frequency_command(
            measurement.offset_from_master_ns,
            now);
        const bool lock_qualified =
            offset_magnitude <=
            static_cast<std::uint64_t>(options_.lock_offset_threshold_ns);

        if (status_.state == PtpDisciplineState::locked) {
            if (offset_magnitude >
                static_cast<std::uint64_t>(options_.unlock_offset_threshold_ns)) {
                ++status_.consecutive_bad_samples;
                status_.consecutive_qualified_samples = 0U;
                if (status_.consecutive_bad_samples >=
                    options_.unlock_required_samples) {
                    status_.state = PtpDisciplineState::acquiring;
                    status_.globally_traceable = false;
                }
            } else {
                status_.consecutive_bad_samples = 0U;
                if (lock_qualified) ++status_.consecutive_qualified_samples;
            }
            return command;
        }

        if (lock_qualified) {
            ++status_.consecutive_qualified_samples;
            if (status_.consecutive_qualified_samples >=
                options_.lock_required_samples) {
                status_.state = PtpDisciplineState::locked;
                status_.consecutive_bad_samples = 0U;
            }
        } else {
            status_.consecutive_qualified_samples = 0U;
        }
        return command;
    }

    void tick(
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now()) noexcept {
        if (!last_sample_time_.has_value() || now <= *last_sample_time_) return;
        const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - *last_sample_time_);
        if (status_.state == PtpDisciplineState::locked &&
            age > options_.sync_timeout) {
            status_.state = PtpDisciplineState::holdover;
            status_.globally_traceable = false;
            status_.consecutive_qualified_samples = 0U;
        }
        if (status_.state == PtpDisciplineState::holdover &&
            age > options_.sync_timeout + options_.holdover_timeout) {
            status_.state = PtpDisciplineState::unlocked;
            status_.globally_traceable = false;
            status_.consecutive_bad_samples = 0U;
        }
        if (status_.state == PtpDisciplineState::acquiring &&
            age > options_.sync_timeout) {
            status_.state = PtpDisciplineState::unlocked;
            status_.globally_traceable = false;
            status_.consecutive_qualified_samples = 0U;
        }
    }

    void record_actuation_failure() noexcept {
        status_.state = PtpDisciplineState::fault;
        status_.globally_traceable = false;
        ++status_.rejected_samples;
        status_.consecutive_qualified_samples = 0U;
    }

    [[nodiscard]] const PtpDisciplineStatus& status() const noexcept {
        return status_;
    }

    [[nodiscard]] std::optional<SmpSynchValue> measured_smp_synch() const noexcept {
        if (status_.state == PtpDisciplineState::locked) {
            return status_.globally_traceable
                ? SmpSynchValue::global_synchronized
                : SmpSynchValue::local_synchronized;
        }
        if (status_.state == PtpDisciplineState::holdover) {
            return SmpSynchValue::local_synchronized;
        }
        return std::nullopt;
    }

private:
    [[nodiscard]] PtpClockCommand frequency_command(
        const std::int64_t offset_ns,
        const std::chrono::steady_clock::time_point now) noexcept {
        if (previous_servo_time_.has_value() &&
            previous_servo_offset_ns_.has_value() &&
            now > *previous_servo_time_) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                now - *previous_servo_time_).count();
            if (elapsed > 0) {
                const long double residual_slope_ppb =
                    static_cast<long double>(
                        offset_ns - *previous_servo_offset_ns_) *
                    static_cast<long double>(ptp_nanoseconds_per_second) /
                    static_cast<long double>(elapsed);
                const long double raw_bias =
                    static_cast<long double>(status_.frequency_adjustment_ppb) -
                    residual_slope_ppb;
                frequency_bias_ppb_ += options_.frequency_filter_alpha *
                    (static_cast<double>(raw_bias) - frequency_bias_ppb_);
            }
        }

        const auto phase_tau_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                options_.phase_time_constant).count();
        long double phase_ppb = 0.0L;
        if (phase_tau_ns > 0) {
            phase_ppb =
                -static_cast<long double>(offset_ns) *
                static_cast<long double>(ptp_nanoseconds_per_second) /
                static_cast<long double>(phase_tau_ns);
        }
        const long double desired =
            static_cast<long double>(frequency_bias_ppb_) + phase_ppb;
        const long double limit =
            static_cast<long double>(options_.maximum_frequency_adjustment_ppb);
        const auto rounded = std::llround(std::clamp(desired, -limit, limit));
        const auto bounded = std::clamp<long long>(
            rounded,
            -static_cast<long long>(options_.maximum_frequency_adjustment_ppb),
            static_cast<long long>(options_.maximum_frequency_adjustment_ppb));
        status_.frequency_adjustment_ppb = static_cast<std::int32_t>(bounded);
        previous_servo_time_ = now;
        previous_servo_offset_ns_ = offset_ns;
        ++status_.frequency_updates;
        return {
            PtpClockCommandKind::set_frequency,
            0LL,
            status_.frequency_adjustment_ppb,
        };
    }

    void reject_sample() noexcept {
        ++status_.rejected_samples;
        status_.consecutive_qualified_samples = 0U;
        ++status_.consecutive_bad_samples;
        if (status_.consecutive_bad_samples >=
            options_.invalid_samples_before_fault) {
            status_.state = PtpDisciplineState::fault;
            status_.globally_traceable = false;
        }
    }

    PtpDisciplineOptions options_{};
    PtpDisciplineStatus status_{};
    std::optional<std::chrono::steady_clock::time_point> last_sample_time_;
    std::optional<std::chrono::steady_clock::time_point> previous_servo_time_;
    std::optional<std::int64_t> previous_servo_offset_ns_;
    std::optional<double> filtered_path_delay_ns_;
    double frequency_bias_ppb_{};
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

class PtpTimeReceiver final {
public:
    explicit PtpTimeReceiver(PtpTimeReceiverOptions options = {}) noexcept
        : options_(std::move(options)) {}

    void reset() noexcept {
        status_ = {};
        selected_quality_.reset();
        selected_source_last_seen_.reset();
        clear_exchanges();
    }

    void reconfigure(PtpTimeReceiverOptions options) noexcept {
        options_ = std::move(options);
        reset();
    }

    [[nodiscard]] bool observe_announce(
        const PtpFrame& frame,
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now()) noexcept {
        if (!profile_matches(frame) ||
            frame.header.message_type != PtpMessageType::announce ||
            !frame.announce.has_value()) {
            return false;
        }
        ++status_.announce_frames;
        const auto& announce = *frame.announce;
        const AnnounceQuality quality{
            announce.priority1,
            announce.clock_class,
            static_cast<std::uint8_t>(announce.clock_accuracy),
            announce.offset_scaled_log_variance,
            announce.priority2,
            announce.grandmaster_identity,
            announce.steps_removed,
        };

        const bool current_is_same =
            source_matches_selected(frame.header.source_port_identity);
        bool selected_is_stale = false;
        if (selected_source_last_seen_.has_value() &&
            now > *selected_source_last_seen_) {
            selected_is_stale =
                now - *selected_source_last_seen_ > options_.source_timeout;
        }
        const bool should_select =
            !status_.selected_source.has_value() ||
            selected_is_stale ||
            current_is_same ||
            (selected_quality_.has_value() &&
             better_quality(quality, *selected_quality_));
        if (!should_select) return false;

        const bool changed = !current_is_same;
        status_.selected_source = frame.header.source_port_identity;
        status_.selected_grandmaster = announce.grandmaster_identity;
        status_.selected_source_globally_traceable =
            ptp_announce_is_globally_traceable(frame);
        selected_quality_ = quality;
        selected_source_last_seen_ = now;
        if (changed) {
            status_.mean_path_delay_ns.reset();
            clear_exchanges();
        }
        return changed;
    }

    void note_pdelay_request(
        const std::uint16_t sequence_id,
        const PtpTimestamp& request_tx_timestamp,
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now()) noexcept {
        pending_pdelay_ = PendingPdelay{
            sequence_id,
            request_tx_timestamp,
            {},
            {},
            0LL,
            false,
            now,
        };
    }

    [[nodiscard]] bool observe_pdelay_response(
        const PtpFrame& frame,
        const PtpTimestamp& local_rx_timestamp,
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now()) noexcept {
        PtpTimestamp peer_request_rx{};
        PtpPortIdentity requester{};
        const bool body_valid = discipline_detail::parse_pdelay_body(
            frame,
            peer_request_rx,
            requester);
        if (!profile_matches(frame) ||
            frame.header.message_type != PtpMessageType::pdelay_resp ||
            !pending_pdelay_.has_value() ||
            !body_valid ||
            requester != options_.local_port_identity ||
            frame.header.sequence_id != pending_pdelay_->sequence_id ||
            !source_matches_selected(frame.header.source_port_identity)) {
            ++status_.rejected_exchanges;
            return false;
        }
        ++status_.pdelay_responses;
        selected_source_last_seen_ = now;
        pending_pdelay_->t2 = peer_request_rx;
        pending_pdelay_->t4 = local_rx_timestamp;
        pending_pdelay_->response_correction_field =
            frame.header.correction_field;
        pending_pdelay_->response_received = true;
        return true;
    }

    [[nodiscard]] std::optional<PtpPeerDelayMeasurement>
    observe_pdelay_response_follow_up(
        const PtpFrame& frame,
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now()) noexcept {
        PtpTimestamp peer_response_tx{};
        PtpPortIdentity requester{};
        const bool body_valid = discipline_detail::parse_pdelay_body(
            frame,
            peer_response_tx,
            requester);
        if (!profile_matches(frame) ||
            frame.header.message_type != PtpMessageType::pdelay_resp_follow_up ||
            !pending_pdelay_.has_value() ||
            !pending_pdelay_->response_received ||
            !body_valid ||
            requester != options_.local_port_identity ||
            frame.header.sequence_id != pending_pdelay_->sequence_id ||
            !source_matches_selected(frame.header.source_port_identity)) {
            ++status_.rejected_exchanges;
            return std::nullopt;
        }
        selected_source_last_seen_ = now;
        const PtpPeerDelayExchange exchange{
            pending_pdelay_->t1,
            pending_pdelay_->t2,
            peer_response_tx,
            pending_pdelay_->t4,
            pending_pdelay_->response_correction_field,
            frame.header.correction_field,
        };
        pending_pdelay_.reset();
        const auto measurement = calculate_peer_delay(exchange);
        if (!measurement.has_value()) {
            ++status_.rejected_exchanges;
            return std::nullopt;
        }
        status_.mean_path_delay_ns = measurement->mean_path_delay_ns;
        ++status_.completed_pdelay_exchanges;
        return measurement;
    }

    [[nodiscard]] std::optional<PtpOffsetMeasurement> observe_sync(
        const PtpFrame& frame,
        const PtpTimestamp& local_rx_timestamp,
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now()) noexcept {
        if (!profile_matches(frame) ||
            frame.header.message_type != PtpMessageType::sync ||
            !source_matches_selected(frame.header.source_port_identity)) {
            return std::nullopt;
        }
        ++status_.sync_frames;
        selected_source_last_seen_ = now;
        pending_sync_ = PendingSync{
            frame.header.source_port_identity,
            frame.header.sequence_id,
            local_rx_timestamp,
            frame.header.correction_field,
            now,
        };
        if (frame.header.is_two_step()) return std::nullopt;
        if (!frame.timestamp.has_value()) {
            ++status_.rejected_exchanges;
            pending_sync_.reset();
            return std::nullopt;
        }
        return complete_sync(*frame.timestamp, 0LL, frame.header.sequence_id);
    }

    [[nodiscard]] std::optional<PtpOffsetMeasurement> observe_follow_up(
        const PtpFrame& frame,
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now()) noexcept {
        if (!profile_matches(frame) ||
            frame.header.message_type != PtpMessageType::follow_up ||
            !source_matches_selected(frame.header.source_port_identity) ||
            !frame.timestamp.has_value()) {
            return std::nullopt;
        }
        ++status_.follow_up_frames;
        selected_source_last_seen_ = now;
        if (!pending_sync_.has_value() ||
            pending_sync_->source != frame.header.source_port_identity ||
            pending_sync_->sequence_id != frame.header.sequence_id) {
            ++status_.rejected_exchanges;
            return std::nullopt;
        }
        return complete_sync(
            *frame.timestamp,
            frame.header.correction_field,
            frame.header.sequence_id);
    }

    [[nodiscard]] bool tick(
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now()) noexcept {
        bool source_dropped = false;
        if (selected_source_last_seen_.has_value() &&
            now > *selected_source_last_seen_ &&
            now - *selected_source_last_seen_ > options_.source_timeout) {
            status_.selected_source.reset();
            status_.selected_grandmaster.reset();
            status_.mean_path_delay_ns.reset();
            status_.selected_source_globally_traceable = false;
            selected_quality_.reset();
            selected_source_last_seen_.reset();
            clear_exchanges();
            source_dropped = true;
        }
        if (pending_sync_.has_value() &&
            now > pending_sync_->observed_at &&
            now - pending_sync_->observed_at > options_.exchange_timeout) {
            pending_sync_.reset();
            ++status_.rejected_exchanges;
        }
        if (pending_pdelay_.has_value() &&
            now > pending_pdelay_->started_at &&
            now - pending_pdelay_->started_at > options_.exchange_timeout) {
            pending_pdelay_.reset();
            ++status_.rejected_exchanges;
        }
        return source_dropped;
    }

    [[nodiscard]] const PtpTimeReceiverStatus& status() const noexcept {
        return status_;
    }

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

    [[nodiscard]] bool profile_matches(const PtpFrame& frame) const noexcept {
        return frame.header.domain_number == options_.domain_number &&
               frame.header.transport_specific == options_.transport_specific;
    }

    [[nodiscard]] bool source_matches_selected(
        const PtpPortIdentity& source) const noexcept {
        return status_.selected_source.has_value() &&
               *status_.selected_source == source;
    }

    [[nodiscard]] static bool better_quality(
        const AnnounceQuality& left,
        const AnnounceQuality& right) noexcept {
        return std::tie(
                   left.priority1,
                   left.clock_class,
                   left.clock_accuracy,
                   left.variance,
                   left.priority2,
                   left.grandmaster_identity,
                   left.steps_removed) <
               std::tie(
                   right.priority1,
                   right.clock_class,
                   right.clock_accuracy,
                   right.variance,
                   right.priority2,
                   right.grandmaster_identity,
                   right.steps_removed);
    }

    void clear_exchanges() noexcept {
        pending_sync_.reset();
        pending_pdelay_.reset();
    }

    [[nodiscard]] std::optional<PtpOffsetMeasurement> complete_sync(
        const PtpTimestamp& origin,
        const std::int64_t follow_up_correction_field,
        const std::uint16_t sequence_id) noexcept {
        if (!pending_sync_.has_value() ||
            pending_sync_->sequence_id != sequence_id ||
            !status_.mean_path_delay_ns.has_value()) {
            ++status_.rejected_exchanges;
            return std::nullopt;
        }
        const PtpSyncExchange exchange{
            origin,
            pending_sync_->local_rx_timestamp,
            *status_.mean_path_delay_ns,
            pending_sync_->sync_correction_field,
            follow_up_correction_field,
        };
        const auto source = pending_sync_->source;
        pending_sync_.reset();
        const auto offset = calculate_offset_from_master(exchange);
        if (!offset.has_value()) {
            ++status_.rejected_exchanges;
            return std::nullopt;
        }
        ++status_.completed_sync_exchanges;
        return PtpOffsetMeasurement{
            source,
            sequence_id,
            *offset,
            exchange.mean_path_delay_ns,
            status_.selected_source_globally_traceable,
        };
    }

    PtpTimeReceiverOptions options_{};
    PtpTimeReceiverStatus status_{};
    std::optional<AnnounceQuality> selected_quality_;
    std::optional<std::chrono::steady_clock::time_point> selected_source_last_seen_;
    std::optional<PendingSync> pending_sync_;
    std::optional<PendingPdelay> pending_pdelay_;
};

} // namespace ar::iec61850::time_sync
