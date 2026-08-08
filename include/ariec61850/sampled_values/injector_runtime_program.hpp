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

enum InjectorSignalField : std::uint16_t {
    injector_field_enabled = 1U << 0U,
    injector_field_rms = 1U << 1U,
    injector_field_dc = 1U << 2U,
    injector_field_phase = 1U << 3U,
    injector_field_frequency = 1U << 4U,
    injector_field_harmonic = 1U << 5U,
    injector_field_harmonic_order = 1U << 6U,
    injector_field_clip = 1U << 7U,
    injector_field_quality = 1U << 8U,
};

struct InjectorChannelEdit final {
    std::uint8_t channel_index{};
    std::uint16_t fields{};
    bool enabled{true};
    std::int32_t rms_counts{};
    std::int32_t dc_counts{};
    std::int32_t phase_millidegrees{};
    std::uint32_t frequency_millihz{50'000U};
    std::uint16_t harmonic_permyriad{};
    std::uint8_t harmonic_order{2U};
    std::uint16_t clip_permyriad{};
    std::uint32_t quality{};
    std::uint32_t duration_samples{};
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

    [[nodiscard]] static bool apply_edit(
        ChannelProfiles& profiles,
        const InjectorChannelEdit& edit) noexcept {
        if (edit.channel_index >= injector_channel_count || edit.fields == 0U) {
            return false;
        }

        auto& channel = profiles[edit.channel_index];
        if ((edit.fields & injector_field_enabled) != 0U) {
            channel.enabled = edit.enabled;
        }
        if ((edit.fields & injector_field_rms) != 0U) {
            channel.rms_counts = edit.rms_counts;
        }
        if ((edit.fields & injector_field_dc) != 0U) {
            channel.dc_counts = edit.dc_counts;
        }
        if ((edit.fields & injector_field_phase) != 0U) {
            channel.phase_millidegrees = edit.phase_millidegrees;
        }
        if ((edit.fields & injector_field_frequency) != 0U) {
            channel.frequency_millihz = edit.frequency_millihz;
        }
        if ((edit.fields & injector_field_harmonic) != 0U) {
            channel.harmonic_permyriad = edit.harmonic_permyriad;
        }
        if ((edit.fields & injector_field_harmonic_order) != 0U) {
            channel.harmonic_order = edit.harmonic_order;
        }
        if ((edit.fields & injector_field_clip) != 0U) {
            channel.clip_permyriad = edit.clip_permyriad;
        }
        if ((edit.fields & injector_field_quality) != 0U) {
            channel.quality = edit.quality;
        }
        return true;
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

    [[nodiscard]] bool stage_manual_edit(
        const std::size_t current_segment,
        const InjectorChannelEdit& edit) noexcept {
        if (current_segment >= segments_.size()) {
            return false;
        }
        auto target = profile_at(current_segment);
        if (!apply_edit(target, edit)) {
            return false;
        }
        return stage_manual(current_segment, target);
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

    [[nodiscard]] bool stage_ramp_edit(
        const std::size_t current_segment,
        const InjectorChannelEdit& edit) noexcept {
        if (current_segment >= segments_.size() || edit.duration_samples < 2U) {
            return false;
        }
        auto target = profile_at(current_segment);
        if (!apply_edit(target, edit)) {
            return false;
        }
        return stage_ramp(current_segment, target, edit.duration_samples);
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
