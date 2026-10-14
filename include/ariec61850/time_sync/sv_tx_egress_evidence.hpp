// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace ar::iec61850::time_sync {

// Diagnostic evidence ONLY. Measured TX descriptor timestamps are from the
// ESP32-P4 EMAC PTP clock; no assumption of externally disciplined time.
// A canonical one-ASDU smpCnt=0 is required for the profile being measured.
// No result from this type independently authorizes smpSynch=1/2.
struct SvTxEgressObservation final {
    std::uint16_t smp_cnt{};
    std::uint32_t sample_epoch_generation{};
    std::uint32_t scheduled_alarm_ticks_low32{};
    std::uint32_t hw_ptp_seconds{};
    std::uint32_t hw_ptp_nanoseconds{};
    std::uint32_t submit_duration_us{};
    bool canonical_tx_success{};
    bool no_missed_slots{};
    bool same_sample_epoch{};
};

enum class SvTxEvidenceResult : std::uint8_t {
    measured,
    not_a_qualified_epoch,
    transmit_failed,
    missing_hw_timestamp,
};

struct SvTxEgressSummary final {
    std::uint32_t attempted{};
    std::uint32_t measured{};
    std::uint32_t rejected_epoch{};
    std::uint32_t failed_transmit{};
    std::uint32_t missing_hw_timestamp{};
    std::int32_t min_phase_ns{std::numeric_limits<std::int32_t>::max()};
    std::int32_t max_phase_ns{std::numeric_limits<std::int32_t>::min()};
    std::int64_t phase_sum_ns{};
    std::uint32_t max_submit_duration_us{};
    std::uint32_t last_ptp_seconds{};
    std::uint32_t last_alarm_ticks_low32{};
    std::uint32_t last_epoch_generation{};
    std::int32_t last_phase_ns{};
};

// Constant-space accumulator: intended to be called only on deliberately
// sparse diagnostic frames. A positive phase is late after a PTP whole-second
// boundary, negative is before it, measured AT MAC egress. This is NOT the
// GPTimer deadline->MAC latency (two different clocks), nor a GM quality test.
class SvTxEgressEvidence final {
public:
    constexpr void reset() noexcept { summary_ = {}; }

    [[nodiscard]] constexpr SvTxEvidenceResult record(
        const SvTxEgressObservation& o) noexcept {
        ++summary_.attempted;
        // Descriptor timestamp acquisition may itself block/timeout. Retain
        // its maximum duration even when hardware evidence is unavailable.
        summary_.max_submit_duration_us =
            std::max(summary_.max_submit_duration_us, o.submit_duration_us);
        if (!o.canonical_tx_success) {
            ++summary_.failed_transmit;
            return SvTxEvidenceResult::transmit_failed;
        }
        if (o.smp_cnt != 0U || o.sample_epoch_generation == 0U ||
            !o.no_missed_slots || !o.same_sample_epoch) {
            ++summary_.rejected_epoch;
            return SvTxEvidenceResult::not_a_qualified_epoch;
        }
        if (o.hw_ptp_seconds == 0U ||
            o.hw_ptp_nanoseconds >= 1'000'000'000U ||
            (o.hw_ptp_seconds == 0U && o.hw_ptp_nanoseconds == 0U)) {
            ++summary_.missing_hw_timestamp;
            return SvTxEvidenceResult::missing_hw_timestamp;
        }
        const std::int32_t phase_ns = o.hw_ptp_nanoseconds < 500'000'000U
            ? static_cast<std::int32_t>(o.hw_ptp_nanoseconds)
            : static_cast<std::int32_t>(
                static_cast<std::int64_t>(o.hw_ptp_nanoseconds) -
                1'000'000'000LL);
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
    SvTxEgressSummary summary_{};
};

} // namespace ar::iec61850::time_sync
