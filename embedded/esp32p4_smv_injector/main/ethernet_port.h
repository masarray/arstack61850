// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "esp_eth.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bring up the current ESP32-P4 Ethernet development board's onboard RMII PHY
 * and internal EMAC using the bench-validated pin mapping.
 *
 * The implementation intentionally lives in a C translation unit. ESP-IDF 5.5.x
 * exposes several Ethernet default-config macros using C designated initializers;
 * keeping that ABI-facing board adapter in C avoids leaking version-sensitive IDF
 * initialization details into the portable C++ publisher code.
 */
esp_eth_handle_t ar_esp32p4_eth_init(void);

/** Restart the optional PTP lab runtime on the initialized Ethernet device. */
bool ar_esp32p4_ptp_start(void);

/** Last PTP START transport preflight failure, or NULL (no failure). */
const char *ar_esp32p4_ptp_preflight_reason(void);

/** Snapshot of actual ESP-IDF driver and physical PHY link state in PTP SHOW. */
void ar_esp32p4_ptp_print_link_status(void);

#ifdef __cplusplus
}
#endif
