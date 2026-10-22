// SPDX-License-Identifier: GPL-3.0-or-later

#include "ptp_lab_task.hpp"

#include "ariec61850/time_sync/ptp.hpp"
#include "ariec61850/time_sync/ptp_power_profile.hpp"
#include "ariec61850/time_sync/ptp_runtime.hpp"
#include "ariec61850/time_sync/ptp_tai_source.hpp"

#include "esp_err.h"
#include "esp_eth.h"
#include "esp_eth_driver.h"
#include "esp_eth_mac.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"

#if ESP_IDF_VERSION_MAJOR < 6
#include "esp_eth_mac_esp.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ar::esp32p4::smv {
namespace {

using ar::iec61850::time_sync::PtpClockAccuracy;
using ar::iec61850::time_sync::PtpFrame;
using ar::iec61850::time_sync::PtpMessageType;
using ar::iec61850::time_sync::PtpPortIdentity;
using ar::iec61850::time_sync::PtpPublisherOptions;
using ar::iec61850::time_sync::PtpPublisherRuntime;
using ar::iec61850::time_sync::PtpSourceReadinessGate;
using ar::iec61850::time_sync::PtpSourceEvidenceFreshness;
using ar::iec61850::time_sync::conservative_lab_time_properties;
using ar::iec61850::time_sync::inspect_ptp_power_profile_cadence;
using ar::iec61850::time_sync::PtpTimeSource;
using ar::iec61850::time_sync::PtpTimestamp;
using ar::iec61850::time_sync::format_ptp_clock_identity;
using ar::iec61850::time_sync::ptp_clock_identity_from_mac;
using ar::iec61850::time_sync::try_parse_ptp_clock_identity;

constexpr char kTag[] = "ar_ptp_lab";
constexpr TickType_t kStartupDelay = pdMS_TO_TICKS(500);
constexpr UBaseType_t kPdelayQueueDepth = 8U;
constexpr std::array<std::uint8_t, 6> kValidationMac{
    0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U};

struct PdelayRequestEvent final {
    PtpPortIdentity requesting_port_identity{};
    PtpTimestamp receive_timestamp{};
    std::uint16_t sequence_id{};
    std::int8_t log_message_interval{};
};

struct PtpLabContext final {
    esp_eth_handle_t eth_handle{};
    std::array<std::uint8_t, 6> source_mac{};
    QueueHandle_t pdelay_queue{};
    TaskHandle_t task_handle{};
    std::optional<PtpPublisherRuntime> runtime;
    std::uint8_t rx_domain{};
    std::uint8_t rx_transport_specific{};
    bool rx_respond_to_peer_delay{};
};

PtpLabContext g_ptp_context{};
enum class PtpSourceStartReason : std::uint8_t {
    not_attempted, starting, ready, ethernet_missing, already_active,
    ieee1588_clock_unavailable, mac_identity_unavailable,
    invalid_runtime_profile, runtime_start_rejected,
    pdelay_rx_timestamp_unavailable, source_tx_evidence_timeout,
    ethernet_tx_invalid_state, source_task_create_failed, source_task_unsupported
};
std::atomic<PtpSourceStartReason> g_source_start_reason{
    PtpSourceStartReason::not_attempted};

// Control-path only: compact last failure, not a second configuration state
// machine. Every rejection is classified before its generic UI projection.
enum class PtpConfigFailure : std::uint8_t {
    none, active_runtime, source_power_profile, invalid_profile,
    raced_runtime_activation
};
std::atomic<PtpConfigFailure> g_ptp_config_failure{PtpConfigFailure::none};
[[nodiscard]] const char* ptp_config_failure_name(const PtpConfigFailure code) noexcept {
    switch (code) {
    case PtpConfigFailure::none: return "NONE";
    case PtpConfigFailure::active_runtime: return "PTP_RUNTIME_ALREADY_ACTIVE";
    case PtpConfigFailure::source_power_profile:
        return "SOURCE_REQUIRES_1S_ANNOUNCE_1S_SYNC_AND_PDELAY";
    case PtpConfigFailure::invalid_profile: return "INVALID_OR_UNSUPPORTED_PTP_PROFILE";
    case PtpConfigFailure::raced_runtime_activation: return "PTP_RUNTIME_ACTIVATED_DURING_CONFIG";
    }
    return "UNKNOWN";
}
[[nodiscard]] const char* source_start_reason_name(
    const PtpSourceStartReason reason) noexcept {
    switch (reason) {
    case PtpSourceStartReason::not_attempted: return "NOT_ATTEMPTED";
    case PtpSourceStartReason::starting: return "STARTING";
    case PtpSourceStartReason::ready: return "READY_TX_EVIDENCE";
    case PtpSourceStartReason::ethernet_missing: return "ETHERNET_HANDLE_MISSING";
    case PtpSourceStartReason::already_active: return "SOURCE_TASK_ALREADY_ACTIVE";
    case PtpSourceStartReason::ieee1588_clock_unavailable: return "IEEE1588_CLOCK_UNAVAILABLE";
    case PtpSourceStartReason::mac_identity_unavailable: return "MAC_IDENTITY_UNAVAILABLE";
    case PtpSourceStartReason::invalid_runtime_profile: return "INVALID_RUNTIME_PROFILE";
    case PtpSourceStartReason::runtime_start_rejected: return "RUNTIME_START_REJECTED";
    case PtpSourceStartReason::pdelay_rx_timestamp_unavailable: return "PDELAY_RX_TIMESTAMP_UNAVAILABLE";
    case PtpSourceStartReason::source_tx_evidence_timeout: return "SOURCE_TX_EVIDENCE_TIMEOUT";
    case PtpSourceStartReason::ethernet_tx_invalid_state: return "ETHERNET_TX_INVALID_STATE";
    case PtpSourceStartReason::source_task_create_failed: return "SOURCE_TASK_CREATE_FAILED";
    case PtpSourceStartReason::source_task_unsupported: return "SOURCE_TASK_UNSUPPORTED";
    }
    return "UNKNOWN";
}
std::atomic_bool g_ptp_started{false};
std::atomic_bool g_bench_global_active{false};
std::atomic_bool g_ptp_ready{false};
std::atomic<std::int64_t> g_ptp_last_announce_us{0LL};
std::atomic<std::int64_t> g_ptp_last_sync_pair_us{0LL};
std::atomic<std::int64_t> g_ptp_freshness_timeout_us{3000000LL};
std::atomic<std::uint32_t> g_ptp_source_generation{0U};
std::atomic_bool g_stop_requested{false};
std::atomic_bool g_ptp_accept_rx{false};
std::atomic<std::uint64_t> g_announce_sent{0U};
std::atomic<std::uint64_t> g_sync_sent{0U};
std::atomic<std::uint64_t> g_follow_up_sent{0U};
std::atomic<std::uint64_t> g_pdelay_frames_sent{0U};
std::atomic<std::uint64_t> g_tx_failure_count{0U};

[[nodiscard]] bool source_has_fresh_evidence() noexcept {
    if (!g_ptp_ready.load(std::memory_order_acquire)) return false;
    return PtpSourceEvidenceFreshness::fresh(
        esp_timer_get_time(),
        g_ptp_last_announce_us.load(std::memory_order_acquire),
        g_ptp_last_sync_pair_us.load(std::memory_order_acquire),
        g_ptp_freshness_timeout_us.load(std::memory_order_acquire));
}

portMUX_TYPE g_control_mux = portMUX_INITIALIZER_UNLOCKED;
ar_ptp_lab_config_t g_runtime_config_override{};
bool g_runtime_config_override_valid = false;

[[nodiscard]] bool all_zero_identity(const std::uint8_t (&identity)[8]) noexcept {
    return std::all_of(
        std::begin(identity),
        std::end(identity),
        [](const std::uint8_t value) { return value == 0U; });
}

void fill_kconfig_defaults(ar_ptp_lab_config_t& config) noexcept {
    config = {};
    config.transport_specific = static_cast<std::uint8_t>(CONFIG_AR_PTP_TRANSPORT_SPECIFIC);
    config.domain_number = static_cast<std::uint8_t>(CONFIG_AR_PTP_DOMAIN);
#if defined(CONFIG_AR_PTP_VLAN) && CONFIG_AR_PTP_VLAN
    config.vlan_enabled = true;
    config.vlan_id = static_cast<std::uint16_t>(CONFIG_AR_PTP_VLAN_ID);
    config.vlan_priority = static_cast<std::uint8_t>(CONFIG_AR_PTP_VLAN_PRIORITY);
#else
    config.vlan_enabled = false;
    config.vlan_id = 0U;
    config.vlan_priority = 0U;
#endif
    config.port_number = static_cast<std::uint16_t>(CONFIG_AR_PTP_PORT_NUMBER);
    config.announce_interval_ms = static_cast<std::uint32_t>(CONFIG_AR_PTP_ANNOUNCE_INTERVAL_MS);
    config.sync_interval_ms = static_cast<std::uint32_t>(CONFIG_AR_PTP_SYNC_INTERVAL_MS);
#if defined(CONFIG_AR_PTP_RESPOND_PDELAY) && CONFIG_AR_PTP_RESPOND_PDELAY
    config.respond_to_peer_delay = true;
#else
    config.respond_to_peer_delay = false;
#endif
    config.priority1 = static_cast<std::uint8_t>(CONFIG_AR_PTP_PRIORITY1);
    config.priority2 = static_cast<std::uint8_t>(CONFIG_AR_PTP_PRIORITY2);
    config.clock_class = static_cast<std::uint8_t>(CONFIG_AR_PTP_CLOCK_CLASS);
    config.clock_accuracy = static_cast<std::uint8_t>(CONFIG_AR_PTP_CLOCK_ACCURACY);
    config.offset_scaled_log_variance =
        static_cast<std::uint16_t>(CONFIG_AR_PTP_OFFSET_SCALED_LOG_VARIANCE);
    config.time_source = static_cast<std::uint8_t>(CONFIG_AR_PTP_TIME_SOURCE);
    config.current_utc_offset = static_cast<std::int16_t>(CONFIG_AR_PTP_CURRENT_UTC_OFFSET);
    config.bench_global_gm = false;
    config.bench_utc_epoch_seconds = 0U;
    config.bench_epoch_received_mono_us = 0LL;

    const std::string configured_identity{CONFIG_AR_PTP_CLOCK_IDENTITY};
    if (!configured_identity.empty()) {
        ar::iec61850::time_sync::PtpClockIdentity parsed{};
        if (try_parse_ptp_clock_identity(configured_identity, parsed)) {
            config.clock_identity_override = true;
            std::copy(parsed.begin(), parsed.end(), std::begin(config.clock_identity));
        }
    }
}

[[nodiscard]] ar_ptp_lab_config_t selected_config() noexcept {
    ar_ptp_lab_config_t result{};
    bool has_override = false;
    portENTER_CRITICAL(&g_control_mux);
    has_override = g_runtime_config_override_valid;
    if (has_override) result = g_runtime_config_override;
    portEXIT_CRITICAL(&g_control_mux);

    // Avoid heap/string work inside the critical section when defaults include
    // a textual ClockIdentity override.
    if (!has_override) fill_kconfig_defaults(result);
    return result;
}

[[nodiscard]] std::optional<PtpPublisherOptions> options_from_config(
    const ar_ptp_lab_config_t& config,
    const std::array<std::uint8_t, 6>& source_mac,
    std::string* validation_error = nullptr) {
    PtpPublisherOptions options;
    options.transport_specific = config.transport_specific;
    options.domain_number = config.domain_number;
    options.source_mac = source_mac;
    if (config.vlan_enabled) {
        options.vlan_id = config.vlan_id;
        options.vlan_priority = config.vlan_priority;
    } else {
        options.vlan_id = std::nullopt;
        options.vlan_priority = 0U;
    }
    options.clock_identity = ptp_clock_identity_from_mac(source_mac);
    if (config.clock_identity_override) {
        std::copy(
            std::begin(config.clock_identity),
            std::end(config.clock_identity),
            options.clock_identity.begin());
    }
    options.port_number = config.port_number;
    options.announce_interval = std::chrono::milliseconds{config.announce_interval_ms};
    options.sync_interval = std::chrono::milliseconds{config.sync_interval_ms};
    options.follow_up_delay = std::chrono::milliseconds::zero();
    // ESP32-P4 intentionally stays two-step so the exact EMAC egress timestamp
    // can be placed in Follow_Up instead of approximating a one-step timestamp.
    options.two_step_clock = true;
    options.respond_to_peer_delay = config.respond_to_peer_delay;
    options.priority1 = config.priority1;
    options.priority2 = config.priority2;
    // An isolated simulator is explicitly configured to *emit* the same
    // clock-quality fields a GNSS GM would emit. Timing still comes from
    // this ESP32-P4's own IEEE1588 MAC clock; it is never an external source.
    options.clock_class = config.bench_global_gm && config.role == AR_PTP_ROLE_LAB_SOURCE
        ? 6U : config.clock_class;
    options.clock_accuracy = config.bench_global_gm && config.role == AR_PTP_ROLE_LAB_SOURCE
        ? PtpClockAccuracy::within_1_us
        : static_cast<PtpClockAccuracy>(config.clock_accuracy);
    options.offset_scaled_log_variance = config.bench_global_gm &&
        config.role == AR_PTP_ROLE_LAB_SOURCE ? 0x4E5DU : config.offset_scaled_log_variance;
    options.time_source = config.bench_global_gm && config.role == AR_PTP_ROLE_LAB_SOURCE
        ? PtpTimeSource::gps : static_cast<PtpTimeSource>(config.time_source);
    options.current_utc_offset = config.current_utc_offset;

    std::string error;
    if (!PtpPublisherRuntime::validate_options(options, error)) {
        if (validation_error != nullptr) *validation_error = std::move(error);
        return std::nullopt;
    }
    if (validation_error != nullptr) validation_error->clear();
    return options;
}

[[nodiscard]] bool validate_runtime_config(
    const ar_ptp_lab_config_t& config,
    std::string& error) {
    // LAB_SOURCE has no external clock input or traceability evidence.
    // Reject source-quality advertising that would mislead an IED BMCA.
    if (config.role == AR_PTP_ROLE_LAB_SOURCE && config.bench_global_gm &&
        (config.bench_utc_epoch_seconds < 1577836800U ||
         config.bench_epoch_received_mono_us <= 0LL ||
         esp_timer_get_time() < config.bench_epoch_received_mono_us ||
         esp_timer_get_time() - config.bench_epoch_received_mono_us > 30000000LL)) {
        error = "BENCH GM requires a fresh UTC epoch from Studio (30-second lease)";
        return false;
    }
    if (config.role == AR_PTP_ROLE_LAB_SOURCE &&
        (config.clock_class != 248U ||
         config.clock_accuracy !=
             static_cast<std::uint8_t>(PtpClockAccuracy::unknown) ||
         config.time_source !=
             static_cast<std::uint8_t>(PtpTimeSource::internal_oscillator))) {
        error = "LAB_SOURCE requires clockClass=248, clockAccuracy=UNKNOWN "
                "and timeSource=INTERNAL_OSCILLATOR (no external reference)";
        return false;
    }
    if (config.clock_identity_override && all_zero_identity(config.clock_identity)) {
        error = "clockIdentity override must be non-zero";
        return false;
    }
    auto options = options_from_config(config, kValidationMac, &error);
    if (!options.has_value()) return false;
#if !defined(CONFIG_AR_PTP_ALLOW_NONSTANDARD_LAB_CADENCE) || !CONFIG_AR_PTP_ALLOW_NONSTANDARD_LAB_CADENCE
    if (config.role == AR_PTP_ROLE_LAB_SOURCE &&
        !inspect_ptp_power_profile_cadence(*options).compatible()) {
        error = "IEC/IEEE 61850-9-3 lab SOURCE requires 1s Announce, 1s Sync "
                "and hardware Pdelay responder; nonstandard bench mode is opt-in";
        return false;
    }
#endif
    return true;
}

[[nodiscard]] std::optional<PtpPublisherOptions> make_runtime_options(
    const std::array<std::uint8_t, 6>& source_mac) {
    const auto config = selected_config();
    std::string validation_error;
    auto options = options_from_config(config, source_mac, &validation_error);
    if (!options.has_value()) {
        ESP_LOGE(kTag, "Invalid PTP runtime profile: %s", validation_error.c_str());
    }
    return options;
}

void reset_live_status() noexcept {
    g_announce_sent.store(0U, std::memory_order_release);
    g_sync_sent.store(0U, std::memory_order_release);
    g_follow_up_sent.store(0U, std::memory_order_release);
    g_pdelay_frames_sent.store(0U, std::memory_order_release);
    g_tx_failure_count.store(0U, std::memory_order_release);
}

void record_live_sent(const PtpMessageType message_type) noexcept {
    switch (message_type) {
    case PtpMessageType::announce:
        g_announce_sent.fetch_add(1U, std::memory_order_relaxed);
        break;
    case PtpMessageType::sync:
        g_sync_sent.fetch_add(1U, std::memory_order_relaxed);
        break;
    case PtpMessageType::follow_up:
        g_follow_up_sent.fetch_add(1U, std::memory_order_relaxed);
        break;
    case PtpMessageType::pdelay_resp:
    case PtpMessageType::pdelay_resp_follow_up:
        g_pdelay_frames_sent.fetch_add(1U, std::memory_order_relaxed);
        break;
    default:
        break;
    }
}

#if defined(SOC_EMAC_IEEE1588V2_SUPPORTED) && SOC_EMAC_IEEE1588V2_SUPPORTED && ESP_IDF_VERSION_MAJOR < 6

[[nodiscard]] bool valid_hw_timestamp(const eth_mac_time_t& timestamp) noexcept {
    return timestamp.nanoseconds < 1'000'000'000U &&
           (timestamp.seconds != 0U || timestamp.nanoseconds != 0U);
}

[[nodiscard]] PtpTimestamp to_ptp_timestamp(const eth_mac_time_t& timestamp) noexcept {
    return {
        static_cast<std::uint64_t>(timestamp.seconds),
        timestamp.nanoseconds,
    };
}

[[nodiscard]] bool enable_hardware_ptp(const esp_eth_handle_t eth_handle) noexcept {
    bool enable = true;
    const auto result = esp_eth_ioctl(
        eth_handle,
        static_cast<esp_eth_io_cmd_t>(ETH_MAC_ESP_CMD_PTP_ENABLE),
        &enable);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Failed to enable ESP32-P4 IEEE1588 hardware clock: %s",
                 esp_err_to_name(result));
        return false;
    }
    return true;
}

