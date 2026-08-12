// SPDX-License-Identifier: GPL-3.0-or-later

#include "ptp_lab_task.hpp"
#include "ptp_receiver_task.hpp"

#include "ariec61850/time_sync/ptp_discipline.hpp"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "sdkconfig.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>

extern "C" {
void ar_ptp_source_get_default_config(ar_ptp_lab_config_t* config);
bool ar_ptp_source_configure(const ar_ptp_lab_config_t* config);
void ar_ptp_source_start(esp_eth_handle_t eth_handle);
void ar_ptp_source_stop(void);
bool ar_ptp_source_is_running(void);
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
std::atomic<int> g_active_role{static_cast<int>(AR_PTP_ROLE_LAB_SOURCE)};

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

[[nodiscard]] ar_ptp_role_t active_role() noexcept {
    return static_cast<ar_ptp_role_t>(g_active_role.load(std::memory_order_acquire));
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
    if (config == nullptr || ar_ptp_source_is_running() || ar::esp32p4::smv::ptp_receiver_is_running() || !ar::esp32p4::smv::valid_public_config(*config)) return false;
    if (!ar_ptp_source_configure(config)) return false;
    portENTER_CRITICAL(&ar::esp32p4::smv::g_ptp_facade_mux);
    if (ar_ptp_source_is_running() || ar::esp32p4::smv::ptp_receiver_is_running()) {
        portEXIT_CRITICAL(&ar::esp32p4::smv::g_ptp_facade_mux);
        return false;
    }
    ar::esp32p4::smv::g_facade_override = *config;
    ar::esp32p4::smv::g_facade_override_valid = true;
    portEXIT_CRITICAL(&ar::esp32p4::smv::g_ptp_facade_mux);
    return true;
}

extern "C" bool ar_ptp_lab_get_config(ar_ptp_lab_config_t* config) {
    if (config == nullptr) return false;
    *config = ar::esp32p4::smv::selected_config();
    return true;
}

extern "C" bool ar_ptp_lab_start(const esp_eth_handle_t eth_handle) {
    if (eth_handle == nullptr || ar_ptp_lab_is_running()) return false;
    const auto config = ar::esp32p4::smv::selected_config();
    if (!ar::esp32p4::smv::valid_public_config(config)) {
        ESP_LOGE(ar::esp32p4::smv::kTag,
                 "PTP start rejected: invalid role/cadence/discipline threshold combination");
        return false;
    }
    ar::esp32p4::smv::g_active_role.store(static_cast<int>(config.role), std::memory_order_release);
    if (config.role == AR_PTP_ROLE_LAB_SOURCE) {
        if (!ar_ptp_source_configure(&config)) return false;
        ar_ptp_source_start(eth_handle);
        return ar_ptp_source_is_running();
    }
    return ar::esp32p4::smv::ptp_receiver_start(eth_handle, config);
}

extern "C" void ar_ptp_lab_stop(void) {
    if (ar::esp32p4::smv::active_role() == AR_PTP_ROLE_LAB_SOURCE) ar_ptp_source_stop();
    else ar::esp32p4::smv::ptp_receiver_stop();
}

extern "C" bool ar_ptp_lab_is_running(void) {
    return ar_ptp_source_is_running() || ar::esp32p4::smv::ptp_receiver_is_running();
}

extern "C" bool ar_ptp_lab_get_status(ar_ptp_lab_status_t* status) {
    if (status == nullptr) return false;
    *status = {};
    bool result = false;
    if (ar_ptp_source_is_running()) {
        result = ar_ptp_source_get_status(status);
        if (result) {
            status->role = AR_PTP_ROLE_LAB_SOURCE;
            status->discipline_state = AR_PTP_DISCIPLINE_UNLOCKED;
        }
    } else if (ar::esp32p4::smv::ptp_receiver_is_running()) {
        result = ar::esp32p4::smv::ptp_receiver_get_status(*status);
    } else {
        status->role = ar::esp32p4::smv::selected_config().role;
        status->discipline_state = AR_PTP_DISCIPLINE_UNLOCKED;
        result = true;
    }
    if (result) ar::esp32p4::smv::log_p2_status(*status);
    return result;
}
