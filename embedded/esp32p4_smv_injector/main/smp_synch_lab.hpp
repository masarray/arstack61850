// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/time_sync/smp_synch_policy.hpp"

#include "esp_eth.h"

#include <cstddef>
#include <cstdint>
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
[[nodiscard]] SmpSynchLabStatus smp_synch_lab_status() noexcept;

/**
 * Future PTP-P2 hook. AUTO consumes this value only when the discipline/lock
 * engine has real measured evidence. Passing std::nullopt removes that evidence
 * and immediately returns AUTO to the safe smpSynch=0 default.
 */
void smp_synch_lab_set_measured(std::optional<SmpSynchValue> value) noexcept;

/**
 * Transmit shim used only by app_main.cpp. It patches the already-encoded SV
 * smpSynch TLV immediately before TX, allowing live lab transitions without
 * rebuilding the deterministic packet template. Non-SV frames are untouched.
 */
esp_err_t smp_synch_lab_transmit(
    esp_eth_handle_t handle,
    void* buffer,
    std::size_t length) noexcept;

} // namespace ar::esp32p4::smv

#if defined(AR_SMP_SYNCH_INTERCEPT_TX) && AR_SMP_SYNCH_INTERCEPT_TX
#define esp_eth_transmit(handle, buffer, length) \
    ::ar::esp32p4::smv::smp_synch_lab_transmit((handle), (buffer), (length))
#endif
