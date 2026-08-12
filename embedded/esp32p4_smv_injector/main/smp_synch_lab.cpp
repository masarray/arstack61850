// SPDX-License-Identifier: GPL-3.0-or-later

#include "smp_synch_lab.hpp"

#include "ariec61850/sampled_values/wire_field_offsets.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ar::esp32p4::smv {
namespace {

using ar::iec61850::sampled_values::find_smp_synch_value_offset;
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

[[nodiscard]] SmpSynchDecision current_decision() noexcept {
    const auto mode = static_cast<SvSyncPolicyMode>(
        g_mode.load(std::memory_order_acquire));
    return resolve_sv_sync_policy(mode, measured_value());
}

} // namespace

void smp_synch_lab_set_mode(const SvSyncPolicyMode mode) noexcept {
    g_mode.store(static_cast<std::uint8_t>(mode), std::memory_order_release);
}

SvSyncPolicyMode smp_synch_lab_mode() noexcept {
    return static_cast<SvSyncPolicyMode>(g_mode.load(std::memory_order_acquire));
}

SmpSynchLabStatus smp_synch_lab_status() noexcept {
    const auto measured = measured_value();
    return {current_decision(), measured.has_value()};
}

void smp_synch_lab_set_measured(const std::optional<SmpSynchValue> value) noexcept {
    if (!value.has_value()) {
        g_measured_value.store(-1, std::memory_order_release);
        return;
    }
    g_measured_value.store(static_cast<int>(static_cast<std::uint8_t>(*value)),
                           std::memory_order_release);
}

esp_err_t smp_synch_lab_transmit(
    const esp_eth_handle_t handle,
    void* buffer,
    const std::size_t length) noexcept {
    if (buffer != nullptr) {
        auto bytes = std::span<std::uint8_t>{static_cast<std::uint8_t*>(buffer), length};
        const auto value_offset = find_smp_synch_value_offset(bytes);
        if (value_offset.has_value()) {
            const auto decision = current_decision();
            bytes[*value_offset] = static_cast<std::uint8_t>(decision.value);
        }
    }
    return ::esp_eth_transmit(handle, buffer, length);
}

} // namespace ar::esp32p4::smv
