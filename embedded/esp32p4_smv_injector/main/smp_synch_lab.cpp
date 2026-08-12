// SPDX-License-Identifier: GPL-3.0-or-later

#include "smp_synch_lab.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ar::esp32p4::smv {
namespace {

using ar::iec61850::time_sync::resolve_sv_sync_policy;

constexpr std::uint16_t kSvEtherType = 0x88BAU;
constexpr std::uint16_t kVlanEtherType = 0x8100U;
constexpr std::uint16_t kQinQEtherType = 0x88A8U;
constexpr std::size_t kEthernetHeaderBytes = 14U;
constexpr std::size_t kVlanHeaderBytes = 4U;
constexpr std::size_t kSvApplicationHeaderBytes = 8U;
constexpr std::size_t kMaximumBerProbeBytes = 256U;

std::atomic<std::uint8_t> g_mode{
    static_cast<std::uint8_t>(SvSyncPolicyMode::external_ptp_auto)};
// -1 means PTP-P2 has not supplied measured lock evidence yet.
std::atomic<int> g_measured_value{-1};

[[nodiscard]] std::uint16_t read_u16_be(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[0]) << 8U) |
        static_cast<std::uint16_t>(bytes[1]));
}

[[nodiscard]] std::optional<std::size_t> sv_payload_offset(
    const std::span<const std::uint8_t> frame) noexcept {
    if (frame.size() < kEthernetHeaderBytes) return std::nullopt;

    std::size_t type_offset = 12U;
    std::uint16_t ether_type = read_u16_be(frame.data() + type_offset);
    for (int tags = 0; tags < 2 &&
         (ether_type == kVlanEtherType || ether_type == kQinQEtherType); ++tags) {
        type_offset += kVlanHeaderBytes;
        if (type_offset + 2U > frame.size()) return std::nullopt;
        ether_type = read_u16_be(frame.data() + type_offset);
    }
    if (ether_type != kSvEtherType) return std::nullopt;

    const std::size_t payload = type_offset + 2U;
    if (payload + kSvApplicationHeaderBytes >= frame.size()) return std::nullopt;
    return payload;
}

[[nodiscard]] std::optional<std::size_t> find_smp_synch_value_offset(
    const std::span<const std::uint8_t> frame,
    const std::size_t sv_payload) noexcept {
    const std::size_t begin = sv_payload + kSvApplicationHeaderBytes;
    const std::size_t end = std::min(frame.size(), begin + kMaximumBerProbeBytes);
    if (begin + 4U > end) return std::nullopt;

    // smpSynch is context tag 0x85 with one-byte value. To avoid accidentally
    // matching bytes inside svID/confRev, also require that the next byte starts
    // the expected smpRate (0x86) or seqOfData (0x87) field.
    for (std::size_t index = begin; index + 3U < end; ++index) {
        if (frame[index] == 0x85U && frame[index + 1U] == 0x01U &&
            (frame[index + 3U] == 0x86U || frame[index + 3U] == 0x87U)) {
            return index + 2U;
        }
    }
    return std::nullopt;
}

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
    if (buffer != nullptr && length >= kEthernetHeaderBytes) {
        auto bytes = std::span<std::uint8_t>{static_cast<std::uint8_t*>(buffer), length};
        if (const auto payload = sv_payload_offset(bytes); payload.has_value()) {
            if (const auto value_offset = find_smp_synch_value_offset(bytes, *payload);
                value_offset.has_value()) {
                const auto decision = current_decision();
                bytes[*value_offset] = static_cast<std::uint8_t>(decision.value);
            }
        }
    }
    return ::esp_eth_transmit(handle, buffer, length);
}

} // namespace ar::esp32p4::smv
