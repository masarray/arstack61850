// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <tuple>
#include <span>
#include "ariec61850/time_sync/ptp.hpp"

namespace ar::iec61850::time_sync {

// A single strict source for foreign-PTP wire admission. Matching only
// domain number is insufficient on a utility bus carrying multiple profiles.
struct PtpForeignWireProfile final {
    std::uint8_t domain{};
    std::uint8_t transport_specific{};
    bool vlan_enabled{};
    std::uint16_t vlan_id{};
    std::uint8_t vlan_priority{};

    [[nodiscard]] bool matches(
        const PtpFrame& frame,
        const std::span<const std::uint8_t> ethernet) const noexcept {
        if (frame.header.domain_number != domain ||
            frame.header.transport_specific != transport_specific ||
            frame.outer_vlan_id.has_value()) return false;
        if (!vlan_enabled) return !frame.vlan_id.has_value();
        return frame.vlan_id.has_value() && *frame.vlan_id == vlan_id &&
            ethernet.size() >= 18U &&
            ethernet[12] == 0x81U && ethernet[13] == 0x00U &&
            (ethernet[14] >> 5U) == vlan_priority;
    }
};

// A comparison of announced clock datasets is only *candidate selection*.
// It cannot prove external offset, path delay, servo lock or UTC traceability.
struct PtpAnnounceRank final {
    std::uint8_t priority1{255};
    std::uint8_t clock_class{255};
    std::uint8_t clock_accuracy{255};
    std::uint16_t variance{65535};
    std::uint8_t priority2{255};
    std::array<std::uint8_t, 8> gm_identity{};
    std::uint16_t steps_removed{65535};
    std::array<std::uint8_t, 8> source_identity{};
    std::uint16_t source_port{};

    [[nodiscard]] constexpr auto order_key() const noexcept {
        return std::tie(priority1, clock_class, clock_accuracy, variance,
                        priority2, gm_identity, steps_removed);
    }
};

// At most four foreign port candidates, no heap or unbounded scans. This
// avoids a pathological single-slot gate that never qualifies when two GMs
// Announce alternately on the same utility LAN.
struct PtpExternalCandidateGate final {
    static constexpr std::uint64_t kStableMs = 3000U;
    static constexpr std::uint64_t kStaleMs = 2500U;
    static constexpr std::uint8_t kMinAnnounces = 3U;
    static constexpr std::size_t kMaxCandidates = 4U;

    // Caller serializes access on a short control-plane RX spinlock.
    void observe(const PtpAnnounceRank& incoming,
                 const PtpAnnounceRank& local,
                 const std::uint64_t now_ms) noexcept {
        auto* slot = find_source(incoming);
        const bool valid = incoming.gm_identity != local.gm_identity &&
            incoming.steps_removed <= 255U &&
            incoming.order_key() < local.order_key();
        if (!valid) {
            if (slot) *slot = {}; // immediately revoke worsened GM quality
            return;
        }
        if (slot == nullptr) {
            slot = find_free_or_stale(now_ms);
            if (slot == nullptr) {
                // Retain stronger, already-qualifying candidates when a
                // fifth, less capable clock begins broadcasting.
                slot = &slots_[0];
                for (auto& other : slots_) {
                    if (slot->rank.order_key() < other.rank.order_key())
                        slot = &other;
                }
                if (!(incoming.order_key() < slot->rank.order_key()))
                    return;
            }
            *slot = {};
            slot->rank = incoming;
            slot->first_ms = now_ms;
            slot->last_ms = now_ms;
            slot->observations = 1U;
            slot->occupied = true;
            return;
        }
        if (incoming.order_key() != slot->rank.order_key() ||
            now_ms < slot->last_ms ||
            now_ms - slot->last_ms > kStaleMs) {
            *slot = {};
            slot->rank = incoming;
            slot->first_ms = now_ms;
            slot->last_ms = now_ms;
            slot->observations = 1U;
            slot->occupied = true;
            return;
        }
        // Identical-timestamp burst cannot increase qualification counter.
        if (now_ms > slot->last_ms) {
            slot->last_ms = now_ms;
            if (slot->observations < kMinAnnounces) ++slot->observations;
        }
    }

    [[nodiscard]] bool stable(const std::uint64_t now_ms) const noexcept {
        for (const auto& slot : slots_) {
            if (slot.occupied && now_ms >= slot.last_ms &&
                now_ms - slot.last_ms <= kStaleMs &&
                now_ms >= slot.first_ms &&
                now_ms - slot.first_ms >= kStableMs &&
                slot.observations >= kMinAnnounces) return true;
        }
        return false;
    }

    void reset() noexcept { slots_ = {}; }

private:
    struct Slot {
        PtpAnnounceRank rank{};
        std::uint64_t first_ms{};
        std::uint64_t last_ms{};
        std::uint8_t observations{};
        bool occupied{};
    };
    [[nodiscard]] Slot* find_source(const PtpAnnounceRank& rank) noexcept {
        for (auto& slot : slots_)
            if (slot.occupied && slot.rank.source_identity == rank.source_identity &&
                slot.rank.source_port == rank.source_port) return &slot;
        return nullptr;
    }
    [[nodiscard]] Slot* find_free_or_stale(
        const std::uint64_t now_ms) noexcept {
        for (auto& slot : slots_)
            if (!slot.occupied || now_ms < slot.last_ms ||
                now_ms - slot.last_ms > kStaleMs) return &slot;
        return nullptr;
    }
    std::array<Slot, kMaxCandidates> slots_{};
};

struct PtpExternalHandoverGuard final {
    static constexpr std::uint32_t kMinSwitchDwellMs = 30000U;
    [[nodiscard]] static constexpr bool cooldown_expired(
        const std::uint32_t now_ms,
        const std::uint32_t not_before_ms) noexcept {
        // Bounded 30-s deadline survives ESP32's 32-bit ms wrap.
        return static_cast<std::int32_t>(now_ms - not_before_ms) >= 0;
    }

    // No forced SV shutdown; an operator's SV START owns output. Handover is
    // deferred until TX has really stopped, and SOURCE/RECEIVER must be
    // mutually exclusive. Manual PTP action always wins.
    [[nodiscard]] static constexpr bool may_handover(
        const bool auto_enabled, const bool manual_override,
        const bool candidate_stable, const bool sv_tx_running,
        const bool local_source_owned, const bool receiver_owned,
        const bool link_up) noexcept {
        return auto_enabled && !manual_override && candidate_stable &&
            !sv_tx_running && local_source_owned && !receiver_owned && link_up;
    }

    // The time receiver may recover local GM only with an explicit bench
    // fallback option and no selected Announce for an entire quiet window.
    [[nodiscard]] static constexpr bool may_fallback(
        const bool fallback_opted_in, const bool manual_override,
        const bool sv_tx_running, const bool receiver_owned,
        const bool external_source_selected, const bool link_up,
        const std::uint64_t quiet_for_ms) noexcept {
        return fallback_opted_in && !manual_override && !sv_tx_running &&
            receiver_owned && !external_source_selected && link_up &&
            quiet_for_ms >= 15000U;
    }
};
} // namespace ar::iec61850::time_sync
