// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/time_sync/sv_ptp_phase_authority.hpp"
#include "ariec61850/sampled_values/rational_schedule.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace ar::iec61850::time_sync;

#define CHECK(x) do { if (!(x)) throw std::runtime_error( \
    std::string{"check failed: "} + #x + " line " + \
    std::to_string(__LINE__)); } while (false)

namespace {

constexpr std::uint64_t kEpochZero = 1700000000ULL;
constexpr std::uint32_t kPtpfractionNs = 250000000U;

// Synthetic EMAC PTP second with an independent GPTimer running +80 ppm.
// A true physical validation also requires the actual EMAC TX timestamps.
SvPtpPhaseObservation snapshot(
    const std::uint32_t epoch, const std::int32_t phase_us,
    const std::uint32_t read_span_us = 8U) {
    const std::uint32_t ptp_boundary = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(epoch) * 1000080ULL);
    const std::uint32_t read_mid = ptp_boundary + 250020U;
    return {read_mid - (read_span_us / 2U),
            read_mid + (read_span_us / 2U),
            static_cast<std::uint32_t>(
                static_cast<std::int64_t>(ptp_boundary) + phase_us),
            epoch, kEpochZero + epoch, kPtpfractionNs};
}

void check_phase_sign_and_bounded_corrections() {
    SvPtpPhaseAuthority controller;
    const auto warm = controller.observe(snapshot(1U, 220));
    CHECK(warm.accepted);
    CHECK(!warm.software_tracking);
    CHECK(warm.slew_ticks_next_epoch == 0);
    CHECK(warm.phase_error_us == 200); // 80ppm * 0.25s = 20us
    const auto second = controller.observe(snapshot(2U, 300));
    CHECK(second.accepted);
    CHECK(second.phase_error_us == 280);
    CHECK(second.slew_ticks_next_epoch < 0);
    CHECK(second.slew_ticks_next_epoch >= -400);
    CHECK(!second.software_tracking);
    // A coarse software START may initially be several milliseconds
    // from the PTP epoch. Acquire monotonically, never jump the counter.
    SvPtpPhaseAuthority coarse;
    CHECK(coarse.observe(snapshot(1U, 9000)).accepted);
    const auto coarse_recovery = coarse.observe(snapshot(2U, 9080));
    CHECK(coarse_recovery.accepted);
    CHECK(coarse_recovery.slew_ticks_next_epoch == -400);
    CHECK(!coarse_recovery.software_tracking);
    const auto step = controller.observe(snapshot(3U, 65000));
    CHECK(!step.accepted);
    CHECK(step.clock_discontinuity);
    CHECK(step.slew_ticks_next_epoch == 0);
    CHECK(step.state == SvPtpPhaseState::invalid);
    // A fault never retains an integrator/slew command across rearm.
    const auto rearm = controller.observe(snapshot(4U, 100));
    CHECK(rearm.accepted);
    CHECK(rearm.slew_ticks_next_epoch == 0);
}

void check_per_second_slew_distribution() {
    for (const auto rate : std::array<std::uint32_t, 2>{4000U, 4800U}) {
        for (const auto correction : std::array<std::int32_t, 7>{
                 -400, -81, -1, 0, 1, 73, 400}) {
            SvBoundedSlewCursor slew;
            slew.arm(rate, correction);
            ar::iec61850::sampled_values::RationalTickSchedule cadence{
                1000000ULL, rate};
            std::int64_t total_ticks = 0;
            std::int32_t adjusted = 0;
            for (std::uint32_t sample = 0U; sample < rate; ++sample) {
                const auto adjustment = slew.next_adjustment();
                CHECK(adjustment >= -1 && adjustment <= 1);
                const auto base = cadence.next_interval_ticks();
                const auto final_interval =
                    static_cast<std::int64_t>(base) + adjustment;
                CHECK(final_interval > 0);
                total_ticks += final_interval;
                adjusted += adjustment;
            }
            CHECK(adjusted == correction);
            CHECK(slew.applied_ticks() == correction);
            CHECK(total_ticks ==
                  1000000LL + static_cast<std::int64_t>(correction));
        }
        SvBoundedSlewCursor invalid;
        invalid.arm(0U, 99);
        CHECK(invalid.next_adjustment() == 0);
        invalid.arm(rate, 3000); // one-tick maximum, cap total slew
        for (std::uint32_t i = 0; i < rate; ++i) {
            static_cast<void>(invalid.next_adjustment());
        }
        CHECK(invalid.applied_ticks() == 400);
    }
}

