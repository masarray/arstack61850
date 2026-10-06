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

} // namespace ar::esp32p4::smv
