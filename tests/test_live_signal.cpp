// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/sampled_values/live_signal_state.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <thread>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

#define CHECK(condition) do { \
    if (!(condition)) { \
        throw std::runtime_error(std::string{"CHECK failed: "} + #condition + \
                                 " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
    } \
} while (false)

// Preserve PR #248: publisher and SHOW/diagnostics are separate readers.
// Every visible generation must be internally coherent while one writer
// publishes full 4I+4V snapshots. This also exercises the fourth bank.
void concurrent_publisher_and_diagnostics_snapshots_are_coherent() {
    using namespace ar::iec61850::sampled_values;
    SvLiveSignalBank bank;
    std::atomic<bool> done{false};
    std::atomic<bool> inconsistent{false};
    std::atomic<std::uint32_t> readers_ready{0U};
    std::atomic<std::uint32_t> reads{0U};
    const auto reader = [&] {
        readers_ready.fetch_add(1U);
        do {
            const auto s = bank.snapshot();
            if (s.generation != 0U) {
                const auto number = static_cast<std::int32_t>(s.generation);
                if (s.channels[0].rms_counts != number ||
                    s.channels[1].phase_millidegrees != number ||
                    s.channels[4].rms_counts != number * 2 ||
                    s.channels[7].quality != static_cast<std::uint32_t>(number)) {
                    inconsistent.store(true);
                }
            }
            reads.fetch_add(1U);
        } while (!done.load());
    };
    std::thread publisher(reader);
    std::thread diagnostics(reader);
    while (readers_ready.load() != 2U) std::this_thread::yield();
    std::uint32_t committed = 0U;
    for (std::uint32_t i = 0U; i < 20000U; ++i) {
        auto s = bank.snapshot();
        const auto next = static_cast<std::int32_t>(s.generation + 1U);
        s.channels[0].rms_counts = next;
        s.channels[1].phase_millidegrees = next;
        s.channels[4].rms_counts = next * 2;
        s.channels[7].quality = static_cast<std::uint32_t>(next);
        if (bank.publish(s)) ++committed;
        if ((i % 128U) == 0U) std::this_thread::yield();
    }
    done.store(true);
    publisher.join();
    diagnostics.join();
    CHECK(committed > 100U);
    CHECK(reads.load() >= 2U);
    CHECK(!inconsistent.load());
}

void bank_commits_complete_generations() {
    using namespace ar::iec61850::sampled_values;

    SvLiveSignalBank bank;
    const auto before = bank.snapshot();
    CHECK(before.generation == 0U);
    CHECK(before.channels[0].rms_counts == 1000);
    CHECK(before.channels[1].phase_millidegrees == -120000);

    auto next = before;
    next.frequency_millihz = 60000U;
    next.channels[0].rms_counts = 2500;
    next.channels[4].rms_counts = 12000;
    next.channels[4].phase_millidegrees = 15000;

    CHECK(bank.publish(next));
    const auto after = bank.snapshot();
    CHECK(after.generation == 1U);
    CHECK(after.frequency_millihz == 60000U);
    CHECK(after.channels[0].rms_counts == 2500);
    CHECK(after.channels[4].rms_counts == 12000);
    CHECK(after.channels[4].phase_millidegrees == 15000);

    auto dc = after;
    dc.frequency_millihz = 0U;
    CHECK(bank.publish(dc));
    CHECK(bank.snapshot().frequency_millihz == 0U);

    auto invalid = dc;
    invalid.frequency_millihz = 500U;
    CHECK(!bank.publish(invalid));
    CHECK(bank.snapshot().frequency_millihz == 0U);
}

void dc_engine_outputs_constant_engineering_values() {
    using namespace ar::iec61850::sampled_values;

    SvFixedPointSineEngine engine;
    auto state = SvLiveSignalBank::default_state();
    state.frequency_millihz = 0U;
    state.channels[0].rms_counts = 1234;
    state.channels[0].phase_millidegrees = -90000;
    state.channels[1].rms_counts = -4321;

    const auto phase_before = engine.phase_accumulator();
    for (int sample = 0; sample < 12; ++sample) {
        const auto row = engine.next(state, 4000U);
        CHECK(row[0] == 1234);
        CHECK(row[1] == -4321);
    }
    CHECK(engine.phase_accumulator() == phase_before);
}

void ct_saturation_shape_is_bounded_and_asymmetric() {
    using namespace ar::iec61850::sampled_values;

    SvFixedPointSineEngine engine;
    auto state = SvLiveSignalBank::default_state();
    state.current_shape.enabled = true;
    state.current_shape.dc_offset_permille = 300;
    state.current_shape.harmonic_permille = 280U;
    state.current_shape.harmonic_order = 2U;
    state.current_shape.clip_permille = 600U;

    std::int32_t maximum = std::numeric_limits<std::int32_t>::min();
    std::int32_t minimum = std::numeric_limits<std::int32_t>::max();
    std::int64_t sum{};
    for (int sample = 0; sample < 80; ++sample) {
        const auto row = engine.next(state, 4000U);
        maximum = std::max(maximum, row[0]);
        minimum = std::min(minimum, row[0]);
        sum += row[0];
    }
    CHECK(maximum <= 850);
    CHECK(minimum >= -850);
    CHECK(maximum > 700);
    CHECK(sum > 0);
}