void disable_hardware_ptp(const esp_eth_handle_t eth_handle) noexcept {
    if (eth_handle == nullptr) return;
    bool enable = false;
    const auto result = esp_eth_ioctl(
        eth_handle,
        static_cast<esp_eth_io_cmd_t>(ETH_MAC_ESP_CMD_PTP_ENABLE),
        &enable);
    if (result != ESP_OK) {
        ESP_LOGW(kTag, "Unable to restore normal EMAC mode after PTP SOURCE: %s",
                 esp_err_to_name(result));
    }
}

// Returns true only when the EMAC was seeded with an epoch expressed in
// PTP/TAI seconds. A plausible RTC is not an accuracy/traceability guarantee.
[[nodiscard]] bool seed_hardware_clock(
    const esp_eth_handle_t eth_handle,
    const ar_ptp_lab_config_t& config) noexcept {
    eth_mac_time_t initial_time{};
    const std::int64_t now_mono_us = esp_timer_get_time();
    const bool bench = config.bench_global_gm;
    if (bench && (config.bench_utc_epoch_seconds < 1577836800U ||
                  config.bench_epoch_received_mono_us <= 0LL ||
                  now_mono_us < config.bench_epoch_received_mono_us ||
                  now_mono_us - config.bench_epoch_received_mono_us > 30000000LL)) {
        ESP_LOGE(kTag, "BENCH GM epoch missing or expired before EMAC clock start");
        return false;
    }
    // The serial CONFIG event is the timebase seed: interpolate elapsed
    // monotonic time before switching on the MAC. Every subsequent PTP
    // and SV sample uses the SAME hardware PTP epoch, not the PC clock.
    const std::uint64_t elapsed_us = bench
        ? static_cast<std::uint64_t>(
              now_mono_us - config.bench_epoch_received_mono_us)
        : 0ULL;
    const std::int64_t utc_seconds = bench
        ? static_cast<std::int64_t>(config.bench_utc_epoch_seconds) +
              static_cast<std::int64_t>(elapsed_us / 1000000ULL)
        : static_cast<std::int64_t>(std::time(nullptr));
    const auto tai_seconds = ar::iec61850::time_sync::try_seed_ptp_tai_seconds(
        utc_seconds, config.current_utc_offset);
    if (tai_seconds.has_value()) {
        initial_time.seconds = *tai_seconds;
        initial_time.nanoseconds = bench
            ? static_cast<std::uint32_t>((elapsed_us % 1000000ULL) * 1000ULL)
            : 0U;
    }
    const auto result = esp_eth_ioctl(
        eth_handle, static_cast<esp_eth_io_cmd_t>(ETH_MAC_ESP_CMD_S_PTP_TIME),
        &initial_time);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Hardware PTP clock seed failed: %s", esp_err_to_name(result));
        return false;
    }
    if (!tai_seconds.has_value()) {
        ESP_LOGW(kTag,
            "No plausible UTC epoch: EMAC PTP free-runs without PTP-timescale validity; "
            "clock quality remains UNKNOWN (not IED traceable)");
        return false;
    }
    // A successful write ioctl alone does not prove that the MAC is operating
    // on the intended timebase. Verify once at control-plane START, never
    // during each SV sample or PTP packet.
    eth_mac_time_t readback{};
    const auto read_result = esp_eth_ioctl(
        eth_handle, static_cast<esp_eth_io_cmd_t>(ETH_MAC_ESP_CMD_G_PTP_TIME),
        &readback);
    if (read_result != ESP_OK || readback.nanoseconds >= 1'000'000'000U ||
        static_cast<std::uint64_t>(readback.seconds) <
            static_cast<std::uint64_t>(*tai_seconds) ||
        static_cast<std::uint64_t>(readback.seconds) >
            static_cast<std::uint64_t>(*tai_seconds) + 1ULL) {
        ESP_LOGE(kTag,
            "Hardware PTP TAI seed read-back invalid; withholding PTP-timescale claim");
        return false;
    }
    ESP_LOGI(kTag,
        "PTP/TAI seed applied from local RTC and configured UTC offset; "
        "absolute time accuracy/traceability NOT established");
    return true;
}

