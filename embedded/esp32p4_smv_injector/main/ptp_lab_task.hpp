// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "esp_eth.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the optional PTPv2 Layer-2 laboratory timing companion.
 *
 * This is an isolated troubleshooting/interoperability helper, not a GPS-backed
 * or certified grandmaster. The task runs independently from the realtime SV path.
 */
void ar_ptp_lab_start(esp_eth_handle_t eth_handle);

#ifdef __cplusplus
}
#endif
