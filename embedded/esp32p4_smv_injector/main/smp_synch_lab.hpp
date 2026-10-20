// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/time_sync/smp_synch_policy.hpp"

#include <optional>

namespace ar::esp32p4::smv {

using ar::iec61850::time_sync::SmpSynchDecision;
using ar::iec61850::time_sync::SmpSynchValue;
using ar::iec61850::time_sync::SvSyncPolicyMode;

struct SmpSynchLabStatus final {
    SmpSynchDecision decision{};
    bool measured_input_valid{};
};

/** Set AUTO/FORCE_0/FORCE_1/FORCE_2. Safe to change while SV is running. */
void smp_synch_lab_set_mode(SvSyncPolicyMode mode) noexcept;

[[nodiscard]] SvSyncPolicyMode smp_synch_lab_mode() noexcept;
[[nodiscard]] SmpSynchDecision smp_synch_lab_decision() noexcept;
[[nodiscard]] SmpSynchLabStatus smp_synch_lab_status() noexcept;

/**
 * PTP-P2 measurement hook. AUTO consumes this value only when the
 * discipline/lock engine has real measured evidence. Passing std::nullopt
 * removes that evidence and immediately returns AUTO to smpSynch=0.
 */
void smp_synch_lab_set_measured(std::optional<SmpSynchValue> value) noexcept;

/** SOURCE AUTO only: assert local sync after independent scheduled-phase AND
 * canonical MAC TX evidence have been qualified. Must revoke on STOP, source
 * change, missed slot, TX failure or stale phase; never claims global sync. */
void smp_synch_set_source_local_qualified(bool qualified) noexcept;
[[nodiscard]] bool smp_synch_source_local_qualified() noexcept;

/** RECEIVER AUTO only: independently qualified SV sample phase AND actual
 * canonical TX MAC descriptor continuity. PTP discipline LOCKED alone is
 * insufficient; optional GLOBAL still comes only from traceable GM evidence.
 * This is the existing receiver timeline gate, not a second sync authority. */
void smp_synch_set_receiver_timeline_qualified(bool qualified) noexcept;
[[nodiscard]] bool smp_synch_receiver_timeline_qualified() noexcept;

} // namespace ar::esp32p4::smv
