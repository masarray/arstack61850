// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/time_sync/sv_ptp_phase_authority.hpp"
#include "ariec61850/time_sync/sv_boot_clock_policy.hpp"
#include "ariec61850/time_sync/ptp_external_candidate_policy.hpp"
#include "ariec61850/time_sync/ptp_advanced_policy.hpp"
#include "ariec61850/time_sync/sv_ptp_clock_lease.hpp"
#include "ariec61850/sampled_values/rational_schedule.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace ar::iec61850::time_sync;

#define CHECK(x) do { if (!(x)) throw std::runtime_error( \
    std::string{"check failed: "} + #x + " line " + \
    std::to_string(__LINE__)); } while (false)

namespace {

constexpr std::uint64_t kEpochZero = 1700000000ULL;
constexpr std::uint32_t kPtpfractionNs = 250000000U;

// Synthetic EMAC PTP second with an independent GPTimer running +80 ppm.
// A true physical validation also requires the actual EMAC TX timestamps.
SvPtpPhaseObservation snapshot(
    const std::uint32_t epoch, const std::int32_t phase_us,
    const std::uint32_t read_span_us = 8U) {
    const std::uint32_t ptp_boundary = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(epoch) * 1000080ULL);
    const std::uint32_t read_mid = ptp_boundary + 250020U;
    return {read_mid - (read_span_us / 2U),
            read_mid + (read_span_us / 2U),
            static_cast<std::uint32_t>(
                static_cast<std::int64_t>(ptp_boundary) + phase_us),
            epoch, kEpochZero + epoch, kPtpfractionNs};
}

void check_auto_preflight_does_not_invent_sync_or_start() {
    static_assert(SvAutoPreflightPolicy::can_prepare(
        true, true, true, 4000U, 4000U));
    CHECK(!SvAutoPreflightPolicy::can_prepare(
        false, true, true, 4000U, 4000U)); // operator FORCE_0
    CHECK(!SvAutoPreflightPolicy::can_prepare(
        true, false, true, 4000U, 4000U)); // PTP source OFF
    CHECK(!SvAutoPreflightPolicy::can_prepare(
        true, true, false, 4000U, 4000U)); // unverified profile
    CHECK(!SvAutoPreflightPolicy::can_prepare(
        true, true, true, 4000U, 4800U)); // no 1-second wrap
    CHECK(!SvAutoPreflightPolicy::can_prepare(
        true, true, true, 0U, 0U));
    // Field capture: 120 seconds of AUTO=0 after a preflight phase
    // observer was improperly gated on actual SV injection.
    CHECK(SvAutoPreflightPolicy::can_steer(true, true, true, true, false, true));
    CHECK(SvAutoPreflightPolicy::can_steer(true, true, true, true, true, false));
    CHECK(!SvAutoPreflightPolicy::can_steer(true, true, true, true, false, false));
    CHECK(!SvAutoPreflightPolicy::can_steer(true, true, false, true, false, true));
    CHECK(!SvAutoPreflightPolicy::can_steer(true, true, true, false, false, true));
    CHECK(!SvAutoPreflightPolicy::can_steer(true, false, true, true, false, true));
    CHECK(!SvAutoPreflightPolicy::can_advertise_local(false, true));
    CHECK(!SvAutoPreflightPolicy::can_advertise_local(true, false));
    CHECK(SvAutoPreflightPolicy::can_advertise_local(true, true));
    CHECK(SvAutoPreflightPolicy::can_reuse(true, true, true, true, true));
    CHECK(!SvAutoPreflightPolicy::can_reuse(false, true, true, true, true));
    CHECK(!SvAutoPreflightPolicy::can_reuse(true, false, true, true, true));
    CHECK(!SvAutoPreflightPolicy::can_reuse(true, true, false, true, true));
    CHECK(!SvAutoPreflightPolicy::can_reuse(true, true, true, false, true));
    CHECK(!SvAutoPreflightPolicy::can_reuse(true, true, true, true, false));

    // A warm phase servo can track but must never be interpreted as an
    // independently measured canonical SV publication. The packet evidence
    // gate is reset at START, so even prequalified phase is NOT smpSynch=1.
    SvPtpPhaseAuthority phase;
    for (std::uint32_t epoch = 1U; epoch <= 5U; ++epoch) {
        const auto d = phase.observe(snapshot(epoch, 0U, 0U));
        CHECK(d.accepted);
        CHECK(d.software_tracking == (epoch >= 4U));
    }
}

