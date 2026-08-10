// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/sampled_values/deterministic_injector.hpp"
#include "ariec61850/sampled_values/injector_presets.hpp"

#include <array>
#include <cstdint>

namespace {

using namespace ar::iec61850::sampled_values;

bool exact_cycle_smoke() noexcept {
    const auto channels = make_balanced_4i4v_profile(1'000, 5'774, 50'000U);
    const std::array<InjectorScenarioSegment, 1U> segments{
        make_hold_segment(channels)};
    DeterministicSvInjector injector(segments, 4'000U, false);
    if (!injector.valid()) {
        return false;
    }

    InjectorSample sample{};
    std::array<std::int32_t, 81U> values{};
    for (std::size_t index = 0U; index < values.size(); ++index) {
        if (!injector.step(sample)) {
            return false;
        }
        values[index] = sample.values[0];
    }

    return values[0] == 0 &&
        values[20] >= 1'408 && values[20] <= 1'420 &&
        values[40] >= -2 && values[40] <= 2 &&
        values[60] <= -1'408 && values[60] >= -1'420 &&
        values[80] >= -2 && values[80] <= 2;
}

bool reset_is_byte_reproducible() noexcept {
    const auto channels = make_balanced_4i4v_profile();
    const std::array<InjectorScenarioSegment, 1U> segments{
        make_hold_segment(channels)};
    DeterministicSvInjector injector(segments, 4'000U, true);
    if (!injector.valid()) {
        return false;
    }

    std::array<std::int32_t, 160U> first{};
    std::array<std::int32_t, 160U> second{};
    InjectorSample sample{};
    for (auto& value : first) {
        if (!injector.step(sample)) {
            return false;
        }
        value = sample.values[4];
    }

    injector.reset();
    for (auto& value : second) {
        if (!injector.step(sample)) {
            return false;
        }
        value = sample.values[4];
    }
    return first == second;
}

bool linear_segment_uses_integer_sample_timeline() noexcept {
    auto low = make_balanced_4i4v_profile(1'000, 1'000, 0U);
    auto high = low;
    for (auto& channel : low) {
        channel.phase_millidegrees = 90'000;
    }
    for (auto& channel : high) {
        channel.phase_millidegrees = 90'000;
    }
    high[0].rms_counts = 4'000;

    const std::array<InjectorScenarioSegment, 2U> segments{
        InjectorScenarioSegment{1U, InjectorSegmentTransition::step, low},
        InjectorScenarioSegment{3U, InjectorSegmentTransition::linear_from_previous, high}};
    DeterministicSvInjector injector(segments, 4'000U, false);
    if (!injector.valid()) {
        return false;
    }

    InjectorSample sample{};
    std::array<std::int32_t, 4U> values{};
    for (auto& value : values) {
        if (!injector.step(sample)) {
            return false;
        }
        value = sample.values[0];
    }

    return values[0] >= 1'408 && values[0] <= 1'420 &&
        values[1] >= 1'408 && values[1] <= 1'420 &&
        values[2] > values[1] &&
        values[3] > values[2] &&
        values[3] >= 5'650 && values[3] <= 5'665 &&
        injector.finished();
}

} // namespace

int main() {
    if (!exact_cycle_smoke()) {
        return 1;
    }
    if (!reset_is_byte_reproducible()) {
        return 2;
    }
    if (!linear_segment_uses_integer_sample_timeline()) {
        return 3;
    }
    return 0;
}
