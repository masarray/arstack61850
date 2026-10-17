// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace ar::iec61850::time_sync {

// Portable, allocation-free CONTROL-PLANE authority for the relationship
// between ESP32-P4 GPTimer sample deadlines and the EMAC IEEE1588 PTP epoch.
// It does NOT establish hardware capture accuracy or authorize smpSynch.
// The caller collects a PTP read bracketed by GPTimer raw counters outside
// the ISR, together with the ISR-published last smpCnt=0 *deadline*.
// All timer tick values are 1 MHz and intentionally wrap at 32 bits.
struct SvPtpPhaseObservation final {
    std::uint32_t timer_before_ticks{};
    std::uint32_t timer_after_ticks{};
    std::uint32_t last_sv_epoch_ticks{};
    std::uint32_t last_sv_epoch_generation{};
    std::uint64_t ptp_seconds{};
    std::uint32_t ptp_nanoseconds{};
};

enum class SvPtpPhaseState : std::uint8_t {
    acquiring,
    tracking,
    invalid,
};

struct SvPtpPhaseDecision final {
    SvPtpPhaseState state{SvPtpPhaseState::acquiring};
    bool accepted{};
    bool clock_discontinuity{};
    std::int32_t phase_error_us{};
    std::uint32_t observation_uncertainty_us{};
    std::int32_t slew_ticks_next_epoch{};
    // This is a software timing assessment, NOT a valid wire smpSynch claim.
    bool software_tracking{};
    // Stable integer PTP second paired with the last smpCnt=0 deadline.
    // A first observation is a baseline; only two consecutive epochs bind.
    bool epoch_second_continuous{};
    std::uint64_t sv_epoch_ptp_second{};
};

struct SvPtpPhasePolicy final {
    std::uint32_t maximum_clock_read_span_us{500U};
    std::uint32_t maximum_clock_rate_error_us{400U};
    std::uint32_t maximum_observation_spacing_us{1500000U};
    std::int32_t maximum_phase_error_us{20000};
    std::int32_t maximum_slew_per_second_us{400};
    std::int32_t integral_limit_us{250};
    std::int32_t phase_deadband_us{2};
    std::int32_t software_tracking_threshold_us{60};
    std::uint32_t consecutive_tracking_samples{4U};
};

namespace sv_phase_detail {
[[nodiscard]] constexpr std::int64_t signed_delta_u32(
    const std::uint32_t newer, const std::uint32_t older) noexcept {
    constexpr std::int64_t modulus = 4294967296LL;
    const std::uint32_t delta = newer - older;
    return delta <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
        ? static_cast<std::int64_t>(delta)
        : static_cast<std::int64_t>(delta) - modulus;
}

[[nodiscard]] constexpr std::int32_t normalize_to_nearest_ptp_second(
    const std::int64_t ticks) noexcept {
    constexpr std::int64_t second = 1000000LL;
    std::int64_t v = (ticks + (second / 2)) % second;
    if (v < 0) v += second;
    return static_cast<std::int32_t>(v - (second / 2));
}

[[nodiscard]] constexpr std::int64_t magnitude(
    const std::int64_t value) noexcept {
    return value < 0 ? -value : value;
}
} // namespace sv_phase_detail

class SvPtpPhaseAuthority final {
public:
    explicit constexpr SvPtpPhaseAuthority(
        const SvPtpPhasePolicy policy = {}) noexcept : policy_(policy) {}

    constexpr void reset() noexcept {
        have_previous_ = false;
        previous_sv_epoch_ptp_second_ = 0U;
        integral_us_ = 0;
        tracking_samples_ = 0U;
        state_ = SvPtpPhaseState::acquiring;
    }

    // Call after source loss, publisher stop/re-arm, detected clock step,
    // missing PTP source, or stale observations. No stale steering survives.
    [[nodiscard]] constexpr SvPtpPhaseDecision invalidate() noexcept {
        reset();
        state_ = SvPtpPhaseState::invalid;
        return {state_, false, true, 0, 0U, 0, false, false, 0U};
    }

