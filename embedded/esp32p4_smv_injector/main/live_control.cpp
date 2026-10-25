// SPDX-License-Identifier: GPL-3.0-or-later

#include "live_control.hpp"
#include "ethernet_port.h"
#include "profile_control.hpp"
#include "smp_synch_lab.hpp"
#include "ariec61850/sampled_values/live_signal_transaction.hpp"

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_AR_PTP_LAB_TX
#include "ptp_lab_task.hpp"
#endif

#include <array>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string_view>

namespace ar::esp32p4::smv {
namespace {

using ar::iec61850::sampled_values::SvLiveSignalBank;
using ar::iec61850::sampled_values::SvLiveSignalState;
using ar::iec61850::sampled_values::SvScenarioTransaction;
using ar::iec61850::sampled_values::live_channel_index;

constexpr char kTag[] = "ar_smv_ctrl";
constexpr std::array<std::string_view, 8> kChannelNames{
    "IA", "IB", "IC", "IN", "UA", "UB", "UC", "UN"};
constexpr std::string_view kTokenDelimiters{" \t\r\n"};
// The serial console is a non-authoritative observation/control plane.
// Its heartbeat measures UI/USB health but MUST NEVER own a publisher lease.
// Only explicit STOP, physical Ethernet link loss, or true runtime TX faults
// can stop an already started autonomous SV session.
constexpr std::uint32_t kSlowHeartbeatGapMs = 2000U;

enum class SvStopCause : std::uint32_t {
    none = 0U, operator_request, runtime_fault, ethernet_link_down
};

const char* sv_stop_cause_name(const SvStopCause cause) noexcept {
    switch (cause) {
        case SvStopCause::operator_request: return "OPERATOR_STOP";
        case SvStopCause::runtime_fault: return "RUNTIME_FAULT";
        case SvStopCause::ethernet_link_down: return "ETHERNET_LINK_DOWN";
        default: return "NONE";
    }
}

SvLiveSignalBank g_signal_bank;
SvScenarioTransaction g_scenario_txn;
std::atomic<bool> g_running{false};
std::atomic<bool> g_start_request{false};
std::atomic<bool> g_physical_link_up{false};
std::atomic<TaskHandle_t> g_publisher_task{nullptr};
std::atomic<bool> g_control_heartbeat_seen{false};
std::atomic<std::uint32_t> g_last_heartbeat_ms{0U};
std::atomic<std::uint32_t> g_max_heartbeat_gap_ms{0U};
std::atomic<std::uint32_t> g_slow_heartbeat_events{0U};
std::atomic<SvStopCause> g_last_sv_stop_cause{SvStopCause::none};
std::atomic<std::uint32_t> g_sv_stop_seq{0U};
std::atomic<std::uint32_t> g_last_sv_stop_ms{0U};
std::atomic<std::uint32_t> g_sv_stop_heartbeat_age_ms{0U};
portMUX_TYPE g_heartbeat_mux = portMUX_INITIALIZER_UNLOCKED;
std::uint64_t g_boot_id{0U};

void wake_publisher() noexcept {
    const auto task = g_publisher_task.load(std::memory_order_acquire);
    if (task != nullptr) {
        xTaskNotifyGive(task);
    }
}

std::uint32_t monotonic_ms() noexcept {
    return static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL);
}

// Preserve stop provenance in board RAM even if serial output scrolls away.
// SHOW reports the same reason after the operator has restarted the publisher.
void record_sv_stop(const SvStopCause cause, const std::uint32_t age_ms) noexcept {
    g_last_sv_stop_cause.store(cause, std::memory_order_release);
    g_sv_stop_heartbeat_age_ms.store(age_ms, std::memory_order_release);
    g_last_sv_stop_ms.store(monotonic_ms(), std::memory_order_release);
    g_sv_stop_seq.fetch_add(1U, std::memory_order_acq_rel);
}

void reset_serial_heartbeat_diagnostics() noexcept {
    portENTER_CRITICAL(&g_heartbeat_mux);
    g_control_heartbeat_seen.store(false, std::memory_order_release);
    g_last_heartbeat_ms.store(0U, std::memory_order_release);
    portEXIT_CRITICAL(&g_heartbeat_mux);
}

// Deliberately NO callback/timer based on host heartbeat. The board's
// realtime operation must not depend on Studio process or USB health.
void record_control_heartbeat() noexcept {
    portENTER_CRITICAL(&g_heartbeat_mux);
    const std::uint32_t now_ms = monotonic_ms();
    if (g_control_heartbeat_seen.load(std::memory_order_relaxed)) {
        const std::uint32_t gap_ms = now_ms -
            g_last_heartbeat_ms.load(std::memory_order_relaxed);
        if (gap_ms > g_max_heartbeat_gap_ms.load(std::memory_order_relaxed))
            g_max_heartbeat_gap_ms.store(gap_ms, std::memory_order_relaxed);
        if (gap_ms > kSlowHeartbeatGapMs)
            g_slow_heartbeat_events.fetch_add(1U, std::memory_order_relaxed);
    }
    g_last_heartbeat_ms.store(now_ms, std::memory_order_release);
    g_control_heartbeat_seen.store(true, std::memory_order_release);
    portEXIT_CRITICAL(&g_heartbeat_mux);
    // No esp_timer_stop/start from this control task, and no lease renewal
    // after the watchdog has already revoked the RUNNING session.
}

void uppercase_ascii(char* text) noexcept {
    if (text == nullptr) return;
    for (; *text != '\0'; ++text) {
        *text = static_cast<char>(std::toupper(static_cast<unsigned char>(*text)));
    }
}

bool is_safe_ascii_command_line(const char* line) noexcept {
    if (line == nullptr) return false;
    bool has_payload = false;
    for (const auto* cursor = reinterpret_cast<const unsigned char*>(line);
         *cursor != 0U; ++cursor) {
        const auto byte = *cursor;
        if (byte == static_cast<unsigned char>(' ') ||
            byte == static_cast<unsigned char>('\t') ||
            byte == static_cast<unsigned char>('\r') ||
            byte == static_cast<unsigned char>('\n')) {
            continue;
        }
        if (byte < 0x20U || byte > 0x7EU) return false;
        has_payload = true;
    }
    return has_payload;
}

bool has_extra_token(char** save) noexcept {
    return strtok_r(nullptr, kTokenDelimiters.data(), save) != nullptr;
}

bool parse_i32(const char* text, std::int32_t& value) noexcept {
    if (text == nullptr || *text == '\0') return false;
    errno = 0;
    char* end{};
    const long parsed = std::strtol(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed < std::numeric_limits<std::int32_t>::min() ||
        parsed > std::numeric_limits<std::int32_t>::max()) return false;
    value = static_cast<std::int32_t>(parsed);
    return true;
}

bool parse_u32(const char* text, std::uint32_t& value) noexcept {
    if (text == nullptr || *text == '\0' || *text == '-') return false;
    errno = 0;
    char* end{};
    const unsigned long parsed = std::strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed > std::numeric_limits<std::uint32_t>::max()) return false;
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool parse_u64(const char* text, std::uint64_t& out, const int base) noexcept {
    if (text == nullptr || *text == '\0' || *text == '-') return false;
    errno = 0;
    char* end{};
    const auto value = std::strtoull(text, &end, base);
    if (errno || end == text || *end != '\0') return false;
    out = static_cast<std::uint64_t>(value);
    return true;
}

void print_help() noexcept {
    ESP_LOGI(kTag, "Live control commands (bench serial transport):");
    ESP_LOGI(kTag, "  IDENTIFY | STOP | SHOW | ZERO | HELP");
    ESP_LOGI(kTag, "  CFG BEGIN <token> <boot_hex> | CFG FREQ/SET/ENABLE/SHAPE ...");
    ESP_LOGI(kTag, "  CFG COMMIT <token> <hash_hex> | CFG SHOW <token> | START <token> <boot_hex> <hash_hex>");
    ESP_LOGI(kTag, "  FREQ <millihertz>");
    ESP_LOGI(kTag, "  SET <IA|IB|IC|IN|UA|UB|UC|UN> <rms_wire_counts> <phase_mdeg> [quality]");
    ESP_LOGI(kTag, "  ENABLE <channel> <0|1>");
    ESP_LOGI(kTag, "  QUALITY <channel> <uint32/0xhex>");
    ESP_LOGI(kTag, "  SHAPE CT <0|1> <dc_permille> <harmonic_permille> <order> <clip_permille>");
    ESP_LOGI(kTag, "  PTP SHOW | PTP DIAG | PTP START | PTP STOP");
    ESP_LOGI(kTag, "  PTP POLICY <INTERNAL_GM_ONLY|AUTO_PREFER_EXTERNAL|EXTERNAL_ONLY|MONITOR_ONLY> (PTP+SV must be STOPPED)");
    ESP_LOGI(kTag, "  PTP CONFIG <domain> <transport> <vlan> <vid> <pcp> <announce_ms> <sync_ms> <pdelay> [bench_global_0_1 UTC_epoch_seconds]");
    ESP_LOGI(kTag, "GUI profile deployment uses bounded PROFILE subcommands while STOPPED.");
}

void print_identity() noexcept {
    std::array<std::uint8_t, 6> device_id{};
    if (esp_read_mac(device_id.data(), ESP_MAC_EFUSE_FACTORY) != ESP_OK) {
        ESP_LOGE(kTag, "ARSTACK identity unavailable: factory device ID read failed");
        return;
    }
    const esp_app_desc_t* app = esp_app_get_description();
    const char* version = app != nullptr ? app->version : "unknown";
#if CONFIG_AR_PTP_LAB_TX
    const char* capabilities =
        "SMV-4I4V,PROFILE,LIVE-SETPOINTS,SV-LAN-AUTONOMY-V1,SV-CONFIG-TXN-V1,"
        "PTP-P2,PTP-GM-BENCH-V1,PTP-POLICY-M3D-V1,SMPSYNCH-AUTO,"
        "PROFILE-BINARY-V1,PROFILE-REUSE-V1";
#else
    const char* capabilities =
        "SMV-4I4V,PROFILE,LIVE-SETPOINTS,SV-LAN-AUTONOMY-V1,"
        "SV-CONFIG-TXN-V1,PROFILE-BINARY-V1,PROFILE-REUSE-V1";
#endif
    ESP_LOGI(kTag,
             "ARSTACK identity product=SMV-INJECTOR target=ESP32-P4 protocol=1 device_id=%02X%02X%02X%02X%02X%02X firmware=%s build=%s boot_id=%016llX capabilities=%s",
             static_cast<unsigned>(device_id[0]), static_cast<unsigned>(device_id[1]),
             static_cast<unsigned>(device_id[2]), static_cast<unsigned>(device_id[3]),
             static_cast<unsigned>(device_id[4]), static_cast<unsigned>(device_id[5]),
             version,
             ARSTACK_FIRMWARE_BUILD_ID,
             static_cast<unsigned long long>(g_boot_id),
             capabilities);
}

void print_ptp_state() noexcept {
#if CONFIG_AR_PTP_LAB_TX
    ar_ptp_lab_status_t status{};
    ar_ptp_lab_config_t config{};
    if (!ar_ptp_lab_get_status(&status) || !ar_ptp_lab_get_config(&config)) {
        ESP_LOGE(kTag, "PTP status unavailable");
        return;
    }
    ar_esp32p4_ptp_print_link_status(); // actual Ethernet driver + PHY event state
    const auto policy = ar_ptp_lab_advanced_policy();
    ESP_LOGI(kTag,
        "PTPPOLICY mode=%s role=%s boot_auto=%u ext_candidate=%u source_generation=%lu sv_running=%u",
        ar_ptp_lab_advanced_policy_name(policy),
        config.role == AR_PTP_ROLE_LAB_SOURCE ? "SOURCE" :
            config.role == AR_PTP_ROLE_TIME_RECEIVER ? "RECEIVER" : "MONITOR",
        ar_ptp_boot_supervisor_autostart_allowed() ? 1U : 0U,
        ar_ptp_source_external_candidate_stable() ? 1U : 0U,
        static_cast<unsigned long>(ar_ptp_lab_source_generation()),
        live_tx_running() ? 1U : 0U);
    ESP_LOGI(kTag,
             "PTP status=%s Announce=%llu Sync=%llu FollowUp=%llu PdelayFrames=%llu TXfail=%llu",
             status.is_running ? "RUNNING" : "STOPPED",
             static_cast<unsigned long long>(status.announce_sent),
             static_cast<unsigned long long>(status.sync_sent),
             static_cast<unsigned long long>(status.follow_up_sent),
             static_cast<unsigned long long>(status.peer_delay_frames_sent),
             static_cast<unsigned long long>(status.tx_failure_count));
    if (config.vlan_enabled) {
        ESP_LOGI(kTag,
                 "PTP config domain=%u transportSpecific=0x%X VLAN=%u/%u Announce=%lu ms Sync=%lu ms Pdelay=%s",
                 static_cast<unsigned>(config.domain_number),
                 static_cast<unsigned>(config.transport_specific),
                 static_cast<unsigned>(config.vlan_id),
                 static_cast<unsigned>(config.vlan_priority),
                 static_cast<unsigned long>(config.announce_interval_ms),
                 static_cast<unsigned long>(config.sync_interval_ms),
                 config.respond_to_peer_delay ? "ON" : "OFF");
    } else {
        ESP_LOGI(kTag,
                 "PTP config domain=%u transportSpecific=0x%X VLAN=OFF Announce=%lu ms Sync=%lu ms Pdelay=%s",
                 static_cast<unsigned>(config.domain_number),
                 static_cast<unsigned>(config.transport_specific),
                 static_cast<unsigned long>(config.announce_interval_ms),
                 static_cast<unsigned long>(config.sync_interval_ms),
                 config.respond_to_peer_delay ? "ON" : "OFF");
    }
#else
    ESP_LOGW(kTag, "PTP unavailable: firmware built without CONFIG_AR_PTP_LAB_TX");
#endif
}

void print_sv_sync_diagnostics() noexcept {
    // Operator-triggered read-only snapshot of the ACTUAL internal gate.
    // Never read MAC descriptors or add any polling to the 4kHz publisher.
    const auto s = smp_synch_diag_snapshot();
    ESP_LOGI(kTag,
        "SVSYNC DIAG events=%lu acquired=%lu revoked=%lu source=%u receiver=%u "
        "phase_us=%ld uncertainty_us=%lu mac_pairs=%lu clean=%lu epoch=%lu "
        "accepted=%u tracking=%u",
        static_cast<unsigned long>(s.events),
        static_cast<unsigned long>(s.acquisitions),
        static_cast<unsigned long>(s.losses),
        smp_synch_source_local_qualified() ? 1U : 0U,
        smp_synch_receiver_timeline_qualified() ? 1U : 0U,
        static_cast<long>(s.phase_error_us),
        static_cast<unsigned long>(s.uncertainty_us),
        static_cast<unsigned long>(s.mac_pairs),
        static_cast<unsigned long>(s.clean_epochs),
        static_cast<unsigned long>(s.sample_epoch),
        s.phase_accepted ? 1U : 0U,
        s.software_tracking ? 1U : 0U);
    for (std::size_t i = 1U;
         i < static_cast<std::size_t>(SvSyncLossReason::count); ++i) {
        if (s.cause_counts[i] == 0U) continue;
        const auto reason = static_cast<SvSyncLossReason>(i);
        ESP_LOGI(kTag, "SVSYNC CAUSE reason=%s count=%lu",
                 smp_synch_diag_reason_name(reason),
                 static_cast<unsigned long>(s.cause_counts[i]));
    }
    for (std::size_t i = 0U; i < s.recent_count; ++i) {
        const auto& e = s.recent[i];
        ESP_LOGI(kTag,
                 "SVSYNC EVENT seq=%lu uptime_ms=%lu state=%s reason=%s "
                 "event_phase_us=%ld event_MACpairs=%lu",
                 static_cast<unsigned long>(e.sequence),
                 static_cast<unsigned long>(e.monotonic_ms),
                 e.acquired ? "ACQUIRED" : "REVOKED",
                 smp_synch_diag_reason_name(e.reason),
                 static_cast<long>(e.phase_error_us),
                 static_cast<unsigned long>(e.mac_pairs));
    }
    ESP_LOGI(kTag, "SVSYNC END events=%lu", static_cast<unsigned long>(s.events));
}

void handle_ptp_command(char* save) noexcept {
    char* subcommand = strtok_r(nullptr, kTokenDelimiters.data(), &save);
    if (subcommand == nullptr) {
        ESP_LOGE(kTag, "PTP subcommand required");
        return;
    }
    uppercase_ascii(subcommand);

    if (std::strcmp(subcommand, "SHOW") == 0) {
        if (has_extra_token(&save)) ESP_LOGE(kTag, "Usage: PTP SHOW");
        else print_ptp_state();
        return;
    }
    if (std::strcmp(subcommand, "DIAG") == 0) {
        if (has_extra_token(&save)) ESP_LOGE(kTag, "Usage: PTP DIAG");
        else print_sv_sync_diagnostics();
        return;
    }
#if CONFIG_AR_PTP_LAB_TX
    if (std::strcmp(subcommand, "START") == 0) {
        if (has_extra_token(&save)) {
            ESP_LOGE(kTag, "Usage: PTP START");
            return;
        }
        ar_ptp_boot_supervisor_operator_override();
        if (!ar_esp32p4_ptp_start()) {
            ar_ptp_lab_config_t active{};
            const bool is_source = ar_ptp_lab_get_config(&active) &&
                active.role == AR_PTP_ROLE_LAB_SOURCE;
            if (is_source) {
                const char* preflight = ar_esp32p4_ptp_preflight_reason();
                ESP_LOGE(kTag, "PTP start rejected: source_reason=%s",
                         preflight != nullptr ? preflight :
                             ar_ptp_source_last_start_reason());
                // Distinguish an unplugged Ethernet cable from a MAC driver
                // that rejected transmit while the physical link was UP.
                ar_esp32p4_ptp_print_link_status();
            } else {
                ESP_LOGE(kTag, "PTP start rejected: receiver/monitor runtime unavailable");
            }
        } else {
            ESP_LOGI(kTag, "PTP start accepted");
        }
        print_ptp_state();
        return;
    }
    if (std::strcmp(subcommand, "STOP") == 0) {
        if (has_extra_token(&save)) {
            ESP_LOGE(kTag, "Usage: PTP STOP");
            return;
        }
        ar_ptp_boot_supervisor_operator_override();
        ar_ptp_lab_stop();
        ESP_LOGI(kTag, "PTP stop requested");
        return;
    }
    if (std::strcmp(subcommand, "POLICY") == 0) {
        char* requested = strtok_r(nullptr, kTokenDelimiters.data(), &save);
        if (requested == nullptr || has_extra_token(&save)) {
            ESP_LOGE(kTag, "Usage: PTP POLICY <INTERNAL_GM_ONLY|AUTO_PREFER_EXTERNAL|EXTERNAL_ONLY|MONITOR_ONLY>");
            return;
        }
        uppercase_ascii(requested);
        ar_ptp_advanced_mode_t desired;
        if (std::strcmp(requested, "INTERNAL_GM_ONLY") == 0)
            desired = AR_PTP_POLICY_INTERNAL_GM_ONLY;
        else if (std::strcmp(requested, "AUTO_PREFER_EXTERNAL") == 0)
            desired = AR_PTP_POLICY_AUTO_PREFER_EXTERNAL;
        else if (std::strcmp(requested, "EXTERNAL_ONLY") == 0)
            desired = AR_PTP_POLICY_EXTERNAL_ONLY;
        else if (std::strcmp(requested, "MONITOR_ONLY") == 0)
            desired = AR_PTP_POLICY_MONITOR_ONLY;
        else {
            ESP_LOGE(kTag, "PTP POLICY rejected: INVALID_MODE");
            return;
        }
        // Always reject a time-authority role mutation during live SV or
        // PTP ownership. No implicit STOP/restart of a healthy clock.
        if (live_tx_running() || ar_ptp_source_runtime_active() ||
            ar_ptp_lab_is_running()) {
            ESP_LOGE(kTag, "PTP POLICY rejected: STOP_SV_AND_PTP_FIRST");
            return;
        }
        if (!ar_ptp_lab_set_advanced_policy(desired)) {
            ESP_LOGE(kTag, "PTP POLICY rejected: BUSY_OR_UNSUPPORTED_PROFILE");
            return;
        }
        ESP_LOGI(kTag, "PTP POLICY accepted mode=%s",
            ar_ptp_lab_advanced_policy_name(desired));
        print_ptp_state();
        return;
    }
    if (std::strcmp(subcommand, "CONFIG") == 0) {
        ar_ptp_boot_supervisor_operator_override();
        std::array<std::uint32_t, 8> values{};
        for (auto& value : values) {
            if (!parse_u32(strtok_r(nullptr, kTokenDelimiters.data(), &save), value)) {
                ESP_LOGE(kTag, "Usage: PTP CONFIG <domain> <transport> <vlan> <vid> <pcp> <announce_ms> <sync_ms> <pdelay>");
                return;
            }
        }
        bool bench_global = false;
        std::uint32_t utc_epoch_seconds = 0U;
        std::uint16_t utc_epoch_millis = 0U;
        char* bench_text = strtok_r(nullptr, kTokenDelimiters.data(), &save);
        if (bench_text != nullptr) {
            std::uint32_t bench = 0U;
            std::uint64_t utc_input = 0ULL;
            if (!parse_u32(bench_text, bench) || bench > 1U ||
                !parse_u64(strtok_r(nullptr, kTokenDelimiters.data(), &save), utc_input, 10)) {
                ESP_LOGE(kTag, "PTP configuration rejected: expected bench mode and UTC epoch");
                return;
            }
            // Accept both legacy Unix seconds and new Unix milliseconds.
            // Full milliseconds avoid the ~0..999ms truncation caused by
            // QDateTime::currentSecsSinceEpoch on the Windows host.
            const bool epoch_in_milliseconds = utc_input >= 100000000000ULL;
            const std::uint64_t seconds = epoch_in_milliseconds
                ? utc_input / 1000ULL : utc_input;
            if (seconds > UINT32_MAX || seconds < 1577836800ULL) {
                ESP_LOGE(kTag, "PTP configuration rejected: UTC epoch outside supported range");
                return;
            }
            utc_epoch_seconds = static_cast<std::uint32_t>(seconds);
            utc_epoch_millis = epoch_in_milliseconds
                ? static_cast<std::uint16_t>(utc_input % 1000ULL) : 0U;
            bench_global = bench != 0U;
        }
        if (has_extra_token(&save) || values[0] > 255U || values[1] > 15U || values[2] > 1U ||
            values[3] > 4094U || values[4] > 7U || values[5] < 100U || values[5] > 10000U ||
            values[6] < 20U || values[6] > 5000U || values[7] > 1U) {
            ESP_LOGE(kTag, "PTP configuration rejected: value outside safe bounds");
            return;
        }
        ar_ptp_lab_config_t config{};
        if (!ar_ptp_lab_get_config(&config)) {
            ESP_LOGE(kTag, "PTP configuration unavailable");
            return;
        }
        config.domain_number = static_cast<std::uint8_t>(values[0]);
        config.transport_specific = static_cast<std::uint8_t>(values[1]);
        config.vlan_enabled = values[2] != 0U;
        config.vlan_id = static_cast<std::uint16_t>(values[3]);
        config.vlan_priority = static_cast<std::uint8_t>(values[4]);
        config.announce_interval_ms = values[5];
        config.sync_interval_ms = values[6];
        config.respond_to_peer_delay = values[7] != 0U;
        config.bench_global_gm = bench_global;
        config.bench_utc_epoch_seconds = bench_global ? utc_epoch_seconds : 0U;
        config.bench_utc_epoch_millisecond = bench_global ? utc_epoch_millis : 0U;
        config.bench_epoch_received_mono_us = bench_global ? esp_timer_get_time() : 0LL;
        if (!ar_ptp_lab_configure(&config)) {
            ESP_LOGE(kTag, "PTP configuration rejected: reason=%s",
                     ar_ptp_lab_last_config_error());
            return;
        }
        ESP_LOGI(kTag, "PTP configuration accepted");
        ESP_LOGI(kTag, "PTP GM emulation=%u UTC_seed=%lu.%03u",
                 config.bench_global_gm ? 1U : 0U,
                 static_cast<unsigned long>(config.bench_utc_epoch_seconds),
                 static_cast<unsigned>(config.bench_utc_epoch_millisecond));
        print_ptp_state();
        return;
    }
#endif
    ESP_LOGE(kTag, "Unknown or unavailable PTP subcommand '%s'", subcommand);
}

void print_state() noexcept {
    const auto state = g_signal_bank.snapshot();
    ESP_LOGI(kTag,
             "state=%s generation=%llu signal_frequency=%u mHz",
             g_running.load(std::memory_order_acquire) ? "RUNNING" : "STOPPED",
             static_cast<unsigned long long>(state.generation),
             static_cast<unsigned>(state.frequency_millihz));
    const std::uint32_t now_ms = monotonic_ms();
    const std::uint32_t hb_last =
        g_last_heartbeat_ms.load(std::memory_order_acquire);
    const bool hb_seen = g_control_heartbeat_seen.load(std::memory_order_acquire);
    const auto stop_reason = g_last_sv_stop_cause.load(std::memory_order_acquire);
    ESP_LOGI(kTag,
             "SVLIFE state=%s stop_cause=%s stop_seq=%lu stop_uptime_ms=%lu "
             "stop_hb_age_ms=%lu hb_age_ms=%lu hb_seen=%u lease_ms=%lu watchdog=%u "
             "hb_max_gap_ms=%lu hb_slow_events=%lu",
             g_running.load(std::memory_order_acquire) ? "RUNNING" : "STOPPED",
             sv_stop_cause_name(stop_reason),
             static_cast<unsigned long>(g_sv_stop_seq.load(std::memory_order_acquire)),
             static_cast<unsigned long>(g_last_sv_stop_ms.load(std::memory_order_acquire)),
             static_cast<unsigned long>(g_sv_stop_heartbeat_age_ms.load(std::memory_order_acquire)),
             static_cast<unsigned long>(hb_seen ? now_ms - hb_last : 0U),
             hb_seen ? 1U : 0U,
             0UL, 0U,
             static_cast<unsigned long>(
                 g_max_heartbeat_gap_ms.load(std::memory_order_acquire)),
             static_cast<unsigned long>(
                 g_slow_heartbeat_events.load(std::memory_order_acquire)));
    ESP_LOGI(kTag, "SVLAN owner=BOARD physical_link=%s output=%s studio_control=OPTIONAL",
             g_physical_link_up.load(std::memory_order_acquire) ? "UP" : "DOWN",
             g_running.load(std::memory_order_acquire) ? "RUNNING" : "STOPPED");
    // This is sticky historical TRANSPORT evidence, not the current STOP
    // condition. A restarted publisher may be RUNNING while last_stop_seq>0.
    const auto tx = live_tx_fault_snapshot();
    ESP_LOGI(kTag,
             "SVTXFAULT state=%s svlife_stop_seq=%lu burst_now=%lu failed_total=%lu bursts=%lu "
             "recovered=%lu first_error=%ld last_error=%ld link_now_at_failure=%u "
             "last_stop_seq=%lu last_stop_uptime_ms=%lu last_stop_burst=%lu "
             "last_stop_first_error=%ld last_stop_last_error=%ld last_stop_link_up=%u",
             g_running.load(std::memory_order_acquire) ? "RUNNING" : "STOPPED",
             static_cast<unsigned long>(g_sv_stop_seq.load(std::memory_order_acquire)),
             static_cast<unsigned long>(tx.current_streak),
             static_cast<unsigned long>(tx.failed_frames),
             static_cast<unsigned long>(tx.failure_bursts),
             static_cast<unsigned long>(tx.recovered_bursts),
             static_cast<long>(tx.first_error),
             static_cast<long>(tx.last_error),
             tx.last_link_up ? 1U : 0U,
             static_cast<unsigned long>(tx.stop_sequence),
             static_cast<unsigned long>(tx.stop_uptime_ms),
             static_cast<unsigned long>(tx.stop_streak),
             static_cast<long>(tx.stop_first_error),
             static_cast<long>(tx.stop_last_error),
             tx.stop_link_up ? 1U : 0U);
    // Bounded coherent 8-channel snapshot for reconnecting Studio. BEGIN and
    // END share the same immutable bank generation and boot epoch; no write.
    ESP_LOGI(kTag, "SVBANK BEGIN generation=%llu boot=%016llX hz_millihz=%u",
             static_cast<unsigned long long>(state.generation),
             static_cast<unsigned long long>(g_boot_id),
             static_cast<unsigned>(state.frequency_millihz));
    for (std::size_t index = 0U; index < state.channels.size(); ++index) {
        const auto& channel = state.channels[index];
        ESP_LOGI(kTag,
                 "%.*s rms_counts=%ld phase_mdeg=%ld quality=0x%08lX enabled=%u",
                 static_cast<int>(kChannelNames[index].size()),
                 kChannelNames[index].data(),
                 static_cast<long>(channel.rms_counts),
                 static_cast<long>(channel.phase_millidegrees),
                 static_cast<unsigned long>(channel.quality),
                 channel.enabled ? 1U : 0U);
    }
    ESP_LOGI(kTag, "SVBANK END generation=%llu boot=%016llX",
             static_cast<unsigned long long>(state.generation),
             static_cast<unsigned long long>(g_boot_id));
}

bool publish_state(const SvLiveSignalState& state) noexcept {
    if (!g_signal_bank.publish(state)) {
        ESP_LOGE(kTag, "Rejected live signal update: value validation failed");
        return false;
    }
    const auto active = g_signal_bank.snapshot();
    ESP_LOGI(kTag, "Live signal generation %llu committed",
             static_cast<unsigned long long>(active.generation));
    return true;
}

void handle_line(char* line) noexcept {
    char* save{};
    char* command = strtok_r(line, kTokenDelimiters.data(), &save);
    if (command == nullptr) return;
    uppercase_ascii(command);
    bool staged = false;
    if (std::strcmp(command, "CFG") == 0) {
        command = strtok_r(nullptr, kTokenDelimiters.data(), &save);
        if (command == nullptr) { g_scenario_txn.reject(); return; }
        uppercase_ascii(command);
        if (std::strcmp(command, "BEGIN") == 0) {
            std::uint64_t token{}, boot{};
            const bool valid = parse_u64(strtok_r(nullptr, kTokenDelimiters.data(), &save), token, 10) &&
                parse_u64(strtok_r(nullptr, kTokenDelimiters.data(), &save), boot, 16) &&
                !has_extra_token(&save) && boot == g_boot_id &&
                g_scenario_txn.begin(token, g_signal_bank.snapshot(), monotonic_ms(),
                                     g_running.load(std::memory_order_acquire));
            if (!valid) { g_scenario_txn.reset(); ESP_LOGE(kTag, "CFG BEGIN rejected"); return; }
            ESP_LOGI(kTag, "CFG BEGIN token=%llu accepted boot=%016llX",
                     static_cast<unsigned long long>(token), static_cast<unsigned long long>(g_boot_id));
            return;
        }
        if (std::strcmp(command, "COMMIT") == 0) {
            std::uint64_t token{}, hash{};
            if (!parse_u64(strtok_r(nullptr, kTokenDelimiters.data(), &save), token, 10) ||
                !parse_u64(strtok_r(nullptr, kTokenDelimiters.data(), &save), hash, 16) ||
                has_extra_token(&save) ||
                g_scenario_txn.began_running() !=
                    g_running.load(std::memory_order_acquire)) {
                g_scenario_txn.reject();
                ESP_LOGE(kTag, "CFG COMMIT rejected: malformed or changed RUN state"); return;
            }
            const auto result = g_scenario_txn.commit(g_signal_bank, token, hash, monotonic_ms());
            if (result != SvScenarioTransaction::Result::accepted) {
                ESP_LOGE(kTag, "CFG REJECT token=%llu code=%u",
                         static_cast<unsigned long long>(token), static_cast<unsigned>(result)); return;
            }
            ESP_LOGI(kTag, "CFG COMMITTED token=%llu hash=%016llX generation=%llu boot=%016llX",
                     static_cast<unsigned long long>(token),
                     static_cast<unsigned long long>(g_scenario_txn.checksum()),
                     static_cast<unsigned long long>(g_scenario_txn.applied_generation()),
                     static_cast<unsigned long long>(g_boot_id));
            return;
        }
        if (std::strcmp(command, "SHOW") == 0) {
            std::uint64_t token{};
            if (!parse_u64(strtok_r(nullptr, kTokenDelimiters.data(), &save), token, 10) ||
                has_extra_token(&save) ||
                g_scenario_txn.began_running() !=
                    g_running.load(std::memory_order_acquire) ||
                !g_scenario_txn.confirm_readback(g_signal_bank, token, monotonic_ms())) {
                ESP_LOGE(kTag, "CFG READBACK rejected"); return;
            }
            ESP_LOGI(kTag, "CFG READBACK token=%llu hash=%016llX generation=%llu boot=%016llX",
                     static_cast<unsigned long long>(token),
                     static_cast<unsigned long long>(g_scenario_txn.checksum()),
                     static_cast<unsigned long long>(g_scenario_txn.applied_generation()),
                     static_cast<unsigned long long>(g_boot_id));
            return;
        }
        if (std::strcmp(command, "ABORT") == 0) {
            g_scenario_txn.reset();
            ESP_LOGI(kTag, "CFG ABORT accepted");
            return;
        }
        staged = std::strcmp(command, "FREQ") == 0 || std::strcmp(command, "SET") == 0 ||
                 std::strcmp(command, "ENABLE") == 0 || std::strcmp(command, "QUALITY") == 0 ||
                 std::strcmp(command, "SHAPE") == 0;
        if (!staged || !g_scenario_txn.staging() ||
            g_scenario_txn.began_running() !=
                g_running.load(std::memory_order_acquire)) {
            g_scenario_txn.reject();
            ESP_LOGE(kTag, "CFG rejected: no staging or invalid command");
            return;
        }
    }

    // Running 4I4V setpoints must change as ONE committed bank generation.
    // A legacy single-channel SET would otherwise put mixed old/new rows on
    // real SV frames. Staged CFG remains allowed while RUNNING.
    if (!staged && g_running.load(std::memory_order_acquire) &&
        (std::strcmp(command, "FREQ") == 0 ||
         std::strcmp(command, "SET") == 0 ||
         std::strcmp(command, "ENABLE") == 0 ||
         std::strcmp(command, "QUALITY") == 0 ||
         std::strcmp(command, "SHAPE") == 0 ||
         std::strcmp(command, "ZERO") == 0)) {
        ESP_LOGE(kTag, "Live edit rejected: use atomic CFG transaction while RUNNING");
        return;
    }

    if (std::strcmp(command, "HEARTBEAT") == 0) {
        if (has_extra_token(&save)) {
            ESP_LOGE(kTag, "Usage: HEARTBEAT");
        } else {
            record_control_heartbeat();
        }
        return;
    }

    if (std::strcmp(command, "IDENTIFY") == 0) {
        if (has_extra_token(&save)) ESP_LOGE(kTag, "Usage: IDENTIFY");
        else print_identity();
        return;
    }

    if (std::strcmp(command, "START") == 0) {
        std::uint64_t token{}, boot{}, hash{};
        if (!parse_u64(strtok_r(nullptr, kTokenDelimiters.data(), &save), token, 10) ||
            !parse_u64(strtok_r(nullptr, kTokenDelimiters.data(), &save), boot, 16) ||
            !parse_u64(strtok_r(nullptr, kTokenDelimiters.data(), &save), hash, 16) ||
            has_extra_token(&save) || boot != g_boot_id ||
            g_running.load(std::memory_order_acquire) ||
            !g_scenario_txn.can_start(g_signal_bank, token, hash, monotonic_ms())) {
            ESP_LOGE(kTag, "START rejected: CFG commit/readback token or boot invalid");
            return;
        }
        // START requires physical link, not GUI presence or heartbeats.
        // A later link-down event revokes the session atomically.
        if (!g_physical_link_up.load(std::memory_order_acquire)) {
            ESP_LOGE(kTag, "START rejected: physical Ethernet link DOWN");
            return;
        }
        profile_control_abort_staging();
        g_scenario_txn.consume_start();
        g_start_request.store(true, std::memory_order_release);
        g_running.store(true, std::memory_order_release);
        // PHY-down racing with the START command must not leave RUNNING
        // latched after the link-loss callback has already observed STOPPED.
        if (!g_physical_link_up.load(std::memory_order_acquire)) {
            live_control_stop_for_link_loss();
            ESP_LOGE(kTag, "START rejected: Ethernet link fell during arm");
            return;
        }
        wake_publisher();
        ESP_LOGI(kTag, "START accepted: profile armed token=%llu hash=%016llX, phase and smpCnt reset at first sample boundary",
                 static_cast<unsigned long long>(token), static_cast<unsigned long long>(hash));
        return;
    }
    if (std::strcmp(command, "STOP") == 0) {
        if (has_extra_token(&save)) {
            ESP_LOGE(kTag, "Usage: STOP");
            return;
        }
        const bool was_running = g_running.exchange(false, std::memory_order_acq_rel);
        g_scenario_txn.reset();
        if (was_running) record_sv_stop(SvStopCause::operator_request, 0U);
        wake_publisher();
        ESP_LOGI(kTag, "STOP accepted: SV transmission suppressed");
        return;
    }
    if (std::strcmp(command, "SHOW") == 0) {
        if (has_extra_token(&save)) {
            ESP_LOGE(kTag, "Usage: SHOW");
            return;
        }
        print_state();
        return;
    }
    if (std::strcmp(command, "HELP") == 0) {
        if (has_extra_token(&save)) {
            ESP_LOGE(kTag, "Usage: HELP");
            return;
        }
        print_help();
        return;
    }
    if (std::strcmp(command, "PROFILE") == 0) {
        g_scenario_txn.reset();
        handle_profile_command(save);
        return;
    }
    if (std::strcmp(command, "PTP") == 0) {
        handle_ptp_command(save);
        return;
    }
    if (std::strcmp(command, "ZERO") == 0) {
        if (has_extra_token(&save)) {
            ESP_LOGE(kTag, "Usage: ZERO");
            return;
        }
        auto state = g_signal_bank.snapshot();
        for (auto& channel : state.channels) channel.rms_counts = 0;
        static_cast<void>(publish_state(state));
        return;
    }
    if (std::strcmp(command, "FREQ") == 0) {
        char* value_text = strtok_r(nullptr, kTokenDelimiters.data(), &save);
        std::uint32_t frequency{};
        if (!parse_u32(value_text, frequency) || has_extra_token(&save)) {
            if (staged) g_scenario_txn.reject();
            ESP_LOGE(kTag, "Usage: FREQ <millihertz>");
            return;
        }
        auto state = staged ? g_scenario_txn.draft() : g_signal_bank.snapshot();
        state.frequency_millihz = frequency;
        if (staged) {
            g_scenario_txn.draft() = state;
            g_scenario_txn.mark_frequency();
        } else {
            g_scenario_txn.reset();
            static_cast<void>(publish_state(state));
        }
        return;
    }

    if (std::strcmp(command, "SHAPE") == 0) {
        char* shape_text = strtok_r(nullptr, kTokenDelimiters.data(), &save);
        if (shape_text == nullptr) {
            if (staged) g_scenario_txn.reject();
            ESP_LOGE(kTag, "Usage: SHAPE CT <0|1> <dc_permille> <harmonic_permille> <order> <clip_permille>");
            return;
        }
        uppercase_ascii(shape_text);
        char* enabled_text = strtok_r(nullptr, kTokenDelimiters.data(), &save);
        char* dc_text = strtok_r(nullptr, kTokenDelimiters.data(), &save);
        char* harmonic_text = strtok_r(nullptr, kTokenDelimiters.data(), &save);
        char* order_text = strtok_r(nullptr, kTokenDelimiters.data(), &save);
        char* clip_text = strtok_r(nullptr, kTokenDelimiters.data(), &save);
        std::uint32_t enabled{};
        std::int32_t dc_offset{};
        std::uint32_t harmonic{};
        std::uint32_t order{};
        std::uint32_t clip{};
        if (std::strcmp(shape_text, "CT") != 0 || !parse_u32(enabled_text, enabled) ||
            !parse_i32(dc_text, dc_offset) || !parse_u32(harmonic_text, harmonic) ||
            !parse_u32(order_text, order) || !parse_u32(clip_text, clip) || has_extra_token(&save) ||
            enabled > 1U || dc_offset < -3000 || dc_offset > 3000 || harmonic > 3000U ||
            order < 2U || order > 63U || clip < 10U || clip > 10000U) {
            if (staged) g_scenario_txn.reject();
            ESP_LOGE(kTag, "Usage: SHAPE CT <0|1> <dc_permille> <harmonic_permille> <order> <clip_permille>");
            return;
        }
        auto state = staged ? g_scenario_txn.draft() : g_signal_bank.snapshot();
        state.current_shape.enabled = enabled != 0U;
        state.current_shape.dc_offset_permille = dc_offset;
        state.current_shape.harmonic_permille = harmonic;
        state.current_shape.harmonic_order = static_cast<std::uint8_t>(order);
        state.current_shape.clip_permille = clip;
        if (staged) {
            g_scenario_txn.draft() = state;
            g_scenario_txn.mark_shape();
        } else {
            g_scenario_txn.reset();
            static_cast<void>(publish_state(state));
        }
        ESP_LOGI(kTag, "CT saturation shape %s: DC=%ld.%u%% H%u=%lu.%u%% clip=%lu.%u%%",
                 enabled ? "enabled" : "disabled",
                 static_cast<long>(dc_offset / 10), static_cast<unsigned>(std::abs(dc_offset % 10)),
                 static_cast<unsigned>(order),
                 static_cast<unsigned long>(harmonic / 10U), static_cast<unsigned>(harmonic % 10U),
                 static_cast<unsigned long>(clip / 10U), static_cast<unsigned>(clip % 10U));
        return;
    }

    if (std::strcmp(command, "SET") == 0 ||
        std::strcmp(command, "ENABLE") == 0 ||
        std::strcmp(command, "QUALITY") == 0) {
        char* channel_text = strtok_r(nullptr, kTokenDelimiters.data(), &save);
        if (channel_text == nullptr) {
            if (staged) g_scenario_txn.reject();
            ESP_LOGE(kTag, "Channel name is required");
            return;
        }
        uppercase_ascii(channel_text);
        const auto channel_index = live_channel_index(channel_text);
        if (!channel_index.has_value()) {
            if (staged) g_scenario_txn.reject();
            ESP_LOGE(kTag, "Unknown channel '%s'", channel_text);
            return;
        }

        auto state = staged ? g_scenario_txn.draft() : g_signal_bank.snapshot();
        auto& channel = state.channels[*channel_index];
        if (std::strcmp(command, "SET") == 0) {
            char* rms_text = strtok_r(nullptr, kTokenDelimiters.data(), &save);
            char* phase_text = strtok_r(nullptr, kTokenDelimiters.data(), &save);
            char* quality_text = strtok_r(nullptr, kTokenDelimiters.data(), &save);
            std::int32_t rms{};
            std::int32_t phase{};
            if (!parse_i32(rms_text, rms) || !parse_i32(phase_text, phase)) {
            if (staged) g_scenario_txn.reject();
                ESP_LOGE(kTag, "Usage: SET <channel> <rms_wire_counts> <phase_mdeg> [quality]");
                return;
            }
            std::uint32_t quality{};
            if (quality_text != nullptr && !parse_u32(quality_text, quality)) {
            if (staged) g_scenario_txn.reject();
                ESP_LOGE(kTag, "Invalid quality value '%s'", quality_text);
                return;
            }
            if (has_extra_token(&save)) {
                if (staged) g_scenario_txn.reject();
                ESP_LOGE(kTag, "Usage: SET <channel> <rms_wire_counts> <phase_mdeg> [quality]");
                return;
            }
            channel.rms_counts = rms;
            channel.phase_millidegrees = phase;
            if (quality_text != nullptr) channel.quality = quality;
            if (staged) {
                g_scenario_txn.draft() = state;
                g_scenario_txn.mark_set(*channel_index);
            } else {
                g_scenario_txn.reset();
                static_cast<void>(publish_state(state));
            }
            return;
        }

        if (std::strcmp(command, "ENABLE") == 0) {
            char* enabled_text = strtok_r(nullptr, kTokenDelimiters.data(), &save);
            std::uint32_t enabled{};
            if (!parse_u32(enabled_text, enabled) || enabled > 1U || has_extra_token(&save)) {
            if (staged) g_scenario_txn.reject();
                ESP_LOGE(kTag, "Usage: ENABLE <channel> <0|1>");
                return;
            }
            channel.enabled = enabled != 0U;
            if (staged) {
                g_scenario_txn.draft() = state;
                g_scenario_txn.mark_enabled(*channel_index);
            } else {
                g_scenario_txn.reset();
                static_cast<void>(publish_state(state));
            }
            return;
        }

        char* quality_text = strtok_r(nullptr, kTokenDelimiters.data(), &save);
        std::uint32_t quality{};
        if (!parse_u32(quality_text, quality) || has_extra_token(&save)) {
            if (staged) g_scenario_txn.reject();
            ESP_LOGE(kTag, "Usage: QUALITY <channel> <uint32/0xhex>");
            return;
        }
        channel.quality = quality;
        if (staged) g_scenario_txn.draft() = state;
        else { g_scenario_txn.reset(); static_cast<void>(publish_state(state)); }
        return;
    }

    if (staged) g_scenario_txn.reject();
    ESP_LOGW(kTag, "Unknown command '%s'; type HELP", command);
}

} // namespace

