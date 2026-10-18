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
    static constexpr std::uint32_t kMaxSoftwarePhaseBoundUs = 4U;
    static constexpr std::uint32_t kEvidenceStride = kSvTxEvidenceEpochStride;
    static constexpr std::uint32_t kMaxEvidenceAgeEpochs = kEvidenceStride + 1U;
    static constexpr std::uint32_t kRequiredMeasuredPairs = 2U;

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

} // namespace ar::iec61850::time_sync
