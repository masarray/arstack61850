// SPDX-License-Identifier: GPL-3.0-or-later

#include "ptp_lab_task.hpp"

#include "ariec61850/time_sync/ptp.hpp"
#include "ariec61850/time_sync/ptp_runtime.hpp"

#include "esp_err.h"
#include "esp_eth.h"
#include "esp_eth_driver.h"
#include "esp_eth_mac.h"
#include "esp_idf_version.h"
#include "esp_log.h"
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
std::atomic_bool g_ptp_started{false};
std::atomic_bool g_stop_requested{false};
std::atomic_bool g_ptp_accept_rx{false};
std::atomic<std::uint64_t> g_announce_sent{0U};
std::atomic<std::uint64_t> g_sync_sent{0U};
std::atomic<std::uint64_t> g_follow_up_sent{0U};
std::atomic<std::uint64_t> g_pdelay_frames_sent{0U};
std::atomic<std::uint64_t> g_tx_failure_count{0U};

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
#else
    config.vlan_enabled = false;
#endif
    config.vlan_id = static_cast<std::uint16_t>(CONFIG_AR_PTP_VLAN_ID);
    config.vlan_priority = static_cast<std::uint8_t>(CONFIG_AR_PTP_VLAN_PRIORITY);
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
    options.clock_class = config.clock_class;
    options.clock_accuracy = static_cast<PtpClockAccuracy>(config.clock_accuracy);
    options.offset_scaled_log_variance = config.offset_scaled_log_variance;
    options.time_source = static_cast<PtpTimeSource>(config.time_source);
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
    if (config.clock_identity_override && all_zero_identity(config.clock_identity)) {
        error = "clockIdentity override must be non-zero";
        return false;
    }
    return options_from_config(config, kValidationMac, &error).has_value();
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

void seed_hardware_clock(const esp_eth_handle_t eth_handle) noexcept {
    eth_mac_time_t initial_time{};
    const std::time_t system_time = std::time(nullptr);
    if (system_time > 0 &&
        static_cast<std::uint64_t>(system_time) <= std::numeric_limits<std::uint32_t>::max()) {
        initial_time.seconds = static_cast<std::uint32_t>(system_time);
    }
    const auto result = esp_eth_ioctl(
        eth_handle,
        static_cast<esp_eth_io_cmd_t>(ETH_MAC_ESP_CMD_S_PTP_TIME),
        &initial_time);
    if (result != ESP_OK) {
        ESP_LOGW(kTag, "Unable to seed hardware PTP time: %s", esp_err_to_name(result));
    }
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
    return esp_eth_transmit(eth_handle, frame.data(), frame.size()) == ESP_OK;
}

