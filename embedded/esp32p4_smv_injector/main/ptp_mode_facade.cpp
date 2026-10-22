// SPDX-License-Identifier: GPL-3.0-or-later

#include "ptp_lab_task.hpp"
#include "ptp_receiver_task.hpp"
#include "smp_synch_lab.hpp"

#include "ariec61850/time_sync/ptp_discipline.hpp"
#include "ariec61850/time_sync/ptp_canonical_authority.hpp"
#include "ariec61850/time_sync/ptp_emac_role_ownership.hpp"
#include "ariec61850/time_sync/ptp_control_cancellation.hpp"
#include "ariec61850/time_sync/ptp_external_failover.hpp"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <array>
#include <cstdint>
#include <cstdio>

extern "C" {
void ar_ptp_source_get_default_config(ar_ptp_lab_config_t* config);
bool ar_ptp_source_configure(const ar_ptp_lab_config_t* config);
void ar_ptp_source_start(esp_eth_handle_t eth_handle);
void ar_ptp_source_stop(void);
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
ar::iec61850::time_sync::PtpControlCancellationGate g_role_control;

// The source RX callback never changes PTP ownership. One low-priority
// supervisor performs role transitions through the existing stop/config/start
// cancellation gate. Its policy is deterministic and requires actual incoming
// Ethernet Announce plus independently measured receiver LOCK.
portMUX_TYPE g_external_mux = portMUX_INITIALIZER_UNLOCKED;
ar::iec61850::time_sync::PtpExternalFailoverPolicy g_external_policy;
std::atomic_bool g_external_enabled{false};
std::atomic_bool g_external_switching{false};
std::atomic_bool g_external_worker_created{false};
TaskHandle_t g_external_worker_task{};
esp_eth_handle_t g_external_eth{};
ar_ptp_lab_config_t g_external_source_config{};
std::uint64_t g_external_handovers{};
std::uint64_t g_external_returns{};
std::uint64_t g_external_failures{};

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

extern "C" void ar_ptp_lab_get_default_config(ar_ptp_lab_config_t* config) {
    if (config == nullptr) return;
    *config = ar::esp32p4::smv::default_config();
}

extern "C" bool ar_ptp_lab_configure(const ar_ptp_lab_config_t* config) {
    if (config == nullptr) return false;
    // While auto failover owns mode transitions, external/manual config
    // mutations would race the worker and can splice two EMAC clocks.
    if (ar::esp32p4::smv::g_external_enabled.load(std::memory_order_acquire) &&
        xTaskGetCurrentTaskHandle() != ar::esp32p4::smv::g_external_worker_task)
        return false;
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
    if (started && uncancelled && config.role == AR_PTP_ROLE_LAB_SOURCE) {
        ar::esp32p4::smv::g_external_source_config = config;
        ar::esp32p4::smv::g_external_eth = eth_handle;
    }
    return started && uncancelled;
}

extern "C" void ar_ptp_lab_start(const esp_eth_handle_t eth_handle) {
    static_cast<void>(ar_ptp_lab_try_start(eth_handle));
}

extern "C" void ar_ptp_lab_stop(void) {
    // Human STOP always wins over background acquisition/return.
    ar::esp32p4::smv::g_external_enabled.store(false, std::memory_order_release);
    portENTER_CRITICAL(&ar::esp32p4::smv::g_external_mux);
    ar::esp32p4::smv::g_external_policy.set_enabled(
        false, static_cast<std::uint64_t>(esp_timer_get_time() / 1000LL));
    portEXIT_CRITICAL(&ar::esp32p4::smv::g_external_mux);
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


// ---- Operator-opt-in external GM automatic SOURCE <-> RECEIVER handoff ----
// Never call operator ar_ptp_lab_stop() here: it deliberately disables AUTO.
// STOP must still use the *same* cancellation/EMAC cleanup mechanism.
namespace ar::esp32p4::smv {
namespace {

[[nodiscard]] std::uint64_t external_now_ms() noexcept {
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000LL);
}

void handoff_stop() noexcept {
    smp_synch_set_source_local_qualified(false, SvSyncLossReason::ptp_stop);
    smp_synch_set_receiver_timeline_qualified(false, SvSyncLossReason::ptp_stop);
    if (g_role_control.request_stop_and_try_acquire()) {
        static_cast<void>(g_role_control.finish(stop_active_ptp_roles));
    }
}

[[nodiscard]] bool await_exclusive_emac() noexcept {
    // SOURCE stop is bounded 500ms; receiver asynchronous cleanup must also
    // complete before configuring/reseeding the MAC IEEE1588 hardware clock.
    for (unsigned i = 0U; i < 200U; ++i) {
        if (!g_external_enabled.load(std::memory_order_acquire)) return false;
        if (emac_role_free()) return true;
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    ESP_LOGE(kTag, "PTP AUTO handoff aborted: previous EMAC worker did not stop");
    return false;
}

// Physical Ethernet handle is captured when the operator starts PTP SOURCE.

void external_worker(void*) {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(250));
        if (!g_external_enabled.load(std::memory_order_acquire)) continue;
        const bool source = ar_ptp_source_runtime_active();
        const ar_ptp_lab_config_t profile = selected_config();
        // Include unexpected RECEIVER worker exit as an unlock observation:
        // fallback may not depend on the old task still being alive.
        const bool receiver = !source &&
            profile.role == AR_PTP_ROLE_TIME_RECEIVER;
        bool locked = false;
        if (receiver && ptp_receiver_runtime_active()) {
            ar_ptp_lab_status_t status{};
            if (ptp_receiver_get_status(status)) {
                locked = status.is_running && status.source_selected &&
                    status.timing_authority_clock_usable &&
                    status.discipline_state == AR_PTP_DISCIPLINE_LOCKED;
            }
        }
        ar::iec61850::time_sync::PtpExternalHandover request{};
        const auto now = external_now_ms();
        portENTER_CRITICAL(&g_external_mux);
        request = g_external_policy.evaluate(source, receiver, locked, now);
        portEXIT_CRITICAL(&g_external_mux);
        if (request == ar::iec61850::time_sync::PtpExternalHandover::none)
            continue;
        if (g_external_switching.exchange(true, std::memory_order_acq_rel))
            continue;
        const auto next_role =
            request == ar::iec61850::time_sync::PtpExternalHandover::enter_receiver
                ? AR_PTP_ROLE_TIME_RECEIVER : AR_PTP_ROLE_LAB_SOURCE;
        ESP_LOGW(kTag, "PTP AUTO physical handoff requested: %s -> %s",
                 source ? "SOURCE" : "RECEIVER", role_name(next_role));
        bool success = false;
        if (g_external_enabled.load(std::memory_order_acquire)) {
            // Explicitly break PTP ownership and clear SV sync claims.
            ar_ptp_lab_config_t target{};
            portENTER_CRITICAL(&g_external_mux);
            target = g_external_source_config;
            portEXIT_CRITICAL(&g_external_mux);
            target.role = next_role;
            if (next_role == AR_PTP_ROLE_TIME_RECEIVER) {
                target.bench_global_gm = false;
                target.bench_utc_epoch_seconds = 0U;
                target.bench_utc_epoch_millisecond = 0U;
                target.bench_epoch_received_mono_us = 0LL;
            }
            handoff_stop();
            if (await_exclusive_emac() && ar_ptp_lab_configure(&target) &&
                g_external_enabled.load(std::memory_order_acquire)) {
                success = ar_ptp_lab_try_start(g_external_eth);
            }
        }
        portENTER_CRITICAL(&g_external_mux);
        if (success) {
            if (next_role == AR_PTP_ROLE_TIME_RECEIVER) {
                ++g_external_handovers;
                g_external_policy.switched_to_receiver(external_now_ms());
            } else {
                ++g_external_returns;
                g_external_policy.switched_to_source(external_now_ms());
            }
        } else {
            ++g_external_failures;
            // Failed receiver START must not strand an idling EMAC without
            // attempting to restore its previous, operator-authorized SOURCE.
            // This fallback is executed below after releasing the mux.
            g_external_policy.switched_to_source(external_now_ms());
        }
        portEXIT_CRITICAL(&g_external_mux);
        if (!success && next_role == AR_PTP_ROLE_TIME_RECEIVER &&
            g_external_enabled.load(std::memory_order_acquire)) {
            ar_ptp_lab_config_t original{};
            portENTER_CRITICAL(&g_external_mux);
            original = g_external_source_config;
            portEXIT_CRITICAL(&g_external_mux);
            handoff_stop();
            if (await_exclusive_emac() && ar_ptp_lab_configure(&original) &&
                g_external_enabled.load(std::memory_order_acquire)) {
                const bool restored = ar_ptp_lab_try_start(g_external_eth);
                ESP_LOGW(kTag, "PTP AUTO receiver failed; restore SOURCE=%u",
                         restored ? 1U : 0U);
            } else {
                ESP_LOGE(kTag, "PTP AUTO rollback blocked by EMAC ownership/config");
            }
        }
        ESP_LOGI(kTag, "PTP AUTO handoff result=%s success=%u (receiver LOCK independent)",
                 role_name(next_role), success ? 1U : 0U);
        g_external_switching.store(false, std::memory_order_release);
    }
}

} // namespace
} // namespace ar::esp32p4::smv