    // One well-bracketed observation per approximately 1-second SV epoch.
    // Wrong epoch generation, PTP steps and implausible clock drift are
    // rejected before generating any correction. The signed correction is
    // distributed by SvBoundedSlewCursor over the NEXT complete epoch.
    [[nodiscard]] constexpr SvPtpPhaseDecision observe(
        const SvPtpPhaseObservation o) noexcept {
        constexpr std::uint32_t ns_per_second = 1000000000U;
        if (o.ptp_seconds == 0U || o.ptp_nanoseconds >= ns_per_second ||
            o.last_sv_epoch_generation == 0U) {
            return invalidate();
        }
        const std::uint32_t span = o.timer_after_ticks - o.timer_before_ticks;
        if (span > policy_.maximum_clock_read_span_us) return invalidate();
        const std::uint32_t midpoint = o.timer_before_ticks + (span / 2U);
        // There must have been a real ISR epoch reasonably recently.
        const std::uint32_t epoch_age = midpoint - o.last_sv_epoch_ticks;
        if (epoch_age > 1100000U) return invalidate();

        const std::int64_t raw_phase =
            sv_phase_detail::signed_delta_u32(o.last_sv_epoch_ticks, midpoint) +
            static_cast<std::int64_t>(o.ptp_nanoseconds / 1000U);
        // Do not discard the integer-second label by modulo reduction.
        // The bounded signed shift also handles readings just before a PTP
        // second boundary; GPTimer low32 can wrap independently.
        constexpr std::int64_t second_us = 1000000LL;
        const std::int64_t shift = raw_phase >= 0
            ? (raw_phase + second_us / 2) / second_us
            : (raw_phase - second_us / 2) / second_us;
        if (shift < -1 || shift > 1 ||
            (shift < 0 && o.ptp_seconds == 0U) ||
            (shift > 0 && o.ptp_seconds ==
                std::numeric_limits<std::uint64_t>::max())) {
            return invalidate();
        }
        const std::uint64_t sv_epoch_second = shift < 0
            ? o.ptp_seconds - 1U
            : o.ptp_seconds + static_cast<std::uint64_t>(shift);
        const std::int32_t phase = static_cast<std::int32_t>(
            raw_phase - shift * second_us);
        const std::uint32_t uncertainty = ((span + 1U) / 2U) + 1U;
        if (sv_phase_detail::magnitude(phase) >
            policy_.maximum_phase_error_us) {
            return invalidate();
        }

        if (have_previous_) {
            const std::uint32_t epoch_advance =
                o.last_sv_epoch_generation - previous_epoch_generation_;
            if (epoch_advance != 1U ||
                sv_epoch_second <= previous_sv_epoch_ptp_second_ ||
                sv_epoch_second - previous_sv_epoch_ptp_second_ != 1U ||
                o.ptp_seconds < previous_ptp_seconds_ ||
                o.ptp_seconds - previous_ptp_seconds_ > 2U) {
                return invalidate();
            }
            const std::int64_t ptp_ns =
                static_cast<std::int64_t>(
                    o.ptp_seconds - previous_ptp_seconds_) * ns_per_second +
                static_cast<std::int64_t>(o.ptp_nanoseconds) -
                static_cast<std::int64_t>(previous_ptp_nanoseconds_);
            if (ptp_ns <= 0LL ||
                ptp_ns > static_cast<std::int64_t>(
                    policy_.maximum_observation_spacing_us) * 1000LL) {
                return invalidate();
            }
            const std::uint32_t timer_delta = midpoint - previous_midpoint_;
            const std::int64_t ptp_elapsed_us = (ptp_ns + 500LL) / 1000LL;
            if (sv_phase_detail::magnitude(
                    static_cast<std::int64_t>(timer_delta) - ptp_elapsed_us) >
                policy_.maximum_clock_rate_error_us) {
                return invalidate();
            }
        }

        previous_midpoint_ = midpoint;
        previous_ptp_seconds_ = o.ptp_seconds;
        previous_ptp_nanoseconds_ = o.ptp_nanoseconds;
        previous_epoch_generation_ = o.last_sv_epoch_generation;
        previous_sv_epoch_ptp_second_ = sv_epoch_second;
        const bool first_observation = !have_previous_;
        have_previous_ = true;

        // A small nominal phase residual is NOT sufficiently qualified
        // when the GPTimer/PTP read bracket is wider than the tracking
        // tolerance. Include the full bracket uncertainty before advancing
        // consecutive software-tracking samples (never wire smpSynch).
        const std::int64_t worst_case_phase_us =
            sv_phase_detail::magnitude(phase) +
            static_cast<std::int64_t>(uncertainty);
        if (worst_case_phase_us <=
            policy_.software_tracking_threshold_us) {
            if (tracking_samples_ < policy_.consecutive_tracking_samples) {
                ++tracking_samples_;
            }
        } else {
            tracking_samples_ = 0U;
        }
        state_ = tracking_samples_ >= policy_.consecutive_tracking_samples
            ? SvPtpPhaseState::tracking : SvPtpPhaseState::acquiring;

        if (first_observation) {
            return {state_, true, false, phase, uncertainty, 0, false,
                    false, sv_epoch_second};
        }

        // Bounded PI servo. The integrator estimates fixed frequency bias,
        // while the proportional term steers phase. All state is fixed-size,
        // and control is run at ~1 Hz outside the realtime publisher.
        const std::int32_t input =
            sv_phase_detail::magnitude(phase) <= policy_.phase_deadband_us
            ? 0 : phase;
        integral_us_ = std::clamp(
            integral_us_ + (input / 4),
            -policy_.integral_limit_us, policy_.integral_limit_us);
        const std::int32_t requested = std::clamp(
            -input - integral_us_,
            -policy_.maximum_slew_per_second_us,
            policy_.maximum_slew_per_second_us);
        return {state_, true, false, phase, uncertainty, requested,
                state_ == SvPtpPhaseState::tracking, true, sv_epoch_second};
    }

private:
    SvPtpPhasePolicy policy_{};
    bool have_previous_{};
    std::uint32_t previous_midpoint_{};
    std::uint32_t previous_ptp_nanoseconds_{};
    std::uint32_t previous_epoch_generation_{};
    std::uint64_t previous_ptp_seconds_{};
    std::uint64_t previous_sv_epoch_ptp_second_{};
    std::int32_t integral_us_{};
    std::uint32_t tracking_samples_{};
    SvPtpPhaseState state_{SvPtpPhaseState::acquiring};
};