[[nodiscard]] PtpTimestamp hardware_time(const esp_eth_handle_t eth_handle) noexcept {
    eth_mac_time_t timestamp{};
    const auto result = esp_eth_ioctl(
        eth_handle,
        static_cast<esp_eth_io_cmd_t>(ETH_MAC_ESP_CMD_G_PTP_TIME),
        &timestamp);
    if (result != ESP_OK || timestamp.nanoseconds >= 1'000'000'000U) {
        return {};
    }
    return to_ptp_timestamp(timestamp);
}

[[nodiscard]] bool send_frame(
    const esp_eth_handle_t eth_handle,
    std::vector<std::uint8_t>& frame) noexcept {
    if (frame.empty()) return false;
    const auto result = esp_eth_transmit(eth_handle, frame.data(), frame.size());
    if (result != ESP_OK) {
        if (result == ESP_ERR_INVALID_STATE)
            g_source_start_reason.store(
                PtpSourceStartReason::ethernet_tx_invalid_state, std::memory_order_release);
        ESP_LOGE(kTag, "PTP general TX failed: %s", esp_err_to_name(result));
        return false;
    }
    return true;
}

[[nodiscard]] bool transmit_with_hw_timestamp(
    const esp_eth_handle_t eth_handle,
    std::vector<std::uint8_t>& frame,
    PtpTimestamp& timestamp) noexcept {
    if (frame.empty()) return false;
    eth_mac_time_t tx_timestamp{};
    // ESP-IDF 5.5 forwards argc as the number of variadic arguments. The
    // ESP32-P4 EMAC implementation computes buf_num = argc / 2, therefore one
    // Ethernet frame requires two arguments: buffer pointer + buffer length.
    const auto result = esp_eth_transmit_ctrl_vargs(
        eth_handle,
        &tx_timestamp,
        2U,
        frame.data(),
        frame.size());
    if (result != ESP_OK) {
        if (result == ESP_ERR_INVALID_STATE)
            g_source_start_reason.store(
                PtpSourceStartReason::ethernet_tx_invalid_state, std::memory_order_release);
        ESP_LOGE(kTag, "PTP hardware-timestamp TX failed: %s", esp_err_to_name(result));
        return false;
    }
    if (!valid_hw_timestamp(tx_timestamp)) {
        ESP_LOGE(kTag,
                 "PTP TX completed without a valid hardware timestamp: sec=%lu ns=%lu",
                 static_cast<unsigned long>(tx_timestamp.seconds),
                 static_cast<unsigned long>(tx_timestamp.nanoseconds));
        return false;
    }
    timestamp = to_ptp_timestamp(tx_timestamp);
    return true;
}

