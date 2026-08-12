// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "esp_eth.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ar_ptp_role_t {
    AR_PTP_ROLE_LAB_SOURCE = 0,
    AR_PTP_ROLE_TIME_RECEIVER = 1,
    AR_PTP_ROLE_MONITOR = 2,
} ar_ptp_role_t;

typedef enum ar_ptp_discipline_state_t {
    AR_PTP_DISCIPLINE_UNLOCKED = 0,
    AR_PTP_DISCIPLINE_ACQUIRING = 1,
    AR_PTP_DISCIPLINE_LOCKED = 2,
    AR_PTP_DISCIPLINE_HOLDOVER = 3,
    AR_PTP_DISCIPLINE_FAULT = 4,
} ar_ptp_discipline_state_t;

typedef struct ar_ptp_lab_config_t {
    ar_ptp_role_t role;
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
    uint32_t pdelay_request_interval_ms;
    uint8_t priority1;
    uint8_t priority2;
    uint8_t clock_class;
    uint8_t clock_accuracy;
    uint16_t offset_scaled_log_variance;
    uint8_t time_source;
    int16_t current_utc_offset;

    // P2 measured discipline thresholds. These are used only in TIME_RECEIVER.
    int64_t maximum_path_delay_ns;
    int64_t maximum_path_delay_jitter_ns;
    int64_t lock_offset_threshold_ns;
    int64_t unlock_offset_threshold_ns;
    int64_t phase_step_threshold_ns;
    uint32_t lock_required_samples;
    uint32_t sync_timeout_ms;
    uint32_t holdover_timeout_ms;
    int32_t maximum_frequency_adjustment_ppb;
} ar_ptp_lab_config_t;

typedef struct ar_ptp_lab_status_t {
    bool is_running;
    ar_ptp_role_t role;
    uint64_t announce_sent;
    uint64_t sync_sent;
    uint64_t follow_up_sent;
    uint64_t peer_delay_frames_sent;
    uint64_t peer_delay_requests_sent;
    uint64_t tx_failure_count;

    uint64_t announce_received;
    uint64_t sync_received;
    uint64_t follow_up_received;
    // Raw matching-profile Pdelay_Req/Resp/Resp_Follow_Up frames observed on RX.
    // This remains meaningful in passive MONITOR mode where no exchange is owned.
    uint64_t peer_delay_frames_observed;
    // Responses accepted into an owned receiver exchange correlation path.
    uint64_t peer_delay_responses_received;

    ar_ptp_discipline_state_t discipline_state;
    bool source_selected;
    uint8_t selected_source_clock_identity[8];
    uint16_t selected_source_port_number;
    bool offset_valid;
    int64_t offset_from_master_ns;
    bool mean_path_delay_valid;
    int64_t mean_path_delay_ns;
    bool path_delay_jitter_valid;
    int64_t path_delay_jitter_ns;
    int32_t frequency_adjustment_ppb;
    uint64_t accepted_discipline_samples;
    uint64_t rejected_discipline_samples;
    bool globally_traceable;
    bool measured_smp_synch_valid;
    uint8_t measured_smp_synch;
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
 * Start the optional PTPv2 Layer-2 timing task.
 *
 * LAB_SOURCE is an isolated interoperability helper, TIME_RECEIVER disciplines
 * the ESP32-P4 IEEE1588 clock from an external PTP source, and MONITOR remains
 * passive. None of these modes is a GPS-backed or certified grandmaster claim.
 */
void ar_ptp_lab_start(esp_eth_handle_t eth_handle);

/** Request a clean stop of the PTP task without touching the SV hot path. */
void ar_ptp_lab_stop(void);

/** Return whether the PTP task currently owns an active runtime. */
bool ar_ptp_lab_is_running(void);

/** Obtain a coherent live status snapshot for serial/GUI observability. */
bool ar_ptp_lab_get_status(ar_ptp_lab_status_t* status);

#ifdef __cplusplus
}
#endif
