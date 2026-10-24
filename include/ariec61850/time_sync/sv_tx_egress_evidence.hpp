// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace ar::iec61850::time_sync {

// One actual canonical descriptor timestamp every two smpCnt=0 epochs.
// Baseline at ~2s, two coherent pairs at ~4s and ~6s permit AUTO to
// qualify within the 10s operator target IF independent software phase,
// PTP source and publisher continuity have also qualified. Never force 1.
inline constexpr std::uint32_t kSvTxEvidenceEpochStride = 2U;

// Sparse, read-only MAC descriptor TX evidence. No result authorizes
// smpSynch=1/2, grandmaster quality or IED synchronization.
struct SvTxEgressObservation final {
    std::uint16_t smp_cnt{};
    std::uint32_t sample_epoch_generation{};
    std::uint32_t publisher_session_generation{};
    std::uint32_t publisher_fault_generation{};
    std::uint32_t ptp_source_generation{};
    std::uint32_t scheduled_alarm_ticks_low32{};
    std::uint32_t hw_ptp_seconds{};
    std::uint32_t hw_ptp_nanoseconds{};
    std::uint32_t submit_duration_us{};
    bool canonical_tx_success{};
    bool no_missed_slots{};
    bool same_sample_epoch{};
};

enum class SvTxEvidenceResult : std::uint8_t {
    measured,                    // two consecutive coherent sparse epochs
    baseline_established,        // first observation or controlled reacquisition
    not_a_qualified_epoch,
    transmit_failed,
    missing_hw_timestamp,
    publisher_discontinuity,     // stop/restart/fault or missed epoch sequence
    ptp_clock_discontinuity,     // hardware PTP time stepped/skipped seconds
};

struct SvTxEgressSummary final {
    std::uint32_t attempted{};
    std::uint32_t baseline_established{};
    std::uint32_t measured{};
    std::uint32_t rejected_epoch{};
    std::uint32_t failed_transmit{};
    std::uint32_t missing_hw_timestamp{};
    std::uint32_t publisher_discontinuities{};
    std::uint32_t ptp_clock_discontinuities{};
    std::uint32_t ptp_source_changes{};
    std::int32_t min_phase_ns{std::numeric_limits<std::int32_t>::max()};
    std::int32_t max_phase_ns{std::numeric_limits<std::int32_t>::min()};
    std::int64_t phase_sum_ns{};
    std::uint32_t max_submit_duration_us{};
    std::uint32_t last_ptp_seconds{};
    std::uint32_t last_alarm_ticks_low32{};
    std::uint32_t last_epoch_generation{};
    std::int32_t last_phase_ns{};
};

// Evidence cadence follows the shared firmware stride. Readings are NOT
// inferred from packet rate or PCAP timestamps.
// No heap, locks, I/O or clocks; suitable for strict portable unit testing.
class SvTxEgressEvidence final {
public:
    explicit constexpr SvTxEgressEvidence(
        const std::uint32_t expected_epoch_stride = kSvTxEvidenceEpochStride) noexcept
        : expected_stride_(expected_epoch_stride) {}

    constexpr void reset() noexcept {
        summary_ = {};
        invalidate_baseline();
    }

