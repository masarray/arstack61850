// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/time_sync/ptp.hpp"
#include "ariec61850/time_sync/ptp_external_failover.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace ar::iec61850::time_sync {

// SOURCE hardware RX admission, independent of any GUI or selected role.
// This matches the receiver's precise VLAN/domain/transport evidence fence;
// other VLANs, untagged frames, and a self Announce cannot authorize takeover.
struct PtpExternalListenProfile final {
    std::uint8_t domain{};
    std::uint8_t transport_specific{};
    bool vlan_present{};
    std::uint16_t vlan_id{};
    std::array<std::uint8_t, 8> local_clock{};
};

[[nodiscard]] inline std::optional<PtpExternalCandidate>
ptp_external_candidate_from_frame(
    const PtpFrame& frame,
    const PtpExternalListenProfile& profile) noexcept {
    if (frame.header.message_type != PtpMessageType::announce ||
        !frame.announce.has_value() ||
        frame.header.domain_number != profile.domain ||
        frame.header.transport_specific != profile.transport_specific ||
        frame.outer_vlan_id.has_value() ||
        (profile.vlan_present
            ? (!frame.vlan_id.has_value() || *frame.vlan_id != profile.vlan_id)
            : frame.vlan_id.has_value()) ||
        frame.header.source_port_identity.clock_identity == profile.local_clock)
        return std::nullopt;

    PtpExternalCandidate candidate{};
    candidate.port_clock = frame.header.source_port_identity.clock_identity;
    candidate.port_number = frame.header.source_port_identity.port_number;
    candidate.grandmaster_clock = frame.announce->grandmaster_identity;
    candidate.clock_class = frame.announce->clock_class;
    candidate.steps_removed = frame.announce->steps_removed;
    if (candidate.port_number == 0U || candidate.clock_class == 255U ||
        candidate.steps_removed >= 255U) return std::nullopt;
    for (const auto* clock : {&candidate.port_clock, &candidate.grandmaster_clock}) {
        bool nonzero = false;
        for (const auto b : *clock) nonzero |= b != 0U;
        if (!nonzero) return std::nullopt;
    }
    return candidate;
}
} // namespace ar::iec61850::time_sync
