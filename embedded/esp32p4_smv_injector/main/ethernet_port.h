// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "esp_eth.h"

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

#ifdef __cplusplus
}
#endif