[[nodiscard]] bool send_sync_follow_up(PtpLabContext& context) {
    if (!context.runtime.has_value()) return false;
    auto& runtime = *context.runtime;

    auto sync = runtime.prepare_sync(PtpTimestamp{});
    if (!sync.has_value()) return false;

    PtpTimestamp tx_timestamp;
    if (!transmit_with_hw_timestamp(
            context.eth_handle,
            sync->ethernet_frame,
            tx_timestamp)) {
        return false;
    }
    runtime.record_sent(PtpMessageType::sync);
    record_live_sent(PtpMessageType::sync);

    auto follow_up = runtime.prepare_follow_up(sync->sequence_id, tx_timestamp);
    if (!follow_up.has_value() ||
        !send_frame(context.eth_handle, follow_up->ethernet_frame)) {
        return false;
    }
    runtime.record_sent(PtpMessageType::follow_up);
    record_live_sent(PtpMessageType::follow_up);
    return true;
}

[[nodiscard]] bool send_announce(PtpLabContext& context) {
    if (!context.runtime.has_value()) return false;
    auto& runtime = *context.runtime;
    auto announce = runtime.prepare_announce(hardware_time(context.eth_handle));
    if (!announce.has_value() ||
        !send_frame(context.eth_handle, announce->ethernet_frame)) {
        return false;
    }
    runtime.record_sent(PtpMessageType::announce);
    record_live_sent(PtpMessageType::announce);
    return true;
}

