// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ptp_lab_task.hpp"

namespace ar::esp32p4::smv {

/**
 * Start TIME_RECEIVER or MONITOR and return only after the hardware timestamp
 * receive path is ready. Returns false on setup failure or readiness timeout.
 */
[[nodiscard]] bool ptp_receiver_start(
    esp_eth_handle_t eth_handle,
    const ar_ptp_lab_config_t& config) noexcept;

void ptp_receiver_stop() noexcept;

/** True only after hardware PTP + RX timestamp callback readiness completes. */
[[nodiscard]] bool ptp_receiver_is_running() noexcept;

[[nodiscard]] bool ptp_receiver_get_status(ar_ptp_lab_status_t& status) noexcept;

} // namespace ar::esp32p4::smv
