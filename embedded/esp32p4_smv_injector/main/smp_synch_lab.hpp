// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/time_sync/smp_synch_policy.hpp"
#include <array>
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
enum class SvSyncLossReason : std::uint8_t {
    none, missed_slot, ethernet_link, observer_stale, mac_egress,
    canonical_tx, ptp_lease, observer_epoch, phase_or_mac,
    phase_handoff, publisher_stop, ptp_stop, operator_mode, other,
    count
};
constexpr std::size_t kSvSyncFlightEventCapacity = 16;
struct SvSyncFlightEvent final {
    std::uint32_t sequence{};
    std::uint32_t monotonic_ms{};
    SvSyncLossReason reason{SvSyncLossReason::none};
    bool acquired{};
};
struct SvSyncFlightSnapshot final {
    std::uint32_t events{};
    std::uint32_t losses{};
    std::uint32_t acquisitions{};
    std::int32_t phase_error_us{};
    std::uint32_t uncertainty_us{};
    std::uint32_t mac_pairs{};
    std::uint32_t clean_epochs{};
    std::uint32_t sample_epoch{};
    bool phase_accepted{};
    bool software_tracking{};
    std::array<std::uint32_t, static_cast<std::size_t>(SvSyncLossReason::count)> cause_counts{};
    std::array<SvSyncFlightEvent, kSvSyncFlightEventCapacity> recent{};
    std::size_t recent_count{};
};
/** Constant-memory on-board recorder. Records transition causes without logging,
 * allocation, blocking I/O, mutex, or adding work on ordinary 4kHz frames. */
void smp_synch_diag_observe(std::int32_t phase_us, std::uint32_t uncertainty_us,
                           std::uint32_t mac_pairs, std::uint32_t clean_epochs,
                           std::uint32_t epoch, bool accepted, bool tracking) noexcept;
[[nodiscard]] SvSyncFlightSnapshot smp_synch_diag_snapshot() noexcept;
[[nodiscard]] const char* smp_synch_diag_reason_name(SvSyncLossReason reason) noexcept;
void smp_synch_set_source_local_qualified(
    bool qualified, SvSyncLossReason reason = SvSyncLossReason::other,
    std::uint32_t monotonic_ms = 0U) noexcept;
[[nodiscard]] bool smp_synch_source_local_qualified() noexcept;

/** RECEIVER AUTO only: independently qualified SV sample phase AND actual
 * canonical TX MAC descriptor continuity. PTP discipline LOCKED alone is
 * insufficient; optional GLOBAL still comes only from traceable GM evidence.
 * This is the existing receiver timeline gate, not a second sync authority. */
void smp_synch_set_receiver_timeline_qualified(
    bool qualified, SvSyncLossReason reason = SvSyncLossReason::other,
    std::uint32_t monotonic_ms = 0U) noexcept;
[[nodiscard]] bool smp_synch_receiver_timeline_qualified() noexcept;

} // namespace ar::esp32p4::smv