void live_control_initialize(
    const std::int32_t current_rms_counts,
    const std::int32_t voltage_rms_counts) noexcept {
    auto initial = g_signal_bank.snapshot();
    for (std::size_t index = 0U; index < 3U; ++index) initial.channels[index].rms_counts = current_rms_counts;
    initial.channels[3].rms_counts = 0;
    for (std::size_t index = 4U; index < 7U; ++index) initial.channels[index].rms_counts = voltage_rms_counts;
    initial.channels[7].rms_counts = 0;
    static_cast<void>(g_signal_bank.publish(initial));
    g_scenario_txn.reset();
    g_running.store(false, std::memory_order_release);
    g_start_request.store(false, std::memory_order_release);
    reset_serial_heartbeat_diagnostics();
    g_max_heartbeat_gap_ms.store(0U, std::memory_order_release);
    g_slow_heartbeat_events.store(0U, std::memory_order_release);
    g_last_sv_stop_cause.store(SvStopCause::none, std::memory_order_release);
    g_sv_stop_seq.store(0U, std::memory_order_release);
    g_last_sv_stop_ms.store(0U, std::memory_order_release);
    g_sv_stop_heartbeat_age_ms.store(0U, std::memory_order_release);

    const std::uint64_t random_high = static_cast<std::uint64_t>(esp_random());
    const std::uint64_t random_low = static_cast<std::uint64_t>(esp_random());
    g_boot_id = (random_high << 32U) | random_low;
    if (g_boot_id == 0U) g_boot_id = 1U;

    // There is intentionally no USB/Studio lease watchdog. Ethernet and
    // canonical TX health remain fail-closed under the publisher task.
}

