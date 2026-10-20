// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/time_sync/smp_synch_policy.hpp"
#include "smp_synch_lab.hpp" // link the ACTUAL ESP32 firmware AUTO resolver

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

void test_receiver_auto_requires_fresh_ptp_and_real_timeline() {
    using namespace ar::esp32p4::smv;
    smp_synch_lab_set_mode(SvSyncPolicyMode::honest_unsynchronized);
    smp_synch_set_source_local_qualified(false);
    smp_synch_set_receiver_timeline_qualified(false);
    smp_synch_lab_set_measured(std::nullopt);
    smp_synch_lab_set_mode(SvSyncPolicyMode::external_ptp_auto);

    smp_synch_lab_set_measured(SmpSynchValue::global_synchronized);
    require(smp_synch_lab_decision().value == SmpSynchValue::not_synchronized,
            "PTP GM LOCKED/TRACEABLE alone may not set wire smpSynch=2");
    smp_synch_set_receiver_timeline_qualified(true);
    require(smp_synch_lab_decision().value == SmpSynchValue::global_synchronized,
            "real receiver phase/MAC plus traceable LOCK must enable GLOBAL");

    smp_synch_lab_set_measured(SmpSynchValue::local_synchronized);
    require(smp_synch_lab_decision().value == SmpSynchValue::local_synchronized,
            "degraded traceability must downgrade GLOBAL to LOCAL");
    smp_synch_lab_set_measured(std::nullopt);
    require(!smp_synch_receiver_timeline_qualified(),
            "PTP loss must revoke receiver sample timeline");
    require(smp_synch_lab_decision().value == SmpSynchValue::not_synchronized,
            "PTP loss must immediately emit smpSynch=0");
    smp_synch_lab_set_measured(SmpSynchValue::global_synchronized);
    require(smp_synch_lab_decision().value == SmpSynchValue::not_synchronized,
            "PTP relock without new phase/MAC MUST remain unsynchronized");

    smp_synch_set_receiver_timeline_qualified(true);
    smp_synch_lab_set_measured(SmpSynchValue::not_synchronized);
    require(!smp_synch_receiver_timeline_qualified(),
            "an explicit measured UNSYNC must revoke receiver timeline");
    smp_synch_lab_set_measured(SmpSynchValue::global_synchronized);
    require(smp_synch_lab_decision().value == SmpSynchValue::not_synchronized,
            "a subsequent LOCK after UNSYNC must requalify");

    smp_synch_set_source_local_qualified(true);
    require(smp_synch_lab_decision().value == SmpSynchValue::local_synchronized,
            "LAB_SOURCE may only advertise measured LOCAL");
    smp_synch_lab_set_mode(SvSyncPolicyMode::local_compatibility);
    smp_synch_lab_set_mode(SvSyncPolicyMode::external_ptp_auto);
    require(!smp_synch_source_local_qualified() &&
            !smp_synch_receiver_timeline_qualified(),
            "manual override return must invalidate both stale AUTO proofs");
    require(smp_synch_lab_decision().value == SmpSynchValue::not_synchronized,
            "AUTO after manual override is fail closed");
}

void test_internal_sync_recorder_captures_cause_and_bounded_history() {
    using namespace ar::esp32p4::smv;
    smp_synch_set_source_local_qualified(false);
    const auto before = smp_synch_diag_snapshot();
    smp_synch_diag_observe(-3, 2U, 4U, 10U, 101U, true, true);
    for (unsigned i = 0; i < 21U; ++i) {
        smp_synch_set_source_local_qualified(true, SvSyncLossReason::none,
                                             1000U + 2U*i);
        smp_synch_set_source_local_qualified(false, SvSyncLossReason::mac_egress,
                                             1001U + 2U*i);
    }
    const auto after = smp_synch_diag_snapshot();
    require(after.acquisitions >= before.acquisitions + 21U,
            "on-board recorder missed acquired edges");
    require(after.losses >= before.losses + 21U,
            "on-board recorder missed revocations");
    require(after.cause_counts[static_cast<std::size_t>(SvSyncLossReason::mac_egress)] >= 21U,
            "MAC timing reason must not be collapsed into generic phase loss");
    require(after.recent_count == kSvSyncFlightEventCapacity,
            "flight recorder ring must retain exactly sixteen latest edges");
    require(after.recent[after.recent_count-1].reason == SvSyncLossReason::mac_egress &&
            !after.recent[after.recent_count-1].acquired,
            "newest loss must retain exact cause");
    require(after.phase_error_us == -3 && after.uncertainty_us == 2U &&
            after.mac_pairs == 4U && after.clean_epochs == 10U &&
            after.sample_epoch == 101U && after.phase_accepted &&
            after.software_tracking,
            "flight recorder snapshot must retain measured observer inputs");
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
    test_receiver_auto_requires_fresh_ptp_and_real_timeline();
    test_internal_sync_recorder_captures_cause_and_bounded_history();
    test_all_lab_compatibility_modes();
    test_labels_are_stable_for_ui_and_evidence();
    std::cout << "smpSynch lab policy tests passed\n";
    return EXIT_SUCCESS;
}
