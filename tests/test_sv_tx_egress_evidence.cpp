// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/time_sync/sv_tx_egress_evidence.hpp"

#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace ar::iec61850::time_sync;

#define REQUIRE(x) do { if (!(x)) throw std::runtime_error(\
    std::string{"check failed: "} + #x + " at " + \
    std::to_string(__LINE__)); } while (false)

namespace {
SvTxEgressObservation make_good(
    const std::uint32_t epoch, const std::uint32_t seconds,
    const std::uint32_t nanoseconds = 100000U) {
    SvTxEgressObservation o{};
    o.smp_cnt = 0U;
    o.sample_epoch_generation = epoch;
    o.publisher_session_generation = 7U;
    o.publisher_fault_generation = 0U;
    o.ptp_source_generation = 3U;
    o.scheduled_alarm_ticks_low32 = epoch * 1000000U;
    o.hw_ptp_seconds = seconds;
    o.hw_ptp_nanoseconds = nanoseconds;
    o.submit_duration_us = 65U;
    o.canonical_tx_success = true;
    o.no_missed_slots = true;
    o.same_sample_epoch = true;
    return o;
}
void check_auto_local_sync_gate_requires_both_clocks_and_fresh_mac() {
    SvLocalSyncEvidenceGate gate;
    auto o = make_good(4U, 1700000016U, 2000U);
    REQUIRE(!gate.qualified(7U, 0U, 3U, 4U, true, 0U));
    gate.observe(o, SvTxEvidenceResult::baseline_established, 2000);
    REQUIRE(!gate.qualified(7U, 0U, 3U, 4U, true, 0U));
    // Hardware packet TX normally follows the top-of-second sampling
    // instant by processing/queue delay. 110 us is valid publication delay,
    // and MUST NOT be mistaken for 110 us sampling phase error.
    gate.observe(o, SvTxEvidenceResult::measured, 110000);
    REQUIRE(!gate.qualified(7U, 0U, 3U, 4U, true, 0U));
    o.sample_epoch_generation = 6U;
    gate.observe(o, SvTxEvidenceResult::measured, 175000);
    REQUIRE(gate.qualified(7U, 0U, 3U, 6U, true, 4U));
    REQUIRE(gate.qualified(7U, 0U, 3U, 9U, true, 4U));
    REQUIRE(!gate.qualified(7U, 0U, 3U, 10U, true, 4U));
    REQUIRE(!gate.qualified(7U, 0U, 3U, 6U, true, 5U));
    REQUIRE(!gate.qualified(7U, 0U, 3U, 6U, false, 0U));
    REQUIRE(!gate.qualified(7U, 1U, 3U, 6U, true, 0U));
    REQUIRE(!gate.qualified(7U, 0U, 4U, 6U, true, 0U));
    REQUIRE(!gate.qualified(8U, 0U, 3U, 6U, true, 0U));
    REQUIRE(!gate.qualified(7U, 0U, 3U, 5U, true, 0U));
    // A new source, bad SV timestamp, skipped frame or TX failure
    // immediately revokes the old promotion and requires fresh pairs.
    o.sample_epoch_generation = 8U;
    gate.observe(o, SvTxEvidenceResult::measured, 300000);
    REQUIRE(!gate.qualified(7U, 0U, 3U, 8U, true, 0U));
    // A TX timestamp unexpectedly before the sample epoch is invalid,
    // even if a symmetric +/− phase comparison would have accepted it.
    gate.observe(o, SvTxEvidenceResult::measured, -9000);
    REQUIRE(!gate.qualified(7U, 0U, 3U, 8U, true, 0U));
    gate.observe(o, SvTxEvidenceResult::measured, 110000);
    o.sample_epoch_generation = 10U;
    gate.observe(o, SvTxEvidenceResult::measured, 180000);
    REQUIRE(gate.qualified(7U, 0U, 3U, 10U, true, 0U));
    gate.observe(o, SvTxEvidenceResult::transmit_failed, 1000);
    REQUIRE(!gate.qualified(7U, 0U, 3U, 10U, true, 0U));
    o.sample_epoch_generation = 12U;
    gate.observe(o, SvTxEvidenceResult::measured, 1000);
    o.sample_epoch_generation = 14U;
    o.publisher_fault_generation = 1U;
    gate.observe(o, SvTxEvidenceResult::measured, 1000);
    REQUIRE(!gate.qualified(7U, 1U, 3U, 14U, true, 0U));
    gate.reset();
    REQUIRE(!gate.qualified(7U, 1U, 3U, 14U, true, 0U));
}

// Full target-cadence path: a real descriptor timestamp is sampled only at
// the 2s, 4s and 6s canonical sample epochs; software/PTP phase remains
// independent, so MAC presence alone can never authorize AUTO local.
void check_fast_source_auto_qualification_with_fresh_evidence() {
    static_assert(kSvTxEvidenceEpochStride == 2U);
    static_assert(SvLocalSyncEvidenceGate::kEvidenceStride ==
                  kSvTxEvidenceEpochStride);
    static_assert(SvLocalSyncEvidenceGate::kMaxEvidenceAgeEpochs == 3U);
    SvTxEgressEvidence tx;
    SvLocalSyncEvidenceGate gate;
    constexpr std::uint32_t base_seconds = 1700000000U;
    for (const std::uint32_t elapsed : {2U, 4U, 6U}) {
        const auto o = make_good(elapsed, base_seconds + elapsed, 90000U);
        const auto result = tx.record(o);
        REQUIRE(result == (elapsed == 2U
            ? SvTxEvidenceResult::baseline_established
            : SvTxEvidenceResult::measured));
        gate.observe(o, result, tx.summary().last_phase_ns);
        REQUIRE(gate.measured_pairs() == (elapsed / 2U) - 1U);
        REQUIRE(gate.qualified(7U, 0U, 3U, elapsed, true, 4U) ==
                (elapsed == 6U));
        // No fake sync claim from the MAC descriptor alone.
        REQUIRE(!gate.qualified(7U, 0U, 3U, elapsed, false, 0U));
    }
    REQUIRE(tx.summary().measured == 2U);
    REQUIRE(!gate.qualified(7U, 0U, 3U, 6U, true, 5U));
    REQUIRE(!gate.qualified(7U, 0U, 3U, 10U, true, 4U));
    REQUIRE(!gate.qualified(7U, 1U, 3U, 6U, true, 4U));
    REQUIRE(!gate.qualified(7U, 0U, 4U, 6U, true, 4U));
    // A publisher fault immediately revokes qualification, regardless of
    // good-looking timestamps and the original six-second acquisition.
    auto fault = make_good(8U, base_seconds + 8U, 90000U);
    fault.publisher_fault_generation = 1U;
    const auto changed = tx.record(fault);
    REQUIRE(changed == SvTxEvidenceResult::publisher_discontinuity);
    gate.observe(fault, changed, tx.summary().last_phase_ns);
    REQUIRE(gate.measured_pairs() == 0U);
    REQUIRE(!gate.qualified(7U, 0U, 3U, 8U, true, 4U));
}

// Internal SOURCE is the same EMAC oscillator driving PTP and SMV. Reuse
// the independent software tracking gate, but skip the arbitrary 10-s
// receiver/cold-stability probation once real MAC egress has corroborated it.
// A manual FORCE_2 mode never participates in this test or AUTO admission.
void check_internal_source_auto_acquires_without_ten_second_probation() {
    SvTxEgressEvidence tx;
    SvLocalSyncEvidenceGate mac;
    SvSourceSelfClockAcquisition source;
    constexpr std::uint32_t base = 1700001200U;
    REQUIRE(!source.observe(false, false, 7U, 0U, 3U, 1U));

    // A descriptor BASELINE at 2s proves nothing; do not promote.
    auto first = make_good(2U, base + 2U, 85000U);
    auto result = tx.record(first);
    mac.observe(first, result, tx.summary().last_phase_ns);
    REQUIRE(result == SvTxEvidenceResult::baseline_established);
    REQUIRE(!mac.qualified_source_fast(7U, 0U, 3U, 2U, true, 2U));
    REQUIRE(!source.observe(true, false, 7U, 0U, 3U, 2U));

    // 4s: the FIRST coherent measured TX pair and actual software phase.
    auto second = make_good(4U, base + 4U, 85000U);
    result = tx.record(second);
    mac.observe(second, result, tx.summary().last_phase_ns);
    REQUIRE(result == SvTxEvidenceResult::measured);
    REQUIRE(mac.qualified_source_fast(7U, 0U, 3U, 4U, true, 4U));
    REQUIRE(!mac.qualified(7U, 0U, 3U, 4U, true, 4U));
    REQUIRE(!mac.qualified_source_fast(7U, 0U, 3U, 4U, false, 0U));
    // Hardware PCAP 2026-10-22: a 2-3us read-span uncertainty added to
    // a healthy -2..-3us sampled phase must NOT oscillate SOURCE sync.
    REQUIRE(mac.qualified_source_fast(7U, 0U, 3U, 4U, true, 5U));
    REQUIRE(!mac.qualified_source_fast(7U, 0U, 3U, 4U, true, 13U));
    REQUIRE(!mac.qualified(7U, 0U, 3U, 4U, true, 5U));
    REQUIRE(!source.observe(true,
        mac.qualified_source_fast(7U, 0U, 3U, 4U, true, 3U),
        7U, 0U, 3U, 4U)); // still only one qualified 1-Hz phase epoch
    REQUIRE(source.clean_epochs() == 1U);

    // 5s: a second independent continuous PTP-second/phase measurement
    // proves the SOURCE and SV timelines still agree. No 10s artificial
    // wait or GNSS/external receiver needed.
    REQUIRE(source.observe(true,
        mac.qualified_source_fast(7U, 0U, 3U, 5U, true, 3U),
        7U, 0U, 3U, 5U));
    REQUIRE(source.clean_epochs() == 2U);

    // Both fast and full routes must revoke when the clock/GM changes,
    // MAC evidence expires, a publisher fault occurs, or an epoch is skipped.
    REQUIRE(!source.observe(true,
        mac.qualified_source_fast(7U, 0U, 4U, 6U, true, 3U),
        7U, 0U, 4U, 6U));
    REQUIRE(source.clean_epochs() == 0U);
    REQUIRE(!source.observe(true, true, 7U, 0U, 4U, 7U));
    REQUIRE(!source.observe(true, true, 7U, 0U, 4U, 9U)); // discontinuity
    REQUIRE(source.clean_epochs() == 0U);
    REQUIRE(!source.observe(true, true, 7U, 0U, 4U, 10U));
    REQUIRE(source.observe(true, true, 7U, 0U, 4U, 11U));
    REQUIRE(!source.observe(true, false, 7U, 0U, 4U, 12U)); // TX fail
    REQUIRE(!source.observe(true, true, 7U, 1U, 4U, 13U)); // fault
    REQUIRE(!source.observe(false, true, 7U, 1U, 4U, 14U)); // phase lost
    REQUIRE(!mac.qualified_source_fast(7U, 0U, 3U, 8U, true, 3U));
}

void check_physical_capture_source_microsecond_quantization_no_flapping() {
    // Regression for ARStack_SV_20261022_212108_316: the board had continuous
    // 4000/s smpCnt and PTP, while +/-3us phase + 2..3us read uncertainty
    // caused 2->0 at the next epoch under the old combined 4us limit.
    SvTxEgressEvidence tx;
    SvLocalSyncEvidenceGate mac;
    SvSourceSelfClockAcquisition source;
    const auto baseline = make_good(2U, 1700001802U, 85000U);
    REQUIRE(tx.record(baseline) == SvTxEvidenceResult::baseline_established);
    mac.observe(baseline, SvTxEvidenceResult::baseline_established,
                tx.summary().last_phase_ns);
    bool locked = false;
    for (std::uint32_t epoch = 4U; epoch <= 20U; ++epoch) {
        if ((epoch % 2U) == 0U) {
            const auto sample = make_good(epoch, 1700001800U + epoch, 85000U);
            const auto result = tx.record(sample);
            REQUIRE(result == SvTxEvidenceResult::measured);
            mac.observe(sample, result, tx.summary().last_phase_ns);
        }
        const std::uint32_t measured_plus_uncertainty =
            (epoch % 3U) == 0U ? 6U : (epoch % 3U) == 1U ? 5U : 4U;
        const bool valid = mac.qualified_source_fast(
            7U, 0U, 3U, epoch, true, measured_plus_uncertainty);
        REQUIRE(valid);
        locked = source.observe(true, valid, 7U, 0U, 3U, epoch);
        REQUIRE(locked == (epoch >= 5U));
        // External-GM strictness is unchanged. The SOURCE fast route does
        // not weaken RECEIVER's 4us phase qualification.
        if (measured_plus_uncertainty > 4U)
            REQUIRE(!mac.qualified(7U, 0U, 3U, epoch, true,
                                   measured_plus_uncertainty));
    }
    // Actual timebase discontinuity, TX fault, MAC staleness still revoke.
    REQUIRE(!source.observe(true,
        mac.qualified_source_fast(7U, 0U, 4U, 21U, true, 5U),
        7U, 0U, 4U, 21U));
    REQUIRE(!mac.qualified_source_fast(7U, 0U, 3U, 25U, true, 5U));
    REQUIRE(!mac.qualified_source_fast(7U, 1U, 3U, 20U, true, 5U));
    REQUIRE(!mac.qualified_source_fast(7U, 0U, 3U, 20U, true, 13U));
}

void check_external_gm_receiver_requires_real_sv_egress_and_phase() {
    SvTxEgressEvidence tx;
    SvLocalSyncEvidenceGate mac;
    SvLocalRefAcquisition entry;
    constexpr auto t0 = 1700000200U;
    // A LOCKED/traceable external GM by itself never authorizes wire SV.
    REQUIRE(!mac.qualified(7U, 0U, 3U, 1U, true, 2U));
    REQUIRE(!entry.observe(false, 7U, 0U, 3U, 1U));
    bool timeline = false;
    for (std::uint32_t epoch = 2U; epoch <= 20U; ++epoch) {
        if ((epoch % 2U) == 0U) {
            const auto o = make_good(epoch, t0 + epoch, 90000U);
            const auto result = tx.record(o);
            mac.observe(o, result, tx.summary().last_phase_ns);
        }
        timeline = entry.observe(
            mac.qualified(7U, 0U, 3U, epoch, true, 2U),
            7U, 0U, 3U, epoch);
    }
    REQUIRE(timeline);
    // A clock identity/GM or receiver discipline loss revokes immediately.
    REQUIRE(!mac.qualified(7U, 0U, 4U, 20U, true, 2U));
    REQUIRE(!entry.observe(false, 7U, 0U, 4U, 21U));
    REQUIRE(!entry.observe(true, 7U, 1U, 4U, 22U));
    REQUIRE(!entry.observe(true, 8U, 1U, 4U, 23U));
    REQUIRE(!entry.observe(true, 8U, 1U, 4U, 23U));
}

void check_warm_phase_preflight_cannot_fake_canonical_mac() {
    SvLocalRefAcquisition gate;
    static_assert(SvLocalRefAcquisition::kWarmRequiredLiveEpochs == 3U);
    // SOURCE/RECEIVER may accumulate phase-only evidence with SV TX OFF.
    // No number of such observations may ever assert smpSynch.
    for (std::uint32_t e = 1U; e <= 10U; ++e) {
        gate.observe_preflight(true, 7U, 0U, 3U, e);
        REQUIRE(gate.clean_epochs() == 0U);
    }
    REQUIRE(gate.preflight_epochs() == 10U);
    // Descriptor baseline does not exist during the first live epochs:
    // preserve stable phase history but NEVER claim LOCAL without real MAC.
    REQUIRE(!gate.observe_live(true, false, 7U, 0U, 3U, 11U));
    REQUIRE(!gate.observe_live(true, false, 7U, 0U, 3U, 12U));
    REQUIRE(gate.preflight_epochs() == 10U);
    REQUIRE(!gate.observe_live(true, true, 7U, 0U, 3U, 13U));
    REQUIRE(!gate.observe_live(true, true, 7U, 0U, 3U, 14U));
    REQUIRE(gate.observe_live(true, true, 7U, 0U, 3U, 15U));
    REQUIRE(gate.clean_epochs() == 3U);
    // An established MAC evidence loss must revoke immediately.
    REQUIRE(!gate.observe_live(true, false, 7U, 0U, 3U, 16U));
    REQUIRE(gate.clean_epochs() == 0U);
    REQUIRE(gate.preflight_epochs() == 0U);
    // Cold recovery still demands ten complete live phase+MAC epochs.
    for (std::uint32_t e = 17U; e <= 26U; ++e) {
        REQUIRE(gate.observe_live(true, true, 7U, 0U, 3U, e)
                == (e == 26U));
    }
    REQUIRE(!gate.observe_live(false, true, 7U, 0U, 3U, 27U));
}

void check_warm_preflight_negative_session_and_clock_faults() {
    auto warm = [](SvLocalRefAcquisition& gate) {
        for (std::uint32_t e = 1U; e <= 10U; ++e)
            gate.observe_preflight(true, 7U, 0U, 3U, e);
        REQUIRE(gate.preflight_epochs() == 10U);
    };
    {
        SvLocalRefAcquisition gate;
        warm(gate);
        REQUIRE(!gate.observe_live(true, true, 7U, 0U, 4U, 11U)); // GM/source change
        REQUIRE(!gate.observe_live(true, true, 7U, 0U, 4U, 12U));
        REQUIRE(!gate.observe_live(true, true, 7U, 0U, 4U, 13U));
    }
    {
        SvLocalRefAcquisition gate;
        warm(gate);
        REQUIRE(!gate.observe_live(true, true, 7U, 1U, 3U, 11U)); // publisher fault
        REQUIRE(gate.preflight_epochs() == 0U);
    }
    {
        SvLocalRefAcquisition gate;
        warm(gate);
        REQUIRE(!gate.observe_live(true, true, 8U, 0U, 3U, 11U)); // new timer session
        REQUIRE(gate.preflight_epochs() == 0U);
    }
    {
        SvLocalRefAcquisition gate;
        warm(gate);
        REQUIRE(!gate.observe_live(true, true, 7U, 0U, 3U, 12U)); // skipped epoch
        REQUIRE(gate.preflight_epochs() == 0U);
    }
    {
        SvLocalRefAcquisition gate;
        warm(gate);
        REQUIRE(!gate.observe_live(false, true, 7U, 0U, 3U, 11U)); // phase/lease loss
        REQUIRE(gate.preflight_epochs() == 0U);
    }
    {
        SvLocalRefAcquisition gate;
        warm(gate);
        REQUIRE(!gate.observe_live(true, true, 7U, 0U, 3U, 11U));
        REQUIRE(!gate.observe_live(true, true, 7U, 0U, 3U, 11U)); // duplicate
        REQUIRE(gate.preflight_epochs() == 0U);
    }
    {
        SvLocalRefAcquisition gate;
        warm(gate);
        // No MAC descriptor evidence for eight live seconds: the warm
        // privilege expires and COLD retains the strict ten-live gate.
        for (std::uint32_t e = 11U; e <= 19U; ++e)
            REQUIRE(!gate.observe_live(true, false, 7U, 0U, 3U, e));
        REQUIRE(gate.preflight_epochs() == 0U);
        for (std::uint32_t e = 20U; e <= 29U; ++e)
            REQUIRE(gate.observe_live(true, true, 7U, 0U, 3U, e) == (e == 29U));
    }
    {
        SvLocalRefAcquisition gate;
        for (std::uint32_t e = 1U; e <= 9U; ++e)
            gate.observe_preflight(true, 7U, 0U, 3U, e);
        // Nine warm phase epochs are not enough for the 3-live shortcut.
        for (std::uint32_t e = 10U; e <= 19U; ++e)
            REQUIRE(gate.observe_live(true, true, 7U, 0U, 3U, e) == (e == 19U));
    }
}

void check_no_intermitttent_local_at_start() {
    SvLocalRefAcquisition gate;
    static_assert(SvLocalRefAcquisition::kRequiredCleanEpochs == 10U);
    REQUIRE(!gate.observe(false, 7U, 0U, 3U, 5U));
    REQUIRE(!gate.observe(true, 7U, 0U, 3U, 6U));
    REQUIRE(!gate.observe(false, 7U, 0U, 3U, 7U));
    REQUIRE(gate.clean_epochs() == 0U);

    // Field evidence: first LOCAL was present for just one second.
    // Three clean sample epochs must no longer be enough to advertise
    // a synchronization claim; a new invalid epoch revokes immediately.
    for (std::uint32_t epoch = 8U; epoch <= 10U; ++epoch)
        REQUIRE(!gate.observe(true, 7U, 0U, 3U, epoch));
    REQUIRE(gate.clean_epochs() == 3U);
    REQUIRE(!gate.observe(false, 7U, 0U, 3U, 11U));
    REQUIRE(gate.clean_epochs() == 0U);

    // 10 genuinely continuous SOURCE/phase/MAC epochs can finally
    // advertise LOCAL. Any loss must still de-assert immediately.
    for (std::uint32_t epoch = 12U; epoch <= 21U; ++epoch)
        REQUIRE(gate.observe(true, 7U, 0U, 3U, epoch) == (epoch == 21U));
    REQUIRE(gate.clean_epochs() == 10U);
    REQUIRE(!gate.observe(false, 7U, 0U, 3U, 22U));
    REQUIRE(gate.clean_epochs() == 0U);

    for (std::uint32_t epoch = 23U; epoch <= 27U; ++epoch)
        REQUIRE(!gate.observe(true, 7U, 0U, 3U, epoch));
    REQUIRE(!gate.observe(true, 7U, 0U, 3U, 29U)); // gap
    for (std::uint32_t epoch = 30U; epoch <= 37U; ++epoch)
        REQUIRE(!gate.observe(true, 7U, 0U, 3U, epoch));
    REQUIRE(gate.observe(true, 7U, 0U, 3U, 38U));
    REQUIRE(!gate.observe(true, 7U, 1U, 3U, 39U)); // TX fault
    REQUIRE(!gate.observe(true, 7U, 1U, 4U, 40U)); // source restart
    REQUIRE(!gate.observe(true, 8U, 1U, 4U, 41U)); // publisher restart
    REQUIRE(!gate.observe(true, 8U, 1U, 4U, 41U)); // duplicate
    REQUIRE(!gate.observe(true, 0U, 1U, 4U, 42U));
    REQUIRE(!gate.observe(true, 8U, 1U, 0U, 43U));
}
void check_local_only_after_ten_clean_seconds() {
    SvLocalRefAcquisition gate;
    for (std::uint32_t t = 1U; t <= 15U; ++t)
        REQUIRE(gate.observe(t >= 6U, 7U, 0U, 3U, t) == (t == 15U));
}

void check_baseline_and_contiguous_ptp() {
    SvTxEgressEvidence e{16U};
    auto o = make_good(16U, 1700000000U, 125000U);
    REQUIRE(e.record(o) == SvTxEvidenceResult::baseline_established);
    REQUIRE(e.summary().measured == 0U); // one point cannot establish continuity
    o = make_good(32U, 1700000015U, 999900000U);
    o.scheduled_alarm_ticks_low32 = 0x00000027U;
    REQUIRE(e.record(o) == SvTxEvidenceResult::measured);
    REQUIRE(e.summary().last_phase_ns == -100000);
    REQUIRE(e.summary().last_alarm_ticks_low32 == 0x27U);
    REQUIRE(e.summary().min_phase_ns == -100000);
    o = make_good(48U, 1700000032U, 0U);
    REQUIRE(e.record(o) == SvTxEvidenceResult::measured);
    REQUIRE(e.summary().phase_sum_ns == -100000LL);
    o = make_good(64U, 1700000048U, 125000U);
    REQUIRE(e.record(o) == SvTxEvidenceResult::measured);
    REQUIRE(e.summary().max_phase_ns == 125000);
    REQUIRE(e.summary().phase_sum_ns == 25000LL);
    REQUIRE(e.summary().attempted == 4U);
    REQUIRE(e.summary().measured == 3U);
}
void check_clock_steps_recovery_and_session() {
    SvTxEgressEvidence e{16U};
    REQUIRE(e.record(make_good(16U, 1700000000U)) ==
            SvTxEvidenceResult::baseline_established);
    // +1 sec PTP clock step despite perfect-looking within-second phase.
    REQUIRE(e.record(make_good(32U, 1700000017U)) ==
            SvTxEvidenceResult::ptp_clock_discontinuity);
    REQUIRE(e.summary().measured == 0U);
    REQUIRE(e.summary().ptp_clock_discontinuities == 1U);
    REQUIRE(e.record(make_good(48U, 1700000033U)) ==
            SvTxEvidenceResult::measured); // two coherent post-step epochs
    auto restarted = make_good(64U, 1700000049U);
    restarted.publisher_session_generation = 8U;
    REQUIRE(e.record(restarted) == SvTxEvidenceResult::publisher_discontinuity);
    restarted.sample_epoch_generation = 80U;
    restarted.hw_ptp_seconds = 1700000065U;
    REQUIRE(e.record(restarted) == SvTxEvidenceResult::measured);
    restarted.sample_epoch_generation = 96U;
    restarted.hw_ptp_seconds = 1700000081U;
    ++restarted.publisher_fault_generation; // lost a canonical frame mid-window
    REQUIRE(e.record(restarted) == SvTxEvidenceResult::publisher_discontinuity);
    REQUIRE(e.summary().publisher_discontinuities == 2U);
    restarted.sample_epoch_generation = 112U;
    restarted.hw_ptp_seconds = 1700000097U;
    REQUIRE(e.record(restarted) == SvTxEvidenceResult::measured);
    // Detect 16-second sample epoch discontinuity even if PTP time looks fine.
    restarted.sample_epoch_generation = 144U;
    restarted.hw_ptp_seconds = 1700000113U;
    REQUIRE(e.record(restarted) == SvTxEvidenceResult::publisher_discontinuity);
    // Restarted PTP source can retain plausible seconds; its generation
    // nevertheless must discard the previous comparison window.
    restarted.sample_epoch_generation = 160U;
    restarted.hw_ptp_seconds = 1700000129U;
    ++restarted.ptp_source_generation;
    REQUIRE(e.record(restarted) == SvTxEvidenceResult::ptp_clock_discontinuity);
    REQUIRE(e.summary().ptp_source_changes == 1U);
    restarted.sample_epoch_generation = 176U;
    restarted.hw_ptp_seconds = 1700000145U;
    REQUIRE(e.record(restarted) == SvTxEvidenceResult::measured);
}
void check_invalid_evidence_requires_rebaseline() {
    SvTxEgressEvidence e{16U};
    auto o = make_good(16U, 1700000000U);
    REQUIRE(e.record(o) == SvTxEvidenceResult::baseline_established);
    o = make_good(32U, 1700000016U);
    o.ptp_source_generation = 0U;
    REQUIRE(e.record(o) == SvTxEvidenceResult::not_a_qualified_epoch);
    o = make_good(48U, 1700000032U);
    REQUIRE(e.record(o) == SvTxEvidenceResult::baseline_established);
    o = make_good(64U, 1700000048U);
    o.hw_ptp_seconds = 0U;
    REQUIRE(e.record(o) == SvTxEvidenceResult::missing_hw_timestamp);
    o = make_good(80U, 1700000064U);
    REQUIRE(e.record(o) == SvTxEvidenceResult::baseline_established);
    o = make_good(96U, 1700000080U);
    o.submit_duration_us = 900U;
    o.no_missed_slots = false; // descriptor blocking exceeded slot
    REQUIRE(e.record(o) == SvTxEvidenceResult::not_a_qualified_epoch);
    REQUIRE(e.summary().max_submit_duration_us == 900U);
    o = make_good(112U, 1700000096U);
    REQUIRE(e.record(o) == SvTxEvidenceResult::baseline_established);
    o = make_good(128U, 1700000112U);
    o.canonical_tx_success = false;
    REQUIRE(e.record(o) == SvTxEvidenceResult::transmit_failed);
    o = make_good(144U, 1700000128U);
    REQUIRE(e.record(o) == SvTxEvidenceResult::baseline_established);
    o = make_good(160U, 1700000144U);
    REQUIRE(e.record(o) == SvTxEvidenceResult::measured);
    REQUIRE(e.summary().measured == 1U);
    REQUIRE(e.summary().missing_hw_timestamp == 1U);
    REQUIRE(e.summary().failed_transmit == 1U);
    REQUIRE(e.summary().rejected_epoch == 2U);
    e.reset();
    REQUIRE(e.summary().attempted == 0U);
    REQUIRE(e.summary().measured == 0U);
}
void check_counter_wrap_and_signed_negative_phase() {
    SvTxEgressEvidence e{16U};
    auto o = make_good(0xfffffff8U, 1700000099U, 999999900U);
    REQUIRE(e.record(o) == SvTxEvidenceResult::baseline_established);
    o = make_good(8U, 1700000115U, 999999900U);
    REQUIRE(e.record(o) == SvTxEvidenceResult::measured);
    REQUIRE(e.summary().last_phase_ns == -100);
    // A 2-second backward clock step cannot silently look like a good phase.
    o = make_good(24U, 1700000129U, 999999900U);
    REQUIRE(e.record(o) == SvTxEvidenceResult::ptp_clock_discontinuity);
    SvTxEgressEvidence invalid{0U};
    REQUIRE(invalid.record(make_good(16U, 1700000000U)) ==
            SvTxEvidenceResult::not_a_qualified_epoch);
}
}

int main() {
    try {
        check_auto_local_sync_gate_requires_both_clocks_and_fresh_mac();
        check_fast_source_auto_qualification_with_fresh_evidence();
        check_internal_source_auto_acquires_without_ten_second_probation();
        check_physical_capture_source_microsecond_quantization_no_flapping();
        check_external_gm_receiver_requires_real_sv_egress_and_phase();
        check_warm_phase_preflight_cannot_fake_canonical_mac();
        check_warm_preflight_negative_session_and_clock_faults();
        check_no_intermitttent_local_at_start();
        check_local_only_after_ten_clean_seconds();
        check_baseline_and_contiguous_ptp();
        check_clock_steps_recovery_and_session();
        check_invalid_evidence_requires_rebaseline();
        check_counter_wrap_and_signed_negative_phase();
        std::cout << "SV descriptor TX evidence temporal continuity tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
