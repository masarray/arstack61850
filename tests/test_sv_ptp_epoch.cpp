// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/time_sync/sv_ptp_epoch.hpp"
#include "ariec61850/sampled_values/rational_schedule.hpp"

#include <cstdint>
#include <array>
#include <iostream>
#include <optional>

using namespace ar::iec61850::time_sync;

int main() {
    const auto require = [](bool valid, const char* why) {
        if (!valid) { std::cerr << why << '\n'; return false; }
        return true;
    };
    const auto at_quarter = plan_sv_ptp_second_arm({250000000U, 100U, 108U});
    if (!require(at_quarter.has_value() && at_quarter->first_alarm_us == 749996U,
                 "PTP quarter-second epoch failed")) return 1;
    const auto near_second = plan_sv_ptp_second_arm({999995000U, 0U, 2U});
    if (!require(near_second.has_value() && near_second->first_alarm_us == 1000004U,
                 "near-boundary must roll forward safely")) return 1;
    // Earlier PTP reads must not translate into late timer alarms.
    const auto rebased = rebase_sv_ptp_second_arm(*at_quarter, 108U, 308U);
    if (!require(rebased.has_value() &&
                 rebased->first_alarm_us == 749796U &&
                 rebased->clock_read_uncertainty_us ==
                     at_quarter->clock_read_uncertainty_us,
                 "GPTimer START setup latency was not compensated")) return 1;
    const auto late = rebase_sv_ptp_second_arm(
        {200000U, 5U}, 1000U, 191000U, 20000U, 200000U);
    if (!require(late.has_value() && late->first_alarm_us == 1010000U,
                 "late first alarm must defer one full PTP second")) return 1;
    if (!require(!rebase_sv_ptp_second_arm(*at_quarter, 108U, 100109U),
                 "stale PTP clock read accepted")) return 1;
    if (!require(!rebase_sv_ptp_second_arm(*at_quarter, 108U, 107U),
                 "backwards monotonic arm time accepted")) return 1;
    if (!require(!rebase_sv_ptp_second_arm({0U, 0U}, 108U, 110U),
                 "invalid zero alarm accepted")) return 1;
    const auto start = plan_sv_ptp_second_arm({0U, 100U, 100U});
    if (!require(start.has_value() && start->first_alarm_us == 1000000U,
                 "exact second must target next second")) return 1;
    if (!require(!plan_sv_ptp_second_arm({1000000000U, 0U, 1U}).has_value(),
                 "invalid nanoseconds accepted")) return 1;
    if (!require(!plan_sv_ptp_second_arm({100U, 100U, 90U}).has_value(),
                 "backwards clock bracket accepted")) return 1;
    if (!require(!plan_sv_ptp_second_arm({100U, 0U, 5000U}).has_value(),
                 "unbounded EMAC read accepted")) return 1;
    if (!require(!qualified_sv_sync_value(SmpSynchValue::global_synchronized, false),
                 "PTP lock falsely promoted SV sync")) return 1;
    if (!require(qualified_sv_sync_value(SmpSynchValue::local_synchronized, true) ==
                 SmpSynchValue::local_synchronized,
                 "qualified local clock value lost")) return 1;
    if (!require(!qualified_sv_sync_value(std::nullopt, true),
                 "missing clock evidence accepted")) return 1;
    // Pure scheduling proof: the existing portable rational scheduler has
    // precisely one million timer ticks per whole 4000/4800-sample epoch.
    // This validates arithmetic/rollover, NOT physical EMAC egress phase.
    for (const auto rate : std::array<std::uint32_t, 2>{4000U, 4800U}) {
        ar::iec61850::sampled_values::RationalTickSchedule schedule{1000000ULL, rate};
        std::uint64_t elapsed_ticks = 0U;
        std::uint32_t counter = 0U;
        for (std::uint32_t sample = 0U; sample < rate * 120U; ++sample) {
            const auto duration = schedule.next_interval_ticks();
            if (!require(duration == 1000000U / rate ||
                         duration == (1000000U / rate) + 1U,
                         "rational cadence interval invalid")) return 1;
            elapsed_ticks += duration;
            counter = (counter + 1U) % rate;
            if ((sample + 1U) % rate == 0U) {
                if (!require(elapsed_ticks == (sample + 1ULL) / rate * 1000000ULL,
                             "rational schedule drifted from 1-second epoch")) return 1;
                if (!require(counter == 0U, "sample counter rollover not at epoch")) return 1;
            }
        }
    }
    std::cout << "SV/PTP epoch and synchronization gates passed\n";
    return 0;
}
