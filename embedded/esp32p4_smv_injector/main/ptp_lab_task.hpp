// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "esp_eth.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ar_ptp_lab_config_t {
    uint8_t transport_specific;
    uint8_t domain_number;
    bool vlan_enabled;
    uint16_t vlan_id;
    uint8_t vlan_priority;
    bool clock_identity_override;
    uint8_t clock_identity[8];
    uint16_t port_number;
    uint32_t announce_interval_ms;
    uint32_t sync_interval_ms;
    bool respond_to_peer_delay;
    uint8_t priority1;
    uint8_t priority2;
    uint8_t clock_class;
    uint8_t clock_accuracy;
    uint16_t offset_scaled_log_variance;
    uint8_t time_source;
    int16_t current_utc_offset;
} ar_ptp_lab_config_t;

typedef struct ar_ptp_lab_status_t {
    bool is_running;
    uint64_t announce_sent;
    uint64_t sync_sent;
    uint64_t follow_up_sent;
    uint64_t peer_delay_frames_sent;
    uint64_t tx_failure_count;
} ar_ptp_lab_status_t;

/** Fill a configuration snapshot from the firmware's Kconfig defaults. */
void ar_ptp_lab_get_default_config(ar_ptp_lab_config_t* config);

/**
 * Apply a runtime profile while PTP is stopped.
 * Returns false when the profile is invalid or the task is running.
 */
bool ar_ptp_lab_configure(const ar_ptp_lab_config_t* config);

/** Return the currently selected runtime profile (override or Kconfig default). */
bool ar_ptp_lab_get_config(ar_ptp_lab_config_t* config);

/**
 * Start the optional PTPv2 Layer-2 laboratory timing companion.
 *
 * This is an isolated troubleshooting/interoperability helper, not a GPS-backed
 * or certified grandmaster. The task runs independently from the realtime SV path.
 */
void ar_ptp_lab_start(esp_eth_handle_t eth_handle);

/** Request a clean stop of the PTP lab task without touching the SV hot path. */
void ar_ptp_lab_stop(void);

/** Return whether the PTP lab task currently owns an active runtime. */
bool ar_ptp_lab_is_running(void);

/** Obtain a lock-free live status snapshot suitable for a future GUI/control plane. */
bool ar_ptp_lab_get_status(ar_ptp_lab_status_t* status);

#ifdef __cplusplus
}
#endif