void live_control_bind_publisher_task(const TaskHandle_t task) noexcept {
    g_publisher_task.store(task, std::memory_order_release);
}

void live_control_set_ethernet_link(const bool up) noexcept {
    g_physical_link_up.store(up, std::memory_order_release);
    if (!up) live_control_stop_for_link_loss();
}

void live_control_stop_for_link_loss() noexcept {
    const bool was_running = g_running.exchange(false, std::memory_order_acq_rel);
    g_start_request.store(false, std::memory_order_release);
    if (was_running) {
        record_sv_stop(SvStopCause::ethernet_link_down, 0U);
        wake_publisher();
        ESP_LOGW(kTag, "SV output STOPPED: physical Ethernet link DOWN (rearm requires explicit START)");
    }
}

void live_control_force_stop() noexcept {
    const bool was_running = g_running.exchange(false, std::memory_order_acq_rel);
    g_start_request.store(false, std::memory_order_release);
    // Preserve the optional USB heartbeat diagnostic even if the Ethernet
    // publisher fails: the root cause must not be misreported as COM loss.
    if (was_running) record_sv_stop(SvStopCause::runtime_fault, 0U);
    wake_publisher();
    ESP_LOGW(kTag, "STOP accepted: SV transmission suppressed by fail-closed runtime");
}