[[nodiscard]] bool send_pdelay_response(
    PtpLabContext& context,
    const PdelayRequestEvent& request) {
    if (!context.runtime.has_value()) return false;
    auto& runtime = *context.runtime;

    auto response = runtime.prepare_pdelay_response(
        request.requesting_port_identity,
        request.sequence_id,
        request.log_message_interval,
        request.receive_timestamp);
    if (!response.has_value()) return false;

    PtpTimestamp response_tx_timestamp;
    if (!transmit_with_hw_timestamp(
            context.eth_handle,
            response->ethernet_frame,
            response_tx_timestamp)) {
        return false;
    }
    runtime.record_sent(PtpMessageType::pdelay_resp);
    record_live_sent(PtpMessageType::pdelay_resp);

    auto follow_up = runtime.prepare_pdelay_response_follow_up(
        request.requesting_port_identity,
        request.sequence_id,
        request.log_message_interval,
        response_tx_timestamp);
    if (!follow_up.has_value() ||
        !send_frame(context.eth_handle, follow_up->ethernet_frame)) {
        return false;
    }
    runtime.record_sent(PtpMessageType::pdelay_resp_follow_up);
    record_live_sent(PtpMessageType::pdelay_resp_follow_up);

    if (g_pdelay_frames_sent.load(std::memory_order_relaxed) == 2U) {
        ESP_LOGI(kTag, "First hardware-timestamped Pdelay exchange completed");
    }
    return true;
}

esp_err_t ptp_input_info(
    esp_eth_handle_t,
    std::uint8_t* buffer,
    const std::uint32_t length,
    void* priv,
    void* info) {
    auto* context = static_cast<PtpLabContext*>(priv);
    if (buffer == nullptr) return ESP_OK;

    if (g_ptp_accept_rx.load(std::memory_order_acquire) &&
        context != nullptr &&
        context->rx_respond_to_peer_delay &&
        context->pdelay_queue != nullptr &&
        info != nullptr) {
        PtpFrame frame;
        if (ar::iec61850::time_sync::PtpCodec::try_parse_ethernet_frame(
                std::span<const std::uint8_t>{buffer, length},
                frame) &&
            frame.peer_delay_multicast &&
            frame.header.message_type == PtpMessageType::pdelay_req &&
            frame.header.domain_number == context->rx_domain &&
            frame.header.transport_specific == context->rx_transport_specific) {
            const auto& rx_timestamp = *static_cast<const eth_mac_time_t*>(info);
            if (valid_hw_timestamp(rx_timestamp)) {
                const PdelayRequestEvent event{
                    frame.header.source_port_identity,
                    to_ptp_timestamp(rx_timestamp),
                    frame.header.sequence_id,
                    frame.header.log_message_interval,
                };
                if (xQueueSend(context->pdelay_queue, &event, 0U) == pdTRUE &&
                    context->task_handle != nullptr) {
                    xTaskNotifyGive(context->task_handle);
                }
            }
        }
    }

    std::free(buffer);
    return ESP_OK;
}

void finish_runtime(PtpLabContext& context) {
    g_ptp_accept_rx.store(false, std::memory_order_release);
    g_ptp_ready.store(false, std::memory_order_release);
    g_bench_global_active.store(false, std::memory_order_release);
    g_ptp_last_announce_us.store(0LL, std::memory_order_release);
    g_ptp_last_sync_pair_us.store(0LL, std::memory_order_release);
    if (context.runtime.has_value()) {
        context.runtime->stop();
        const auto status = context.runtime->status();
        ESP_LOGI(kTag,
                 "PTP stopped: Announce=%llu Sync=%llu FollowUp=%llu PdelayFrames=%llu%s%s",
                 static_cast<unsigned long long>(status.announce_sent),
                 static_cast<unsigned long long>(status.sync_sent),
                 static_cast<unsigned long long>(status.follow_up_sent),
                 static_cast<unsigned long long>(status.peer_delay_responses_sent),
                 status.last_error.empty() ? "" : " lastError=",
                 status.last_error.empty() ? "" : status.last_error.c_str());
    }
    disable_hardware_ptp(context.eth_handle);
    context.task_handle = nullptr;
    g_ptp_ready.store(false, std::memory_order_release);
    g_ptp_started.store(false, std::memory_order_release);
}