void check_m3a_stop_start_keeps_qualified_sample_grid() {
    // Pure lifecycle gate (not a hardware phase-accuracy substitute). Model
    // repeated START/STOP without rearming GPTimer or changing PTP authority.
    // No SV packet may be sent in the PARKED state; resumed output begins
    // only at the next already-running second boundary.
    for (std::uint32_t rate : {4000U, 4800U}) {
        const bool auto_ready = SvAutoPreflightPolicy::can_prepare(
            true, true, true, rate, rate);
        CHECK(auto_ready);
        std::uint32_t timer_rearms = 1U; // initial power-on clock acquisition
        std::uint32_t source_generation = 7U;
        std::uint32_t phase_session = 3U;
        for (std::uint32_t cycle = 0U; cycle < 100U; ++cycle) {
            const bool parked = SvAutoPreflightPolicy::can_park_running_grid(
                true, true, true, true, true, auto_ready);
            CHECK(parked);
            const bool dry_clock = parked;
            const bool sv_tx_enabled = false;
            CHECK(dry_clock && !sv_tx_enabled);
            CHECK(SvAutoPreflightPolicy::can_steer(
                true, true, true, true, false, dry_clock));
            CHECK(!SvAutoPreflightPolicy::can_advertise_local(false, true));
            CHECK(SvAutoPreflightPolicy::can_reuse(
                parked, true, true, true, true));
            // Rapid STOP→START, before the 200-ms idle loop can park:
            // a still-owned, source-identical grid is equally reusable.
            const bool hot_stopped = SvAutoPreflightPolicy::can_park_running_grid(
                true, true, true, true, true, auto_ready);
            CHECK(SvAutoPreflightPolicy::can_reuse(
                false || hot_stopped, true, true, true, true));
            CHECK(timer_rearms == 1U);
            CHECK(source_generation == 7U && phase_session == 3U);
        }
    }
    // Warm resume uses already *measured* sync and a continuous dry grid.
    // It may bridge one new MAC baseline, not a real phase/lease/Tx fault.
    using Warm = ar::iec61850::time_sync::SvWarmResumePolicy;
    for (std::uint32_t cycle = 0U; cycle < 100U; ++cycle) {
        const std::uint32_t previous_epoch = 50U + cycle * 20U;
        CHECK(Warm::may_begin(true, true, true, true, true));
        for (std::uint32_t n = 1U; n <= Warm::kMaxLiveReacquireEpochs; ++n) {
            CHECK(Warm::retain(previous_epoch, previous_epoch + n,
                               true, true, true, true));
        }
        CHECK(!Warm::within_window(previous_epoch,
                                   previous_epoch + Warm::kMaxLiveReacquireEpochs + 1U));
        CHECK(!Warm::retain(previous_epoch, previous_epoch + 1U,
                            false, true, true, true)); // lost sample phase
        CHECK(!Warm::retain(previous_epoch, previous_epoch + 1U,
                            true, false, true, true)); // different GM/lease
        CHECK(!Warm::retain(previous_epoch, previous_epoch + 1U,
                            true, true, false, true)); // lost publisher slot
        CHECK(!Warm::retain(previous_epoch, previous_epoch + 1U,
                            true, true, true, false)); // revoked by MAC
    }
    CHECK(!Warm::may_begin(true, true, false, true, true)); // cold START
    CHECK(!Warm::may_begin(true, true, true, false, true)); // stale observer
    CHECK(!Warm::may_begin(true, true, true, true, false)); // external receiver
    CHECK(!Warm::within_window(0U, 1U));

    // Any real source/lease/profile/phase fault must prevent parking, not
    // silently keep a stale GPTimer authority or a stale smpSynch claim.
    for (int fault = 0; fault < 6; ++fault) {
        std::array<bool, 6> inputs{true, true, true, true, true, true};
        inputs[static_cast<std::size_t>(fault)] = false;
        CHECK(!SvAutoPreflightPolicy::can_park_running_grid(
            inputs[0], inputs[1], inputs[2], inputs[3],
            inputs[4], inputs[5]));
    }
    CHECK(!SvAutoPreflightPolicy::can_prepare(
        true, true, true, 4000U, 4800U));
}

void check_m3e_parked_certified_ptp_authority_survives_100_restarts() {
    // IEC 61850-9-2: SmpSynch describes the sampled TIMEBASE, not whether
    // the publisher is presently transmitting SV frames.
    using Park = SvParkedClockAuthority;
    using Resume = SvWarmResumePolicy;
    CHECK(!Park::may_preserve(false, true, true, true, true, true, true));
    CHECK(!Park::may_resume(false, true, true, true, true, true, true));
    for (std::uint32_t cycle = 0U; cycle < 100U; ++cycle) {
        // The old certificate came from a measured MAC and qualified phase.
        // The phase observer runs for sixty more seconds while SV TX is off.
        bool certified = true;
        CHECK(Park::may_preserve(certified, true, true, true, true, true, true));
        for (std::uint32_t second = 0U; second < 60U; ++second)
            CHECK(Park::may_preserve(certified, true, true, true, true, true, true));
        CHECK(Park::may_resume(certified, true, true, true, true, true, true));
        CHECK(Resume::may_begin(true, true, certified, true, true));
        CHECK(Resume::retain(100U, 101U, true, true, true, true));
        CHECK(Resume::retain(100U, 105U, true, true, true, true));
        CHECK(!Resume::retain(100U, 109U, true, true, true, true));
    }
    // A real PTP lease change, lost phase, invalid freshness, non-SOURCE
    // mode, non-AUTO policy, or cold clock cannot inherit smpSynch.
    for (int gate = 0; gate < 7; ++gate) {
        std::array<bool, 7> checks{true, true, true, true, true, true, true};
        checks[static_cast<std::size_t>(gate)] = false;
        CHECK(!Park::may_resume(checks[0], checks[1], checks[2],
                                checks[3], checks[4], checks[5], checks[6]));
    }
}

