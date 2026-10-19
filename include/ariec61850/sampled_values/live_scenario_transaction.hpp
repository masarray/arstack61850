// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/sampled_values/live_signal_state.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace ar::iec61850::sampled_values {

// Control-plane only. The publisher continues to read the existing
// SvLiveSignalBank; the staged scenario is NEVER a second live authority.
// One UART writer owns this fixed-size object. No allocations or locks.
class SvLiveScenarioTransaction final {
public:
    enum class CommitResult : std::uint8_t {
        applied, no_transaction, wrong_token, invalid, incomplete,
        stale_generation, hash_mismatch, publish_failed
    };

    [[nodiscard]] static std::uint64_t fingerprint(const SvLiveSignalState& state) noexcept {
        // FNV-1a over explicitly encoded little-endian fields, NOT struct
        // padding/endianness and NOT the volatile bank generation. Versioned
        // integrity fingerprint, not an authentication/security primitive.
        std::uint64_t digest = 14695981039346656037ULL;
        const auto feed = [&digest](std::uint32_t field) noexcept {
            for (int byte = 0; byte < 4; ++byte) {
                digest ^= static_cast<std::uint8_t>(field & 0xffU);
                digest *= 1099511628211ULL;
                field >>= 8U;
            }
        };
        feed(0x41525331U); // "ARS1" transaction wire schema
        feed(state.frequency_millihz);
        for (const auto& channel : state.channels) {
            feed(static_cast<std::uint32_t>(channel.rms_counts));
            feed(static_cast<std::uint32_t>(channel.phase_millidegrees));
            feed(channel.quality);
            feed(channel.enabled ? 1U : 0U);
        }
        feed(state.current_shape.enabled ? 1U : 0U);
        feed(static_cast<std::uint32_t>(state.current_shape.dc_offset_permille));
        feed(state.current_shape.harmonic_permille);
        feed(state.current_shape.harmonic_order);
        feed(state.current_shape.clip_permille);
        return digest;
    }

    [[nodiscard]] bool begin(std::uint32_t token, const SvLiveSignalBank& bank) noexcept {
        if (token == 0U || token == applied_token_) return false;
        invalidate();
        staged_ = bank.snapshot();
        baseline_generation_ = staged_.generation;
        token_ = token;
        active_ = true;
        return true;
    }

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] std::uint32_t token() const noexcept { return token_; }
    [[nodiscard]] std::uint32_t applied_token() const noexcept { return applied_token_; }
    [[nodiscard]] std::uint64_t applied_hash() const noexcept { return applied_hash_; }
    [[nodiscard]] std::uint64_t applied_generation() const noexcept { return applied_generation_; }

    void invalidate() noexcept {
        active_ = false;
        poisoned_ = false;
        token_ = 0U;
        applied_token_ = 0U;
        applied_hash_ = 0U;
        applied_generation_ = 0U;
        set_mask_ = 0U;
        enable_mask_ = 0U;
        frequency_seen_ = false;
        shape_seen_ = false;
    }

    void poison() noexcept { if (active_) poisoned_ = true; }

    [[nodiscard]] bool set_frequency(std::uint32_t millihz) noexcept {
        if (!active_) return false;
        auto next = staged_;
        next.frequency_millihz = millihz;
        if (!SvLiveSignalBank::validate(next)) return reject();
        staged_ = next;
        frequency_seen_ = true;
        return true;
    }

    [[nodiscard]] bool set_channel(std::size_t index, std::int32_t rms,
                                   std::int32_t phase, std::uint32_t quality) noexcept {
        if (!active_) return false;
        if (index >= kLiveSignalChannelCount) return reject();
        auto next = staged_;
        next.channels[index].rms_counts = rms;
        next.channels[index].phase_millidegrees = phase;
        next.channels[index].quality = quality;
        if (!SvLiveSignalBank::validate(next)) return reject();
        staged_ = next;
        set_mask_ |= static_cast<std::uint8_t>(1U << index);
        return true;
    }

    [[nodiscard]] bool set_enabled(std::size_t index, bool enabled) noexcept {
        if (!active_) return false;
        if (index >= kLiveSignalChannelCount) return reject();
        staged_.channels[index].enabled = enabled;
        enable_mask_ |= static_cast<std::uint8_t>(1U << index);
        return true;
    }

    [[nodiscard]] bool set_shape(const SvCurrentWaveformShape& shape) noexcept {
        if (!active_) return false;
        auto next = staged_;
        next.current_shape = shape;
        if (!SvLiveSignalBank::validate(next)) return reject();
        staged_ = next;
        shape_seen_ = true;
        return true;
    }

    [[nodiscard]] CommitResult commit(std::uint32_t token, std::uint64_t expected_hash,
                                      SvLiveSignalBank& bank) noexcept {
        if (!active_) return CommitResult::no_transaction;
        if (token != token_) return CommitResult::wrong_token;
        if (poisoned_) return CommitResult::invalid;
        if (!frequency_seen_ || !shape_seen_ ||
            set_mask_ != 0xffU || enable_mask_ != 0xffU) {
            return CommitResult::incomplete;
        }
        if (!SvLiveSignalBank::validate(staged_) ||
            (staged_.frequency_millihz == 0U && staged_.current_shape.enabled)) {
            return CommitResult::invalid;
        }
        if (bank.snapshot().generation != baseline_generation_) {
            return CommitResult::stale_generation;
        }
        if (fingerprint(staged_) != expected_hash) return CommitResult::hash_mismatch;
        if (!bank.publish(staged_)) return CommitResult::publish_failed;
        const auto applied = bank.snapshot();
        if (fingerprint(applied) != expected_hash) return CommitResult::publish_failed;
        applied_generation_ = applied.generation;
        applied_hash_ = expected_hash;
        applied_token_ = token_;
        active_ = false;
        return CommitResult::applied;
    }

    [[nodiscard]] bool matches_applied(std::uint32_t token, std::uint64_t hash,
                                       const SvLiveSignalBank& bank) const noexcept {
        if (active_ || token == 0U || token != applied_token_ || hash != applied_hash_)
            return false;
        const auto observed = bank.snapshot();
        return observed.generation == applied_generation_ &&
            fingerprint(observed) == applied_hash_;
    }

    [[nodiscard]] const SvLiveSignalState& staged() const noexcept { return staged_; }

private:
    [[nodiscard]] bool reject() noexcept {
        poisoned_ = true;
        return false;
    }

    SvLiveSignalState staged_{};
    std::uint64_t baseline_generation_{};
    std::uint64_t applied_generation_{};
    std::uint64_t applied_hash_{};
    std::uint32_t token_{};
    std::uint32_t applied_token_{};
    std::uint8_t set_mask_{};
    std::uint8_t enable_mask_{};
    bool frequency_seen_{};
    bool shape_seen_{};
    bool poisoned_{};
    bool active_{};
};

} // namespace ar::iec61850::sampled_values
