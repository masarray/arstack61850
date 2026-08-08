// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/sampled_values/deterministic_injector.hpp"
#include "ariec61850/sampled_values/injector_presets.hpp"
#include "ariec61850/sampled_values/injector_runtime_program.hpp"
#include "ariec61850/sampled_values/injector_sequence_builder.hpp"

#include <array>
#include <cstdint>

namespace {

using namespace ar::iec61850::sampled_values;

[[nodiscard]] InjectorRuntimeProgram::ChannelProfiles constant_profile(
    const std::int32_t ia_rms,
    const std::int32_t ia_phase_millidegrees = 90'000) noexcept {
    auto channels = make_balanced_4i4v_profile(ia_rms, 5'774, 0U);
    for (auto& channel : channels) {
        channel.frequency_millihz = 0U;
    }
    channels[0].phase_millidegrees = ia_phase_millidegrees;
    return channels;
}

[[nodiscard]] bool near(const std::int32_t value, const std::int32_t expected) noexcept {
    const auto difference = value > expected ? value - expected : expected - value;
    return difference <= 8;
}

bool hot_manual_update_preserves_timeline() noexcept {
    InjectorRuntimeProgram program(constant_profile(1'000));
    DeterministicSvInjector injector(program.segments(), 4'000U, true);
    if (!injector.valid()) {
        return false;
    }

    InjectorSample sample{};
    for (std::uint64_t index = 0U; index < 10U; ++index) {
        if (!injector.step(sample) || sample.sample_index != index ||
            !near(sample.values[0], 1'414)) {
            return false;
        }
    }

    if (!program.stage_manual(injector.segment_index(), constant_profile(2'000))) {
        return false;
    }

    // One final sample completes the active hold slot; the next sample uses the
    // staged profile without resetting the global sample index.
    if (!injector.step(sample) || sample.sample_index != 10U ||
        !near(sample.values[0], 1'414)) {
        return false;
    }
    if (!injector.step(sample) || sample.sample_index != 11U ||
        !near(sample.values[0], 2'828)) {
        return false;
    }

    // Phase changes are also boundary-applied through the engine's existing
    // inter-segment phase-offset logic.
    if (!program.stage_manual(injector.segment_index(), constant_profile(2'000, 0))) {
        return false;
    }
    if (!injector.step(sample) || sample.sample_index != 12U ||
        !near(sample.values[0], 2'828)) {
        return false;
    }
    return injector.step(sample) && sample.sample_index == 13U &&
        near(sample.values[0], 0);
}

bool ramp_is_sample_indexed_and_holds_target() noexcept {
    InjectorRuntimeProgram program(constant_profile(1'000));
    DeterministicSvInjector injector(program.segments(), 4'000U, true);
    if (!injector.valid()) {
        return false;
    }

    InjectorSample sample{};
    if (!injector.step(sample)) {
        return false;
    }
    if (!program.stage_ramp(
            injector.segment_index(), constant_profile(4'000), 3U)) {
        return false;
    }

    std::array<std::int32_t, 5U> observed{};
    for (auto& value : observed) {
        if (!injector.step(sample)) {
            return false;
        }
        value = sample.values[0];
    }

    return near(observed[0], 1'414) &&
        near(observed[1], 1'414) &&
        near(observed[2], 3'535) &&
        near(observed[3], 5'656) &&
        near(observed[4], 5'656) &&
        program.mode() == InjectorRuntimeSourceMode::ramp;
}

bool finite_sequence_ends_in_hold() noexcept {
    InjectorRuntimeProgram program(constant_profile(1'000));
    DeterministicSvInjector injector(program.segments(), 4'000U, true);
    if (!injector.valid()) {
        return false;
    }

    const std::array<InjectorSequenceState, 2U> states{
        InjectorSequenceState{2U, InjectorSegmentTransition::step, constant_profile(2'000)},
        InjectorSequenceState{2U, InjectorSegmentTransition::step, constant_profile(3'000)}};

    InjectorSample sample{};
    if (!injector.step(sample) ||
        !program.stage_sequence(injector.segment_index(), states)) {
        return false;
    }

    std::array<std::int32_t, 7U> observed{};
    for (auto& value : observed) {
        if (!injector.step(sample)) {
            return false;
        }
        value = sample.values[0];
    }

    return near(observed[0], 1'414) &&
        near(observed[1], 2'828) &&
        near(observed[2], 2'828) &&
        near(observed[3], 4'242) &&
        near(observed[4], 4'242) &&
        near(observed[5], 4'242) &&
        near(observed[6], 4'242) &&
        program.mode() == InjectorRuntimeSourceMode::sequence;
}

bool sequence_builder_is_transactional() noexcept {
    const auto base = constant_profile(1'000);
    InjectorSequenceBuilder builder;
    builder.begin(base);

    if (!builder.begin_state(2U, InjectorSegmentTransition::step)) {
        return false;
    }
    InjectorChannelEdit first{};
    first.channel_index = 0U;
    first.fields = injector_field_rms;
    first.rms_counts = 2'000;
    if (!builder.edit_channel(first) || builder.ready() ||
        !builder.commit_state()) {
        return false;
    }

    if (!builder.begin_state(3U, InjectorSegmentTransition::linear_from_previous)) {
        return false;
    }
    InjectorChannelEdit second{};
    second.channel_index = 0U;
    second.fields = injector_field_rms;
    second.rms_counts = 4'000;
    if (!builder.edit_channel(second) || !builder.commit_state() ||
        !builder.ready() || builder.state_count() != 2U) {
        return false;
    }

    const auto states = builder.states();
    if (states.size() != 2U ||
        states[0].channels[0].rms_counts != 2'000 ||
        states[1].channels[0].rms_counts != 4'000 ||
        states[1].transition != InjectorSegmentTransition::linear_from_previous) {
        return false;
    }

    InjectorRuntimeProgram program(base);
    DeterministicSvInjector injector(program.segments(), 4'000U, true);
    InjectorSample sample{};
    if (!injector.step(sample) ||
        !program.stage_sequence(injector.segment_index(), states)) {
        return false;
    }

    // An abort discards only the builder transaction and cannot affect the
    // already committed runtime program.
    builder.abort();
    return !builder.active() && !builder.ready() &&
        program.mode() == InjectorRuntimeSourceMode::sequence;
}

} // namespace

int main() {
    if (!hot_manual_update_preserves_timeline()) {
        return 1;
    }
    if (!ramp_is_sample_indexed_and_holds_target()) {
        return 2;
    }
    if (!finite_sequence_ends_in_hold()) {
        return 3;
    }
    if (!sequence_builder_is_transactional()) {
        return 4;
    }
    return 0;
}
