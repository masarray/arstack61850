// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace ar::iec61850::time_sync {

// Runtime ownership, not measured LOCK/READY. The EMAC PTP timestamp path
// is exclusive: neither role may take it while the other worker is starting,
// waiting for measured evidence, in holdover, or cleaning up after STOP.
struct PtpEmacRoleOwnership final {
    bool source_worker_active{};
    bool receiver_worker_active{};
    bool receiver_cleanup_pending{};
};

[[nodiscard]] constexpr bool can_change_ptp_emac_role(
    const PtpEmacRoleOwnership owners) noexcept {
    return !owners.source_worker_active &&
           !owners.receiver_worker_active &&
           !owners.receiver_cleanup_pending;
}

} // namespace ar::iec61850::time_sync
