// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <tuple>

namespace ar::iec61850::time_sync {

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

    [[nodiscard]] constexpr auto order_key() const noexcept {
        return std::tie(priority1, clock_class, clock_accuracy, variance,
                        priority2, gm_identity, steps_removed);
    }
};

struct PtpExternalCandidateGate final {
    static constexpr std::uint64_t kStableMs = 3000U;
    static constexpr std::uint64_t kStaleMs = 2500U;
    static constexpr std::uint8_t kMinAnnounces = 3U;

    // Called only with a *parsed*, domain/transport/VLAN-matched Announce.
    // No heap, dynamic clock reads, mutex, network action, or task start.
    // Caller serializes access on one small RX control-path lock.
    void observe(const PtpAnnounceRank& incoming,
                 const PtpAnnounceRank& local,
                 const std::uint64_t now_ms) noexcept {
        if (incoming.gm_identity == local.gm_identity ||
            incoming.steps_removed > 255U ||
            !(incoming.order_key() < local.order_key())) return;

        // Consecutive source identity and quality are required, including
        // unchanged class/priority. A single spoofed/burst Announce cannot
        // request an EMAC timebase handover.
        if (!has_candidate_ || incoming.order_key() != candidate_.order_key() ||
            now_ms < last_seen_ms_ || now_ms - last_seen_ms_ > kStaleMs) {
            candidate_ = incoming;
            has_candidate_ = true;
            first_seen_ms_ = now_ms;
            last_seen_ms_ = now_ms;
            count_ = 1U;
            return;
        }
        // Additional datagrams at the SAME timestamp do not qualify;
        // hysteresis is measured in true monotonic time.
        if (now_ms > last_seen_ms_) {
            last_seen_ms_ = now_ms;
            if (count_ < kMinAnnounces) ++count_;
        }
    }

    [[nodiscard]] bool stable(const std::uint64_t now_ms) const noexcept {
        return has_candidate_ && now_ms >= last_seen_ms_ &&
            now_ms - last_seen_ms_ <= kStaleMs &&
            now_ms >= first_seen_ms_ &&
            now_ms - first_seen_ms_ >= kStableMs &&
            count_ >= kMinAnnounces;
    }

    void reset() noexcept {
        candidate_ = {};
        has_candidate_ = false;
        first_seen_ms_ = 0U;
        last_seen_ms_ = 0U;
        count_ = 0U;
    }

private:
    PtpAnnounceRank candidate_{};
    std::uint64_t first_seen_ms_{};
    std::uint64_t last_seen_ms_{};
    std::uint8_t count_{};
    bool has_candidate_{};
};

struct PtpExternalHandoverGuard final {
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