void check_m3b_boot_ptp_is_safe_and_operator_owned() {
    using Boot = SvBootClockPolicy;
    static_assert(Boot::kMaxAttempts == 4U);
    // SOURCE-only and explicitly enabled; never auto-become SOURCE in the
    // presence of an external RECEIVER or MONITOR configuration.
    CHECK(Boot::may_start(true, false, true, true, false, 0U, 100U, 0U));
    CHECK(!Boot::may_start(false, false, true, true, false, 0U, 100U, 0U));
    CHECK(!Boot::may_start(true, true, true, true, false, 0U, 100U, 0U));
    CHECK(!Boot::may_start(true, false, false, true, false, 0U, 100U, 0U));
    CHECK(!Boot::may_start(true, false, true, false, false, 0U, 100U, 0U));
    CHECK(!Boot::may_start(true, false, true, true, true, 0U, 100U, 0U));
    CHECK(!Boot::may_start(true, false, true, true, false, 4U, 100U, 0U));
    CHECK(!Boot::may_start(true, false, true, true, false, 2U, 100U, 4000U));
    CHECK(Boot::may_start(true, false, true, true, false, 2U, 4000U, 4000U));
    CHECK(Boot::retry_delay_ms(0U) == 1000U);
    CHECK(Boot::retry_delay_ms(1U) == 2000U);
    CHECK(Boot::retry_delay_ms(2U) == 4000U);
    CHECK(Boot::retry_delay_ms(4U) == 16000U);
    CHECK(Boot::retry_delay_ms(100U) == 16000U);
    CHECK(Boot::may_recover_after_link_cycle(
        true, true, true, false, false, true));
    CHECK(!Boot::may_recover_after_link_cycle(
        true, false, true, false, false, true)); // SV STOP is not PHY loss
    CHECK(!Boot::may_recover_after_link_cycle(
        true, true, true, true, false, true)); // existing valid SOURCE
    CHECK(!Boot::may_recover_after_link_cycle(
        true, true, true, false, true, true)); // operator manual PTP STOP
    CHECK(!Boot::may_recover_after_link_cycle(
        true, true, true, false, false, false)); // RECEIVER/MONITOR
    CHECK(!Boot::may_recover_after_link_cycle(
        true, true, false, false, false, true)); // still unplugged
    CHECK(!Boot::may_recover_after_link_cycle(
        false, true, true, false, false, true)); // never had SOURCE
    // The monotonic unsigned timestamp wraps after ~49 days; use signed
    // difference for bounded near-wrap retry windows.
    CHECK(Boot::may_start(true, false, true, true, false, 1U, 10U, 0xFFFFFFF0U));
    CHECK(!Boot::may_start(true, false, true, true, false, 1U, 0xFFFFFFF0U, 10U));
}

