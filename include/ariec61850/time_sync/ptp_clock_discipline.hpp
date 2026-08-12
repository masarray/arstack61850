// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/time_sync/ptp_discipline_types.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

namespace ar::iec61850::time_sync {

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
               options.maximum_acquisition_phase_step_ns >=
                   options.fault_offset_threshold_ns &&
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
        last_accepted_sample_time_.reset();
        previous_servo_time_.reset();
        previous_servo_offset_ns_.reset();
        filtered_path_delay_ns_.reset();
        frequency_bias_ppb_ = 0.0;
        large_acquisition_phase_step_used_ = false;
        static_cast<void>(now);
    }

    [[nodiscard]] PtpClockCommand observe(
        const PtpOffsetMeasurement& measurement,
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now()) noexcept {
        status_.offset_from_master_ns = measurement.offset_from_master_ns;
        status_.mean_path_delay_ns = measurement.mean_path_delay_ns;

        const auto offset_magnitude =
            discipline_detail::magnitude(measurement.offset_from_master_ns);
        const auto fault_limit =
            static_cast<std::uint64_t>(options_.fault_offset_threshold_ns);
        const auto acquisition_limit =
            static_cast<std::uint64_t>(options_.maximum_acquisition_phase_step_ns);
        const bool acquisition_state =
            status_.state == PtpDisciplineState::unlocked ||
            status_.state == PtpDisciplineState::acquiring;
        const bool exceeds_normal_fault_limit = offset_magnitude > fault_limit;
        const bool bounded_large_acquisition =
            acquisition_state &&
            !large_acquisition_phase_step_used_ &&
            exceeds_normal_fault_limit &&
            offset_magnitude <= acquisition_limit;

        if (measurement.mean_path_delay_ns < 0 ||
            measurement.mean_path_delay_ns > options_.maximum_path_delay_ns ||
            (exceeds_normal_fault_limit && !bounded_large_acquisition)) {
            reject_sample();
            if (exceeds_normal_fault_limit) {
                status_.state = PtpDisciplineState::fault;
                status_.globally_traceable = false;
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

        // Synchronization age is based only on measurements that have passed
        // path, offset and jitter qualification. Rejected traffic must never
        // extend LOCKED or global traceability beyond the configured timeout.
        last_accepted_sample_time_ = now;

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
            std::int64_t phase_step_ns{};
            if (!discipline_detail::checked_subtract(
                    0LL,
                    measurement.offset_from_master_ns,
                    phase_step_ns)) {
                reject_sample();
                status_.state = PtpDisciplineState::fault;
                status_.globally_traceable = false;
                return {};
            }
            if (bounded_large_acquisition) {
                large_acquisition_phase_step_used_ = true;
            }
            status_.state = PtpDisciplineState::acquiring;
            status_.consecutive_qualified_samples = 0U;
            previous_servo_time_.reset();
            previous_servo_offset_ns_.reset();
            ++status_.phase_steps;
            return {
                PtpClockCommandKind::step_phase,
                phase_step_ns,
                status_.frequency_adjustment_ppb,
            };
        }

        const auto command = frequency_command(measurement.offset_from_master_ns, now);
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
        if (!last_accepted_sample_time_.has_value() ||
            now <= *last_accepted_sample_time_) {
            return;
        }
        const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - *last_accepted_sample_time_);
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

    /**
     * Revoke only the global provenance claim immediately. A subsequent
     * qualified measurement may restore it if fresh Announce evidence again
     * proves global traceability. This allows LOCKED local timing to remain 1
     * while ensuring stale/degraded Announce evidence can never leave AUTO at 2.
     */
    void revoke_global_traceability() noexcept {
        status_.globally_traceable = false;
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
        if (status_.consecutive_bad_samples >= options_.invalid_samples_before_fault) {
            status_.state = PtpDisciplineState::fault;
            status_.globally_traceable = false;
        }
    }

    PtpDisciplineOptions options_{};
    PtpDisciplineStatus status_{};
    std::optional<std::chrono::steady_clock::time_point> last_accepted_sample_time_;
    std::optional<std::chrono::steady_clock::time_point> previous_servo_time_;
    std::optional<std::int64_t> previous_servo_offset_ns_;
    std::optional<double> filtered_path_delay_ns_;
    double frequency_bias_ppb_{};
    bool large_acquisition_phase_step_used_{};
};

} // namespace ar::iec61850::time_sync
