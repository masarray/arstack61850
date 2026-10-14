// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>

namespace ar::iec61850::time_sync {

// Portable EMAC clock evidence only. Never implies SV GPTimer or PPS phase.
struct PtpReceiverClockObservation final {
    bool runtime_ready{};
    bool source_selected{};
    std::array<std::uint8_t, 8> source_clock_identity{};
    std::uint16_t source_port_number{};
    bool discipline_locked{};
    bool clock_discontinuity{};
};

struct PtpReceiverClockDecision final {
    // A lease is reusable only when BOTH generation and usable agree.
    std::uint32_t generation{};
    bool clock_usable{};
};

// Worker-owned, allocation-free clock lease gate. Phase steps, GM switches
// and actuation failures revoke a still-LOCKED clock until unlock/relock.
class PtpReceiverClockAuthority final {
public:
    [[nodiscard]] PtpReceiverClockDecision observe(
        const PtpReceiverClockObservation& input) noexcept {
        const bool selected = input.runtime_ready && input.source_selected;
        const bool locked = selected && input.discipline_locked;
        const bool identity_changed =
            selected && (!prior_selected_ ||
                input.source_clock_identity != prior_identity_ ||
                input.source_port_number != prior_port_);
        const bool state_changed =
            (initialized_ ? (prior_running_ != input.runtime_ready ||
                             prior_selected_ != selected ||
                             prior_locked_ != locked)
                          : input.runtime_ready) ||
            identity_changed;
        if (state_changed || input.clock_discontinuity) {
            ++generation_;
            if (generation_ == 0U) ++generation_; // zero is never admitted
        }
        if (identity_changed || input.clock_discontinuity) blocked_ = true;
        // An *observed* unlocked state precedes all renewed lock admission.
        if (!locked && !input.clock_discontinuity) blocked_ = false;
        initialized_ = true;
        prior_running_ = input.runtime_ready;
        prior_selected_ = selected;
        prior_locked_ = locked;
        prior_identity_ = input.source_clock_identity;
        prior_port_ = input.source_port_number;
        return {generation_, locked && !blocked_};
    }
private:
    std::uint32_t generation_{};
    bool initialized_{};
    bool prior_running_{};
    bool prior_selected_{};
    bool prior_locked_{};
    bool blocked_{};
    std::array<std::uint8_t, 8> prior_identity_{};
    std::uint16_t prior_port_{};
};

} // namespace ar::iec61850::time_sync