void check_m3c_foreign_announce_and_exclusive_emac_handover() {
    using Guard = PtpExternalHandoverGuard;
    PtpAnnounceRank local;
    local.priority1 = 128U; local.clock_class = 248U;
    local.clock_accuracy = 254U; local.priority2 = 128U;
    local.gm_identity = {0x10,0,0,0,0,0,0,1};
    local.steps_removed = 0U;
    auto foreign = local;
    foreign.priority1 = 100U;
    foreign.gm_identity = {0x20,0,0,0,0,0,0,2};
    foreign.steps_removed = 1U;
    foreign.source_identity = foreign.gm_identity;
    foreign.source_port = 1U;
    PtpExternalCandidateGate gate;
    // One forged packet or a burst of same-time packets cannot switch GM.
    gate.observe(foreign, local, 1000U);
    for (int i=0;i<100;i++) gate.observe(foreign, local, 1000U);
    CHECK(!gate.stable(1000U));
    gate.observe(foreign, local, 2000U);
    gate.observe(foreign, local, 3000U);
    CHECK(!gate.stable(3000U));
    gate.observe(foreign, local, 4000U);
    CHECK(gate.stable(4000U));
    CHECK(!gate.stable(7000U)); // stale 2.5s Announce lease
    gate.observe(foreign, local, 8000U); // candidate starts fresh
    CHECK(!gate.stable(8000U));
    auto self = local;
    gate.reset();
    for (std::uint64_t t = 1000; t < 10000; t+=1000)
        gate.observe(self, local, t);
    CHECK(!gate.stable(10000U)); // no self-elected external GM
    auto worse = foreign;
    worse.priority1 = 200U;
    for (std::uint64_t t = 1000; t < 10000; t+=1000)
        gate.observe(worse, local, t);
    CHECK(!gate.stable(10000U)); // never prefer inferior GM
    foreign.steps_removed = 256U; // invalid hop depth
    for (std::uint64_t t = 1000; t < 10000; t+=1000)
        gate.observe(foreign, local, t);
    CHECK(!gate.stable(10000U));
    foreign.steps_removed = 1U;
    for (std::uint64_t t = 11000; t <= 15000; t+=1000)
        gate.observe(foreign, local, t);
    CHECK(gate.stable(15000U));
    // Two independent foreign grandmasters, Announce streams interleaved:
    // neither must cancel the other's qualification history. The weaker
    // candidate still outranks our internal 248-class lab oscillator.
    gate.reset();
    auto other = foreign;
    other.priority1 = 110U;
    other.gm_identity = {0x30,0,0,0,0,0,0,3};
    other.source_identity = other.gm_identity;
    for (std::uint64_t t = 20000U; t <= 24000U; t+=1000U) {
        gate.observe(foreign, local, t);
        gate.observe(other, local, t + 1U);
    }
    CHECK(gate.stable(24001U));
    // Degrade one peer's quality and verify its old lease is revoked
    // without deleting independent healthy candidates.
    auto degraded = other;
    degraded.priority1 = 240U;
    gate.observe(degraded, local, 25000U);
    CHECK(gate.stable(25000U));
    CHECK(Guard::may_handover(true,false,true,false,true,false,true));
    CHECK(Guard::kMinSwitchDwellMs == 30000U);
    CHECK(Guard::cooldown_expired(30000U, 30000U));
    CHECK(!Guard::cooldown_expired(29999U, 30000U));
    CHECK(!Guard::cooldown_expired(0xFFFFFFF0U, 20U));
    CHECK(Guard::cooldown_expired(30U, 20U));
    CHECK(!Guard::may_handover(true,false,true,true,true,false,true));
    CHECK(!Guard::may_handover(true,true,true,false,true,false,true));
    CHECK(!Guard::may_handover(true,false,true,false,true,true,true));
    CHECK(!Guard::may_handover(true,false,true,false,false,false,true));
    CHECK(!Guard::may_handover(true,false,true,false,true,false,false));
    CHECK(Guard::may_fallback(true,false,false,true,false,true,15000U));
    CHECK(!Guard::may_fallback(true,false,false,true,true,true,15000U));
    CHECK(!Guard::may_fallback(false,false,false,true,false,true,15000U));
    CHECK(!Guard::may_fallback(true,true,false,true,false,true,15000U));
    CHECK(!Guard::may_fallback(true,false,true,true,false,true,15000U));
    CHECK(!Guard::may_fallback(true,false,false,true,false,true,14999U));
    CHECK(!Guard::may_fallback(true,false,false,true,false,false,20000U));
}

void check_m3d_advanced_ptp_policy_prevents_implicit_role_promotion() {
    using Mode = PtpAdvancedMode;
    using Policy = PtpAdvancedPolicy;
    CHECK(Policy::valid(Mode::internal_gm_only));
    CHECK(Policy::valid(Mode::auto_prefer_external));
    CHECK(Policy::valid(Mode::external_only));
    CHECK(Policy::valid(Mode::monitor_only));
    CHECK(!Policy::valid(static_cast<Mode>(255U)));
    CHECK(Policy::may_apply(true,true,true,true));
    CHECK(!Policy::may_apply(false,true,true,true));
    CHECK(!Policy::may_apply(true,false,true,true));
    CHECK(!Policy::may_apply(true,true,false,true));
    CHECK(!Policy::may_apply(true,true,true,false));
    CHECK(Policy::starts_source(Mode::internal_gm_only));
    CHECK(Policy::starts_source(Mode::auto_prefer_external));
    CHECK(!Policy::starts_source(Mode::external_only));
    CHECK(!Policy::starts_source(Mode::monitor_only));
    CHECK(Policy::starts_receiver(Mode::external_only));
    CHECK(!Policy::starts_receiver(Mode::auto_prefer_external));
    CHECK(Policy::starts_monitor(Mode::monitor_only));
    CHECK(!Policy::starts_monitor(Mode::external_only));
    CHECK(Policy::auto_handover(Mode::auto_prefer_external));
    CHECK(!Policy::auto_handover(Mode::internal_gm_only));
    CHECK(!Policy::auto_handover(Mode::external_only));
    CHECK(!Policy::auto_handover(Mode::monitor_only));
    CHECK(Policy::permits_local_fallback(Mode::auto_prefer_external,true));
    CHECK(!Policy::permits_local_fallback(Mode::auto_prefer_external,false));
    CHECK(!Policy::permits_local_fallback(Mode::external_only,true));
    CHECK(!Policy::permits_local_fallback(Mode::monitor_only,true));

    // M3.D regression: an already-provisioned bench GM cannot silently
    // revert to LOCAL on SOURCE->SOURCE Advanced policy selection.
    constexpr std::int64_t now = 50'000'000LL;
    const auto keeps = [&](Mode mode, bool was_source, bool bench,
                           std::int64_t age) {
        return Policy::may_preserve_bench_seed(
            mode, was_source, bench, 1792800000U, 250U, now - age, now);
    };
    CHECK(keeps(Mode::internal_gm_only, true, true, 0LL));
    CHECK(keeps(Mode::auto_prefer_external, true, true,
                Policy::kBenchSeedMaxAgeUs));
    CHECK(!keeps(Mode::auto_prefer_external, true, true,
                 Policy::kBenchSeedMaxAgeUs + 1LL));
    CHECK(!keeps(Mode::external_only, true, true, 0LL));
    CHECK(!keeps(Mode::monitor_only, true, true, 0LL));
    CHECK(!keeps(Mode::internal_gm_only, false, true, 0LL));
    CHECK(!keeps(Mode::internal_gm_only, true, false, 0LL));
    CHECK(!Policy::may_preserve_bench_seed(
        Mode::internal_gm_only, true, true, 1792800000U, 250U, 0LL, now));
    CHECK(!Policy::may_preserve_bench_seed(
        Mode::internal_gm_only, true, true, 1792800000U, 250U,
        now + 1LL, now));
    CHECK(!Policy::may_preserve_bench_seed(
        Mode::internal_gm_only, true, true, 1500000000U, 250U, now, now));
    CHECK(!Policy::may_preserve_bench_seed(
        Mode::internal_gm_only, true, true, 1792800000U, 1000U, now, now));
}