void ptp_lab_task(void* argument) {
    auto& context = *static_cast<PtpLabContext*>(argument);
    vTaskDelay(kStartupDelay);

    if (!enable_hardware_ptp(context.eth_handle)) {
        g_source_start_reason.store(
            PtpSourceStartReason::ieee1588_clock_unavailable, std::memory_order_release);
        ESP_LOGE(kTag, "PTP lab broadcaster stopped: hardware timestamp clock unavailable");
        finish_runtime(context);
        vTaskDelete(nullptr);
        return;
    }
    const auto selected_ptp_config = selected_config();
    const bool ptp_tai_epoch_seeded = seed_hardware_clock(
        context.eth_handle, selected_ptp_config);
    if (selected_ptp_config.bench_global_gm && !ptp_tai_epoch_seeded) {
        g_source_start_reason.store(
            PtpSourceStartReason::ieee1588_clock_unavailable, std::memory_order_release);
        ESP_LOGE(kTag, "BENCH GM rejected: EMAC PTP/TAI seed not established");
        finish_runtime(context);
        vTaskDelete(nullptr);
        return;
    }

    const auto mac_result = esp_eth_ioctl(
        context.eth_handle,
        ETH_CMD_G_MAC_ADDR,
        context.source_mac.data());
    if (mac_result != ESP_OK) {
        g_source_start_reason.store(
            PtpSourceStartReason::mac_identity_unavailable, std::memory_order_release);
        ESP_LOGE(kTag, "PTP lab broadcaster stopped: MAC address unavailable: %s",
                 esp_err_to_name(mac_result));
        finish_runtime(context);
        vTaskDelete(nullptr);
        return;
    }

    auto profile = make_runtime_options(context.source_mac);
    if (!profile.has_value()) {
        g_source_start_reason.store(
            PtpSourceStartReason::invalid_runtime_profile, std::memory_order_release);
        finish_runtime(context);
        vTaskDelete(nullptr);
        return;
    }

    // IEEE 1588-2008 timeProperties must match the actual EMAC timescale.
    // Flagging PTP/TAI does not imply traceability to UTC/GNSS.
#if defined(CONFIG_AR_PTP_MANUAL_UTC_OFFSET_VALID) && CONFIG_AR_PTP_MANUAL_UTC_OFFSET_VALID
    constexpr bool kOperatorVerifiedTaiUtcOffset = true;
#else
    constexpr bool kOperatorVerifiedTaiUtcOffset = false;
#endif
    profile->time_properties = selected_ptp_config.bench_global_gm
        ? ar::iec61850::time_sync::PtpTimeProperties{true, true, true, true}
        : conservative_lab_time_properties(
              ptp_tai_epoch_seeded, kOperatorVerifiedTaiUtcOffset);
    if (!profile->time_properties.ptp_timescale) {
        ESP_LOGW(kTag, "PTP timescale/UTC offset UNKNOWN: local RTC or "
                 "TAI-UTC offset not independently verified; no traceability claim");
    }
    context.runtime.emplace(std::move(*profile));
    if (!context.runtime->start()) {
        g_source_start_reason.store(
            PtpSourceStartReason::runtime_start_rejected, std::memory_order_release);
        ESP_LOGE(kTag,
                 "PTP runtime rejected profile: %s",
                 context.runtime->status().last_error.c_str());
        finish_runtime(context);
        vTaskDelete(nullptr);
        return;
    }

    g_bench_global_active.store(
        selected_ptp_config.bench_global_gm && ptp_tai_epoch_seeded,
        std::memory_order_release);
    const auto options = context.runtime->options();
    // Nonstandard bench cadences are explicitly opt-in, so expiry tracks the
    // larger validated interval. Default power-profile TTL = 3 seconds.
    const auto interval_ms = std::max(
        options.announce_interval.count(), options.sync_interval.count());
    g_ptp_freshness_timeout_us.store(
        static_cast<std::int64_t>(interval_ms) * 3000LL,
        std::memory_order_release);
    context.rx_domain = options.domain_number;
    context.rx_transport_specific = options.transport_specific;
    context.rx_respond_to_peer_delay = options.respond_to_peer_delay;

    bool pdelay_enabled = false;
    if (options.respond_to_peer_delay && context.pdelay_queue != nullptr) {
        const auto input_result = esp_eth_update_input_path_info(
            context.eth_handle,
            &ptp_input_info,
            &context);
        if (input_result == ESP_OK) {
            pdelay_enabled = true;
            g_ptp_accept_rx.store(true, std::memory_order_release);
        } else {
            ESP_LOGW(kTag,
                     "Hardware RX timestamp path unavailable; Pdelay responder disabled: %s",
                     esp_err_to_name(input_result));
        }
    }

    // A Power Profile slave needs peer-delay replies to qualify network
    // propagation time. Do not advertise a usable timing source when the
    // requested hardware RX/Pdelay path could not be established.
    if (options.respond_to_peer_delay && !pdelay_enabled) {
        g_source_start_reason.store(
            PtpSourceStartReason::pdelay_rx_timestamp_unavailable,
            std::memory_order_release);
        ESP_LOGE(kTag, "PTP SOURCE rejected: required hardware Pdelay response path unavailable");
        finish_runtime(context);
        vTaskDelete(nullptr);
        return;
    }

    const auto clock_identity = format_ptp_clock_identity(options.clock_identity);
    ESP_LOGW(kTag,
             "PTP LAB TX enabled: troubleshooting/interoperability helper only; not a GPS-backed or certified grandmaster");
    ESP_LOGI(kTag,
             "PTP runtime domain=%u transportSpecific=0x%X clockIdentity=%s port=%u VLAN=%s Announce=%lld ms Sync=%lld ms HW Sync TX=enabled HW Pdelay=%s",
             static_cast<unsigned>(options.domain_number),
             static_cast<unsigned>(options.transport_specific),
             clock_identity.c_str(),
             static_cast<unsigned>(options.port_number),
             options.vlan_id.has_value() ? "tagged" : "untagged",
             static_cast<long long>(options.announce_interval.count()),
             static_cast<long long>(options.sync_interval.count()),
             pdelay_enabled ? "enabled" : "disabled");

    std::uint32_t consecutive_failures = 0U;
    PtpSourceReadinessGate source_readiness;
    while (!g_stop_requested.load(std::memory_order_acquire)) {
        // A stalled/suspended SOURCE task must not carry yesterday's READY
        // into a fresh Announce+Sync. Accessors also perform this live check.
        if (g_ptp_ready.load(std::memory_order_acquire) &&
            !source_has_fresh_evidence()) {
            source_readiness.invalidate();
            g_ptp_ready.store(false, std::memory_order_release);
            g_ptp_last_announce_us.store(0LL, std::memory_order_release);
            g_ptp_last_sync_pair_us.store(0LL, std::memory_order_release);
        }
        bool attempted = false;
        bool success = true;

        PdelayRequestEvent pdelay_request;
        while (context.pdelay_queue != nullptr &&
               xQueueReceive(context.pdelay_queue, &pdelay_request, 0U) == pdTRUE) {
            attempted = true;
            const bool response_delivered = send_pdelay_response(context, pdelay_request);
            source_readiness.observe_peer_delay(response_delivered);
            success = response_delivered && success;
        }

        const auto due = context.runtime->poll_due();
        if (due.announce) {
            attempted = true;
            const bool announce_delivered = send_announce(context);
            source_readiness.observe_announce(announce_delivered);
            if (announce_delivered) {
                g_ptp_last_announce_us.store(
                    esp_timer_get_time(), std::memory_order_release);
            }
            success = announce_delivered && success;
        }
        if (due.sync) {
            attempted = true;
            // True only if the actual Sync TX timestamp AND matching Follow_Up
            // TX succeeded; Sync alone never qualifies the source.
            const bool pair_delivered = send_sync_follow_up(context);
            source_readiness.observe_sync_pair(pair_delivered);
            if (pair_delivered) {
                g_ptp_last_sync_pair_us.store(
                    esp_timer_get_time(), std::memory_order_release);
            }
            success = pair_delivered && success;
        }
        // A failed peer-delay response in the same batch must also revoke
        // any freshly sent core messages; no readiness resurrection by Pdelay.
        if (!success) {
            source_readiness.invalidate();
            g_ptp_last_announce_us.store(0LL, std::memory_order_release);
            g_ptp_last_sync_pair_us.store(0LL, std::memory_order_release);
        }
        const bool qualified_ready = source_readiness.ready() &&
            PtpSourceEvidenceFreshness::fresh(
                esp_timer_get_time(),
                g_ptp_last_announce_us.load(std::memory_order_acquire),
                g_ptp_last_sync_pair_us.load(std::memory_order_acquire),
                g_ptp_freshness_timeout_us.load(std::memory_order_acquire));
        const bool was_ready = g_ptp_ready.load(std::memory_order_acquire);
        if (!qualified_ready) {
            g_ptp_ready.store(false, std::memory_order_release);
        } else if (!was_ready) {
            g_ptp_source_generation.fetch_add(1U, std::memory_order_release);
            g_source_start_reason.store(
                PtpSourceStartReason::ready, std::memory_order_release);
            g_ptp_ready.store(true, std::memory_order_release);
        }

        if (attempted) {
            if (success) {
                consecutive_failures = 0U;
                if (qualified_ready) context.runtime->clear_error();
            } else {
                context.runtime->record_error("Ethernet transmit or hardware timestamp failure");
                g_tx_failure_count.fetch_add(1U, std::memory_order_relaxed);
                ++consecutive_failures;
                if (consecutive_failures == 1U || (consecutive_failures % 16U) == 0U) {
                    ESP_LOGW(kTag,
                             "PTP TX failed (%lu consecutive); Ethernet link/driver may not be ready",
                             static_cast<unsigned long>(consecutive_failures));
                }
            }
        }

        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5));
    }

    finish_runtime(context);
    vTaskDelete(nullptr);
}