void fixed_point_engine_preserves_frequency_and_phase() {
    using namespace ar::iec61850::sampled_values;

    SvFixedPointSineEngine engine;
    auto state = SvLiveSignalBank::default_state();
    state.frequency_millihz = 50000U;

    const auto step = SvFixedPointSineEngine::phase_step_for(50000U, 4000U);
    const auto accumulated = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(step) * 80ULL);
    CHECK(accumulated < 80U || accumulated > 0xFFFFFFB0U);

    std::int32_t max_a{};
    std::int32_t min_a{};
    for (std::size_t sample = 0U; sample < 80U; ++sample) {
        const auto row = engine.next(state, 4000U);
        max_a = std::max(max_a, row[0]);
        min_a = std::min(min_a, row[0]);
    }
    CHECK(max_a > 1300);
    CHECK(max_a < 1500);
    CHECK(min_a < -1300);
    CHECK(min_a > -1500);

    const auto phase_before = engine.phase_accumulator();
    state.channels[0].rms_counts = 2000;
    static_cast<void>(engine.next(state, 4000U));
    CHECK(engine.phase_accumulator() != phase_before);

    const auto phase_before_skip = engine.phase_accumulator();
    engine.advance(state, 4000U, 3U);
    CHECK(engine.phase_accumulator() != phase_before_skip);

    CHECK(SvFixedPointSineEngine::phase_from_millidegrees(0) == 0U);
    const auto phase_120 = SvFixedPointSineEngine::phase_from_millidegrees(120000);
    const auto phase_minus_120 = SvFixedPointSineEngine::phase_from_millidegrees(-120000);
    CHECK(phase_120 != 0U);
    CHECK(phase_minus_120 != 0U);
    CHECK(phase_120 != phase_minus_120);
}

// Independent waveform regression for the operator's non-balanced 4I+4V
// table. The firmware MUST NOT reset current phases to the voltage defaults.
// A full 80-sample/50Hz fundamental projection checks each channel's RMS
// and signed phase in the actual fixed-point generator, not a GUI mock.
void custom_four_current_four_voltage_table_reaches_sine_samples() {
    using namespace ar::iec61850::sampled_values;

    SvFixedPointSineEngine engine;
    auto state = SvLiveSignalBank::default_state();
    state.frequency_millihz = 50000U;
    const std::array<std::int32_t, 8> rms_counts{
        1000, 800, 1200, 0, 5774, 5774, 5774, 0};
    const std::array<std::int32_t, 8> phases_mdeg{
        -30000, -145000, 87000, 0, 0, -120000, 120000, 0};
    for (std::size_t ch = 0; ch < state.channels.size(); ++ch) {
        state.channels[ch].rms_counts = rms_counts[ch];
        state.channels[ch].phase_millidegrees = phases_mdeg[ch];
        state.channels[ch].enabled = true;
    }

    constexpr double pi = 3.14159265358979323846;
    constexpr int points = 80;
    std::array<double, 8> sin_projection{};
    std::array<double, 8> cos_projection{};
    for (int n = 0; n < points; ++n) {
        const auto values = engine.next(state, 4000U);
        const double angle = (2.0 * pi * n) / points;
        for (std::size_t ch = 0; ch < values.size(); ++ch) {
            sin_projection[ch] += values[ch] * std::sin(angle);
            cos_projection[ch] += values[ch] * std::cos(angle);
        }
    }
    for (std::size_t ch = 0; ch < rms_counts.size(); ++ch) {
        const double estimated_rms = std::hypot(sin_projection[ch], cos_projection[ch])
            * std::sqrt(2.0) / points;
        if (rms_counts[ch] == 0) {
            CHECK(estimated_rms < 1.0);
            continue;
        }
        const double estimated_phase_deg = std::atan2(
            cos_projection[ch], sin_projection[ch]) * 180.0 / pi;
        const double phase_error_deg = std::remainder(
            estimated_phase_deg - phases_mdeg[ch] / 1000.0, 360.0);
        CHECK(std::abs(phase_error_deg) < 1.0);
        CHECK(std::abs(estimated_rms - rms_counts[ch]) <
              std::max(4.0, rms_counts[ch] * 0.012));
    }
}

void independent_channels_change_without_stream_restart() {
    using namespace ar::iec61850::sampled_values;

    SvLiveSignalBank bank;
    SvFixedPointSineEngine engine;

    const auto first_generation = bank.snapshot().generation;
    for (std::size_t sample = 0U; sample < 17U; ++sample) {
        const auto active = bank.snapshot();
        static_cast<void>(engine.next(active, 4000U));
    }
    const auto phase_before_update = engine.phase_accumulator();

    auto updated = bank.snapshot();
    updated.channels[0].rms_counts = 3500;
    updated.channels[0].phase_millidegrees = 30000;
    updated.channels[6].rms_counts = 9000;
    CHECK(bank.publish(updated));

    const auto active = bank.snapshot();
    CHECK(active.generation == first_generation + 1U);
    const auto row = engine.next(active, 4000U);
    CHECK(engine.phase_accumulator() != phase_before_update);
    CHECK(row[0] != 0);
    CHECK(row[6] != 0);
}

} // namespace

int main() {
    try {
        bank_commits_complete_generations();
        concurrent_publisher_and_diagnostics_snapshots_are_coherent();
        fixed_point_engine_preserves_frequency_and_phase();
        dc_engine_outputs_constant_engineering_values();
        ct_saturation_shape_is_bounded_and_asymmetric();
        independent_channels_change_without_stream_restart();
        custom_four_current_four_voltage_table_reaches_sine_samples();
        std::cout << "[PASS] live Sampled Values signal state\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] live Sampled Values signal state: " << error.what() << '\n';
        return 1;
    }
}