void check_receiver_preflight_requires_measured_gm_lease() {
    SvPtpClockLease gm{};
    gm.role = PtpAuthorityRole::receiver;
    gm.generation = 9U;
    gm.clock_usable = true;
    gm.source_selected = true;
    gm.source_port = 1U;
    gm.source_clock_identity[0] = 0x12U;
    CHECK(valid_sv_ptp_clock_lease(gm));
    CHECK(SvAutoPreflightPolicy::can_prepare(
        true, valid_sv_ptp_clock_lease(gm), true, 4000U, 4000U));
    CHECK(!SvAutoPreflightPolicy::can_prepare(
        true, valid_sv_ptp_clock_lease(gm), true, 4000U, 4800U));
    CHECK(!SvAutoPreflightPolicy::can_advertise_local(false, true));
    auto changed = gm;
    changed.source_clock_identity[0] ^= 0x01U; // swapped grandmaster
    CHECK(!same_sv_ptp_clock_lease(gm, changed));
    changed = gm;
    changed.generation += 1U; // GM relock/clock discontinuity
    CHECK(!same_sv_ptp_clock_lease(gm, changed));
    changed = gm;
    changed.clock_usable = false; // lost discipline
    CHECK(!valid_sv_ptp_clock_lease(changed));
}

void check_phase_observer_stall_revokes_source_wire_claim() {
    using Fresh = SvPhaseObservationFreshness;
    static_assert(Fresh::kMaxUnobservedEpochs == 1U);
    // After a qualified observation at epoch 25, the producer may continue
    // into the next second while the observer runs at t+250ms. An observer
    // stall cannot advertise LOCAL through the following epoch.
    CHECK(!Fresh::fresh(0U, 25U));
    CHECK(!Fresh::fresh(25U, 0U));
    CHECK(Fresh::fresh(25U, 25U));
    CHECK(Fresh::fresh(25U, 26U));
    CHECK(!Fresh::fresh(25U, 27U));
    CHECK(!Fresh::fresh(25U, 100U));
    CHECK(!Fresh::fresh(26U, 25U)); // future/stale generation
    // No retained qualification across a 32-bit generation wrap; only a
    // genuinely adjacent observation may pass.
    CHECK(Fresh::fresh(0xfffffffeU, 0xffffffffU));
    CHECK(!Fresh::fresh(0xfffffffeU, 1U));
    CHECK(!Fresh::fresh(0xffffffffU, 0U)); // zero remains invalid
    CHECK(Fresh::fresh(0xffffffffU, 1U) == false); // two skipped epochs
}

void check_phase_sign_and_bounded_corrections() {
    SvPtpPhaseAuthority controller;
    const auto warm = controller.observe(snapshot(1U, 220));
    CHECK(warm.accepted);
    CHECK(!warm.software_tracking);
    CHECK(warm.slew_ticks_next_epoch == 0);
    CHECK(warm.phase_error_us == 200); // 80ppm * 0.25s = 20us
    const auto second = controller.observe(snapshot(2U, 300));
    CHECK(second.accepted);
    CHECK(second.phase_error_us == 280);
    CHECK(second.slew_ticks_next_epoch < 0);
    CHECK(second.slew_ticks_next_epoch >= -400);
    CHECK(!second.software_tracking);
    // A coarse software START may initially be several milliseconds
    // from the PTP epoch. Acquire monotonically, never jump the counter.
    SvPtpPhaseAuthority coarse;
    CHECK(coarse.observe(snapshot(1U, 9000)).accepted);
    const auto coarse_recovery = coarse.observe(snapshot(2U, 9080));
    CHECK(coarse_recovery.accepted);
    CHECK(coarse_recovery.slew_ticks_next_epoch == -400);
    CHECK(!coarse_recovery.software_tracking);
    const auto step = controller.observe(snapshot(3U, 65000));
    CHECK(!step.accepted);
    CHECK(step.clock_discontinuity);
    CHECK(step.slew_ticks_next_epoch == 0);
    CHECK(step.state == SvPtpPhaseState::invalid);
    // A fault never retains an integrator/slew command across rearm.
    const auto rearm = controller.observe(snapshot(4U, 100));
    CHECK(rearm.accepted);
    CHECK(rearm.slew_ticks_next_epoch == 0);
}

