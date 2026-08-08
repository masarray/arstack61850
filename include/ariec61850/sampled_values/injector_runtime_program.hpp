// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/sampled_values/deterministic_injector.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::sampled_values {

inline constexpr std::size_t injector_runtime_program_capacity = 32U;

enum class InjectorRuntimeSourceMode : std::uint8_t {
    manual,
    ramp,
    sequence,
};

struct InjectorSequenceState final {
    std::uint32_t duration_samples{};
    InjectorSegmentTransition transition{InjectorSegmentTransition::step};
    std::array<InjectorSignalProfile, injector_channel_count> channels{};
};

// Fixed-capacity source program used for sample-boundary-safe runtime changes.
//
// The DeterministicSvInjector owns no copy of these segments; it reads this
// caller-owned ring. The publisher task is therefore the single writer and can
// stage a new manual profile, ramp, or finite sequence without resetting the
// logical sample index or smpCnt. The current hold slot is changed from an
// indefinite duration to one sample; the engine then advances naturally into
// the newly staged slots and applies its existing phase-offset transition.
class InjectorRuntimeProgram final {
public:
    using ChannelProfiles =
        std::array<InjectorSignalProfile, injector_channel_count>;

    explicit InjectorRuntimeProgram(const ChannelProfiles& initial) noexcept {
        reset(initial);
    }

    void reset(const ChannelProfiles& initial) noexcept {
        for (auto& segment : segments_) {
            segment.duration_samples = 0U;
            segment.transition = InjectorSegmentTransition::step;
            segment.channels = initial;
        }
        revision_ = 1U;
        mode_ = InjectorRuntimeSourceMode::manual;
    }

    [[nodiscard]] std::span<const InjectorScenarioSegment> segments() const noexcept {
        return segments_;
    }

    [[nodiscard]] std::uint32_t revision() const noexcept { return revision_; }
    [[nodiscard]] InjectorRuntimeSourceMode mode() const noexcept { return mode_; }

    [[nodiscard]] const ChannelProfiles& profile_at(
        const std::size_t segment_index) const noexcept {
        return segments_[segment_index % segments_.size()].channels;
    }

    [[nodiscard]] bool stage_manual(
        const std::size_t current_segment,
        const ChannelProfiles& target) noexcept {
        if (current_segment >= segments_.size()) {
            return false;
        }

        const auto target_index = next_index(current_segment);
        prepare_hold(target_index, target);
        release_current(current_segment);
        bump_revision();
        mode_ = InjectorRuntimeSourceMode::manual;
        return true;
    }

    [[nodiscard]] bool stage_ramp(
        const std::size_t current_segment,
        const ChannelProfiles& target,
        const std::uint32_t duration_samples) noexcept {
        if (current_segment >= segments_.size() || duration_samples < 2U) {
            return false;
        }

        const auto ramp_index = next_index(current_segment);
        const auto hold_index = next_index(ramp_index);

        segments_[ramp_index].duration_samples = duration_samples;
        segments_[ramp_index].transition = InjectorSegmentTransition::linear_from_previous;
        segments_[ramp_index].channels = target;
        prepare_hold(hold_index, target);
        release_current(current_segment);
        bump_revision();
        mode_ = InjectorRuntimeSourceMode::ramp;
        return true;
    }

    [[nodiscard]] bool stage_sequence(
        const std::size_t current_segment,
        const std::span<const InjectorSequenceState> states) noexcept {
        if (current_segment >= segments_.size() || states.empty() ||
            states.size() + 1U >= segments_.size()) {
            return false;
        }

        auto index = next_index(current_segment);
        for (const auto& state : states) {
            if (state.duration_samples == 0U) {
                return false;
            }
            segments_[index].duration_samples = state.duration_samples;
            segments_[index].transition = state.transition;
            segments_[index].channels = state.channels;
            index = next_index(index);
        }

        // A finite sequence always ends in an indefinite hold of its final
        // state. Repeating/triggered programs are intentionally a separate
        // orchestration concern so a malformed edit can never create an
        // accidental endless test.
        prepare_hold(index, states.back().channels);
        release_current(current_segment);
        bump_revision();
        mode_ = InjectorRuntimeSourceMode::sequence;
        return true;
    }

private:
    [[nodiscard]] std::size_t next_index(const std::size_t index) const noexcept {
        return index + 1U == segments_.size() ? 0U : index + 1U;
    }

    void release_current(const std::size_t current_segment) noexcept {
        // duration=0 may have accumulated an arbitrarily large
        // sample_in_segment in the injector. Setting it to one guarantees the
        // very next completed sample advances to the newly staged slot.
        segments_[current_segment].duration_samples = 1U;
    }

    void prepare_hold(
        const std::size_t index,
        const ChannelProfiles& channels) noexcept {
        segments_[index].duration_samples = 0U;
        segments_[index].transition = InjectorSegmentTransition::step;
        segments_[index].channels = channels;
    }

    void bump_revision() noexcept {
        ++revision_;
        if (revision_ == 0U) {
            ++revision_;
        }
    }

    std::array<InjectorScenarioSegment, injector_runtime_program_capacity> segments_{};
    std::uint32_t revision_{};
    InjectorRuntimeSourceMode mode_{InjectorRuntimeSourceMode::manual};
};

} // namespace ar::iec61850::sampled_values
