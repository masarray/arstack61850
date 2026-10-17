// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>

namespace ar::iec61850::time_sync {

// Pure, constant-space projection of the canonical runtime status. A PTP
// clock lease is NOT an SV phase/egress certificate.
enum class PtpAuthorityRole : std::uint8_t {
    source = 0U, receiver = 1U, monitor = 2U,
};
struct PtpCanonicalAuthorityEvidence final {
    PtpAuthorityRole role{PtpAuthorityRole::monitor};
    bool runtime_ready{};
    std::uint32_t generation{};
    bool clock_usable_claim{};
    bool selected_source{};
    std::array<std::uint8_t, 8> source_clock_identity{};
    std::uint16_t source_port{};
    bool measured_discipline_locked{};
    bool globally_traceable_claim{};
};
struct PtpCanonicalAuthoritySnapshot final {
    PtpAuthorityRole role{PtpAuthorityRole::monitor};
    std::uint32_t generation{};
    bool clock_usable{};
    bool selected_source{};
    std::array<std::uint8_t, 8> source_clock_identity{};
    std::uint16_t source_port{};
    bool globally_traceable{};
    // Separate SV phase/egress qualification has not been implemented.
    // Never infer it from PTP lock or traceability.
    bool sv_phase_qualified{};
};
[[nodiscard]] constexpr PtpCanonicalAuthoritySnapshot
canonical_ptp_authority(const PtpCanonicalAuthorityEvidence& e) noexcept {
    PtpCanonicalAuthoritySnapshot result{};
    result.role = e.role;
    result.generation = e.generation;
    const bool live_lease = e.runtime_ready && e.generation != 0U &&
                            e.clock_usable_claim;
    if (e.role == PtpAuthorityRole::source) {
        // A ready LAB_SOURCE clock does not imply globally traceable time.
        result.clock_usable = live_lease;
    } else if (e.role == PtpAuthorityRole::receiver) {
        result.selected_source = e.runtime_ready && e.selected_source;
        if (result.selected_source) {
            result.source_clock_identity = e.source_clock_identity;
            result.source_port = e.source_port;
        }
        result.clock_usable = live_lease && result.selected_source &&
                              e.measured_discipline_locked;
        result.globally_traceable = result.clock_usable &&
                                    e.globally_traceable_claim;
    }
    return result;  // MONITOR and unknown roles always fail closed.
}

} // namespace ar::iec61850::time_sync
