// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/time_sync/ptp_monitor.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace ar::iec61850::time_sync {

/**
 * SV synchronization policy used by laboratory publishers.
 *
 * The numeric ordering intentionally mirrors ARIEC61850/ARSVIN's
 * SvSyncPolicyMode so saved lab scenarios can be translated without
 * changing their meaning.
 */
enum class SvSyncPolicyMode : std::uint8_t {
    external_ptp_auto = 0U,
    honest_unsynchronized = 1U,
    local_compatibility = 2U,
    global_compatibility = 3U,
};

enum class SmpSynchDecisionSource : std::uint8_t {
    safe_default,
    measured_timing,
    lab_override,
};

struct SmpSynchDecision final {
    SmpSynchValue value{SmpSynchValue::not_synchronized};
    SvSyncPolicyMode mode{SvSyncPolicyMode::external_ptp_auto};
    SmpSynchDecisionSource source{SmpSynchDecisionSource::safe_default};

    [[nodiscard]] constexpr bool simulated() const noexcept {
        return source == SmpSynchDecisionSource::lab_override;
    }

    [[nodiscard]] constexpr bool measured() const noexcept {
        return source == SmpSynchDecisionSource::measured_timing;
    }
};

/**
 * Qualification of the *actual sample timeline*; deliberately distinct from
 * PTP clock discipline. Defaults to denied. A clock can be LOCKED while the
 * Sampled Values GPTimer and smpCnt epoch are still free-running.
 *
 * The hardware publisher must establish every field from independent timing
 * evidence before AUTO is eligible to announce local/global synchronization.
 */
struct SvSampleTimelineEvidence final {
    bool clock_and_timeline_share_epoch{};
    bool counter_epoch_verified{};
    bool sample_phase_within_profile_limit{};

    [[nodiscard]] constexpr bool qualified() const noexcept {
        return clock_and_timeline_share_epoch &&
               counter_epoch_verified &&
               sample_phase_within_profile_limit;
    }
};

/**
 * Resolve what the SV publisher is allowed to advertise in smpSynch.
 *
 * AUTO requires BOTH a measured PTP clock state and independently qualified
 * SV sample/counter epoch evidence. A PTP LOCKED flag alone is insufficient.
 * Existing publishers using the independent GPTimer intentionally default to
 * unqualified timeline evidence and transmit smpSynch=0.
 *
 * The three non-AUTO modes are explicit lab overrides and may advertise
 * smpSynch 0, 1, or 2 regardless of measured timing. They are therefore
 * always marked simulated so UI/evidence layers cannot confuse a test
 * condition with proven clock synchronization.
 */
[[nodiscard]] constexpr SmpSynchDecision resolve_sv_sync_policy(
    const SvSyncPolicyMode mode,
    const std::optional<SmpSynchValue> measured_synchronization = std::nullopt,
    const SvSampleTimelineEvidence timeline_evidence = {}) noexcept {
    switch (mode) {
    case SvSyncPolicyMode::external_ptp_auto:
        if (measured_synchronization.has_value() && timeline_evidence.qualified()) {
            return {*measured_synchronization, mode, SmpSynchDecisionSource::measured_timing};
        }
        return {SmpSynchValue::not_synchronized, mode, SmpSynchDecisionSource::safe_default};
    case SvSyncPolicyMode::honest_unsynchronized:
        return {SmpSynchValue::not_synchronized, mode, SmpSynchDecisionSource::lab_override};
    case SvSyncPolicyMode::local_compatibility:
        return {SmpSynchValue::local_synchronized, mode, SmpSynchDecisionSource::lab_override};
    case SvSyncPolicyMode::global_compatibility:
        return {SmpSynchValue::global_synchronized, mode, SmpSynchDecisionSource::lab_override};
    }
    return {SmpSynchValue::not_synchronized,
            SvSyncPolicyMode::external_ptp_auto,
            SmpSynchDecisionSource::safe_default};
}

[[nodiscard]] constexpr std::string_view sv_sync_policy_name(
    const SvSyncPolicyMode mode) noexcept {
    switch (mode) {
    case SvSyncPolicyMode::external_ptp_auto:
        return "AUTO";
    case SvSyncPolicyMode::honest_unsynchronized:
        return "FORCE_0";
    case SvSyncPolicyMode::local_compatibility:
        return "FORCE_1_LOCAL";
    case SvSyncPolicyMode::global_compatibility:
        return "FORCE_2_GLOBAL";
    }
    return "AUTO";
}

[[nodiscard]] constexpr std::string_view smp_synch_source_name(
    const SmpSynchDecisionSource source) noexcept {
    switch (source) {
    case SmpSynchDecisionSource::safe_default:
        return "SAFE_DEFAULT";
    case SmpSynchDecisionSource::measured_timing:
        return "MEASURED";
    case SmpSynchDecisionSource::lab_override:
        return "LAB_OVERRIDE";
    }
    return "SAFE_DEFAULT";
}

} // namespace ar::iec61850::time_sync
