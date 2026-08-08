// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/sampled_values/deterministic_injector.hpp"

#include <array>
#include <cstdint>

namespace ar::iec61850::sampled_values {

// Canonical first-trial channel order shared by Windows host simulation and the
// ESP32-P4 injector application: Ia, Ib, Ic, In, Va, Vb, Vc, Vn.
[[nodiscard]] inline std::array<InjectorSignalProfile, injector_channel_count>
make_balanced_4i4v_profile(
    const std::int32_t current_rms_counts = 1'000,
    const std::int32_t voltage_rms_counts = 5'774,
    const std::uint32_t frequency_millihz = 50'000U,
    const std::uint32_t quality = 0U) noexcept {
    std::array<InjectorSignalProfile, injector_channel_count> channels{};

    channels[0] = {true, current_rms_counts, 0, 0, frequency_millihz, 0U, 2U, 0U, quality};
    channels[1] = {true, current_rms_counts, 0, -120'000, frequency_millihz, 0U, 2U, 0U, quality};
    channels[2] = {true, current_rms_counts, 0, 120'000, frequency_millihz, 0U, 2U, 0U, quality};
    channels[3] = {false, 0, 0, 0, frequency_millihz, 0U, 2U, 0U, quality};

    channels[4] = {true, voltage_rms_counts, 0, 0, frequency_millihz, 0U, 2U, 0U, quality};
    channels[5] = {true, voltage_rms_counts, 0, -120'000, frequency_millihz, 0U, 2U, 0U, quality};
    channels[6] = {true, voltage_rms_counts, 0, 120'000, frequency_millihz, 0U, 2U, 0U, quality};
    channels[7] = {false, 0, 0, 0, frequency_millihz, 0U, 2U, 0U, quality};

    return channels;
}

[[nodiscard]] inline InjectorScenarioSegment make_hold_segment(
    const std::array<InjectorSignalProfile, injector_channel_count>& channels,
    const std::uint32_t duration_samples = 0U) noexcept {
    return {
        duration_samples,
        InjectorSegmentTransition::step,
        channels};
}

} // namespace ar::iec61850::sampled_values
