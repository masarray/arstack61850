// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/time_sync/ptp_runtime.hpp"

#include <chrono>

namespace ar::iec61850::time_sync {

// Static configuration compatibility, NOT a declaration of IEEE/IEC
// conformance, grandmaster accuracy, or IED time lock. Runtime hardware
// timestamps, clock quality and Pdelay exchanges need independent evidence.
struct PtpPowerProfileCadence final {
    bool announce_1hz{};
    bool sync_1hz{};
    bool peer_delay_supported{};

    [[nodiscard]] constexpr bool compatible() const noexcept {
        return announce_1hz && sync_1hz && peer_delay_supported;
    }
};

// IEC/IEEE 61850-9-3:2016 defaults for a singly-attached L2/P2P ordinary
// clock: logAnnounceInterval=0, logSyncInterval=0; Pdelay_Req=1s is the
// receiver's own setting. This function checks only publisher-exposed facts.
// Domain and VLAN are deliberately NOT hardcoded: they must match the network.
[[nodiscard]] constexpr PtpPowerProfileCadence inspect_ptp_power_profile_cadence(
    const PtpPublisherOptions& options) noexcept {
    using namespace std::chrono_literals;
    return {
        options.announce_interval == 1000ms,
        options.sync_interval == 1000ms,
        options.respond_to_peer_delay,
    };
}

} // namespace ar::iec61850::time_sync
