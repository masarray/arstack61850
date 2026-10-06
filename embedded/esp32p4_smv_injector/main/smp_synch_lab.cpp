// SPDX-License-Identifier: GPL-3.0-or-later

#include "smp_synch_lab.hpp"

#include <atomic>
#include <cstdint>
#include <optional>

namespace ar::esp32p4::smv {
namespace {

using ar::iec61850::time_sync::resolve_sv_sync_policy;

std::atomic<std::uint8_t> g_mode{
    static_cast<std::uint8_t>(SvSyncPolicyMode::external_ptp_auto)};
// -1 means PTP-P2 has not supplied measured lock evidence yet.
std::atomic<int> g_measured_value{-1};

[[nodiscard]] std::optional<SmpSynchValue> measured_value() noexcept {
    const int raw = g_measured_value.load(std::memory_order_acquire);
    switch (raw) {
    case 0:
        return SmpSynchValue::not_synchronized;
    case 1:
        return SmpSynchValue::local_synchronized;
    case 2:
        return SmpSynchValue::global_synchronized;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] SvSyncPolicyMode current_mode() noexcept {
    return static_cast<SvSyncPolicyMode>(
        g_mode.load(std::memory_order_acquire));
}

} // namespace

void smp_synch_lab_set_mode(const SvSyncPolicyMode mode) noexcept {
    g_mode.store(static_cast<std::uint8_t>(mode), std::memory_order_release);
}

SvSyncPolicyMode smp_synch_lab_mode() noexcept {
    return current_mode();
}

SmpSynchDecision smp_synch_lab_decision() noexcept {
    return resolve_sv_sync_policy(current_mode(), measured_value());
}

SmpSynchLabStatus smp_synch_lab_status() noexcept {
    const auto measured = measured_value();
    return {
        resolve_sv_sync_policy(current_mode(), measured),
        measured.has_value(),
    };
}

void smp_synch_lab_set_measured(const std::optional<SmpSynchValue> value) noexcept {
    if (!value.has_value()) {
        g_measured_value.store(-1, std::memory_order_release);
        return;
    }
    g_measured_value.store(
        static_cast<int>(static_cast<std::uint8_t>(*value)),
        std::memory_order_release);
}

} // namespace ar::esp32p4::smv