    [[nodiscard]] constexpr SvTxEvidenceResult record(
        const SvTxEgressObservation& o) noexcept {
        ++summary_.attempted;
        summary_.max_submit_duration_us =
            std::max(summary_.max_submit_duration_us, o.submit_duration_us);
        if (!o.canonical_tx_success) {
            ++summary_.failed_transmit;
            invalidate_baseline();
            return SvTxEvidenceResult::transmit_failed;
        }
        if (o.smp_cnt != 0U || o.sample_epoch_generation == 0U ||
            o.publisher_session_generation == 0U ||
            o.ptp_source_generation == 0U ||
            !o.no_missed_slots || !o.same_sample_epoch ||
            expected_stride_ == 0U) {
            ++summary_.rejected_epoch;
            invalidate_baseline();
            return SvTxEvidenceResult::not_a_qualified_epoch;
        }
        if (o.hw_ptp_seconds == 0U ||
            o.hw_ptp_nanoseconds >= 1'000'000'000U) {
            ++summary_.missing_hw_timestamp;
            invalidate_baseline();
            return SvTxEvidenceResult::missing_hw_timestamp;
        }

        // At >0.5s we are closest to the NEXT whole-second boundary.
        // unsigned 64-bit prevents unintended wrap of the driver 32-bit field.
        const std::uint64_t nearest_ptp_second =
            static_cast<std::uint64_t>(o.hw_ptp_seconds) +
            (o.hw_ptp_nanoseconds >= 500'000'000U ? 1U : 0U);
        const std::int32_t phase_ns = o.hw_ptp_nanoseconds < 500'000'000U
            ? static_cast<std::int32_t>(o.hw_ptp_nanoseconds)
            : static_cast<std::int32_t>(
                static_cast<std::int64_t>(o.hw_ptp_nanoseconds) -
                1'000'000'000LL);

        if (!have_baseline_) {
            remember(o, nearest_ptp_second);
            ++summary_.baseline_established;
            return SvTxEvidenceResult::baseline_established;
        }
        const bool publisher_continuous =
            o.publisher_session_generation == previous_session_ &&
            o.publisher_fault_generation == previous_fault_generation_ &&
            o.sample_epoch_generation - previous_epoch_generation_ ==
                expected_stride_;
        if (!publisher_continuous) {
            ++summary_.publisher_discontinuities;
            remember(o, nearest_ptp_second); // reacquire; do not claim phase
            ++summary_.baseline_established;
            return SvTxEvidenceResult::publisher_discontinuity;
        }
        if (o.ptp_source_generation != previous_ptp_source_generation_) {
            ++summary_.ptp_source_changes;
            remember(o, nearest_ptp_second);
            ++summary_.baseline_established;
            return SvTxEvidenceResult::ptp_clock_discontinuity;
        }
        if (nearest_ptp_second <= previous_nearest_ptp_second_ ||
            nearest_ptp_second - previous_nearest_ptp_second_ !=
                static_cast<std::uint64_t>(expected_stride_)) {
            ++summary_.ptp_clock_discontinuities;
            remember(o, nearest_ptp_second); // rebase after clock step
            ++summary_.baseline_established;
            return SvTxEvidenceResult::ptp_clock_discontinuity;
        }

        remember(o, nearest_ptp_second);
        ++summary_.measured;
        summary_.min_phase_ns = std::min(summary_.min_phase_ns, phase_ns);
        summary_.max_phase_ns = std::max(summary_.max_phase_ns, phase_ns);
        summary_.phase_sum_ns += phase_ns;
        summary_.last_ptp_seconds = o.hw_ptp_seconds;
        summary_.last_alarm_ticks_low32 = o.scheduled_alarm_ticks_low32;
        summary_.last_epoch_generation = o.sample_epoch_generation;
        summary_.last_phase_ns = phase_ns;
        return SvTxEvidenceResult::measured;
    }

    [[nodiscard]] constexpr const SvTxEgressSummary& summary() const noexcept {
        return summary_;
    }
private:
    constexpr void invalidate_baseline() noexcept {
        have_baseline_ = false;
        previous_nearest_ptp_second_ = 0U;
        previous_epoch_generation_ = 0U;
        previous_session_ = 0U;
        previous_fault_generation_ = 0U;
        previous_ptp_source_generation_ = 0U;
    }
    constexpr void remember(const SvTxEgressObservation& o,
                            const std::uint64_t nearest) noexcept {
        have_baseline_ = true;
        previous_nearest_ptp_second_ = nearest;
        previous_epoch_generation_ = o.sample_epoch_generation;
        previous_session_ = o.publisher_session_generation;
        previous_fault_generation_ = o.publisher_fault_generation;
        previous_ptp_source_generation_ = o.ptp_source_generation;
    }

    std::uint32_t expected_stride_{16U};
    bool have_baseline_{};
    std::uint64_t previous_nearest_ptp_second_{};
    std::uint32_t previous_epoch_generation_{};
    std::uint32_t previous_session_{};
    std::uint32_t previous_fault_generation_{};
    std::uint32_t previous_ptp_source_generation_{};
    SvTxEgressSummary summary_{};
};

// Sparse MAC egress is a corroborating measurement, NOT sufficient by itself
// to declare synchronized samples. The independent software phase observer
// must ALSO verify smpCnt-zero PTP epoch continuity with a bounded clock-read
// uncertainty. Keep this gate independent of ESP-IDF/Qt for fault regression.
class SvLocalSyncEvidenceGate final {
public:
    // MAC TX occurs AFTER the sampling instant: message serialization,
    // descriptor submission and Ethernet queuing take nonzero time.
    // SV SmpCnt=0 is aligned by the software/hardware SAMPLE observer,
    // not by demanding that an outgoing Ethernet frame hit that instant.
    // Bound the measured canonical TX delay to one nominal 4kHz slot.
    // A small negative tolerance accommodates sub-4us sample-phase error.
    static constexpr std::int64_t kMinCanonicalTxOffsetNs = -4000LL;
    static constexpr std::int64_t kMaxCanonicalTxOffsetNs = 250000LL;
    // SOURCE shares the ESP32-P4 EMAC PTP oscillator with the SV GPTimer.
    // 2026-10-22 hardware capture: phase -3..+1 us plus 2..3 us PTP
    // read-bracket uncertainty; a 4-us combined threshold repeatedly
    // revoked smpSynch 2 despite zero dropped/reordered samples and a
    // continuous GM. Only SOURCE gets this bounded measurement margin;
    // EXTERNAL RECEIVER retains the original strict 4-us criterion.
    // 12 us is <5% of one 250-us 4 kHz SV sampling period.
    static constexpr std::uint32_t kMaxSoftwarePhaseBoundUs = 4U;
    static constexpr std::uint32_t kMaxSourceSoftwarePhaseBoundUs = 12U;
    static constexpr std::uint32_t kEvidenceStride = kSvTxEvidenceEpochStride;
    static constexpr std::uint32_t kMaxEvidenceAgeEpochs = kEvidenceStride + 1U;
    static constexpr std::uint32_t kRequiredMeasuredPairs = 2U;