#else

void ptp_lab_task(void*) {
    g_source_start_reason.store(
        PtpSourceStartReason::source_task_unsupported, std::memory_order_release);
    ESP_LOGE(kTag,
             "PTP lab broadcaster requires ESP32-P4 IEEE1588 support and the ESP-IDF 5.x EMAC timestamp adapter");
    g_ptp_accept_rx.store(false, std::memory_order_release);
    g_ptp_context.task_handle = nullptr;
    g_ptp_started.store(false, std::memory_order_release);
    vTaskDelete(nullptr);
}

#endif

void stop_ptp_lab() noexcept;

void start_ptp_lab(const esp_eth_handle_t eth_handle) {
    if (eth_handle == nullptr) {
        g_source_start_reason.store(
            PtpSourceStartReason::ethernet_missing, std::memory_order_release);
        ESP_LOGE(kTag, "PTP lab broadcaster not started: Ethernet handle is null");
        return;
    }

    portENTER_CRITICAL(&g_control_mux);
    if (g_ptp_started.load(std::memory_order_relaxed)) {
        g_source_start_reason.store(
            PtpSourceStartReason::already_active, std::memory_order_release);
        portEXIT_CRITICAL(&g_control_mux);
        return;
    }
    g_source_start_reason.store(
        PtpSourceStartReason::starting, std::memory_order_release);
    g_ptp_started.store(true, std::memory_order_relaxed);
    portEXIT_CRITICAL(&g_control_mux);

    reset_live_status();
    g_bench_global_active.store(false, std::memory_order_release);
    g_ptp_ready.store(false, std::memory_order_release);
    g_ptp_last_announce_us.store(0LL, std::memory_order_release);
    g_ptp_last_sync_pair_us.store(0LL, std::memory_order_release);
    g_ptp_accept_rx.store(false, std::memory_order_release);
    g_stop_requested.store(false, std::memory_order_release);
    g_ptp_context.eth_handle = eth_handle;
    g_ptp_context.runtime.reset();
    g_ptp_context.task_handle = nullptr;
    g_ptp_context.rx_respond_to_peer_delay = false;

    if (g_ptp_context.pdelay_queue == nullptr) {
        g_ptp_context.pdelay_queue = xQueueCreate(
            kPdelayQueueDepth,
            sizeof(PdelayRequestEvent));
    } else {
        xQueueReset(g_ptp_context.pdelay_queue);
    }
    if (g_ptp_context.pdelay_queue == nullptr) {
        ESP_LOGW(kTag, "Pdelay queue allocation failed; broadcaster will run without Pdelay RX responses");
    }

    if (xTaskCreatePinnedToCore(
            &ptp_lab_task,
            "ar_ptp_lab",
            7168,
            &g_ptp_context,
            4,
            &g_ptp_context.task_handle,
            0) != pdPASS) {
        g_ptp_context.task_handle = nullptr;
        g_ptp_started.store(false, std::memory_order_release);
        g_source_start_reason.store(
            PtpSourceStartReason::source_task_create_failed, std::memory_order_release);
        ESP_LOGE(kTag, "Failed to create PTP lab task on CPU0");
        return;
    }

    // At 1 s Announce/Sync intervals the first TX after PTP MAC activation
    // can fail transiently even with an established PHY link. Allow three
    // full source epochs; do NOT call it ready without actual three-frame TX.
    // GUI awaiting_start has a separate bounded 10-second deadline.
    constexpr unsigned kStartReadyPolls = 350U;
    for (unsigned attempt = 0U; attempt < kStartReadyPolls; ++attempt) {
        if (source_has_fresh_evidence() || !g_ptp_started.load(std::memory_order_acquire)) return;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (g_source_start_reason.load(std::memory_order_acquire) !=
        PtpSourceStartReason::ethernet_tx_invalid_state) {
        g_source_start_reason.store(
            PtpSourceStartReason::source_tx_evidence_timeout, std::memory_order_release);
    }
    ESP_LOGE(kTag,
             "PTP source readiness timeout: no verified Announce/Sync/Follow_Up TX; reason=%s",
             source_start_reason_name(g_source_start_reason.load(std::memory_order_acquire)));
    stop_ptp_lab();
}

void stop_ptp_lab() noexcept {
    if (!g_ptp_started.load(std::memory_order_acquire)) return;
    g_stop_requested.store(true, std::memory_order_release);
    if (g_ptp_context.task_handle != nullptr) {
        xTaskNotifyGive(g_ptp_context.task_handle);
    }
    constexpr unsigned kCleanupPolls = 100U;
    for (unsigned attempt = 0U; attempt < kCleanupPolls; ++attempt) {
        if (!g_ptp_started.load(std::memory_order_acquire)) return;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    ESP_LOGE(kTag, "PTP SOURCE cleanup timeout; EMAC rollback incomplete after 500 ms");
}

[[nodiscard]] bool ptp_lab_is_running() noexcept {
    return source_has_fresh_evidence();
}

[[nodiscard]] bool configure_ptp_lab(const ar_ptp_lab_config_t& config) {
    if (g_ptp_started.load(std::memory_order_acquire)) {
        g_ptp_config_failure.store(
            PtpConfigFailure::active_runtime, std::memory_order_release);
        return false;
    }

    std::string error;
    if (!validate_runtime_config(config, error)) {
        const bool invalid_source_cadence =
            config.role == AR_PTP_ROLE_LAB_SOURCE &&
            !inspect_ptp_power_profile_cadence(
                config.announce_interval_ms, config.sync_interval_ms,
                config.respond_to_peer_delay).compatible();
        // Nonstandard experimental builds are not the public SOURCE
        // admission profile. Other errors retain an independent reason.
        g_ptp_config_failure.store(
            invalid_source_cadence ? PtpConfigFailure::source_power_profile :
                                     PtpConfigFailure::invalid_profile,
            std::memory_order_release);
        ESP_LOGE(kTag, "PTP runtime configuration rejected: %s", error.c_str());
        return false;
    }

    portENTER_CRITICAL(&g_control_mux);
    if (g_ptp_started.load(std::memory_order_relaxed)) {
        g_ptp_config_failure.store(
            PtpConfigFailure::raced_runtime_activation, std::memory_order_release);
        portEXIT_CRITICAL(&g_control_mux);
        return false;
    }
    g_runtime_config_override = config;
    g_runtime_config_override_valid = true;
    g_ptp_config_failure.store(PtpConfigFailure::none, std::memory_order_release);
    portEXIT_CRITICAL(&g_control_mux);
    return true;
}

} // namespace
} // namespace ar::esp32p4::smv