[[nodiscard]] bool transmit_with_hw_timestamp(
    const esp_eth_handle_t eth_handle,
    std::vector<std::uint8_t>& frame,
    PtpTimestamp& timestamp) noexcept {
    if (frame.empty()) return false;
    eth_mac_time_t tx_timestamp{};
    // esp_eth_transmit_ctrl_vargs argc counts buffer/length pairs. One PTP
    // Ethernet frame is exactly one pair.
    const auto result = esp_eth_transmit_ctrl_vargs(
        eth_handle,
        &tx_timestamp,
        1U,
        frame.data(),
        frame.size());
    if (result != ESP_OK || !valid_hw_timestamp(tx_timestamp)) {
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
    context.task_handle = nullptr;
    g_ptp_started.store(false, std::memory_order_release);
}

void ptp_lab_task(void* argument) {
    auto& context = *static_cast<PtpLabContext*>(argument);
    vTaskDelay(kStartupDelay);

    if (!enable_hardware_ptp(context.eth_handle)) {
        ESP_LOGE(kTag, "PTP lab broadcaster stopped: hardware timestamp clock unavailable");
        finish_runtime(context);
        vTaskDelete(nullptr);
        return;
    }
    seed_hardware_clock(context.eth_handle);

    const auto mac_result = esp_eth_ioctl(
        context.eth_handle,
        ETH_CMD_G_MAC_ADDR,
        context.source_mac.data());
    if (mac_result != ESP_OK) {
        ESP_LOGE(kTag, "PTP lab broadcaster stopped: MAC address unavailable: %s",
                 esp_err_to_name(mac_result));
        finish_runtime(context);
        vTaskDelete(nullptr);
        return;
    }

    auto profile = make_runtime_options(context.source_mac);
    if (!profile.has_value()) {
        finish_runtime(context);
        vTaskDelete(nullptr);
        return;
    }

    context.runtime.emplace(std::move(*profile));
    if (!context.runtime->start()) {
        ESP_LOGE(kTag,
                 "PTP runtime rejected profile: %s",
                 context.runtime->status().last_error.c_str());
        finish_runtime(context);
        vTaskDelete(nullptr);
        return;
    }

    const auto options = context.runtime->options();
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
    while (!g_stop_requested.load(std::memory_order_acquire)) {
        bool attempted = false;
        bool success = true;

        PdelayRequestEvent pdelay_request;
        while (context.pdelay_queue != nullptr &&
               xQueueReceive(context.pdelay_queue, &pdelay_request, 0U) == pdTRUE) {
            attempted = true;
            success = send_pdelay_response(context, pdelay_request) && success;
        }

        const auto due = context.runtime->poll_due();
        if (due.announce) {
            attempted = true;
            success = send_announce(context) && success;
        }
        if (due.sync) {
            attempted = true;
            success = send_sync_follow_up(context) && success;
        }

        if (attempted) {
            if (success) {
                consecutive_failures = 0U;
                context.runtime->clear_error();
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
    ESP_LOGE(kTag,
             "PTP lab broadcaster requires ESP32-P4 IEEE1588 support and the ESP-IDF 5.x EMAC timestamp adapter");
    g_ptp_accept_rx.store(false, std::memory_order_release);
    g_ptp_context.task_handle = nullptr;
    g_ptp_started.store(false, std::memory_order_release);
    vTaskDelete(nullptr);
}

#endif

void start_ptp_lab(const esp_eth_handle_t eth_handle) {
    if (eth_handle == nullptr) {
        ESP_LOGE(kTag, "PTP lab broadcaster not started: Ethernet handle is null");
        return;
    }

    portENTER_CRITICAL(&g_control_mux);
    if (g_ptp_started.load(std::memory_order_relaxed)) {
        portEXIT_CRITICAL(&g_control_mux);
        return;
    }
    g_ptp_started.store(true, std::memory_order_relaxed);
    portEXIT_CRITICAL(&g_control_mux);

    reset_live_status();
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
        ESP_LOGE(kTag, "Failed to create PTP lab task on CPU0");
    }
}

void stop_ptp_lab() noexcept {
    if (!g_ptp_started.load(std::memory_order_acquire)) return;
    g_stop_requested.store(true, std::memory_order_release);
    if (g_ptp_context.task_handle != nullptr) {
        xTaskNotifyGive(g_ptp_context.task_handle);
    }
}

[[nodiscard]] bool ptp_lab_is_running() noexcept {
    return g_ptp_started.load(std::memory_order_acquire);
}

[[nodiscard]] bool configure_ptp_lab(const ar_ptp_lab_config_t& config) {
    if (g_ptp_started.load(std::memory_order_acquire)) return false;

    std::string error;
    if (!validate_runtime_config(config, error)) {
        ESP_LOGE(kTag, "PTP runtime configuration rejected: %s", error.c_str());
        return false;
    }

    portENTER_CRITICAL(&g_control_mux);
    if (g_ptp_started.load(std::memory_order_relaxed)) {
        portEXIT_CRITICAL(&g_control_mux);
        return false;
    }
    g_runtime_config_override = config;
    g_runtime_config_override_valid = true;
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

extern "C" bool ar_ptp_lab_get_status(ar_ptp_lab_status_t* status) {
    if (status == nullptr) return false;
    status->is_running = ar::esp32p4::smv::g_ptp_started.load(std::memory_order_acquire);
    status->announce_sent = ar::esp32p4::smv::g_announce_sent.load(std::memory_order_relaxed);
    status->sync_sent = ar::esp32p4::smv::g_sync_sent.load(std::memory_order_relaxed);
    status->follow_up_sent = ar::esp32p4::smv::g_follow_up_sent.load(std::memory_order_relaxed);
    status->peer_delay_frames_sent =
        ar::esp32p4::smv::g_pdelay_frames_sent.load(std::memory_order_relaxed);
    status->tx_failure_count =
        ar::esp32p4::smv::g_tx_failure_count.load(std::memory_order_relaxed);
    return true;
}