    // SOURCE and SV use the same local IEEE1588 EMAC clock. One *measured*
    // coherent MAC TX pair is enough to prove that the canonical publisher
    // uses that clock; the independent phase observer still must report
    // continuous software tracking. Keep two pairs for external RECEIVER.
    static constexpr std::uint32_t kSourceRequiredMeasuredPairs = 1U;

    constexpr void reset() noexcept {
        matched_pairs_ = 0U;
        session_ = 0U;
        fault_generation_ = 0U;
        source_generation_ = 0U;
        last_epoch_ = 0U;
    }

    // Call only for the actual canonical SV descriptor timestamp.
    // The phase here is the packet's TX offset from the nearest whole PTP
    // second, i.e. a bound on *publication latency*, NOT sampling phase.
    // A separate GPTimer/EMAC observer qualifies the sampling instant.
    // The lower-level tracker verifies consecutive evidenced PTP epochs
    // and the same source/session.
    constexpr void observe(const SvTxEgressObservation& o,
                           const SvTxEvidenceResult result,
                           const std::int32_t phase_ns) noexcept {
        const std::int64_t phase = static_cast<std::int64_t>(phase_ns);
        if (result != SvTxEvidenceResult::measured ||
            !o.canonical_tx_success || !o.no_missed_slots ||
            !o.same_sample_epoch ||
            phase > kMaxCanonicalTxOffsetNs ||
            phase < kMinCanonicalTxOffsetNs) {
            reset();
            return;
        }
        if (matched_pairs_ != 0U &&
            (session_ != o.publisher_session_generation ||
             fault_generation_ != o.publisher_fault_generation ||
             source_generation_ != o.ptp_source_generation ||
             o.sample_epoch_generation - last_epoch_ != kEvidenceStride)) {
            reset();
        }
        session_ = o.publisher_session_generation;
        fault_generation_ = o.publisher_fault_generation;
        source_generation_ = o.ptp_source_generation;
        last_epoch_ = o.sample_epoch_generation;
        if (matched_pairs_ < kRequiredMeasuredPairs) ++matched_pairs_;
    }

    // Diagnostic read under the same ownership/critical section as observe().
    // This is a count of consecutive measured, coherent descriptor pairs,
    // not a hardware-phase certificate or proof of IED subscription.
    [[nodiscard]] constexpr std::uint32_t measured_pairs() const noexcept {
        return matched_pairs_;
    }

    [[nodiscard]] constexpr bool qualified_source_fast(
        const std::uint32_t session,
        const std::uint32_t fault_generation,
        const std::uint32_t source_generation,
        const std::uint32_t current_epoch,
        const bool software_tracking,
        const std::uint32_t software_phase_bound_us) const noexcept {
        return matched_pairs_ >= kSourceRequiredMeasuredPairs &&
            software_tracking &&
            software_phase_bound_us <= kMaxSourceSoftwarePhaseBoundUs &&
            session != 0U && source_generation != 0U &&
            session == session_ &&
            fault_generation == fault_generation_ &&
            source_generation == source_generation_ &&
            current_epoch >= last_epoch_ &&
            current_epoch - last_epoch_ <= kMaxEvidenceAgeEpochs;
    }

