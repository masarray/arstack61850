// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/time_sync/sv_ptp_epoch.hpp"

#include <cstdint>
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
    std::cout << "SV/PTP epoch and synchronization gates passed\n";
    return 0;
}