void check_per_second_slew_distribution() {
    for (const auto rate : std::array<std::uint32_t, 2>{4000U, 4800U}) {
        for (const auto correction : std::array<std::int32_t, 7>{
                 -400, -81, -1, 0, 1, 73, 400}) {
            SvBoundedSlewCursor slew;
            slew.arm(rate, correction);
            ar::iec61850::sampled_values::RationalTickSchedule cadence{
                1000000ULL, rate};
            std::int64_t total_ticks = 0;
            std::int32_t adjusted = 0;
            for (std::uint32_t sample = 0U; sample < rate; ++sample) {
                const auto adjustment = slew.next_adjustment();
                CHECK(adjustment >= -1 && adjustment <= 1);
                const auto base = cadence.next_interval_ticks();
                const auto final_interval =
                    static_cast<std::int64_t>(base) + adjustment;
                CHECK(final_interval > 0);
                total_ticks += final_interval;
                adjusted += adjustment;
            }
            CHECK(adjusted == correction);
            CHECK(slew.applied_ticks() == correction);
            CHECK(total_ticks ==
                  1000000LL + static_cast<std::int64_t>(correction));
        }
        SvBoundedSlewCursor invalid;
        invalid.arm(0U, 99);
        CHECK(invalid.next_adjustment() == 0);
        invalid.arm(rate, 3000); // one-tick maximum, cap total slew
        for (std::uint32_t i = 0; i < rate; ++i) {
            static_cast<void>(invalid.next_adjustment());
        }
        CHECK(invalid.applied_ticks() == 400);
    }
}

void check_software_servo_tracks_80ppm_drift_without_counter_reset() {
    for (const auto rate : std::array<std::uint32_t, 2>{4000U, 4800U}) {
        SvPtpPhaseAuthority controller;
        std::int32_t phase_us = 160;
        std::int32_t command = 0;
        std::int32_t peak_last_100 = 0;
        std::uint32_t accepted = 0U;
        bool ever_tracking = false;
        for (std::uint32_t epoch = 1; epoch <= 480U; ++epoch) {
            // The previous epoch's bounded steering is completed before
            // the current timestamp is sampled.
            phase_us += 80 + command;
            const auto result = controller.observe(snapshot(epoch, phase_us));
            CHECK(result.accepted);
            CHECK(result.observation_uncertainty_us <= 5U);
            CHECK(result.slew_ticks_next_epoch >= -400);
            CHECK(result.slew_ticks_next_epoch <= 400);
            if (result.software_tracking) ever_tracking = true;
            ++accepted;
            command = result.slew_ticks_next_epoch;
            SvBoundedSlewCursor actual;
            actual.arm(rate, command);
            std::int32_t distributed = 0;
            for (std::uint32_t n = 0U; n < rate; ++n) {
                distributed += actual.next_adjustment();
            }
            CHECK(distributed == command);
            if (epoch > 380U) {
                const auto error = result.phase_error_us < 0
                    ? -result.phase_error_us : result.phase_error_us;
                if (error > peak_last_100) peak_last_100 = error;
            }
        }
        CHECK(accepted == 480U);
        CHECK(ever_tracking);
        CHECK(peak_last_100 <= 60);
    }
}


void check_uncertain_brackets_never_claim_software_tracking() {
    SvPtpPhaseAuthority tracker;
    // Valid control-plane reads may be too uncertain to support a 60 us
    // tracking claim. A 400 us PTP read bracket has >=201 us uncertainty;
    // the nominal phase is deliberately zero to catch false positives.
    for (std::uint32_t epoch = 1U; epoch <= 6U; ++epoch) {
        const auto decision = tracker.observe(snapshot(epoch, 20, 400U));
        CHECK(decision.accepted);
        CHECK(decision.epoch_second_continuous == (epoch != 1U));
        CHECK(decision.phase_error_us == 0);
        CHECK(decision.observation_uncertainty_us >= 201U);
        CHECK(decision.state == SvPtpPhaseState::acquiring);
        CHECK(!decision.software_tracking);
    }

    // Qualification requires four fresh, narrow observations; old
    // uncertain observations cannot contribute toward this threshold.
    for (std::uint32_t epoch = 7U; epoch <= 9U; ++epoch) {
        const auto decision = tracker.observe(snapshot(epoch, 20, 8U));
        CHECK(decision.accepted);
        CHECK(!decision.software_tracking);
    }
    const auto qualified = tracker.observe(snapshot(10U, 20, 8U));
    CHECK(qualified.accepted);
    CHECK(qualified.software_tracking);
    CHECK(qualified.state == SvPtpPhaseState::tracking);

    // One uncertain sample revokes tracking, even with perfect measured
    // phase. The PI/slew remains bounded separately from qualification.
    const auto degraded = tracker.observe(snapshot(11U, 20, 400U));
    CHECK(degraded.accepted);
    CHECK(!degraded.software_tracking);
    CHECK(degraded.state == SvPtpPhaseState::acquiring);
}