    [[nodiscard]] constexpr bool qualified(
        const std::uint32_t session,
        const std::uint32_t fault_generation,
        const std::uint32_t source_generation,
        const std::uint32_t current_epoch,
        const bool software_tracking,
        const std::uint32_t software_phase_bound_us) const noexcept {
        return matched_pairs_ >= kRequiredMeasuredPairs &&
            software_tracking &&
            software_phase_bound_us <= kMaxSoftwarePhaseBoundUs &&
            session != 0U && source_generation != 0U &&
            session == session_ &&
            fault_generation == fault_generation_ &&
            source_generation == source_generation_ &&
            current_epoch >= last_epoch_ &&
            current_epoch - last_epoch_ <= kMaxEvidenceAgeEpochs;
    }
private:
    std::uint32_t matched_pairs_{};
    std::uint32_t session_{};
    std::uint32_t fault_generation_{};
    std::uint32_t source_generation_{};
    std::uint32_t last_epoch_{};
};

// Fast internal SOURCE lock: no external servo or network clock is involved.
// Require *two consecutive* measured phase/PTP epochs AND at least one
// genuine coherent MAC descriptor pair from that same source/session.
// This cannot be enabled by a UI smpSynch=2 override; it takes REAL
// independent phase and canonical Ethernet TX evidence.
class SvSourceSelfClockAcquisition final {
public:
    static constexpr std::uint32_t kRequiredQualifiedLiveEpochs = 2U;

    constexpr void reset() noexcept {
        clean_epochs_ = 0U;
        session_ = 0U;
        faults_ = 0U;
        source_ = 0U;
        epoch_ = 0U;
    }

    [[nodiscard]] constexpr bool observe(
        const bool phase_qualified, const bool canonical_mac_qualified,
        const std::uint32_t session, const std::uint32_t faults,
        const std::uint32_t source, const std::uint32_t epoch) noexcept {
        if (!phase_qualified || !canonical_mac_qualified ||
            session == 0U || source == 0U || epoch == 0U) {
            reset();
            return false;
        }
        if (clean_epochs_ != 0U &&
            (session != session_ || faults != faults_ ||
             source != source_ || epoch - epoch_ != 1U)) {
            reset();
            return false; // Never promote on a discontinuity epoch.
        }
        session_ = session;
        faults_ = faults;
        source_ = source;
        epoch_ = epoch;
        if (clean_epochs_ < kRequiredQualifiedLiveEpochs) ++clean_epochs_;
        return clean_epochs_ == kRequiredQualifiedLiveEpochs;
    }

    [[nodiscard]] constexpr std::uint32_t clean_epochs() const noexcept {
        return clean_epochs_;
    }

private:
    std::uint32_t clean_epochs_{};
    std::uint32_t session_{};
    std::uint32_t faults_{};
    std::uint32_t source_{};
    std::uint32_t epoch_{};
};

// Fail-closed LOCAL entry filter. A COLD start demands ten consecutive
// genuinely transmitted, phase+canonical-MAC-qualified SV epochs. A WARM
// preflight may earn phase-only history while SV TX is disabled; it cannot
// advertise synchronization. After START, WARM promotion still requires three
// consecutive live phase+MAC-qualified epochs, from this very publisher
// session, with no lost epoch, lease mutation, or publisher fault.
//
// Earlier field evidence showed a premature one-second LOCAL claim when
// *only* three live epochs were used. Warm promotion is categorically
// different: it requires ten *additional* measured, consecutive preflight
// phase epochs before the three live epochs. No preflight packet is invented,
// and any known invalid clock, discontinuity, or loss of established MAC
// evidence revokes LOCAL immediately.
class SvLocalRefAcquisition final {
public:
    static constexpr std::uint32_t kRequiredCleanEpochs = 10U;
    static constexpr std::uint32_t kWarmRequiredLiveEpochs = 3U;
    // Enough time for the sparse (every-two-epoch) MAC descriptor pair
    // baseline, but stale preflight is never useful indefinitely.
    static constexpr std::uint32_t kMaxWarmMacAcquisitionEpochs = 8U;

