// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace ar::iec61850::time_sync {

// Deterministic, allocation-free external clock admission for ONE EMAC.
// An Announce is DISCOVERY evidence, never a measured PTP clock lock.
// The caller MUST stop the old PTP worker and release EMAC ownership before
// configuring/starting a different role; SV phase authority is independent.
struct PtpExternalCandidate final {
    std::array<std::uint8_t, 8> port_clock{};
    std::array<std::uint8_t, 8> grandmaster_clock{};
    std::uint16_t port_number{};
    std::uint8_t clock_class{255U};
    std::uint16_t steps_removed{0xFFFFU};

    friend bool operator==(const PtpExternalCandidate&,
                           const PtpExternalCandidate&) = default;
};

enum class PtpExternalHandover : std::uint8_t {
    none, enter_receiver, return_source
};

class PtpExternalFailoverPolicy final {
public:
    static constexpr std::uint32_t minimum_announce_separation_ms = 200U;
    static constexpr std::uint32_t maximum_announce_gap_ms = 3000U;
    static constexpr std::uint32_t required_announces = 3U;
    static constexpr std::uint32_t receiver_acquire_timeout_ms = 12000U;
    static constexpr std::uint32_t receiver_loss_timeout_ms = 5000U;
    static constexpr std::uint32_t retry_cooldown_ms = 20000U;

    void set_enabled(const bool enabled, const std::uint64_t now_ms) noexcept {
        enabled_ = enabled;
        stable_announces_ = 0U;
        candidate_ = {};
        has_candidate_ = false;
        last_announce_ms_ = 0U;
        // Fail closed on a fresh enable and after explicit operator disable.
        last_switch_ms_ = now_ms;
        receiver_locked_once_ = false;
        receiver_lost_since_ms_ = 0U;
    }

    [[nodiscard]] bool enabled() const noexcept { return enabled_; }

    // Only validated, same-domain/same-VLAN/transport Ethernet Announce
    // reaches this function, with self-origin frames removed by RX adapter.
    // Reject bad clocks and flapping sources before considering a handoff.
    void observe_announce(const PtpExternalCandidate& candidate,
                          const std::uint64_t now_ms) noexcept {
        if (!enabled_ || candidate.port_number == 0U ||
            candidate.clock_class == 255U || candidate.steps_removed >= 255U ||
            empty_identity(candidate.port_clock) ||
            empty_identity(candidate.grandmaster_clock)) return;
        if (!has_candidate_ || candidate != candidate_ ||
            now_ms < last_announce_ms_ ||
            now_ms - last_announce_ms_ > maximum_announce_gap_ms) {
            candidate_ = candidate;
            has_candidate_ = true;
            stable_announces_ = 1U;
            last_announce_ms_ = now_ms;
            return;
        }
        if (now_ms - last_announce_ms_ < minimum_announce_separation_ms) return;
        last_announce_ms_ = now_ms;
        if (stable_announces_ < required_announces) ++stable_announces_;
    }

    [[nodiscard]] bool candidate_fresh(const std::uint64_t now_ms) const noexcept {
        return has_candidate_ && now_ms >= last_announce_ms_ &&
               now_ms - last_announce_ms_ <= maximum_announce_gap_ms;
    }

    [[nodiscard]] bool qualified_candidate(const std::uint64_t now_ms) const noexcept {
        return enabled_ && candidate_fresh(now_ms) &&
               stable_announces_ >= required_announces;
    }

    [[nodiscard]] std::uint32_t stable_announces() const noexcept {
        return stable_announces_;
    }

    [[nodiscard]] PtpExternalCandidate candidate() const noexcept {
        return candidate_;
    }

    // Role is a verified physical worker (not an operator dropdown).
    // Never assert LOCK from this policy; actual receiver discipline does.
    [[nodiscard]] PtpExternalHandover evaluate(
        const bool source_worker_active, const bool receiver_worker_active,
        const bool receiver_locked, const std::uint64_t now_ms) noexcept {
        if (!enabled_ || source_worker_active == receiver_worker_active ||
            now_ms < last_switch_ms_) return PtpExternalHandover::none;
        if (source_worker_active) {
            if (qualified_candidate(now_ms) &&
                now_ms - last_switch_ms_ >= retry_cooldown_ms) {
                return PtpExternalHandover::enter_receiver;
            }
            return PtpExternalHandover::none;
        }
        if (receiver_locked) {
            receiver_locked_once_ = true;
            receiver_lost_since_ms_ = 0U;
            return PtpExternalHandover::none;
        }
        if (receiver_lost_since_ms_ == 0U)
            receiver_lost_since_ms_ = now_ms == 0U ? 1U : now_ms;
        const auto lost_ms = now_ms - last_switch_ms_;
        const auto drop_ms = now_ms >= receiver_lost_since_ms_
            ? now_ms - receiver_lost_since_ms_ : 0U;
        if ((!receiver_locked_once_ &&
             lost_ms >= receiver_acquire_timeout_ms) ||
            (receiver_locked_once_ &&
             drop_ms >= receiver_loss_timeout_ms)) {
            return PtpExternalHandover::return_source;
        }
        return PtpExternalHandover::none;
    }

    // Only call after the new physical worker actually started.
    void switched_to_receiver(const std::uint64_t now_ms) noexcept {
        last_switch_ms_ = now_ms;
        receiver_lost_since_ms_ = 0U;
        receiver_locked_once_ = false;
    }
    void switched_to_source(const std::uint64_t now_ms) noexcept {
        last_switch_ms_ = now_ms;
        receiver_lost_since_ms_ = 0U;
        receiver_locked_once_ = false;
        stable_announces_ = 0U;
        has_candidate_ = false;
    }

private:
    [[nodiscard]] static constexpr bool empty_identity(
        const std::array<std::uint8_t, 8>& value) noexcept {
        for (const auto b : value) if (b != 0U) return false;
        return true;
    }

    bool enabled_{};
    bool has_candidate_{};
    PtpExternalCandidate candidate_{};
    std::uint32_t stable_announces_{};
    std::uint64_t last_announce_ms_{};
    std::uint64_t last_switch_ms_{};
    std::uint64_t receiver_lost_since_ms_{};
    bool receiver_locked_once_{};
};

} // namespace ar::iec61850::time_sync