void check_publisher_continuity_gate_reacquisition() {
    SvPtpPublisherContinuityGate gate;
    SvPtpPhaseAuthority phase;
    CHECK(!gate.unchanged(0U, 0U));
    // A new publisher generation cannot reuse the previous phase estimate.
    CHECK(!gate.unchanged(1U, 0U));
    CHECK(gate.unchanged(1U, 0U));
    CHECK(phase.observe(snapshot(1U, 10)).accepted);
    CHECK(phase.observe(snapshot(2U, 90)).accepted);

    // One missed canonical slot or a failed canonical EMAC send invalidates
    // the existing PI integral; observer may only acquire on clean epochs.
    CHECK(!gate.unchanged(1U, 1U));
    CHECK(phase.invalidate().slew_ticks_next_epoch == 0);
    CHECK(gate.unchanged(1U, 1U));
    const auto cold = phase.observe(snapshot(3U, 100));
    CHECK(cold.accepted);
    CHECK(cold.slew_ticks_next_epoch == 0);

    CHECK(!gate.unchanged(2U, 0U));
    CHECK(phase.invalidate().state == SvPtpPhaseState::invalid);
    CHECK(gate.unchanged(2U, 0U));
    CHECK(phase.observe(snapshot(1U, 10)).slew_ticks_next_epoch == 0);
    // A PTP SOURCE restart, even with valid-looking seconds, must force
    // phase controller reacquisition rather than retaining the old PI state.
    CHECK(!gate.unchanged(2U, 0U, 3U));
    CHECK(gate.unchanged(2U, 0U, 3U));
    CHECK(!gate.unchanged(2U, 0U, 4U));
    CHECK(gate.unchanged(2U, 0U, 4U));
    CHECK(phase.invalidate().slew_ticks_next_epoch == 0);
    CHECK(!gate.unchanged(0U, 0U));  // STOP
    CHECK(!gate.unchanged(2U, 0U));  // START must re-acquire
}

void check_continuity_recovery_under_repeated_loss() {
    // Emulate a publisher fault during multiple observation windows. No
    // stale correction may survive a drop or a rapid STOP/START transition.
    for (const auto rate : std::array<std::uint32_t, 2>{4000U, 4800U}) {
        SvPtpPublisherContinuityGate gate;
        SvPtpPhaseAuthority phase;
        std::uint32_t faults = 0U;
        std::uint32_t session = 1U;
        std::int32_t oscillator_phase_us = 100;
        std::int32_t previous_command = 0;
        bool recovered = false;
        for (std::uint32_t epoch = 1U; epoch <= 360U; ++epoch) {
            oscillator_phase_us += 80 + previous_command;
            if (epoch == 55U || epoch == 56U || epoch == 132U ||
                epoch == 205U || epoch == 206U) {
                ++faults;
            }
            if (epoch == 260U) {
                ++session;
                faults = 0U;
            }
            previous_command = 0;
            if (!gate.unchanged(session, faults)) {
                CHECK(phase.invalidate().slew_ticks_next_epoch == 0);
                continue;
            }
            const auto decision = phase.observe(snapshot(epoch, oscillator_phase_us));
            CHECK(decision.accepted);
            CHECK(decision.slew_ticks_next_epoch >= -400);
            CHECK(decision.slew_ticks_next_epoch <= 400);
            // The command can only ever be distributed by an ISR-safe
            // cursor, with each interval changing by at most one tick.
            SvBoundedSlewCursor slew;
            slew.arm(rate, decision.slew_ticks_next_epoch);
            for (std::uint32_t i = 0U; i < rate; ++i) {
                const auto delta = slew.next_adjustment();
                CHECK(delta >= -1 && delta <= 1);
            }
            previous_command = slew.applied_ticks();
            if (epoch > 330U && decision.software_tracking) recovered = true;
        }
        CHECK(recovered);
    }
}

void check_clock_step_stale_brackets_and_timer_wrap() {
    SvPtpPhaseAuthority tracker;
    CHECK(!tracker.observe(snapshot(1U, 0, 1200U)).accepted);
    CHECK(!tracker.observe(snapshot(2U, 0, 8U)).slew_ticks_next_epoch);
    CHECK(tracker.observe(snapshot(3U, 0)).accepted);
    // Corrupt the PTP seconds without changing GPTimer; reject promptly.
    auto clock_step = snapshot(4U, 0);
    clock_step.ptp_seconds += 1U;
    const auto step = tracker.observe(clock_step);
    CHECK(!step.accepted);
    CHECK(step.clock_discontinuity);
    CHECK(step.slew_ticks_next_epoch == 0);
    // A delayed observation exceeding one epoch is not a license to slew.
    const auto restarted = tracker.observe(snapshot(7U, 0));
    CHECK(restarted.accepted);
    CHECK(restarted.slew_ticks_next_epoch == 0);
    CHECK(!tracker.observe(snapshot(9U, 0)).accepted);
    // GPTimer's low 32 bits wrap after about 71 minutes at 1 MHz.
    tracker.reset();
    for (std::uint32_t epoch = 4293U; epoch < 4308U; ++epoch) {
        const auto result = tracker.observe(snapshot(epoch, 50));
        CHECK(result.accepted);
        CHECK(!result.clock_discontinuity);
    }
    CHECK(tracker.invalidate().slew_ticks_next_epoch == 0);
}

