// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/time_sync/smp_synch_policy.hpp"

#include <cstdint>
#include <optional>

namespace ar::iec61850::time_sync {

// A cross-clock observation is obtained OUTSIDE the SV hot path. The PTP
// time read is bracketed by a monotonic clock; half of that span bounds
// the unknown instant at which the EMAC clock was sampled.
struct SvPtpClockObservation final {
    std::uint32_t ptp_nanoseconds{};
    std::uint64_t monotonic_before_us{};
    std::uint64_t monotonic_after_us{};
};

struct SvSecondBoundaryArm final {
    std::uint32_t first_alarm_us{};
    std::uint32_t clock_read_uncertainty_us{};
};

// Plan only the first aligned sample. Subsequent GPTimer deadlines must be
// verified against the PTP clock: this calculation alone never licenses
// smpSynch=1/2 or makes GPTimer frequency-disciplined.
[[nodiscard]] constexpr std::optional<SvSecondBoundaryArm>
plan_sv_ptp_second_arm(const SvPtpClockObservation& o,
                       const std::uint32_t minimum_lead_us = 20000U,
                       const std::uint32_t maximum_read_span_us = 1000U) noexcept {
    constexpr std::uint64_t us_per_second = 1000000ULL;
    constexpr std::uint64_t ns_per_second = 1000000000ULL;
    if (o.ptp_nanoseconds >= ns_per_second ||
        o.monotonic_after_us < o.monotonic_before_us ||
        minimum_lead_us == 0U || minimum_lead_us > us_per_second) {
        return std::nullopt;
    }
    const std::uint64_t read_span_us =
        o.monotonic_after_us - o.monotonic_before_us;
    if (read_span_us > maximum_read_span_us) return std::nullopt;
    // Anchor to the midpoint of the EMAC IOCTL call and advance to the
    // monotonic timestamp captured after that call. All math is bounded.
    const std::uint64_t estimated_ptp_ns =
        (static_cast<std::uint64_t>(o.ptp_nanoseconds) +
         ((read_span_us + 1ULL) / 2ULL) * 1000ULL) % ns_per_second;
    const std::uint64_t uncertainty_us = (read_span_us + 1ULL) / 2ULL;
    const std::uint64_t until_next_us =
        (ns_per_second - estimated_ptp_ns + 999ULL) / 1000ULL;
    // Leave enough time for GPTimer setup and software latency. Starting
    // one epoch later is preferable to a late sample 0.
    const std::uint64_t first_us = until_next_us < minimum_lead_us
        ? until_next_us + us_per_second
        : until_next_us;
    if (first_us == 0U || first_us > 2ULL * us_per_second) return std::nullopt;
    return SvSecondBoundaryArm{
        static_cast<std::uint32_t>(first_us),
        static_cast<std::uint32_t>(uncertainty_us)};
}

// The clock receiver can prove PTP lock, but it cannot independently prove
// the SV publisher's sample phase. AUTO must remain 0 until BOTH domains
// have been qualified. Laboratory overrides remain deliberately separate.
[[nodiscard]] constexpr std::optional<SmpSynchValue>
qualified_sv_sync_value(const std::optional<SmpSynchValue> ptp_clock_sync,
                        const bool timeline_phase_qualified) noexcept {
    if (!timeline_phase_qualified) return std::nullopt;
    return ptp_clock_sync;
}

} // namespace ar::iec61850::time_sync
