// SPDX-License-Identifier: GPL-3.0-or-later

#include "smp_synch_lab.hpp"
#include "ariec61850/time_sync/sv_ptp_epoch.hpp"

#include <algorithm>
#include <array>
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
// Separate from RECEIVER PTP discipline: a valid GM clock is not a
// verified canonical SV sampling/egress timeline.
std::atomic_bool g_sv_timeline_phase_qualified{false};
std::atomic_bool g_source_local_qualified{false};

// Fixed 16-event on-board flight recorder. Two 32-bit atomic words per
// slot avoid 64-bit atomics (not guaranteed lock-free on ESP32-P4). A
// sequence-stamped entry is published LAST with release ordering.
constexpr std::size_t kReasonCount =
    static_cast<std::size_t>(SvSyncLossReason::count);
std::array<std::atomic<std::uint32_t>, kReasonCount> g_cause_counts{};
std::array<std::atomic<std::uint32_t>, kSvSyncFlightEventCapacity> g_event_codes{};
std::array<std::atomic<std::uint32_t>, kSvSyncFlightEventCapacity> g_event_times{};
std::atomic<std::uint32_t> g_event_sequence{0U};
std::atomic<std::uint32_t> g_acquisition_count{0U};
std::atomic<std::uint32_t> g_loss_count{0U};
std::atomic<std::int32_t> g_diag_phase_us{0};
std::atomic<std::uint32_t> g_diag_uncertainty_us{0U};
std::atomic<std::uint32_t> g_diag_mac_pairs{0U};
std::atomic<std::uint32_t> g_diag_clean_epochs{0U};
std::atomic<std::uint32_t> g_diag_epoch{0U};
std::atomic_bool g_diag_accepted{false};
std::atomic_bool g_diag_tracking{false};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::atomic<std::int32_t>::is_always_lock_free);

void record_transition(const SvSyncLossReason reason,
                       const bool qualified,
                       const std::uint32_t uptime_ms) noexcept {
    if (qualified) g_acquisition_count.fetch_add(1U, std::memory_order_relaxed);
    else {
        g_loss_count.fetch_add(1U, std::memory_order_relaxed);
        const auto cause = static_cast<std::size_t>(reason);
        if (cause < kReasonCount)
            g_cause_counts[cause].fetch_add(1U, std::memory_order_relaxed);
    }
    const auto seq = g_event_sequence.fetch_add(1U, std::memory_order_acq_rel) + 1U;
    const auto slot = static_cast<std::size_t>(
        (seq - 1U) % kSvSyncFlightEventCapacity);
    const auto code = ((seq & 0xFFFFU) << 16U) |
        (static_cast<std::uint32_t>(reason) << 1U) |
        (qualified ? 1U : 0U);
    g_event_times[slot].store(uptime_ms, std::memory_order_relaxed);
    g_event_codes[slot].store(code, std::memory_order_release);
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

[[nodiscard]] std::optional<SmpSynchValue> auto_evidence() noexcept {
    // SOURCE has no receiver discipline object. Its local-sync claim
    // originates exclusively from the sample phase + canonical TX gate.
    // Global sync is reserved for measured external time traceability.
    if (g_source_local_qualified.load(std::memory_order_acquire))
        return SmpSynchValue::local_synchronized;
    return ar::iec61850::time_sync::qualified_sv_sync_value(
        measured_value(), g_sv_timeline_phase_qualified.load(
            std::memory_order_acquire));
}

[[nodiscard]] SvSyncPolicyMode current_mode() noexcept {
    return static_cast<SvSyncPolicyMode>(
        g_mode.load(std::memory_order_acquire));
}

} // namespace

void smp_synch_lab_set_mode(const SvSyncPolicyMode mode) noexcept {
    const auto old = g_mode.load(std::memory_order_acquire);
    if (old != static_cast<std::uint8_t>(mode)) {
        // Single serial control-plane writer. Revoke BEFORE exposing AUTO,
        // never allow even one 4kHz wire frame to inherit stale override proof.
        smp_synch_set_source_local_qualified(false, SvSyncLossReason::operator_mode);
        smp_synch_set_receiver_timeline_qualified(false, SvSyncLossReason::operator_mode);
    }
    g_mode.store(static_cast<std::uint8_t>(mode), std::memory_order_release);
}

SvSyncPolicyMode smp_synch_lab_mode() noexcept {
    return current_mode();
}

SmpSynchDecision smp_synch_lab_decision() noexcept {
    return resolve_sv_sync_policy(current_mode(), auto_evidence());
}

SmpSynchLabStatus smp_synch_lab_status() noexcept {
    const auto measured = auto_evidence();
    return {
        resolve_sv_sync_policy(current_mode(), measured),
        measured.has_value(),
    };
}

void smp_synch_lab_set_measured(const std::optional<SmpSynchValue> value) noexcept {
    if (!value.has_value() ||
        *value == SmpSynchValue::not_synchronized) {
        // A LOST/UNLOCKED PTP receiver invalidates the SV timeline proof,
        // not merely the visible measured-PTP value. Relock must establish
        // fresh phase and MAC evidence before AUTO may promote again.
        smp_synch_set_receiver_timeline_qualified(
            false, SvSyncLossReason::ptp_lease);
        g_measured_value.store(
            value.has_value() ? 0 : -1, std::memory_order_release);
        return;
    }
    g_measured_value.store(
        static_cast<int>(static_cast<std::uint8_t>(*value)),
        std::memory_order_release);
}

