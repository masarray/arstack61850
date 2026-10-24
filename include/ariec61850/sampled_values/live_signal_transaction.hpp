// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/sampled_values/live_signal_state.hpp"

#include <cstddef>
#include <cstdint>

namespace ar::iec61850::sampled_values {

// Portable, fixed-cost canonical hash of the *wire-encoded* eight-channel state.
// No padding, host endianness, GUI floating point or signal-bank generation enters it.
[[nodiscard]] inline std::uint64_t sv_scenario_hash(const SvLiveSignalState& state) noexcept {
    std::uint64_t value = UINT64_C(14695981039346656037);
    const auto word = [&value](const std::uint32_t item) noexcept {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            value ^= (item >> shift) & 0xffU;
            value *= UINT64_C(1099511628211);
        }
    };
    word(state.frequency_millihz);
    for (const auto& c : state.channels) {
        word(static_cast<std::uint32_t>(c.rms_counts));
        word(static_cast<std::uint32_t>(c.phase_millidegrees));
        word(c.quality);
        word(c.enabled ? 1U : 0U);
    }
    word(state.current_shape.enabled ? 1U : 0U);
    word(static_cast<std::uint32_t>(state.current_shape.dc_offset_permille));
    word(state.current_shape.harmonic_permille);
    word(state.current_shape.harmonic_order);
    word(state.current_shape.clip_permille);
    return value;
}

// Single control-task writer, no allocation, no ISR involvement. Every input
// must be staged; a lost BEGIN or one rejected channel cannot publish a partial
// scenario. Committed state is published through the existing triple-bank SSOT.
class SvScenarioTransaction final {
public:
    static constexpr std::uint32_t kCompleteMask = (1U << 18U) - 1U;
    static constexpr std::uint32_t kTimeoutMs = 8000U;
    enum class Result : std::uint8_t {
        accepted, inactive, wrong_token, expired, incomplete, invalid, checksum, publish_failed, stale_generation
    };

    [[nodiscard]] bool begin(const std::uint64_t token, const SvLiveSignalState& current,
                             const std::uint32_t now, const bool running = false) noexcept {
        reset();
        if (token == 0U) return false;
        token_ = token;
        staged_ = current;
        baseline_generation_ = current.generation;
        began_running_ = running;
        began_ms_ = now;
        staging_ = true;
        return true;
    }

    [[nodiscard]] SvLiveSignalState& draft() noexcept { return staged_; }
    [[nodiscard]] bool staging() const noexcept { return staging_; }
    [[nodiscard]] bool began_running() const noexcept { return began_running_; }
    [[nodiscard]] std::uint64_t token() const noexcept { return token_; }
    [[nodiscard]] std::uint64_t checksum() const noexcept { return checksum_; }
    [[nodiscard]] std::uint64_t applied_generation() const noexcept { return generation_; }

    void mark_frequency() noexcept { mask_ |= 1U; }
    void mark_shape() noexcept { mask_ |= 2U; }
    void mark_set(const std::size_t index) noexcept {
        if (index < 8U) mask_ |= 1U << (index + 2U);
        else rejected_ = true;
    }
    void mark_enabled(const std::size_t index) noexcept {
        if (index < 8U) mask_ |= 1U << (index + 10U);
        else rejected_ = true;
    }
    void reject() noexcept { rejected_ = true; }

    [[nodiscard]] Result commit(SvLiveSignalBank& bank, const std::uint64_t token,
                                const std::uint64_t expected,
                                const std::uint32_t now) noexcept {
        if (!staging_) return Result::inactive;
        if (token != token_) return Result::wrong_token;
        if (elapsed(now)) { reset(); return Result::expired; }
        if (rejected_ || !SvLiveSignalBank::validate(staged_)) {
            reset(); return Result::invalid;
        }
        if (mask_ != kCompleteMask) return Result::incomplete;
        const auto actual = sv_scenario_hash(staged_);
        if (expected != actual) { reset(); return Result::checksum; }
        // Refuse to overwrite an unrelated live SET/ENABLE/QUALITY/FREQ update
        // while a complete scenario was staged. The single UART control task
        // serializes all bank writes, so this check is valid before publish().
        if (bank.snapshot().generation != baseline_generation_) {
            reset(); return Result::stale_generation;
        }
        if (!bank.publish(staged_)) { reset(); return Result::publish_failed; }
        staging_ = false;
        committed_ = true;
        checksum_ = actual;
        generation_ = bank.snapshot().generation;
        return Result::accepted;
    }

    [[nodiscard]] bool readback_matches(const SvLiveSignalBank& bank,
                                        const std::uint64_t token,
                                        const std::uint32_t now) const noexcept {
        if (!committed_ || token_ != token || elapsed(now)) return false;
        const auto observed = bank.snapshot();
        return observed.generation == generation_ &&
               sv_scenario_hash(observed) == checksum_;
    }

    [[nodiscard]] bool confirm_readback(const SvLiveSignalBank& bank,
                                        const std::uint64_t token,
                                        const std::uint32_t now) noexcept {
        readback_seen_ = readback_matches(bank, token, now);
        return readback_seen_;
    }

    [[nodiscard]] bool can_start(const SvLiveSignalBank& bank,
                                 const std::uint64_t token,
                                 const std::uint64_t hash,
                                 const std::uint32_t now) const noexcept {
        return !began_running_ && readback_seen_ && checksum_ == hash &&
            readback_matches(bank, token, now);
    }

    void consume_start() noexcept { reset(); }
    void reset() noexcept {
        staging_ = false; committed_ = false; rejected_ = false; readback_seen_ = false;
        began_running_ = false;
        mask_ = 0; token_ = 0; checksum_ = 0; generation_ = 0; began_ms_ = 0;
        baseline_generation_ = 0;
    }

private:
    [[nodiscard]] bool elapsed(const std::uint32_t now) const noexcept {
        return static_cast<std::uint32_t>(now - began_ms_) > kTimeoutMs;
    }

    SvLiveSignalState staged_{};
    std::uint64_t token_{0};
    std::uint64_t checksum_{0};
    std::uint64_t generation_{0};
    std::uint64_t baseline_generation_{0};
    std::uint32_t began_ms_{0};
    std::uint32_t mask_{0};
    bool staging_{false};
    bool began_running_{false};
    bool committed_{false};
    bool rejected_{false};
    bool readback_seen_{false};
};
} // namespace ar::iec61850::sampled_values
