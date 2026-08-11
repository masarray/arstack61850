// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "esp_eth.h"

namespace ar::esp32p4::smv {

/**
 * Start the optional PTPv2 Layer-2 laboratory broadcaster.
 *
 * This is a troubleshooting/interoperability timing companion, not a GPS-backed
 * or certified grandmaster. The task is independent from the realtime SV hot path.
 */
void ptp_lab_start(esp_eth_handle_t eth_handle);

} // namespace ar::esp32p4::smv