void smp_synch_set_source_local_qualified(
    const bool qualified, const SvSyncLossReason reason,
    const std::uint32_t monotonic_ms) noexcept {
    if (g_source_local_qualified.exchange(
            qualified, std::memory_order_acq_rel) != qualified)
        record_transition(qualified ? SvSyncLossReason::none : reason,
                          qualified, monotonic_ms);
}

bool smp_synch_source_local_qualified() noexcept {
    return g_source_local_qualified.load(std::memory_order_acquire);
}

void smp_synch_set_receiver_timeline_qualified(
    const bool qualified, const SvSyncLossReason reason,
    const std::uint32_t monotonic_ms) noexcept {
    if (g_sv_timeline_phase_qualified.exchange(
            qualified, std::memory_order_acq_rel) != qualified)
        record_transition(qualified ? SvSyncLossReason::none : reason,
                          qualified, monotonic_ms);
}

void smp_synch_diag_observe(
    const std::int32_t phase_us, const std::uint32_t uncertainty_us,
    const std::uint32_t mac_pairs, const std::uint32_t clean_epochs,
    const std::uint32_t epoch, const bool accepted, const bool tracking) noexcept {
    g_diag_phase_us.store(phase_us, std::memory_order_relaxed);
    g_diag_uncertainty_us.store(uncertainty_us, std::memory_order_relaxed);
    g_diag_mac_pairs.store(mac_pairs, std::memory_order_relaxed);
    g_diag_clean_epochs.store(clean_epochs, std::memory_order_relaxed);
    g_diag_epoch.store(epoch, std::memory_order_release);
    g_diag_accepted.store(accepted, std::memory_order_relaxed);
    g_diag_tracking.store(tracking, std::memory_order_relaxed);
}
SvSyncFlightSnapshot smp_synch_diag_snapshot() noexcept {
    SvSyncFlightSnapshot s{};
    s.events = g_event_sequence.load(std::memory_order_acquire);
    s.acquisitions = g_acquisition_count.load(std::memory_order_relaxed);
    s.losses = g_loss_count.load(std::memory_order_relaxed);
    s.phase_error_us = g_diag_phase_us.load(std::memory_order_relaxed);
    s.uncertainty_us = g_diag_uncertainty_us.load(std::memory_order_relaxed);
    s.mac_pairs = g_diag_mac_pairs.load(std::memory_order_relaxed);
    s.clean_epochs = g_diag_clean_epochs.load(std::memory_order_relaxed);
    s.sample_epoch = g_diag_epoch.load(std::memory_order_acquire);
    s.phase_accepted = g_diag_accepted.load(std::memory_order_relaxed);
    s.software_tracking = g_diag_tracking.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < kReasonCount; ++i)
        s.cause_counts[i] = g_cause_counts[i].load(std::memory_order_relaxed);
    const auto window = static_cast<std::uint32_t>(
        std::min<std::uint32_t>(s.events, kSvSyncFlightEventCapacity));
    for (std::uint32_t offset = window; offset > 0U; --offset) {
        const auto seq = s.events - offset + 1U;
        const auto slot = static_cast<std::size_t>(
            (seq - 1U) % kSvSyncFlightEventCapacity);
        const auto before = g_event_codes[slot].load(std::memory_order_acquire);
        if (((before >> 16U) & 0xFFFFU) != (seq & 0xFFFFU)) continue;
        const auto time = g_event_times[slot].load(std::memory_order_relaxed);
        const auto after = g_event_codes[slot].load(std::memory_order_acquire);
        if (before != after) continue; // overwritten during read
        s.recent[s.recent_count++] = SvSyncFlightEvent{
            seq, time,
            static_cast<SvSyncLossReason>((before >> 1U) & 0xFFU),
            (before & 1U) != 0U
        };
    }
    return s;
}
const char* smp_synch_diag_reason_name(const SvSyncLossReason reason) noexcept {
    switch (reason) {
    case SvSyncLossReason::none: return "ACQUIRED";
    case SvSyncLossReason::missed_slot: return "MISSED_SAMPLE_SLOT";
    case SvSyncLossReason::ethernet_link: return "ETHERNET_LINK_LOSS";
    case SvSyncLossReason::observer_stale: return "PHASE_OBSERVER_STALE";
    case SvSyncLossReason::mac_egress: return "MAC_TX_TIMESTAMP_INVALID";
    case SvSyncLossReason::canonical_tx: return "CANONICAL_SV_TX_FAILURE";
    case SvSyncLossReason::ptp_lease: return "PTP_LEASE_OR_GM_CHANGE";
    case SvSyncLossReason::observer_epoch: return "OBSERVER_EPOCH_OR_LINK";
    case SvSyncLossReason::phase_or_mac: return "PHASE_OR_MAC_GATE_LOST";
    case SvSyncLossReason::phase_handoff: return "PHASE_HANDOFF_RACE";
    case SvSyncLossReason::publisher_stop: return "PUBLISHER_STOP";
    case SvSyncLossReason::ptp_stop: return "PTP_STOP";
    case SvSyncLossReason::operator_mode: return "OPERATOR_MODE_CHANGE";
    case SvSyncLossReason::other: return "UNCLASSIFIED_REVOCATION";
    case SvSyncLossReason::count: break;
    }
    return "UNKNOWN";
}

bool smp_synch_receiver_timeline_qualified() noexcept {
    return g_sv_timeline_phase_qualified.load(std::memory_order_acquire);
}

} // namespace ar::esp32p4::smv
