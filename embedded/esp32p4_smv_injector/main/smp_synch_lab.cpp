// SPDX-License-Identifier: GPL-3.0-or-later

#include "smp_synch_lab.hpp"
#include "live_control.hpp"

#include "ariec61850/sampled_values/wire_field_offsets.hpp"

#include "esp_log.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ar::esp32p4::smv {
namespace {

using ar::iec61850::sampled_values::find_smp_synch_value_offset;
using ar::iec61850::time_sync::resolve_sv_sync_policy;

constexpr char kTag[] = "ar_smv_txguard";
constexpr std::uint16_t kDiagnosticMirrorAppId = 0x4F01U;
// At the production 4000 fps profile this trips after 100 ms of uninterrupted
// canonical SV transport failure. Mirror failures never drive this guard.
constexpr std::uint32_t kCanonicalFailureStopThreshold = 400U;

std::atomic<std::uint8_t> g_mode{
    static_cast<std::uint8_t>(SvSyncPolicyMode::external_ptp_auto)};
// -1 means PTP-P2 has not supplied measured lock evidence yet.
std::atomic<int> g_measured_value{-1};
std::atomic<std::uint32_t> g_consecutive_canonical_tx_failures{0U};
std::atomic_bool g_transport_fault_reported{false};

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

[[nodiscard]] bool diagnostic_mirror_frame(const std::span<const std::uint8_t> bytes) noexcept {
    // Untagged IEC 61850-9-2: EtherType 0x88BA, APPID immediately follows.
    if (bytes.size() >= 16U && bytes[12] == 0x88U && bytes[13] == 0xBAU) {
        const auto app_id = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(bytes[14]) << 8U) | bytes[15]);
        return app_id == kDiagnosticMirrorAppId;
    }

    // 802.1Q tagged IEC 61850-9-2: inner EtherType 0x88BA, then APPID.
    if (bytes.size() >= 20U && bytes[12] == 0x81U && bytes[13] == 0x00U &&
        bytes[16] == 0x88U && bytes[17] == 0xBAU) {
        const auto app_id = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(bytes[18]) << 8U) | bytes[19]);
        return app_id == kDiagnosticMirrorAppId;
    }
    return false;
}

void observe_canonical_tx_result(const esp_err_t result) noexcept {
    if (result == ESP_OK) {
        g_consecutive_canonical_tx_failures.store(0U, std::memory_order_release);
        g_transport_fault_reported.store(false, std::memory_order_release);
        return;
    }

    const auto failures =
        g_consecutive_canonical_tx_failures.fetch_add(1U, std::memory_order_acq_rel) + 1U;
    if (failures < kCanonicalFailureStopThreshold) return;

    // The sample clock can be perfectly healthy while the Ethernet data plane is
    // completely dead. Fail closed instead of leaving Studio green at
    // "Injection running" with txFail=4000.
    if (!g_transport_fault_reported.exchange(true, std::memory_order_acq_rel)) {
        ESP_LOGE(kTag,
                 "Canonical SV transport fault: %lu consecutive TX failures (%s); forcing STOP",
                 static_cast<unsigned long>(failures),
                 esp_err_to_name(result));
    }
    live_control_force_stop();
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
    auto bytes = std::span<std::uint8_t>{};
    if (buffer != nullptr) {
        bytes = std::span<std::uint8_t>{static_cast<std::uint8_t*>(buffer), length};
        const auto value_offset = find_smp_synch_value_offset(bytes);
        if (value_offset.has_value()) {
            const auto decision = current_decision();
            bytes[*value_offset] = static_cast<std::uint8_t>(decision.value);
        }
    }

    const auto result = ::esp_eth_transmit(handle, buffer, length);
    if (!diagnostic_mirror_frame(bytes)) {
        observe_canonical_tx_result(result);
    }
    return result;
}

} // namespace ar::esp32p4::smv
