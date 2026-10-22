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
    // Isolated test-set mode: GNSS-like Grandmaster on the local EMAC clock.
    // The PC supplies an approximate UTC epoch, not a physical GPS input.
    bool bench_global_gm;
    uint32_t bench_utc_epoch_seconds;
    // Millisecond fraction of the same UTC second. Sending seconds alone
    // previously introduced up to 999 ms of avoidable clock seed error.
    uint16_t bench_utc_epoch_millisecond;
    int64_t bench_epoch_received_mono_us;

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
    // Clock authority lease only; neither field proves SMV/PPS phase.
    uint32_t timing_authority_generation;
    bool timing_authority_clock_usable;
} ar_ptp_lab_status_t;

/** Fill a configuration snapshot from the firmware's Kconfig defaults. */
void ar_ptp_lab_get_default_config(ar_ptp_lab_config_t* config);

/**
 * Apply a runtime profile while PTP is stopped.
 * Returns false when the profile is invalid or the task is running.
 */
bool ar_ptp_lab_configure(const ar_ptp_lab_config_t* config);

/** Stable, allocation-free control-plane reason for last rejected profile.
 * Return value remains readable after the failed CONFIG command. */
const char* ar_ptp_lab_last_config_error(void);

/** Return the currently selected runtime profile (override or Kconfig default). */
bool ar_ptp_lab_get_config(ar_ptp_lab_config_t* config);

/**
 * Checked control-path start.
 *
 * Returns true only when this call accepted and established the selected
 * runtime. Returns false for invalid configuration, a runtime already active,
 * a receiver stop still being cleaned up, or hardware/task startup failure.
 */
bool ar_ptp_lab_try_start(esp_eth_handle_t eth_handle);

/**
 * Legacy start entry point retained for P1.5 source ABI compatibility.
 * New user-facing control paths should call ar_ptp_lab_try_start().
 */
void ar_ptp_lab_start(esp_eth_handle_t eth_handle);

/** Request a clean stop of the PTP task without touching the SV hot path. */
void ar_ptp_lab_stop(void);

// Physical Ethernet Announce only (matching transport/domain/VLAN, non-self).
// This callback observes candidates; it must NEVER claim receiver LOCKED.
void ar_ptp_lab_observe_external_announce(
    const uint8_t source_clock[8], const uint8_t grandmaster_clock[8],
    uint16_t source_port, uint8_t clock_class, uint16_t steps_removed,
    uint64_t received_ms);

typedef struct ar_ptp_external_failover_status_t {
    bool enabled;
    bool candidate_fresh;
    uint32_t stable_announces;
    uint64_t handovers;
    uint64_t returns_to_source;
    uint64_t failures;
    bool switching;
} ar_ptp_external_failover_status_t;

// Operator opt-in. Only valid when PTP SOURCE is physically running.
// A failure cannot promote smpSynch or reconfigure the EMAC clock implicitly.
bool ar_ptp_lab_set_external_failover(bool enabled);
bool ar_ptp_lab_get_external_failover_status(ar_ptp_external_failover_status_t* status);

/** Return whether the selected PTP runtime is currently ready/running. */
bool ar_ptp_lab_is_running(void);

/** Nonzero only for a currently ready SOURCE. Increments when PTP SOURCE
 * becomes ready after START or after a clock/TX failure; not traceability. */
uint32_t ar_ptp_lab_source_generation(void);

/** True only for a fresh, active, bench-emulated GM with hardware PTP TX.
 * The SV phase/MAC qualification is checked separately for smpSynch=2. */
bool ar_ptp_lab_bench_global_ready(void);

/** Read-only SOURCE startup result. Persists after failed task teardown and
 * never changes clock authority, SV lock gating or transmission scheduling. */
const char* ar_ptp_source_last_start_reason(void);

// Role-tagged clock lease. Not an SV phase or egress certificate.
typedef struct ar_ptp_timing_authority_t {
    ar_ptp_role_t role;
    uint32_t generation;
    bool clock_usable;
    bool source_selected;
    uint8_t selected_source_clock_identity[8];
    uint16_t selected_source_port_number;
    bool globally_traceable;
    bool sv_phase_qualified;
} ar_ptp_timing_authority_t;

/** Quiet fail-closed status projection for the PTP clock authority. */
bool ar_ptp_lab_get_timing_authority(ar_ptp_timing_authority_t* authority);

/** Obtain a coherent live status snapshot for serial/GUI observability. */
bool ar_ptp_lab_get_status(ar_ptp_lab_status_t* status);

#ifdef __cplusplus
}
#endif
