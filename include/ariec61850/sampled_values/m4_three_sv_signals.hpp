// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/sampled_values/live_signal_state.hpp"
#include "ariec61850/sampled_values/m4_three_sv_roster.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ar::iec61850::sampled_values {

// Three separate 4I+4V input banks. Each bank uses the project's existing
// four-buffer reader-pinning protocol, so the 4kHz publisher sees ONE
// immutable generation while the single host/control-plane writer commits.
// No heap allocation, shared mutable waveform values or per-MU RTOS worker.
//
// Slot 0/1/2 maps to IED/MU1/MU2/MU3 in the compiled M4 SCD roster.
// This class owns SIGNALS only; clock/PTP generation is shared separately.
class M4ThreeSvSignalBanks final {
public:
    static constexpr std::size_t capacity = m4_three_sv_capacity;

    [[nodiscard]] bool publish(
        const std::size_t slot,
        const SvLiveSignalState& complete_eight_channel_state) noexcept {
        return slot < banks_.size() &&
               banks_[slot].publish(complete_eight_channel_state);
    }

    [[nodiscard]] bool snapshot(
        const std::size_t slot,
        SvLiveSignalState& output) const noexcept {
        if (slot >= banks_.size()) return false;
        output = banks_[slot].snapshot();
        return true;
    }

    [[nodiscard]] std::uint64_t generation(
        const std::size_t slot) const noexcept {
        if (slot >= banks_.size()) return 0U;
        return banks_[slot].snapshot().generation;
    }

    // Control-plane only: the one serialized UART writer may use the existing
    // SvScenarioTransaction against a specific bank. Never call this on the
    // publisher task, which only uses snapshot(slot, output).
    [[nodiscard]] SvLiveSignalBank* control_bank_for_serial_writer(
        const std::size_t slot) noexcept {
        return slot < banks_.size() ? &banks_[slot] : nullptr;
    }

private:
    std::array<SvLiveSignalBank, capacity> banks_{};
};


// One atomic three-bit control snapshot for a single shared 4-kHz tick.
// A late-rejoining MU uses the current group smpCnt; no separate clock,
// free-running counter or phase reset when its TX bit is toggled.
class M4ThreeSvStreamGate final {
public:
    static constexpr std::uint8_t all_enabled = 0x07U;

    [[nodiscard]] std::uint8_t snapshot_mask() const noexcept {
        return enabled_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool set_enabled(const std::size_t slot, const bool on) noexcept {
        if (slot >= M4ThreeSvSignalBanks::capacity) return false;
        const auto bit = static_cast<std::uint8_t>(1U << slot);
        if (on) enabled_.fetch_or(bit, std::memory_order_release);
        else enabled_.fetch_and(static_cast<std::uint8_t>(~bit),
                                std::memory_order_release);
        return true;
    }

    void reset_all() noexcept {
        enabled_.store(all_enabled, std::memory_order_release);
    }

private:
    std::atomic<std::uint8_t> enabled_{all_enabled};
};

static_assert(M4ThreeSvSignalBanks::capacity == 3U);

} // namespace ar::iec61850::sampled_values
