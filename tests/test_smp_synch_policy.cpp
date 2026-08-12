// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/time_sync/smp_synch_policy.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>

namespace {

using ar::iec61850::time_sync::SmpSynchDecisionSource;
using ar::iec61850::time_sync::SmpSynchValue;
using ar::iec61850::time_sync::SvSyncPolicyMode;
using ar::iec61850::time_sync::resolve_sv_sync_policy;
using ar::iec61850::time_sync::smp_synch_source_name;
using ar::iec61850::time_sync::sv_sync_policy_name;

void require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void test_auto_is_safe_without_measured_lock() {
    const auto decision = resolve_sv_sync_policy(SvSyncPolicyMode::external_ptp_auto);
    require(decision.value == SmpSynchValue::not_synchronized,
            "AUTO without measured lock must advertise smpSynch=0");
    require(decision.source == SmpSynchDecisionSource::safe_default,
            "AUTO without measured lock must be SAFE_DEFAULT");
    require(!decision.simulated(), "AUTO safe default is not a lab override");
    require(!decision.measured(), "AUTO safe default is not measured");
}

void test_auto_accepts_future_measured_state() {
    const auto local = resolve_sv_sync_policy(
        SvSyncPolicyMode::external_ptp_auto,
        SmpSynchValue::local_synchronized);
    require(local.value == SmpSynchValue::local_synchronized,
            "AUTO must pass through measured local synchronization");
    require(local.source == SmpSynchDecisionSource::measured_timing,
            "AUTO with measured evidence must identify MEASURED source");
    require(local.measured(), "measured decision flag must be true");
    require(!local.simulated(), "measured decision must not be marked simulated");
}

void test_all_lab_compatibility_modes() {
    const auto unsync = resolve_sv_sync_policy(SvSyncPolicyMode::honest_unsynchronized);
    const auto local = resolve_sv_sync_policy(SvSyncPolicyMode::local_compatibility);
    const auto global = resolve_sv_sync_policy(SvSyncPolicyMode::global_compatibility);

    require(unsync.value == SmpSynchValue::not_synchronized,
            "FORCE_0 must advertise smpSynch=0");
    require(local.value == SmpSynchValue::local_synchronized,
            "FORCE_1 must advertise smpSynch=1");
    require(global.value == SmpSynchValue::global_synchronized,
            "FORCE_2 must advertise smpSynch=2");

    require(unsync.simulated() && local.simulated() && global.simulated(),
            "all explicit compatibility modes must be marked simulated");
    require(unsync.source == SmpSynchDecisionSource::lab_override &&
                local.source == SmpSynchDecisionSource::lab_override &&
                global.source == SmpSynchDecisionSource::lab_override,
            "all explicit compatibility modes must report LAB_OVERRIDE source");
}

void test_labels_are_stable_for_ui_and_evidence() {
    require(sv_sync_policy_name(SvSyncPolicyMode::external_ptp_auto) == "AUTO",
            "AUTO label changed");
    require(sv_sync_policy_name(SvSyncPolicyMode::honest_unsynchronized) == "FORCE_0",
            "FORCE_0 label changed");
    require(sv_sync_policy_name(SvSyncPolicyMode::local_compatibility) == "FORCE_1_LOCAL",
            "FORCE_1 label changed");
    require(sv_sync_policy_name(SvSyncPolicyMode::global_compatibility) == "FORCE_2_GLOBAL",
            "FORCE_2 label changed");
    require(smp_synch_source_name(SmpSynchDecisionSource::safe_default) == "SAFE_DEFAULT",
            "SAFE_DEFAULT label changed");
    require(smp_synch_source_name(SmpSynchDecisionSource::measured_timing) == "MEASURED",
            "MEASURED label changed");
    require(smp_synch_source_name(SmpSynchDecisionSource::lab_override) == "LAB_OVERRIDE",
            "LAB_OVERRIDE label changed");
}

} // namespace

int main() {
    test_auto_is_safe_without_measured_lock();
    test_auto_accepts_future_measured_state();
    test_all_lab_compatibility_modes();
    test_labels_are_stable_for_ui_and_evidence();
    std::cout << "smpSynch lab policy tests passed\n";
    return EXIT_SUCCESS;
}
