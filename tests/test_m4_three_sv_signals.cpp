// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/sampled_values/m4_three_sv_signals.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
#define CHECK(condition) do { if (!(condition)) { \
    throw std::runtime_error(std::string{"M4 MU signals failed: "} + #condition + \
                             " at line " + std::to_string(__LINE__)); \
} } while (false)

using namespace ar::iec61850::sampled_values;

void verify_three_atomic_isolated_setpoint_banks() {
    M4ThreeSvSignalBanks banks;
    std::array<SvLiveSignalState, 3> initial{};
    for (std::size_t slot = 0U; slot < 3U; ++slot) {
        CHECK(banks.snapshot(slot, initial[slot]));
        CHECK(initial[slot].generation == 0U);
        CHECK(initial[slot].channels[0].rms_counts == 1000);
    }

    // The selected GUI page index is NOT the state owner: all three MU
    // states remain independently retained through arbitrary page switches.
    auto mu1 = initial[0];
    mu1.frequency_millihz = 0U;             // DC makes wire value obvious
    mu1.channels[0].rms_counts = 1234;
    CHECK(banks.publish(0U, mu1));

    auto mu2 = initial[1];
    mu2.frequency_millihz = 0U;
    mu2.channels[0].rms_counts = 5678;
    mu2.channels[4].rms_counts = 10000;
    mu2.channels[4].quality = 0x11000000U;
    CHECK(banks.publish(1U, mu2));

    auto mu3 = initial[2];
    mu3.frequency_millihz = 0U;
    mu3.channels[0].rms_counts = -321;
    mu3.channels[4].rms_counts = 15555;
    CHECK(banks.publish(2U, mu3));

    std::array<SvFixedPointSineEngine, 3> waveform_engines{};
    const std::array<std::int32_t, 3> currents{1234, 5678, -321};
    for (std::size_t page : {2U, 0U, 1U, 0U, 2U, 1U}) {
        SvLiveSignalState state{};
        CHECK(banks.snapshot(page, state));
        CHECK(state.generation == 1U);
        const auto output = waveform_engines[page].next(state, 4000U);
        CHECK(output[0] == currents[page]);
        CHECK(output[4] == state.channels[4].rms_counts);
    }

    SvLiveSignalState original_mu1{}, original_mu3{};
    CHECK(banks.snapshot(0U, original_mu1));
    CHECK(banks.snapshot(2U, original_mu3));

    SvLiveSignalState edited_mu2{};
    CHECK(banks.snapshot(1U, edited_mu2));
    edited_mu2.channels[1].phase_millidegrees = 45000;
    edited_mu2.channels[1].quality = 0x01000000U;
    CHECK(banks.publish(1U, edited_mu2));
    CHECK(banks.generation(1U) == 2U);
    CHECK(banks.generation(0U) == 1U);
    CHECK(banks.generation(2U) == 1U);

    SvLiveSignalState after_mu1{}, after_mu3{};
    CHECK(banks.snapshot(0U, after_mu1));
    CHECK(banks.snapshot(2U, after_mu3));
    CHECK(after_mu1 == original_mu1);
    CHECK(after_mu3 == original_mu3);

    // Rejected partial/invalid bank must never alter any published slot.
    auto invalid = edited_mu2;
    invalid.channels[0].phase_millidegrees = 360000001;
    CHECK(!banks.publish(1U, invalid));
    CHECK(banks.generation(1U) == 2U);
    CHECK(!banks.publish(3U, edited_mu2));
    SvLiveSignalState ignored{};
    CHECK(!banks.snapshot(3U, ignored));
    CHECK(banks.generation(3U) == 0U);

    std::cout << "PASS M4 P0D1: three independent 4I4V setpoints, "
                 "quality, phase, generation and GUI page switching\n";
}
} // namespace

int main() {
    try {
        verify_three_atomic_isolated_setpoint_banks();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
