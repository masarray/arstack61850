// SPDX-License-Identifier: GPL-3.0-or-later

#include "ptp_lab_task.hpp"
#include "ariec61850/time_sync/ptp_advanced_policy.hpp"
#include "ptp_receiver_task.hpp"
#include "live_control.hpp"
#include "smp_synch_lab.hpp"

#include "ariec61850/time_sync/ptp_discipline.hpp"
#include "ariec61850/time_sync/ptp_canonical_authority.hpp"
#include "ariec61850/time_sync/ptp_emac_role_ownership.hpp"
#include "ariec61850/time_sync/ptp_control_cancellation.hpp"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "sdkconfig.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>

extern "C" {
void ar_ptp_source_get_default_config(ar_ptp_lab_config_t* config);
bool ar_ptp_source_configure(const ar_ptp_lab_config_t* config);
void ar_ptp_source_start(esp_eth_handle_t eth_handle);
void ar_ptp_source_stop(void);
void ar_ptp_source_request_link_down_stop(void);
bool ar_ptp_source_is_running(void);
bool ar_ptp_source_runtime_active(void);
uint32_t ar_ptp_source_generation(void);
bool ar_ptp_source_get_status(ar_ptp_lab_status_t* status);
}

namespace ar::esp32p4::smv {
namespace {

using ar::iec61850::time_sync::PtpClockDiscipline;
using ar::iec61850::time_sync::PtpDisciplineOptions;

constexpr char kTag[] = "ar_ptp_mode";
portMUX_TYPE g_ptp_facade_mux = portMUX_INITIALIZER_UNLOCKED;
ar_ptp_lab_config_t g_facade_override{};
bool g_facade_override_valid = false;
std::atomic_bool g_receiver_stop_pending{false};
// Cancel only automatic boot attempts; operator PTP actions are authoritative.
std::atomic_bool g_boot_source_autostart_allowed{true};
std::atomic<std::uint32_t> g_ptp_policy_generation{1U};
// Compiling external-GM arbitration must NEVER silently opt the relay
// test-set into handover. SOURCE stays its own GM across every SV STOP/START.
// AUTO_PREFER_EXTERNAL is an explicit, operator-selected runtime policy;
// the Kconfig option is a capability gate, NOT the power-on policy.
std::atomic<ar_ptp_advanced_mode_t> g_advanced_ptp_mode{
    AR_PTP_POLICY_INTERNAL_GM_ONLY};
ar::iec61850::time_sync::PtpControlCancellationGate g_role_control;

void stop_active_ptp_roles() noexcept;

struct RoleControlClaim final {
    bool held{g_role_control.try_acquire()};
    [[nodiscard]] bool finish() noexcept {
        if (!held) return false;
        held = false;
        return g_role_control.finish(stop_active_ptp_roles);
    }
    ~RoleControlClaim() noexcept {
        if (held) static_cast<void>(g_role_control.finish(stop_active_ptp_roles));
    }
    RoleControlClaim(const RoleControlClaim&) = delete;
    RoleControlClaim& operator=(const RoleControlClaim&) = delete;
    RoleControlClaim() = default;
};


void fill_p2_defaults(ar_ptp_lab_config_t& config) noexcept {
    config.role = static_cast<ar_ptp_role_t>(CONFIG_AR_PTP_OPERATING_ROLE);
    config.pdelay_request_interval_ms = static_cast<std::uint32_t>(CONFIG_AR_PTP_PDELAY_REQUEST_INTERVAL_MS);
    config.maximum_path_delay_ns = static_cast<std::int64_t>(CONFIG_AR_PTP_MAX_PATH_DELAY_NS);
    config.maximum_path_delay_jitter_ns = static_cast<std::int64_t>(CONFIG_AR_PTP_MAX_PATH_JITTER_NS);
    config.lock_offset_threshold_ns = static_cast<std::int64_t>(CONFIG_AR_PTP_LOCK_OFFSET_NS);
    config.unlock_offset_threshold_ns = static_cast<std::int64_t>(CONFIG_AR_PTP_UNLOCK_OFFSET_NS);
    config.phase_step_threshold_ns = static_cast<std::int64_t>(CONFIG_AR_PTP_PHASE_STEP_THRESHOLD_NS);
    config.lock_required_samples = static_cast<std::uint32_t>(CONFIG_AR_PTP_LOCK_REQUIRED_SAMPLES);
    config.sync_timeout_ms = static_cast<std::uint32_t>(CONFIG_AR_PTP_SYNC_TIMEOUT_MS);
    config.holdover_timeout_ms = static_cast<std::uint32_t>(CONFIG_AR_PTP_HOLDOVER_TIMEOUT_MS);
    config.maximum_frequency_adjustment_ppb = static_cast<std::int32_t>(CONFIG_AR_PTP_MAX_FREQ_ADJ_PPB);
}

[[nodiscard]] PtpDisciplineOptions discipline_options(const ar_ptp_lab_config_t& config) noexcept {
    PtpDisciplineOptions options;
    options.maximum_path_delay_ns = config.maximum_path_delay_ns;
    options.maximum_path_delay_jitter_ns = config.maximum_path_delay_jitter_ns;
    options.lock_offset_threshold_ns = config.lock_offset_threshold_ns;
    options.unlock_offset_threshold_ns = config.unlock_offset_threshold_ns;
    options.phase_step_threshold_ns = config.phase_step_threshold_ns;
    options.lock_required_samples = config.lock_required_samples;
    options.maximum_frequency_adjustment_ppb = config.maximum_frequency_adjustment_ppb;
    options.sync_timeout = std::chrono::milliseconds{config.sync_timeout_ms};
    options.holdover_timeout = std::chrono::milliseconds{config.holdover_timeout_ms};
    return options;
}

[[nodiscard]] bool valid_public_config(const ar_ptp_lab_config_t& config) noexcept {
    if (config.role != AR_PTP_ROLE_LAB_SOURCE && config.role != AR_PTP_ROLE_TIME_RECEIVER && config.role != AR_PTP_ROLE_MONITOR) return false;
    if (config.role != AR_PTP_ROLE_TIME_RECEIVER) return true;
    if (config.pdelay_request_interval_ms < 100U || config.pdelay_request_interval_ms > 10'000U) return false;
    return PtpClockDiscipline::validate_options(discipline_options(config));
}

[[nodiscard]] ar_ptp_lab_config_t default_config() noexcept {
    ar_ptp_lab_config_t config{};
    ar_ptp_source_get_default_config(&config);
    fill_p2_defaults(config);
    return config;
}

[[nodiscard]] ar_ptp_lab_config_t selected_config() noexcept {
    ar_ptp_lab_config_t config{};
    bool has_override = false;
    portENTER_CRITICAL(&g_ptp_facade_mux);
    has_override = g_facade_override_valid;
    if (has_override) config = g_facade_override;
    portEXIT_CRITICAL(&g_ptp_facade_mux);
    return has_override ? config : default_config();
}

[[nodiscard]] bool receiver_stop_cleanup_pending() noexcept {
    if (!g_receiver_stop_pending.load(std::memory_order_acquire)) return false;
    if (ptp_receiver_runtime_active()) return true;
    g_receiver_stop_pending.store(false, std::memory_order_release);
    return false;
}

// Ownership is derived from live workers, never from a stale selected role.
void stop_active_ptp_roles() noexcept {
    // Operator STOP or role handoff revokes the transmitted AUTO local-sync
    // claim immediately, independent of the slower phase observer cadence.
    smp_synch_set_source_local_qualified(
        false, ar::esp32p4::smv::SvSyncLossReason::ptp_stop);
    smp_synch_set_receiver_timeline_qualified(
        false, ar::esp32p4::smv::SvSyncLossReason::ptp_stop);
    if (ar_ptp_source_runtime_active()) {
        ar_ptp_source_stop();
    }
    if (ptp_receiver_runtime_active()) {
        g_receiver_stop_pending.store(true, std::memory_order_release);
        ptp_receiver_stop();
    }
}

[[nodiscard]] bool emac_role_free() noexcept {
    const bool pending = receiver_stop_cleanup_pending();
    return ar::iec61850::time_sync::can_change_ptp_emac_role({
        ar_ptp_source_runtime_active(),
        ptp_receiver_runtime_active(),
        pending,
    });
}

[[nodiscard]] const char* role_name(const ar_ptp_role_t role) noexcept {
    switch (role) {
    case AR_PTP_ROLE_LAB_SOURCE: return "SOURCE";
    case AR_PTP_ROLE_TIME_RECEIVER: return "RECEIVER";
    case AR_PTP_ROLE_MONITOR: return "MONITOR";
    }
    return "UNKNOWN";
}

[[nodiscard]] const char* discipline_name(const ar_ptp_discipline_state_t state) noexcept {
    switch (state) {
    case AR_PTP_DISCIPLINE_UNLOCKED: return "UNLOCKED";
    case AR_PTP_DISCIPLINE_ACQUIRING: return "ACQUIRING";
    case AR_PTP_DISCIPLINE_LOCKED: return "LOCKED";
    case AR_PTP_DISCIPLINE_HOLDOVER: return "HOLDOVER";
    case AR_PTP_DISCIPLINE_FAULT: return "FAULT";
    }
    return "FAULT";
}

[[nodiscard]] ar::iec61850::time_sync::PtpCanonicalAuthoritySnapshot
canonical_ptp_authority_status(const ar_ptp_lab_status_t& status) noexcept {
    using namespace ar::iec61850::time_sync;
    PtpCanonicalAuthorityEvidence e{};
    e.role = static_cast<PtpAuthorityRole>(status.role);
    e.runtime_ready = status.is_running;
    e.generation = status.timing_authority_generation;
    e.clock_usable_claim = status.timing_authority_clock_usable;
    e.selected_source = status.source_selected;
    std::copy_n(status.selected_source_clock_identity, 8U,
                e.source_clock_identity.begin());
    e.source_port = status.selected_source_port_number;
    e.measured_discipline_locked =
        status.discipline_state == AR_PTP_DISCIPLINE_LOCKED;
    e.globally_traceable_claim = status.globally_traceable;
    return canonical_ptp_authority(e);
}

void log_p2_status(const ar_ptp_lab_status_t& status) noexcept {
    char source[32]{};
    if (status.source_selected) {
        std::snprintf(source, sizeof(source), "%02X%02X%02X%02X%02X%02X%02X%02X/%u",
            static_cast<unsigned>(status.selected_source_clock_identity[0]), static_cast<unsigned>(status.selected_source_clock_identity[1]),
            static_cast<unsigned>(status.selected_source_clock_identity[2]), static_cast<unsigned>(status.selected_source_clock_identity[3]),
            static_cast<unsigned>(status.selected_source_clock_identity[4]), static_cast<unsigned>(status.selected_source_clock_identity[5]),
            static_cast<unsigned>(status.selected_source_clock_identity[6]), static_cast<unsigned>(status.selected_source_clock_identity[7]),
            static_cast<unsigned>(status.selected_source_port_number));
    } else {
        std::snprintf(source, sizeof(source), "NONE");
    }

    char offset[32]{};
    char path[32]{};
    char jitter[32]{};
    char measured[8]{};
    if (status.offset_valid) {
        std::snprintf(offset, sizeof(offset), "%lld", static_cast<long long>(status.offset_from_master_ns));
    } else {
        std::snprintf(offset, sizeof(offset), "NA");
    }
    if (status.mean_path_delay_valid) {
        std::snprintf(path, sizeof(path), "%lld", static_cast<long long>(status.mean_path_delay_ns));
    } else {
        std::snprintf(path, sizeof(path), "NA");
    }
    if (status.path_delay_jitter_valid) {
        std::snprintf(jitter, sizeof(jitter), "%lld", static_cast<long long>(status.path_delay_jitter_ns));
    } else {
        std::snprintf(jitter, sizeof(jitter), "NA");
    }
    if (status.measured_smp_synch_valid) {
        std::snprintf(measured, sizeof(measured), "%u", static_cast<unsigned>(status.measured_smp_synch));
    } else {
        std::snprintf(measured, sizeof(measured), "NA");
    }

    ESP_LOGI(kTag,
        "PTP2 role=%s discipline=%s source=%s offset=%s path=%s jitter=%s freq=%ld global=%u measured=%s rxAnnounce=%llu rxSync=%llu rxFollowUp=%llu rxPdelay=%llu pdelayReq=%llu accepted=%llu rejected=%llu",
        role_name(status.role), discipline_name(status.discipline_state), source, offset, path, jitter,
        static_cast<long>(status.frequency_adjustment_ppb), status.globally_traceable ? 1U : 0U, measured,
        static_cast<unsigned long long>(status.announce_received), static_cast<unsigned long long>(status.sync_received),
        static_cast<unsigned long long>(status.follow_up_received), static_cast<unsigned long long>(status.peer_delay_frames_observed),
        static_cast<unsigned long long>(status.peer_delay_requests_sent), static_cast<unsigned long long>(status.accepted_discipline_samples),
        static_cast<unsigned long long>(status.rejected_discipline_samples));
}

} // namespace
} // namespace ar::esp32p4::smv

extern "C" void ar_ptp_boot_supervisor_operator_override(void) {
    ar::esp32p4::smv::g_boot_source_autostart_allowed.store(
        false, std::memory_order_release);
}

extern "C" bool ar_ptp_boot_supervisor_autostart_allowed(void) {
    return ar::esp32p4::smv::g_boot_source_autostart_allowed.load(
        std::memory_order_acquire);
}

extern "C" const char* ar_ptp_lab_advanced_policy_name(
    ar_ptp_advanced_mode_t mode) {
    switch (mode) {
    case AR_PTP_POLICY_INTERNAL_GM_ONLY: return "INTERNAL_GM_ONLY";
    case AR_PTP_POLICY_AUTO_PREFER_EXTERNAL: return "AUTO_PREFER_EXTERNAL";
    case AR_PTP_POLICY_EXTERNAL_ONLY: return "EXTERNAL_ONLY";
    case AR_PTP_POLICY_MONITOR_ONLY: return "MONITOR_ONLY";
    }
    return "INVALID";
}

extern "C" ar_ptp_advanced_mode_t ar_ptp_lab_advanced_policy(void) {
    return ar::esp32p4::smv::g_advanced_ptp_mode.load(
        std::memory_order_acquire);
}

extern "C" uint32_t ar_ptp_lab_advanced_policy_generation(void) {
    return ar::esp32p4::smv::g_ptp_policy_generation.load(
        std::memory_order_acquire);
}

extern "C" bool ar_ptp_lab_set_advanced_policy(
    const ar_ptp_advanced_mode_t requested) {
    using ar::iec61850::time_sync::PtpAdvancedMode;
    using ar::iec61850::time_sync::PtpAdvancedPolicy;
    if (!PtpAdvancedPolicy::valid(
            static_cast<PtpAdvancedMode>(requested))) return false;
    // Shared EMAC owner arbitration: never reconfigure SOURCE under a
    // receiver, even if a worker START/STOP is still being cleaned up.
    ar::esp32p4::smv::RoleControlClaim transaction{};
    if (!transaction.held || !ar::esp32p4::smv::emac_role_free() ||
        ar::esp32p4::smv::live_tx_running())
        return false;
    auto config = ar::esp32p4::smv::selected_config();
    const bool previous_source = config.role == AR_PTP_ROLE_LAB_SOURCE;
    config.role = requested == AR_PTP_POLICY_MONITOR_ONLY
        ? AR_PTP_ROLE_MONITOR
        : requested == AR_PTP_POLICY_EXTERNAL_ONLY
            ? AR_PTP_ROLE_TIME_RECEIVER : AR_PTP_ROLE_LAB_SOURCE;
    // An explicit SOURCE->SOURCE policy selection must not silently
    // downgrade a still-valid bench GM to LOCAL. Freshness is checked again
    // by seed_hardware_clock() when the PTP worker actually starts.
    const bool keep_seed = PtpAdvancedPolicy::may_preserve_bench_seed(
        static_cast<PtpAdvancedMode>(requested), previous_source,
        config.bench_global_gm, config.bench_utc_epoch_seconds,
        config.bench_utc_epoch_millisecond, config.bench_epoch_received_mono_us,
        esp_timer_get_time());
    if (!keep_seed) {
        config.bench_global_gm = false;
        config.bench_utc_epoch_seconds = 0U;
        config.bench_utc_epoch_millisecond = 0U;
        config.bench_epoch_received_mono_us = 0LL;
#if defined(CONFIG_AR_PTP_BOOT_SIMULATED_GLOBAL_GM) && CONFIG_AR_PTP_BOOT_SIMULATED_GLOBAL_GM
        if (PtpAdvancedPolicy::starts_source(
                static_cast<PtpAdvancedMode>(requested))) {
            // New SOURCE policy after external/monitor/expired authority:
            // acquire a FRESH lab seed instead of resurrecting stale UTC.
            const auto lab_default = ar::esp32p4::smv::default_config();
            config.bench_global_gm = lab_default.bench_global_gm;
            config.bench_utc_epoch_seconds = lab_default.bench_utc_epoch_seconds;
            config.bench_utc_epoch_millisecond =
                lab_default.bench_utc_epoch_millisecond;
            config.bench_epoch_received_mono_us =
                lab_default.bench_epoch_received_mono_us;
        }
#endif
    }
    if (!ar::esp32p4::smv::valid_public_config(config) ||
        !ar_ptp_source_configure(&config)) return false;
    portENTER_CRITICAL(&ar::esp32p4::smv::g_ptp_facade_mux);
    if (!ar::esp32p4::smv::emac_role_free() ||
        ar::esp32p4::smv::live_tx_running()) {
        portEXIT_CRITICAL(&ar::esp32p4::smv::g_ptp_facade_mux);
        return false;
    }
    ar::esp32p4::smv::g_facade_override = config;
    ar::esp32p4::smv::g_facade_override_valid = true;
    // PTP POLICY explicitly gives background authority to the selected
    // bench mode. Later PTP STOP or CONFIG disables it until another POLICY.
    ar::esp32p4::smv::g_advanced_ptp_mode.store(
        requested, std::memory_order_release);
    ar::esp32p4::smv::g_boot_source_autostart_allowed.store(
        true, std::memory_order_release);
    // Wake supervisory policy reset with a monotonic generation;
    // old ready_once and handover-failure flags cannot survive a new mode.
    ar::esp32p4::smv::g_ptp_policy_generation.fetch_add(
        1U, std::memory_order_release);
    portEXIT_CRITICAL(&ar::esp32p4::smv::g_ptp_facade_mux);
    return transaction.finish();
}

extern "C" void ar_ptp_lab_get_default_config(ar_ptp_lab_config_t* config) {
    if (config == nullptr) return;
    *config = ar::esp32p4::smv::default_config();
}

extern "C" bool ar_ptp_lab_configure(const ar_ptp_lab_config_t* config) {
    if (config == nullptr) return false;
    ar::esp32p4::smv::RoleControlClaim transaction{};
    if (!transaction.held || !ar::esp32p4::smv::emac_role_free() ||
        !ar::esp32p4::smv::valid_public_config(*config)) return false;
    if (!ar_ptp_source_configure(config)) return false;
    portENTER_CRITICAL(&ar::esp32p4::smv::g_ptp_facade_mux);
    if (!ar::esp32p4::smv::emac_role_free()) {
        portEXIT_CRITICAL(&ar::esp32p4::smv::g_ptp_facade_mux);
        return false;
    }
    ar::esp32p4::smv::g_facade_override = *config;
    ar::esp32p4::smv::g_facade_override_valid = true;
    portEXIT_CRITICAL(&ar::esp32p4::smv::g_ptp_facade_mux);
    return transaction.finish();
}

extern "C" bool ar_ptp_lab_get_config(ar_ptp_lab_config_t* config) {
    if (config == nullptr) return false;
    *config = ar::esp32p4::smv::selected_config();
    return true;
}

extern "C" bool ar_ptp_lab_try_start(const esp_eth_handle_t eth_handle) {
    if (eth_handle == nullptr) return false;
    ar::esp32p4::smv::RoleControlClaim transaction{};
    if (!transaction.held || !ar::esp32p4::smv::emac_role_free()) return false;

    const auto config = ar::esp32p4::smv::selected_config();
    if (!ar::esp32p4::smv::valid_public_config(config)) {
        ESP_LOGE(ar::esp32p4::smv::kTag,
                 "PTP start rejected: invalid role/cadence/discipline thresholds");
        return false;
    }

    bool started = false;
    if (config.role == AR_PTP_ROLE_LAB_SOURCE) {
        if (!ar_ptp_source_configure(&config)) return false;
        ar_ptp_source_start(eth_handle);
        started = ar_ptp_source_is_running();
    } else {
        started = ar::esp32p4::smv::ptp_receiver_start(eth_handle, config);
    }
    // STOP can race with the bounded blocking START: it must win the
    // cancellation handshake before START may report READY to its caller.
    const bool uncancelled = transaction.finish();
    return started && uncancelled;
}

extern "C" void ar_ptp_lab_start(const esp_eth_handle_t eth_handle) {
    static_cast<void>(ar_ptp_lab_try_start(eth_handle));
}

extern "C" void ar_ptp_lab_request_link_down_stop(void) {
    // A verified PHY-down is independent of Studio/USB connection. Prevent
    // the boot PTP supervisor from re-admitting a stopped role on cable replug.
    // Explicit operator PTP START is still the recovery authority.
    ar_ptp_boot_supervisor_operator_override();
    ar::esp32p4::smv::smp_synch_set_source_local_qualified(
        false, ar::esp32p4::smv::SvSyncLossReason::ethernet_link);
    ar::esp32p4::smv::smp_synch_set_receiver_timeline_qualified(
        false, ar::esp32p4::smv::SvSyncLossReason::ethernet_link);
    // Both workers use atomic stop flags, polled by the role-owner task.
    // Do not invoke blocking 500-ms STOP/cleanup or task-handle notifications
    // in the Ethernet event callback.
    ar_ptp_source_request_link_down_stop();
    ar::esp32p4::smv::ptp_receiver_request_link_down_stop();
}

extern "C" void ar_ptp_lab_stop(void) {
    // A PTP stop must revoke both live SV timing claims BEFORE asynchronous
    // task teardown can complete, including external-GM receiver claims.
    ar::esp32p4::smv::smp_synch_set_source_local_qualified(
        false, ar::esp32p4::smv::SvSyncLossReason::ptp_stop);
    ar::esp32p4::smv::smp_synch_set_receiver_timeline_qualified(
        false, ar::esp32p4::smv::SvSyncLossReason::ptp_stop);
    // Publish the cancellation immediately. If START/config owns the
    // slot, that owner drains STOP at the atomic release boundary.
    if (ar::esp32p4::smv::g_role_control.request_stop_and_try_acquire()) {
        static_cast<void>(ar::esp32p4::smv::g_role_control.finish(
            ar::esp32p4::smv::stop_active_ptp_roles));
    }
}

extern "C" bool ar_ptp_lab_is_running(void) {
    return !ar::esp32p4::smv::g_role_control.stop_requested() &&
        (ar_ptp_source_is_running() || ar::esp32p4::smv::ptp_receiver_is_running());
}

extern "C" uint32_t ar_ptp_lab_source_generation(void) {
    return !ar::esp32p4::smv::g_role_control.stop_requested() &&
        ar_ptp_source_is_running() ? ar_ptp_source_generation() : 0U;
}

namespace ar::esp32p4::smv {
namespace {

// Same quiet authority for serial/GUI status and clock-lease consumers.
// Verify SOURCE generation on both sides of status reading; no stale
// generation may be paired with a new START's ready flag.
[[nodiscard]] bool collect_ptp_status(ar_ptp_lab_status_t& status) noexcept {
    status = {};
    bool ok = true;
    if (ar_ptp_source_runtime_active()) {
        const auto before = ar_ptp_source_generation();
        ok = ar_ptp_source_get_status(&status);
        const auto after = ar_ptp_source_generation();
        status.role = AR_PTP_ROLE_LAB_SOURCE;
        status.discipline_state = AR_PTP_DISCIPLINE_UNLOCKED;
        // Operator RUNNING is physical runtime ownership, not a timing LOCK.
        // Freshness may lapse while the SOURCE task continues to emit frames.
        // A transient freshness miss must revoke its clock lease WITHOUT
        // telling Studio that a still-active transmitter has stopped.
        const bool source_clock_qualified = ok && status.is_running &&
            ar_ptp_source_is_running() && before != 0U && before == after;
        status.is_running = ok && ar_ptp_source_runtime_active();
        status.timing_authority_generation = source_clock_qualified ? after : 0U;
        status.timing_authority_clock_usable = source_clock_qualified;
    } else if (ptp_receiver_runtime_active()) {
        ok = ptp_receiver_get_status(status);
    } else {
        status.role = selected_config().role;
        status.discipline_state = AR_PTP_DISCIPLINE_UNLOCKED;
    }
    if (!ok) return false;
    if (g_role_control.stop_requested()) {
        status.is_running = false;
        status.timing_authority_clock_usable = false;
        status.globally_traceable = false;
        status.measured_smp_synch_valid = false;
        status.offset_valid = false;
        status.timing_authority_generation = 0U;
    }
    const auto lease = canonical_ptp_authority_status(status);
    status.timing_authority_clock_usable = lease.clock_usable;
    status.globally_traceable = lease.globally_traceable;
    return true;
}

} // namespace
} // namespace ar::esp32p4::smv

extern "C" bool ar_ptp_lab_get_timing_authority(
    ar_ptp_timing_authority_t* const authority) {
    if (authority == nullptr) return false;
    *authority = {};
    ar_ptp_lab_status_t status{};
    if (!ar::esp32p4::smv::collect_ptp_status(status)) return false;
    const auto lease = ar::esp32p4::smv::canonical_ptp_authority_status(status);
    authority->role = status.role;
    authority->generation = lease.generation;
    authority->clock_usable = lease.clock_usable;
    authority->source_selected = lease.selected_source;
    std::copy(lease.source_clock_identity.begin(),
              lease.source_clock_identity.end(),
              authority->selected_source_clock_identity);
    authority->selected_source_port_number = lease.source_port;
    authority->globally_traceable = lease.globally_traceable;
    authority->sv_phase_qualified =
        (lease.role == ar::iec61850::time_sync::PtpAuthorityRole::source &&
         ar::esp32p4::smv::smp_synch_source_local_qualified()) ||
        (lease.role == ar::iec61850::time_sync::PtpAuthorityRole::receiver &&
         ar::esp32p4::smv::smp_synch_receiver_timeline_qualified());
    return true;
}

extern "C" bool ar_ptp_lab_get_status(ar_ptp_lab_status_t* const status) {
    if (status == nullptr) return false;
    const bool result = ar::esp32p4::smv::collect_ptp_status(*status);
    if (result) ar::esp32p4::smv::log_p2_status(*status);
    return result;
}
