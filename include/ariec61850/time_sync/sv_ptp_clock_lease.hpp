// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ariec61850/time_sync/ptp_canonical_authority.hpp"
#include <array>
#include <cstdint>

namespace ar::iec61850::time_sync {

// A source-scoped *clock* lease, not a PPS capture, SV egress certificate,
// or permission to set smpSynch. The role is part of the identity: SOURCE
// generation 3 and RECEIVER generation 3 are never interchangeable.
struct SvPtpClockLease final {
    PtpAuthorityRole role{PtpAuthorityRole::monitor};
    std::uint32_t generation{};
    bool clock_usable{};
    bool source_selected{};
    std::array<std::uint8_t, 8> source_clock_identity{};
    std::uint16_t source_port{};
};

// One canonical snapshot is the unit of authority. Never piece together
// generation, source identity, and locked bit from separate status reads.
[[nodiscard]] constexpr bool valid_sv_ptp_clock_lease(
    const SvPtpClockLease& lease) noexcept {
    if (!lease.clock_usable || lease.generation == 0U) return false;
    if (lease.role == PtpAuthorityRole::source) return true;
    if (lease.role != PtpAuthorityRole::receiver ||
        !lease.source_selected || lease.source_port == 0U) return false;
    bool nonzero_identity = false;
    for (const auto byte : lease.source_clock_identity) {
        nonzero_identity = nonzero_identity || byte != 0U;
    }
    return nonzero_identity;
}

// An epoch is admissible only while the exact same source-scoped lease is
// observed *before and after* its PTP clock read and at control handoff.
// Loss of LOCKED, GM switch, role swap and step/relock generation always fail.
[[nodiscard]] constexpr bool same_sv_ptp_clock_lease(
    const SvPtpClockLease& expected,
    const SvPtpClockLease& observed) noexcept {
    if (!valid_sv_ptp_clock_lease(expected) ||
        !valid_sv_ptp_clock_lease(observed) ||
        expected.role != observed.role ||
        expected.generation != observed.generation) return false;
    if (expected.role == PtpAuthorityRole::source) return true;
    return expected.source_selected == observed.source_selected &&
           expected.source_port == observed.source_port &&
           expected.source_clock_identity == observed.source_clock_identity;
}

} // namespace ar::iec61850::time_sync