extern "C" void ar_ptp_lab_get_default_config(ar_ptp_lab_config_t* config) {
    if (config == nullptr) return;
    ar::esp32p4::smv::fill_kconfig_defaults(*config);
}

extern "C" bool ar_ptp_lab_configure(const ar_ptp_lab_config_t* config) {
    if (config == nullptr) return false;
    return ar::esp32p4::smv::configure_ptp_lab(*config);
}
extern "C" const char* ar_ptp_lab_last_config_error(void) {
    return ar::esp32p4::smv::ptp_config_failure_name(
        ar::esp32p4::smv::g_ptp_config_failure.load(std::memory_order_acquire));
}

extern "C" bool ar_ptp_lab_get_config(ar_ptp_lab_config_t* config) {
    if (config == nullptr) return false;
    *config = ar::esp32p4::smv::selected_config();
    return true;
}

extern "C" void ar_ptp_lab_start(const esp_eth_handle_t eth_handle) {
    ar::esp32p4::smv::start_ptp_lab(eth_handle);
}

extern "C" void ar_ptp_lab_stop(void) {
    ar::esp32p4::smv::stop_ptp_lab();
}

extern "C" bool ar_ptp_lab_is_running(void) {
    return ar::esp32p4::smv::ptp_lab_is_running();
}

// Lifetime/ownership is deliberately independent of measured PTP READY.
// Used only by the facade to prevent simultaneous EMAC timebase owners.
extern "C" bool ar_ptp_source_runtime_active(void) {
    return ar::esp32p4::smv::g_ptp_started.load(std::memory_order_acquire);
}

extern "C" const char* ar_ptp_source_last_start_reason(void) {
    return ar::esp32p4::smv::source_start_reason_name(
        ar::esp32p4::smv::g_source_start_reason.load(std::memory_order_acquire));
}

extern "C" bool ar_ptp_lab_bench_global_ready(void) {
    return ar::esp32p4::smv::g_bench_global_active.load(
               std::memory_order_acquire) &&
           ar::esp32p4::smv::source_has_fresh_evidence();
}

extern "C" std::uint32_t ar_ptp_source_generation(void) {
    if (!ar::esp32p4::smv::ptp_lab_is_running()) return 0U;
    return ar::esp32p4::smv::g_ptp_source_generation.load(
        std::memory_order_acquire);
}

extern "C" bool ar_ptp_lab_get_status(ar_ptp_lab_status_t* status) {
    if (status == nullptr) return false;
    status->is_running = ar::esp32p4::smv::ptp_lab_is_running();
    status->announce_sent = ar::esp32p4::smv::g_announce_sent.load(std::memory_order_relaxed);
    status->sync_sent = ar::esp32p4::smv::g_sync_sent.load(std::memory_order_relaxed);
    status->follow_up_sent = ar::esp32p4::smv::g_follow_up_sent.load(std::memory_order_relaxed);
    status->peer_delay_frames_sent =
        ar::esp32p4::smv::g_pdelay_frames_sent.load(std::memory_order_relaxed);
    status->tx_failure_count =
        ar::esp32p4::smv::g_tx_failure_count.load(std::memory_order_relaxed);
    return true;
}
