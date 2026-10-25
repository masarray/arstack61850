// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/sampled_values/m4_three_sv_roster.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ar::iec61850::sampled_values {

// Deterministic shared-clock tick planning: this is NOT the PTP servo and
// NEVER independently fabricates lock. Firmware supplies the actual qualified
// lease and sample counter from its one GPTimer/PTP authority.
struct M4GridTick final {
    std::uint64_t ptp_generation{};
    std::uint64_t tick_sequence{};    // absolute 4000Hz grid slot, never TX attempts
    std::uint8_t smp_synch{};          // caller's common PTP decision: 0,1,2
    bool timing_qualified{};
};

struct M4GridFrame final {
    std::uint8_t slot{};             // 0..2 maps to a distinct SCD MU
    std::uint16_t smp_cnt{};         // one common epoch for every enabled MU
    std::uint8_t smp_synch{};        // same PTP decision, never per-MU override
};

struct M4GridBatch final {
    std::array<M4GridFrame, m4_three_sv_capacity> frames{};
    std::uint8_t count{};
    std::uint64_t tick_sequence{};
    std::uint64_t ptp_generation{};
    std::uint64_t missed_ticks{};
    bool timing_discontinuity{};    // stale PTP lease must not emit stale sync
};

class M4ThreeSvGridPlanner final {
public:
    // A new common reference epoch is accepted ONLY by the existing external
    // PTP-to-GPTimer owner. No stream-specific reset of the sample clock.
    void reset(const std::uint64_t ptp_generation) noexcept {
        generation_ = ptp_generation;
        initialized_ = ptp_generation != 0U;
        observed_tick_ = false;
        last_tick_ = 0U;
    }

    void set_enabled(const std::size_t slot, const bool enabled) noexcept {
        if (slot < enabled_.size()) enabled_[slot] = enabled;
    }

    [[nodiscard]] bool enabled(const std::size_t slot) const noexcept {
        return slot < enabled_.size() && enabled_[slot];
    }

    [[nodiscard]] M4GridBatch plan(const M4GridTick tick) noexcept {
        M4GridBatch batch{};
        batch.tick_sequence = tick.tick_sequence;
        batch.ptp_generation = tick.ptp_generation;

        if (!initialized_ || tick.ptp_generation == 0U ||
            tick.ptp_generation != generation_ || !tick.timing_qualified ||
            tick.smp_synch > 2U) {
            batch.timing_discontinuity = true;
            // Revocation is group-wide: callers must explicitly re-arm on
            // the newly qualified PTP epoch, never auto-promote synchrony.
            initialized_ = false;
            return batch;
        }

        if (observed_tick_ && tick.tick_sequence <= last_tick_) {
            batch.timing_discontinuity = true;
            initialized_ = false;
            return batch;
        }
        if (observed_tick_) {
            batch.missed_ticks = tick.tick_sequence - last_tick_ - 1U;
        }
        observed_tick_ = true;
        last_tick_ = tick.tick_sequence;

        const auto counter = static_cast<std::uint16_t>(
            tick.tick_sequence % m4_three_sv_rate_hz);
        for (std::size_t slot = 0U; slot < enabled_.size(); ++slot) {
            if (!enabled_[slot]) continue;
            batch.frames[batch.count++] = M4GridFrame{
                static_cast<std::uint8_t>(slot), counter, tick.smp_synch};
        }
        return batch;
    }

private:
    std::array<bool, m4_three_sv_capacity> enabled_{{true, true, true}};
    std::uint64_t generation_{};
    std::uint64_t last_tick_{};
    bool initialized_{};
    bool observed_tick_{};
};

} // namespace ar::iec61850::sampled_values
