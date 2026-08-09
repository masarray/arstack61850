// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/sampled_values/injector_runtime_program.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::sampled_values {

inline constexpr std::size_t injector_sequence_builder_capacity = 16U;

class InjectorSequenceBuilder final {
public:
    using ChannelProfiles = InjectorRuntimeProgram::ChannelProfiles;

    // Compatibility entry point for existing callers. New transactional
    // control paths should pass the runtime source revision explicitly and
    // reject a stale commit with based_on_revision().
    void begin(const ChannelProfiles& base) noexcept {
        begin(base, 1U);
    }

    void begin(
        const ChannelProfiles& base,
        const std::uint32_t source_revision) noexcept {
        base_ = base;
        working_ = base;
        source_revision_ = source_revision;
        state_count_ = 0U;
        active_ = source_revision != 0U;
        state_open_ = false;
        pending_duration_samples_ = 0U;
        pending_transition_ = InjectorSegmentTransition::step;
    }

    void abort() noexcept {
        active_ = false;
        state_open_ = false;
        state_count_ = 0U;
        source_revision_ = 0U;
        pending_duration_samples_ = 0U;
    }

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool state_open() const noexcept { return state_open_; }
    [[nodiscard]] std::size_t state_count() const noexcept { return state_count_; }
    [[nodiscard]] std::uint32_t source_revision() const noexcept {
        return source_revision_;
    }
    [[nodiscard]] bool based_on_revision(
        const std::uint32_t source_revision) const noexcept {
        return active_ && source_revision_ == source_revision;
    }

    [[nodiscard]] bool begin_state(
        const std::uint32_t duration_samples,
        const InjectorSegmentTransition transition) noexcept {
        if (!active_ || state_open_ || duration_samples == 0U ||
            state_count_ >= states_.size()) {
            return false;
        }

        working_ = state_count_ == 0U
            ? base_
            : states_[state_count_ - 1U].channels;
        pending_duration_samples_ = duration_samples;
        pending_transition_ = transition;
        state_open_ = true;
        return true;
    }

    [[nodiscard]] bool edit_channel(const InjectorChannelEdit& edit) noexcept {
        return state_open_ && InjectorRuntimeProgram::apply_edit(working_, edit);
    }

    [[nodiscard]] bool commit_state() noexcept {
        if (!active_ || !state_open_ || state_count_ >= states_.size()) {
            return false;
        }
        states_[state_count_] = {
            pending_duration_samples_,
            pending_transition_,
            working_};
        ++state_count_;
        state_open_ = false;
        pending_duration_samples_ = 0U;
        return true;
    }

    [[nodiscard]] bool ready() const noexcept {
        return active_ && !state_open_ && state_count_ > 0U;
    }

    [[nodiscard]] std::span<const InjectorSequenceState> states() const noexcept {
        return std::span<const InjectorSequenceState>{states_.data(), state_count_};
    }

private:
    ChannelProfiles base_{};
    ChannelProfiles working_{};
    std::array<InjectorSequenceState, injector_sequence_builder_capacity> states_{};
    std::size_t state_count_{};
    std::uint32_t source_revision_{};
    std::uint32_t pending_duration_samples_{};
    InjectorSegmentTransition pending_transition_{InjectorSegmentTransition::step};
    bool active_{};
    bool state_open_{};
};

} // namespace ar::iec61850::sampled_values
