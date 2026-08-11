// SPDX-License-Identifier: GPL-3.0-or-later

#include "ptp_lab_task.hpp"

#include "ariec61850/time_sync/ptp.hpp"

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

#include <array>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <vector>

namespace ar::esp32p4::smv {
namespace {

using ar::iec61850::time_sync::PtpBuildOptions;
using ar::iec61850::time_sync::PtpClockAccuracy;
using ar::iec61850::time_sync::PtpCodec;
using ar::iec61850::time_sync::PtpFrame;
using ar::iec61850::time_sync::PtpMessageType;
using ar::iec61850::time_sync::PtpPortIdentity;
using ar::iec61850::time_sync::PtpTimeSource;
using ar::iec61850::time_sync::PtpTimestamp;
using ar::iec61850::time_sync::ptp_general_multicast_mac;
using ar::iec61850::time_sync::ptp_peer_delay_multicast_mac;

constexpr char kTag[] = "ar_ptp_lab";
constexpr std::uint16_t kPortNumber = 1U;
constexpr std::uint8_t kPriority1 = 128U;
constexpr std::uint8_t kPriority2 = 128U;
constexpr std::uint8_t kClockClass = 248U;
constexpr std::uint16_t kOffsetScaledLogVariance = 0xFFFFU;
constexpr std::int16_t kCurrentUtcOffset = 37;
constexpr TickType_t kStartupDelay = pdMS_TO_TICKS(500);
constexpr UBaseType_t kPdelayQueueDepth = 8U;

struct PdelayRequestEvent final {
    PtpPortIdentity requesting_port_identity{};
    PtpTimestamp receive_timestamp{};
    std::uint16_t sequence_id{};
    std::int8_t log_message_interval{};
};

struct PtpLabContext final {
    esp_eth_handle_t eth_handle{};
    std::array<std::uint8_t, 6> source_mac{};
    PtpBuildOptions base_options{};
    QueueHandle_t pdelay_queue{};
    TaskHandle_t task_handle{};
    std::uint16_t announce_sequence{};
    std::uint16_t sync_sequence{};
    std::uint64_t pdelay_responses{};
};

PtpLabContext g_ptp_context{};
bool g_ptp_started = false;

[[nodiscard]] std::int8_t interval_to_log2(const std::uint32_t interval_ms) noexcept {
    if (interval_ms == 0U) return 0;
    // PTP logMessageInterval is log2(seconds). The configured lab intervals are
    // represented by the nearest useful bounded integer exponent.
    std::uint32_t numerator = interval_ms;
    std::int8_t exponent = 0;
    while (numerator < 1000U && exponent > -7) {
        numerator *= 2U;
        --exponent;
    }
    while (numerator >= 2000U && exponent < 7) {
        numerator = (numerator + 1U) / 2U;
        ++exponent;
    }
    return exponent;
}

[[nodiscard]] std::array<std::uint8_t, 8> clock_identity_from_mac(
    const std::array<std::uint8_t, 6>& mac) noexcept {
    return {
        mac[0], mac[1], mac[2], 0xFFU, 0xFEU, mac[3], mac[4], mac[5],
    };
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
    const auto result = esp_eth_transmit_ctrl_vargs(
        eth_handle,
        &tx_timestamp,
        2U,
        frame.data(),
        frame.size());
    if (result != ESP_OK || !valid_hw_timestamp(tx_timestamp)) {
        return false;
    }
    timestamp = to_ptp_timestamp(tx_timestamp);
    return true;
}

[[nodiscard]] bool send_sync_follow_up(PtpLabContext& context) noexcept {
    auto sync_options = context.base_options;
    sync_options.sequence_id = context.sync_sequence;
    sync_options.log_message_interval = interval_to_log2(CONFIG_AR_PTP_SYNC_INTERVAL_MS);
    sync_options.two_step = true;
    // Two-step Sync intentionally carries zero originTimestamp. The exact MAC
    // egress timestamp returned by the ESP32-P4 DMA descriptor is carried by
    // the subsequent Follow_Up.
    sync_options.timestamp = {};

    const auto sync_message = PtpCodec::build_sync(sync_options);
    auto sync_frame = PtpCodec::build_ethernet_frame(
        ptp_general_multicast_mac,
        context.source_mac,
        sync_message);

    PtpTimestamp tx_timestamp;
    if (!transmit_with_hw_timestamp(context.eth_handle, sync_frame, tx_timestamp)) {
        return false;
    }

    auto follow_up_options = sync_options;
    follow_up_options.two_step = false;
    follow_up_options.timestamp = tx_timestamp;
    const auto follow_up_message = PtpCodec::build_follow_up(follow_up_options);
    auto follow_up_frame = PtpCodec::build_ethernet_frame(
        ptp_general_multicast_mac,
        context.source_mac,
        follow_up_message);
    if (!send_frame(context.eth_handle, follow_up_frame)) {
        return false;
    }

    context.sync_sequence = static_cast<std::uint16_t>(context.sync_sequence + 1U);
    return true;
}

[[nodiscard]] bool send_announce(PtpLabContext& context) noexcept {
    auto options = context.base_options;
    options.sequence_id = context.announce_sequence;
    options.log_message_interval = interval_to_log2(CONFIG_AR_PTP_ANNOUNCE_INTERVAL_MS);
    options.two_step = false;
    options.timestamp = hardware_time(context.eth_handle);

    const auto message = PtpCodec::build_announce(options, kCurrentUtcOffset);
    auto frame = PtpCodec::build_ethernet_frame(
        ptp_general_multicast_mac,
        context.source_mac,
        message);
    if (!send_frame(context.eth_handle, frame)) {
        return false;
    }
    context.announce_sequence = static_cast<std::uint16_t>(context.announce_sequence + 1U);
    return true;
}

[[nodiscard]] bool send_pdelay_response(
    PtpLabContext& context,
    const PdelayRequestEvent& request) noexcept {
    auto response_options = context.base_options;
    response_options.sequence_id = request.sequence_id;
    response_options.log_message_interval = request.log_message_interval;
    response_options.two_step = true;
    // t2: hardware receive timestamp of Pdelay_Req.
    response_options.timestamp = request.receive_timestamp;

    const auto response_message = PtpCodec::build_pdelay_resp(
        response_options,
        request.requesting_port_identity);
    auto response_frame = PtpCodec::build_ethernet_frame(
        ptp_peer_delay_multicast_mac,
        context.source_mac,
        response_message);

    PtpTimestamp response_tx_timestamp;
    if (!transmit_with_hw_timestamp(
            context.eth_handle,
            response_frame,
            response_tx_timestamp)) {
        return false;
    }

    auto follow_up_options = response_options;
    follow_up_options.two_step = false;
    // t3: hardware transmit timestamp of Pdelay_Resp.
    follow_up_options.timestamp = response_tx_timestamp;
    const auto follow_up_message = PtpCodec::build_pdelay_resp_follow_up(
        follow_up_options,
        request.requesting_port_identity);
    auto follow_up_frame = PtpCodec::build_ethernet_frame(
        ptp_peer_delay_multicast_mac,
        context.source_mac,
        follow_up_message);
    if (!send_frame(context.eth_handle, follow_up_frame)) {
        return false;
    }

    ++context.pdelay_responses;
    if (context.pdelay_responses == 1U) {
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
    if (buffer == nullptr) {
        return ESP_OK;
    }

    if (context != nullptr && context->pdelay_queue != nullptr && info != nullptr) {
        PtpFrame frame;
        if (PtpCodec::try_parse_ethernet_frame(
                std::span<const std::uint8_t>{buffer, length},
                frame) &&
            frame.header.message_type == PtpMessageType::pdelay_req &&
            frame.header.domain_number == context->base_options.domain_number) {
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

    // The injector is a standalone raw-L2 application and currently has no
    // competing TCP/IP input consumer. This registered input path owns the RX
    // buffer and must release it after inspection.
    std::free(buffer);
    return ESP_OK;
}

void ptp_lab_task(void* argument) {
    auto& context = *static_cast<PtpLabContext*>(argument);
    vTaskDelay(kStartupDelay);

    if (!enable_hardware_ptp(context.eth_handle)) {
        ESP_LOGE(kTag, "PTP lab broadcaster stopped: hardware timestamp clock unavailable");
        g_ptp_started = false;
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
        g_ptp_started = false;
        vTaskDelete(nullptr);
        return;
    }

    context.base_options.domain_number = static_cast<std::uint8_t>(CONFIG_AR_PTP_DOMAIN);
    context.base_options.source_port_identity.clock_identity =
        clock_identity_from_mac(context.source_mac);
    context.base_options.source_port_identity.port_number = kPortNumber;
    context.base_options.priority1 = kPriority1;
    context.base_options.priority2 = kPriority2;
    context.base_options.clock_class = kClockClass;
    context.base_options.clock_accuracy = PtpClockAccuracy::unknown;
    context.base_options.offset_scaled_log_variance = kOffsetScaledLogVariance;
    context.base_options.grandmaster_identity = context.base_options.source_port_identity.clock_identity;
    context.base_options.time_source = PtpTimeSource::internal_oscillator;

    bool pdelay_enabled = false;
    if (context.pdelay_queue != nullptr) {
        const auto input_result = esp_eth_update_input_path_info(
            context.eth_handle,
            &ptp_input_info,
            &context);
        if (input_result == ESP_OK) {
            pdelay_enabled = true;
        } else {
            ESP_LOGW(kTag,
                     "Hardware RX timestamp path unavailable; Pdelay responder disabled: %s",
                     esp_err_to_name(input_result));
        }
    }

    ESP_LOGW(kTag,
             "PTP LAB TX enabled: troubleshooting/interoperability helper only; not a GPS-backed or certified grandmaster");
    ESP_LOGI(kTag,
             "PTP domain=%u Announce=%d ms Sync=%d ms HW Sync TX timestamp=enabled HW Pdelay=%s",
             static_cast<unsigned>(context.base_options.domain_number),
             CONFIG_AR_PTP_ANNOUNCE_INTERVAL_MS,
             CONFIG_AR_PTP_SYNC_INTERVAL_MS,
             pdelay_enabled ? "enabled" : "disabled");

    TickType_t next_sync = xTaskGetTickCount();
    TickType_t next_announce = next_sync;
    std::uint32_t consecutive_failures = 0U;

    while (true) {
        bool attempted = false;
        bool success = true;

        PdelayRequestEvent pdelay_request;
        while (context.pdelay_queue != nullptr &&
               xQueueReceive(context.pdelay_queue, &pdelay_request, 0U) == pdTRUE) {
            attempted = true;
            success = send_pdelay_response(context, pdelay_request) && success;
        }

        const TickType_t now = xTaskGetTickCount();
        if (static_cast<std::int32_t>(now - next_announce) >= 0) {
            attempted = true;
            success = send_announce(context) && success;
            next_announce = now + pdMS_TO_TICKS(CONFIG_AR_PTP_ANNOUNCE_INTERVAL_MS);
        }
        if (static_cast<std::int32_t>(now - next_sync) >= 0) {
            attempted = true;
            success = send_sync_follow_up(context) && success;
            next_sync = now + pdMS_TO_TICKS(CONFIG_AR_PTP_SYNC_INTERVAL_MS);
        }

        if (attempted) {
            if (success) {
                consecutive_failures = 0U;
            } else {
                ++consecutive_failures;
                if (consecutive_failures == 1U || (consecutive_failures % 16U) == 0U) {
                    ESP_LOGW(kTag,
                             "PTP TX failed (%lu consecutive); Ethernet link/driver may not be ready",
                             static_cast<unsigned long>(consecutive_failures));
                }
            }
        }

        // A Pdelay RX notification wakes this task immediately; the timeout only
        // bounds the cadence check for Announce/Sync and avoids a busy loop.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5));
    }
}

#else

void ptp_lab_task(void*) {
    ESP_LOGE(kTag,
             "PTP lab broadcaster requires ESP32-P4 IEEE1588 support and the ESP-IDF 5.x EMAC timestamp adapter");
    g_ptp_started = false;
    vTaskDelete(nullptr);
}

#endif

void start_ptp_lab(const esp_eth_handle_t eth_handle) {
    if (eth_handle == nullptr) {
        ESP_LOGE(kTag, "PTP lab broadcaster not started: Ethernet handle is null");
        return;
    }
    if (g_ptp_started) {
        return;
    }

    g_ptp_context = {};
    g_ptp_context.eth_handle = eth_handle;
    g_ptp_context.pdelay_queue = xQueueCreate(
        kPdelayQueueDepth,
        sizeof(PdelayRequestEvent));
    if (g_ptp_context.pdelay_queue == nullptr) {
        ESP_LOGW(kTag, "Pdelay queue allocation failed; broadcaster will run without Pdelay RX responses");
    }

    g_ptp_started = true;
    if (xTaskCreatePinnedToCore(
            &ptp_lab_task,
            "ar_ptp_lab",
            6144,
            &g_ptp_context,
            4,
            &g_ptp_context.task_handle,
            0) != pdPASS) {
        g_ptp_started = false;
        if (g_ptp_context.pdelay_queue != nullptr) {
            vQueueDelete(g_ptp_context.pdelay_queue);
            g_ptp_context.pdelay_queue = nullptr;
        }
        ESP_LOGE(kTag, "Failed to create PTP lab task on CPU0");
    }
}

} // namespace
} // namespace ar::esp32p4::smv

extern "C" void ar_ptp_lab_start(const esp_eth_handle_t eth_handle) {
    ar::esp32p4::smv::start_ptp_lab(eth_handle);
}