SvLiveSignalState live_signal_snapshot() noexcept {
    return g_signal_bank.snapshot();
}

bool live_tx_running() noexcept {
    return g_running.load(std::memory_order_acquire);
}

bool take_start_request() noexcept {
    return g_start_request.exchange(false, std::memory_order_acq_rel);
}

void live_control_task(void*) noexcept {
    std::setvbuf(stdin, nullptr, _IONBF, 0);
    print_help();
    print_state();
    ESP_LOGI(kTag, "Console ready: type a complete command, then press Enter.");

    // S1 startup announcement: once the control task is ready, advertise the
    // same machine-readable identity returned by IDENTIFY. Studio must still
    // use explicit request/response as authoritative, but it no longer depends
    // on landing the first request in a narrow USB re-enumeration window.
    print_identity();

    std::array<char, 192> line{};
    std::size_t length = 0U;
    bool discard_until_eol = false;

    while (true) {
        const int input = std::fgetc(stdin);
        if (input == EOF) {
            std::clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        const auto byte = static_cast<unsigned char>(input);
        if (byte == static_cast<unsigned char>('\r') ||
            byte == static_cast<unsigned char>('\n')) {
            if (discard_until_eol) {
                discard_until_eol = false;
                length = 0U;
                line.fill('\0');
                ESP_LOGW(kTag, "Discarded invalid/overlong bench command");
                continue;
            }
            if (length == 0U) continue;
            line[length] = '\0';
            if (!is_safe_ascii_command_line(line.data())) {
                ESP_LOGW(kTag, "Discarded invalid bench command");
            } else {
                handle_line(line.data());
            }
            length = 0U;
            line.fill('\0');
            continue;
        }

        if (discard_until_eol) continue;
        if (byte == 0x08U || byte == 0x7FU) {
            if (length > 0U) {
                --length;
                line[length] = '\0';
            }
            continue;
        }

        const bool accepted =
            byte == static_cast<unsigned char>(' ') ||
            byte == static_cast<unsigned char>('\t') ||
            (byte >= 0x21U && byte <= 0x7EU);
        if (!accepted || length + 1U >= line.size()) {
            discard_until_eol = true;
            continue;
        }
        line[length++] = static_cast<char>(byte);
    }
}

} // namespace ar::esp32p4::smv
