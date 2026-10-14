// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/time_sync/sv_tx_egress_evidence.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace ar::iec61850::time_sync;

#define REQUIRE(x) do { if (!(x)) throw std::runtime_error(\
    std::string{"check failed: "} + #x + " at " + \
    std::to_string(__LINE__)); } while (false)

int main() {
    try {
        SvTxEgressEvidence evidence;
        SvTxEgressObservation v{};
        v.sample_epoch_generation = 1U;
        v.scheduled_alarm_ticks_low32 = 0xFFFFFFFEU;
        v.hw_ptp_seconds = 1700000000U;
        v.canonical_tx_success = true;
        v.no_missed_slots = true;
        v.same_sample_epoch = true;
        v.hw_ptp_nanoseconds = 125000U;
        v.submit_duration_us = 53U;
        REQUIRE(evidence.record(v) == SvTxEvidenceResult::measured);
        REQUIRE(evidence.summary().last_phase_ns == 125000);
        v.hw_ptp_nanoseconds = 999900000U;
        v.sample_epoch_generation = 2U;
        v.scheduled_alarm_ticks_low32 = 0x00000027U;
        v.submit_duration_us = 65U;
        REQUIRE(evidence.record(v) == SvTxEvidenceResult::measured);
        REQUIRE(evidence.summary().last_phase_ns == -100000);
        REQUIRE(evidence.summary().min_phase_ns == -100000);
        REQUIRE(evidence.summary().max_phase_ns == 125000);
        REQUIRE(evidence.summary().phase_sum_ns == 25000);
        REQUIRE(evidence.summary().max_submit_duration_us == 65U);
        REQUIRE(evidence.summary().last_alarm_ticks_low32 == 0x27U);

        v.hw_ptp_nanoseconds = 0U;
        v.hw_ptp_seconds = 0U;
        REQUIRE(evidence.record(v) == SvTxEvidenceResult::missing_hw_timestamp);
        v.hw_ptp_seconds = 1700000003U;
        v.hw_ptp_nanoseconds = 1000000000U;
        REQUIRE(evidence.record(v) == SvTxEvidenceResult::missing_hw_timestamp);
        v.hw_ptp_nanoseconds = 0U; // valid exact whole-second sample
        REQUIRE(evidence.record(v) == SvTxEvidenceResult::measured);
        REQUIRE(evidence.summary().last_phase_ns == 0);
        v.submit_duration_us = 900U;
        v.hw_ptp_nanoseconds = 1000U;
        v.no_missed_slots = false; // selected HW wait crossed its SV interval
        REQUIRE(evidence.record(v) == SvTxEvidenceResult::not_a_qualified_epoch);
        REQUIRE(evidence.summary().max_submit_duration_us == 900U);
        v.submit_duration_us = 65U;
        v.no_missed_slots = true;
        v.smp_cnt = 1U;
        REQUIRE(evidence.record(v) == SvTxEvidenceResult::not_a_qualified_epoch);
        v.smp_cnt = 0U;
        v.no_missed_slots = false;
        REQUIRE(evidence.record(v) == SvTxEvidenceResult::not_a_qualified_epoch);
        v.no_missed_slots = true;
        v.same_sample_epoch = false;
        REQUIRE(evidence.record(v) == SvTxEvidenceResult::not_a_qualified_epoch);
        v.same_sample_epoch = true;
        v.canonical_tx_success = false;
        REQUIRE(evidence.record(v) == SvTxEvidenceResult::transmit_failed);

        const auto stats = evidence.summary();
        REQUIRE(stats.attempted == 10U);
        REQUIRE(stats.measured == 3U);
        REQUIRE(stats.rejected_epoch == 4U);
        REQUIRE(stats.failed_transmit == 1U);
        REQUIRE(stats.missing_hw_timestamp == 2U);
        REQUIRE(stats.phase_sum_ns == 25000);
        evidence.reset();
        REQUIRE(evidence.summary().measured == 0U);
        REQUIRE(evidence.summary().attempted == 0U);
        std::cout << "SV TX MAC egress evidence gate regression passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