extern "C" void ar_ptp_lab_observe_external_announce(
    const uint8_t source_clock[8], const uint8_t grandmaster_clock[8],
    const uint16_t source_port, const uint8_t clock_class,
    const uint16_t steps_removed, const uint64_t received_ms) {
    if (source_clock == nullptr || grandmaster_clock == nullptr)
        return;
    ar::iec61850::time_sync::PtpExternalCandidate candidate{};
    std::copy_n(source_clock, 8U, candidate.port_clock.begin());
    std::copy_n(grandmaster_clock, 8U, candidate.grandmaster_clock.begin());
    candidate.port_number = source_port;
    candidate.clock_class = clock_class;
    candidate.steps_removed = steps_removed;
    portENTER_CRITICAL(&ar::esp32p4::smv::g_external_mux);
    ar::esp32p4::smv::g_external_policy.observe_announce(candidate, received_ms);
    portEXIT_CRITICAL(&ar::esp32p4::smv::g_external_mux);
}

extern "C" bool ar_ptp_lab_set_external_failover(const bool enabled) {
    using namespace ar::esp32p4::smv;
    if (!enabled) {
        g_external_enabled.store(false, std::memory_order_release);
        portENTER_CRITICAL(&g_external_mux);
        g_external_policy.set_enabled(false, external_now_ms());
        portEXIT_CRITICAL(&g_external_mux);
        return true;
    }
    const auto current = selected_config();
    if (current.role != AR_PTP_ROLE_LAB_SOURCE ||
        !ar_ptp_source_runtime_active() || !ar_ptp_source_is_running() ||
        g_role_control.stop_requested() ||
        g_external_switching.load(std::memory_order_acquire)) return false;
    if (!g_external_worker_created.load(std::memory_order_acquire)) {
        bool expected = false;
        if (g_external_worker_created.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel)) {
            if (xTaskCreatePinnedToCore(
                    &external_worker, "ar_ptp_auto", 6144, nullptr, 2,
                    &g_external_worker_task, 1) != pdPASS) {
                g_external_worker_created.store(false, std::memory_order_release);
                return false;
            }
        }
    }
    if (g_external_worker_task == nullptr) return false;
    portENTER_CRITICAL(&g_external_mux);
    g_external_source_config = current;
    g_external_policy.set_enabled(true, external_now_ms());
    portEXIT_CRITICAL(&g_external_mux);
    g_external_enabled.store(true, std::memory_order_release);
    return true;
}

extern "C" bool ar_ptp_lab_get_external_failover_status(
    ar_ptp_external_failover_status_t* status) {
    if (status == nullptr) return false;
    *status = {};
    portENTER_CRITICAL(&ar::esp32p4::smv::g_external_mux);
    const auto& policy = ar::esp32p4::smv::g_external_policy;
    status->enabled = policy.enabled();
    status->candidate_fresh = policy.candidate_fresh(
        static_cast<std::uint64_t>(esp_timer_get_time() / 1000LL)) &&
        policy.stable_announces() >= ar::iec61850::time_sync::PtpExternalFailoverPolicy::required_announces;
    status->stable_announces = policy.stable_announces();
    status->handovers = ar::esp32p4::smv::g_external_handovers;
    status->returns_to_source = ar::esp32p4::smv::g_external_returns;
    status->failures = ar::esp32p4::smv::g_external_failures;
    portEXIT_CRITICAL(&ar::esp32p4::smv::g_external_mux);
    status->switching = ar::esp32p4::smv::g_external_switching.load(
        std::memory_order_acquire);
    return true;
}