    constexpr void reset() noexcept {
        count_ = 0U;
        preflight_epochs_ = 0U;
        live_age_ = 0U;
        session_ = 0U;
        faults_ = 0U;
        source_ = 0U;
        epoch_ = 0U;
        had_mac_evidence_ = false;
    }

    // Called ONLY with real 1Hz GPTimer/PTP phase observations while the
    // publisher is OFF and its preflight timer remains armed. No MAC evidence
    // or smpSynch claim is implied.
    constexpr void observe_preflight(
        const bool phase_qualified, const std::uint32_t session,
        const std::uint32_t faults, const std::uint32_t source,
        const std::uint32_t epoch) noexcept {
        if (!phase_qualified || session == 0U || source == 0U || epoch == 0U) {
            reset();
            return;
        }
        if (count_ != 0U ||
            (preflight_epochs_ != 0U && !same_epoch_identity(
                session, faults, source, epoch))) {
            reset();
        }
        save_epoch_identity(session, faults, source, epoch);
        if (preflight_epochs_ < kRequiredCleanEpochs) ++preflight_epochs_;
    }

    // Phase continuity and canonical MAC evidence are independent. During
    // the first few live epochs a healthy MAC observer must establish its
    // descriptor baseline; missing *initial* evidence cannot be mistaken
    // for established evidence being revoked. A previously valid MAC gate
    // becoming invalid is immediately fail-closed.
    [[nodiscard]] constexpr bool observe_live(
        const bool phase_qualified, const bool canonical_mac_qualified,
        const std::uint32_t session, const std::uint32_t faults,
        const std::uint32_t source, const std::uint32_t epoch) noexcept {
        if (!phase_qualified || session == 0U || source == 0U || epoch == 0U) {
            reset();
            return false;
        }
        if ((count_ != 0U || preflight_epochs_ != 0U || live_age_ != 0U) &&
            !same_epoch_identity(session, faults, source, epoch)) {
            reset();
        }
        save_epoch_identity(session, faults, source, epoch);
        if (live_age_ < kMaxWarmMacAcquisitionEpochs + 1U) ++live_age_;
        if (live_age_ > kMaxWarmMacAcquisitionEpochs)
            preflight_epochs_ = 0U;

        if (!canonical_mac_qualified) {
            if (had_mac_evidence_) reset();
            return false;
        }
        had_mac_evidence_ = true;
        if (count_ < kRequiredCleanEpochs) ++count_;
        return count_ == kRequiredCleanEpochs ||
            (preflight_epochs_ == kRequiredCleanEpochs &&
             live_age_ <= kMaxWarmMacAcquisitionEpochs &&
             count_ >= kWarmRequiredLiveEpochs);
    }

    // Compatibility contract for callers with an already-combined canonical
    // phase+MAC boolean: unchanged ten-live-epoch fail-closed behavior.
    [[nodiscard]] constexpr bool observe(
        const bool qualified, const std::uint32_t session,
        const std::uint32_t faults, const std::uint32_t source,
        const std::uint32_t epoch) noexcept {
        return observe_live(qualified, qualified, session, faults, source, epoch);
    }

    [[nodiscard]] constexpr std::uint32_t clean_epochs() const noexcept {
        return count_;
    }
    [[nodiscard]] constexpr std::uint32_t preflight_epochs() const noexcept {
        return preflight_epochs_;
    }
    [[nodiscard]] constexpr std::uint32_t live_age_epochs() const noexcept {
        return live_age_;
    }

private:
    [[nodiscard]] constexpr bool same_epoch_identity(
        const std::uint32_t session, const std::uint32_t faults,
        const std::uint32_t source, const std::uint32_t epoch) const noexcept {
        return session_ == session && faults_ == faults &&
            source_ == source && epoch - epoch_ == 1U;
    }
    constexpr void save_epoch_identity(
        const std::uint32_t session, const std::uint32_t faults,
        const std::uint32_t source, const std::uint32_t epoch) noexcept {
        session_ = session;
        faults_ = faults;
        source_ = source;
        epoch_ = epoch;
    }

    std::uint32_t count_{};
    std::uint32_t preflight_epochs_{};
    std::uint32_t live_age_{};
    std::uint32_t session_{};
    std::uint32_t faults_{};
    std::uint32_t source_{};
    std::uint32_t epoch_{};
    bool had_mac_evidence_{};
};

} // namespace ar::iec61850::time_sync
