// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/sampled_values/m4_three_sv_signals.hpp"
#include "ariec61850/sampled_values/live_signal_transaction.hpp"

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

void verify_three_isolated_complete_scenario_transactions() {
    M4ThreeSvSignalBanks banks;
    std::array<SvScenarioTransaction, 3> tx{};
    constexpr std::uint32_t now = 100U;
    SvLiveSignalState before[3]{};
    for (std::size_t slot = 0U; slot < 3U; ++slot) {
        CHECK(banks.snapshot(slot, before[slot]));
        CHECK(banks.control_bank_for_serial_writer(slot) != nullptr);
        CHECK(tx[slot].begin(100U + slot, before[slot], now, true));
        auto& state = tx[slot].draft();
        state.frequency_millihz = 0U;
        tx[slot].mark_frequency();
        state.current_shape = {false, 0, 0, 2, 1000};
        tx[slot].mark_shape();
        for (std::size_t channel = 0U; channel < 8U; ++channel) {
            state.channels[channel].rms_counts =
                static_cast<std::int32_t>(1000U + 500U * slot + channel);
            state.channels[channel].phase_millidegrees =
                static_cast<std::int32_t>(3000U * slot);
            state.channels[channel].quality = static_cast<std::uint32_t>(slot);
            state.channels[channel].enabled = channel != 7U;
            tx[slot].mark_set(channel);
            tx[slot].mark_enabled(channel);
        }
    }
    // Staging even three simultaneous transactions must not change any bank.
    for (std::size_t slot = 0U; slot < 3U; ++slot) {
        SvLiveSignalState current{};
        CHECK(banks.snapshot(slot, current));
        CHECK(current == before[slot]);
    }
    auto* mu2 = banks.control_bank_for_serial_writer(1U);
    CHECK(mu2 != nullptr);
    const auto mu2_hash = sv_scenario_hash(tx[1].draft());
    CHECK(tx[1].commit(*mu2, 101U, mu2_hash, now + 1U) ==
          SvScenarioTransaction::Result::accepted);
    CHECK(tx[1].confirm_readback(*mu2, 101U, now + 2U));
    CHECK(tx[1].readback_matches(*mu2, 101U, now + 2U));
    CHECK(banks.generation(1U) == 1U);
    CHECK(banks.generation(0U) == 0U && banks.generation(2U) == 0U);
    SvLiveSignalState applied{};
    CHECK(banks.snapshot(1U, applied));
    CHECK(applied.channels[0].rms_counts == 1500);
    CHECK(applied.channels[0].quality == 1U);
    CHECK(!tx[1].can_start(*mu2, 101U, mu2_hash, now + 2U));

    // A bad hash cannot partially update MU1 (nor touch the other MUs).
    auto* mu1 = banks.control_bank_for_serial_writer(0U);
    CHECK(mu1 != nullptr);
    CHECK(tx[0].commit(*mu1, 100U, sv_scenario_hash(tx[0].draft()) ^ 1U, now + 1U) ==
          SvScenarioTransaction::Result::checksum);
    CHECK(banks.generation(0U) == 0U && banks.generation(1U) == 1U);

    // A stale MU3 draft must not overwrite a subsequent independent edit.
    auto* mu3 = banks.control_bank_for_serial_writer(2U);
    CHECK(mu3 != nullptr);
    auto edited = mu3->snapshot();
    edited.channels[1].quality = 0xABCDU;
    CHECK(mu3->publish(edited));
    CHECK(tx[2].commit(*mu3, 102U, sv_scenario_hash(tx[2].draft()), now + 1U) ==
          SvScenarioTransaction::Result::stale_generation);
    CHECK(banks.generation(2U) == 1U);
    CHECK(banks.generation(1U) == 1U);

    // A fresh full MU1 transaction is committed while the common grid can
    // keep publishing MU2 and MU3 without resetting either bank.
    CHECK(tx[0].begin(200U, mu1->snapshot(), now + 3U, true));
    auto& fresh = tx[0].draft();
    fresh.frequency_millihz = 60000U;
    tx[0].mark_frequency();
    tx[0].mark_shape();
    for (std::size_t ch = 0U; ch < 8U; ++ch) {
        fresh.channels[ch].quality = static_cast<std::uint32_t>(ch + 10U);
        tx[0].mark_set(ch);
        tx[0].mark_enabled(ch);
    }
    const auto fresh_hash = sv_scenario_hash(fresh);
    CHECK(tx[0].commit(*mu1, 200U, fresh_hash, now + 4U) ==
          SvScenarioTransaction::Result::accepted);
    CHECK(tx[0].confirm_readback(*mu1, 200U, now + 5U));
    CHECK(banks.generation(0U) == 1U);
    CHECK(banks.generation(1U) == 1U && banks.generation(2U) == 1U);
    CHECK(banks.control_bank_for_serial_writer(3U) == nullptr);
    std::cout << "PASS M4 P0D3: isolated transaction, checksum, stale generation, readback\n";
}


void verify_independent_stream_tx_gate() {
    M4ThreeSvStreamGate gate{};
    CHECK(gate.snapshot_mask() == 0x07U);
    CHECK(gate.set_enabled(1U, false));
    CHECK(gate.snapshot_mask() == 0x05U);
    CHECK(gate.set_enabled(2U, false));
    CHECK(gate.snapshot_mask() == 0x01U);
    CHECK(gate.set_enabled(1U, true));
    CHECK(gate.snapshot_mask() == 0x03U);
    CHECK(!gate.set_enabled(3U, true));
    CHECK(gate.snapshot_mask() == 0x03U);

    // Independent electrical phase accumulation continues while packets
    // for one MU are suppressed; shared smpCnt is NEVER restarted.
    std::array<SvFixedPointSineEngine, 3> engines{};
    M4ThreeSvSignalBanks banks{};
    std::array<std::uint32_t, 3> sent{};
    std::array<std::uint16_t, 3> last_sample{};
    for (std::uint32_t tick = 0U; tick < 4050U; ++tick) {
        if (tick == 10U) CHECK(gate.set_enabled(1U, false));
        if (tick == 4020U) CHECK(gate.set_enabled(1U, true));
        const auto mask = gate.snapshot_mask();
        const auto shared_cnt = static_cast<std::uint16_t>(tick % 4000U);
        for (std::size_t slot = 0U; slot < 3U; ++slot) {
            SvLiveSignalState state{};
            CHECK(banks.snapshot(slot, state));
            static_cast<void>(engines[slot].next(state, 4000U));
            if ((mask & (1U << slot)) == 0U) continue;
            last_sample[slot] = shared_cnt;
            ++sent[slot];
        }
    }
    CHECK(sent[0] == 4050U && sent[2] == 4050U);
    CHECK(sent[1] == 40U);  // ticks 0-9 + ticks 4020-4049
    CHECK(last_sample[0] == 49U && last_sample[1] == 49U &&
          last_sample[2] == 49U);
    CHECK(engines[0].phase_accumulator() == engines[1].phase_accumulator());
    CHECK(engines[1].phase_accumulator() == engines[2].phase_accumulator());
    gate.reset_all();
    CHECK(gate.snapshot_mask() == 0x07U);
    std::cout << "PASS M4 P0D3: per-MU TX gates rejoin shared sampling grid\n";
}

} // namespace

int main() {
    try {
        verify_three_atomic_isolated_setpoint_banks();
        verify_three_isolated_complete_scenario_transactions();
        verify_independent_stream_tx_gate();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