void check_software_servo_tracks_80ppm_drift_without_counter_reset() {
    for (const auto rate : std::array<std::uint32_t, 2>{4000U, 4800U}) {
        SvPtpPhaseAuthority controller;
        std::int32_t phase_us = 160;
        std::int32_t command = 0;
        std::int32_t peak_last_100 = 0;
        std::uint32_t accepted = 0U;
        bool ever_tracking = false;
        for (std::uint32_t epoch = 1; epoch <= 480U; ++epoch) {
            // The previous epoch's bounded steering is completed before
            // the current timestamp is sampled.
            phase_us += 80 + command;
            const auto result = controller.observe(snapshot(epoch, phase_us));
            CHECK(result.accepted);
            CHECK(result.observation_uncertainty_us <= 5U);
            CHECK(result.slew_ticks_next_epoch >= -400);
            CHECK(result.slew_ticks_next_epoch <= 400);
            if (result.software_tracking) ever_tracking = true;
            ++accepted;
            command = result.slew_ticks_next_epoch;
            SvBoundedSlewCursor actual;
            actual.arm(rate, command);
            std::int32_t distributed = 0;
            for (std::uint32_t n = 0U; n < rate; ++n) {
                distributed += actual.next_adjustment();
            }
            CHECK(distributed == command);
            if (epoch > 380U) {
                const auto error = result.phase_error_us < 0
                    ? -result.phase_error_us : result.phase_error_us;
                if (error > peak_last_100) peak_last_100 = error;
            }
        }
        CHECK(accepted == 480U);
        CHECK(ever_tracking);
        CHECK(peak_last_100 <= 60);
    }
}

void check_clock_step_stale_brackets_and_timer_wrap() {
    SvPtpPhaseAuthority tracker;
    CHECK(!tracker.observe(snapshot(1U, 0, 1200U)).accepted);
    CHECK(!tracker.observe(snapshot(2U, 0, 8U)).slew_ticks_next_epoch);
    CHECK(tracker.observe(snapshot(3U, 0)).accepted);
    // Corrupt the PTP seconds without changing GPTimer; reject promptly.
    auto clock_step = snapshot(4U, 0);
    clock_step.ptp_seconds += 1U;
    const auto step = tracker.observe(clock_step);
    CHECK(!step.accepted);
    CHECK(step.clock_discontinuity);
    CHECK(step.slew_ticks_next_epoch == 0);
    // A delayed observation exceeding one epoch is not a license to slew.
    const auto restarted = tracker.observe(snapshot(7U, 0));
    CHECK(restarted.accepted);
    CHECK(restarted.slew_ticks_next_epoch == 0);
    CHECK(!tracker.observe(snapshot(9U, 0)).accepted);
    // GPTimer's low 32 bits wrap after about 71 minutes at 1 MHz.
    tracker.reset();
    for (std::uint32_t epoch = 4293U; epoch < 4308U; ++epoch) {
        const auto result = tracker.observe(snapshot(epoch, 50));
        CHECK(result.accepted);
        CHECK(!result.clock_discontinuity);
    }
    CHECK(tracker.invalidate().slew_ticks_next_epoch == 0);
}

} // namespace

int main() {
    try {
        check_phase_sign_and_bounded_corrections();
        check_per_second_slew_distribution();
        check_software_servo_tracks_80ppm_drift_without_counter_reset();
        check_clock_step_stale_brackets_and_timer_wrap();
        std::cout << "SV PTP phase authority 4000/4800 fps regression passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
