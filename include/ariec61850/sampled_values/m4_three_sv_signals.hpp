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

private:
    std::array<SvLiveSignalBank, capacity> banks_{};
};

static_assert(M4ThreeSvSignalBanks::capacity == 3U);

} // namespace ar::iec61850::sampled_values