// A control-plane gate for evidence that a canonical SV publisher has not
// skipped sample slots or failed Ethernet TX since the previous observation.
// A fresh session and any fault require re-acquisition; neither can inherit
// a previously qualified phase controller. This does NOT authorize smpSynch.
class SvPtpPublisherContinuityGate final {
public:
    constexpr void reset() noexcept {
        session_ = 0U;
        fault_generation_ = 0U;
        source_generation_ = 0U;
    }

    [[nodiscard]] constexpr bool unchanged(
        const std::uint32_t session, const std::uint32_t fault_generation,
        const std::uint32_t source_generation = 0U) noexcept {
        if (session == 0U) {
            reset();
            return false;
        }
        if (session_ != session || fault_generation_ != fault_generation ||
            source_generation_ != source_generation) {
            session_ = session;
            fault_generation_ = fault_generation;
            source_generation_ = source_generation;
            return false;
        }
        return true;
    }

private:
    std::uint32_t session_{};
    std::uint32_t fault_generation_{};
    std::uint32_t source_generation_{};
};

// ISR-safe, O(1), no allocation/locks/IOCTL. Each interval differs from the
// existing rational scheduler by at most ONE 1-MHz timer tick. The complete
// correction is distributed across one explicitly armed second (4000/4800
// frames, not inferred universally from a sample-counter modulus).
class SvBoundedSlewCursor final {
public:
    constexpr void arm(const std::uint32_t intervals_per_second,
                       const std::int32_t requested_ticks) noexcept {
        rate_ = intervals_per_second;
        if (rate_ == 0U) {
            correction_ = 0;
        } else {
            const std::int32_t max_ticks = static_cast<std::int32_t>(
                std::min<std::uint32_t>(rate_, 400U));
            correction_ = std::clamp(requested_ticks, -max_ticks, max_ticks);
        }
        accumulator_ = 0U;
        applied_ = 0U;
    }

    [[nodiscard]] constexpr std::int32_t next_adjustment() noexcept {
        if (rate_ == 0U || correction_ == 0) return 0;
        const std::uint32_t amount = correction_ < 0
            ? static_cast<std::uint32_t>(-correction_)
            : static_cast<std::uint32_t>(correction_);
        accumulator_ += amount;
        if (accumulator_ >= rate_) {
            accumulator_ -= rate_;
            applied_ += correction_ < 0 ? -1 : 1;
            return correction_ < 0 ? -1 : 1;
        }
        return 0;
    }

    [[nodiscard]] constexpr std::int32_t applied_ticks() const noexcept {
        return applied_;
    }

private:
    std::uint32_t rate_{};
    std::uint32_t accumulator_{};
    std::int32_t correction_{};
    std::int32_t applied_{};
};

} // namespace ar::iec61850::time_sync