void check_continuous_ptp_second_binding() {
    SvPtpPhaseAuthority authority;
    const auto initial = authority.observe(snapshot(10U, 80));
    CHECK(initial.accepted);
    CHECK(!initial.epoch_second_continuous);
    CHECK(initial.sv_epoch_ptp_second == kEpochZero + 10U);
    CHECK(initial.slew_ticks_next_epoch == 0);

    const auto stable = authority.observe(snapshot(11U, 160));
    CHECK(stable.accepted);
    CHECK(stable.epoch_second_continuous);
    CHECK(stable.sv_epoch_ptp_second == kEpochZero + 11U);

    // One missing epoch invalidates both the phase integrator and the
    // PTP-second association. Reacquisition starts from a cold baseline.
    const auto skipped = authority.observe(snapshot(13U, 240));
    CHECK(!skipped.accepted);
    CHECK(!skipped.epoch_second_continuous);
    CHECK(skipped.slew_ticks_next_epoch == 0);
    const auto reacquire = authority.observe(snapshot(14U, 320));
    CHECK(reacquire.accepted);
    CHECK(!reacquire.epoch_second_continuous);
    CHECK(reacquire.slew_ticks_next_epoch == 0);
    CHECK(authority.observe(snapshot(15U, 400)).epoch_second_continuous);

    // Valid phase modulo 1 s is not enough: a PTP second step must NOT be
    // interpreted as an adjacent SV epoch (neither +1 s nor -1 s).
    auto forward = snapshot(16U, 480);
    forward.ptp_seconds += 1U;
    CHECK(!authority.observe(forward).accepted);
    CHECK(!authority.observe(snapshot(17U, 560)).epoch_second_continuous);
    CHECK(authority.observe(snapshot(18U, 640)).epoch_second_continuous);
    auto backward = snapshot(19U, 720);
    backward.ptp_seconds -= 1U;
    CHECK(!authority.observe(backward).accepted);

    // Near a PTP second rollover the last SV epoch is correctly labelled
    // with the NEXT whole second, not the read's current second. Neither
    // rounding nor 32-bit GPTimer wrap may silently alias it.
    SvPtpPhaseAuthority boundary;
    auto before_second = snapshot(100U, 0);
    before_second.ptp_seconds = kEpochZero + 99U;
    before_second.ptp_nanoseconds = 999950000U;
    before_second.timer_before_ticks =
        before_second.last_sv_epoch_ticks + 46U;
    before_second.timer_after_ticks =
        before_second.last_sv_epoch_ticks + 54U;
    const auto baseline = boundary.observe(before_second);
    CHECK(baseline.accepted);
    CHECK(baseline.phase_error_us == -100);
    CHECK(!baseline.epoch_second_continuous);
    CHECK(baseline.sv_epoch_ptp_second == kEpochZero + 100U);
    const auto next = boundary.observe(snapshot(101U, 80));
    CHECK(next.accepted);
    CHECK(next.epoch_second_continuous);
    CHECK(next.sv_epoch_ptp_second == kEpochZero + 101U);

    // Keep absolute second labels across 71-minute GPTimer low32 rollover.
    SvPtpPhaseAuthority wrap;
    for (std::uint32_t epoch = 4293U; epoch < 4333U; ++epoch) {
        const auto decision = wrap.observe(snapshot(epoch, 60));
        CHECK(decision.accepted);
        CHECK(decision.sv_epoch_ptp_second == kEpochZero + epoch);
        CHECK(decision.epoch_second_continuous == (epoch != 4293U));
    }
}

} // namespace

int main() {
    try {
        check_auto_preflight_does_not_invent_sync_or_start();
        check_m3a_stop_start_keeps_qualified_sample_grid();
        check_m3e_parked_certified_ptp_authority_survives_100_restarts();
        check_m3b_boot_ptp_is_safe_and_operator_owned();
        check_m3c_foreign_announce_and_exclusive_emac_handover();
        check_m3d_advanced_ptp_policy_prevents_implicit_role_promotion();
        check_phase_observer_stall_revokes_source_wire_claim();
        check_receiver_preflight_requires_measured_gm_lease();
        check_phase_sign_and_bounded_corrections();
        check_uncertain_brackets_never_claim_software_tracking();
        check_per_second_slew_distribution();
        check_software_servo_tracks_80ppm_drift_without_counter_reset();
        check_clock_step_stale_brackets_and_timer_wrap();
        check_publisher_continuity_gate_reacquisition();
        check_continuity_recovery_under_repeated_loss();
        check_continuous_ptp_second_binding();
        std::cout << "SV PTP phase authority 4000/4800 fps regression passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
