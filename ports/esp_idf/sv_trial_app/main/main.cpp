// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/ports/esp_idf/ethernet_port.hpp"
#include "ariec61850/sampled_values/deterministic_injector.hpp"
#include "ariec61850/sampled_values/frame.hpp"
#include "ariec61850/sampled_values/injector_controller.hpp"
#include "ariec61850/sampled_values/injector_presets.hpp"
#include "ariec61850/sampled_values/payload_writer.hpp"
#include "ariec61850/sampled_values/publisher.hpp"

#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace {

using namespace ar::iec61850;

constexpr std::uint32_t kSampleRateHz = 4'000U;
constexpr std::uint64_t kPublishIntervalUs = 250U;
constexpr std::uint16_t kSampleCountWrap = 4'000U;
constexpr std::size_t kPayloadBytes =
    sampled_values::injector_channel_count *
    sampled_values::SampledValuesPayloadWriter::int32_quality_pair_bytes;

// Waveshare ESP32-P4-ETH: IP101 PHY on RMII. ESP32-P4's
// ETH_ESP32_EMAC_DEFAULT_CONFIG() supplies the matching default RMII data-plane
// GPIOs; this board-specific layer supplies the PHY management values.
constexpr std::int32_t kPhyAddress = 1;
constexpr int kPhyResetGpio = 51;

constexpr EventBits_t kEthernetLinkUpBit = BIT0;
constexpr UBaseType_t kPublisherTaskPriority = 18U;
constexpr std::uint32_t kPublisherTaskStackBytes = 8'192U;

const char* kTag = "arstack_sv_trial";
EventGroupHandle_t g_link_events{};
TaskHandle_t g_publisher_task{};
esp_eth_handle_t g_eth_handle{};
sampled_values::InjectorController g_injector_controller{};

void ethernet_event_handler(
    void* argument,
    esp_event_base_t event_base,
    const std::int32_t event_id,
    void* event_data) {
    (void)argument;
    (void)event_base;

    auto handle = *static_cast<esp_eth_handle_t*>(event_data);
    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED: {
        std::array<std::uint8_t, 6U> mac{};
        if (esp_eth_ioctl(handle, ETH_CMD_G_MAC_ADDR, mac.data()) == ESP_OK) {
            ESP_LOGI(
                kTag,
                "Ethernet link up, MAC %02x:%02x:%02x:%02x:%02x:%02x",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        } else {
            ESP_LOGI(kTag, "Ethernet link up");
        }
        xEventGroupSetBits(g_link_events, kEthernetLinkUpBit);
        break;
    }
    case ETHERNET_EVENT_DISCONNECTED:
        xEventGroupClearBits(g_link_events, kEthernetLinkUpBit);
        ESP_LOGW(kTag, "Ethernet link down; SV transmission paused");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(kTag, "Ethernet driver started");
        break;
    case ETHERNET_EVENT_STOP:
        xEventGroupClearBits(g_link_events, kEthernetLinkUpBit);
        ESP_LOGI(kTag, "Ethernet driver stopped");
        break;
    default:
        break;
    }
}

esp_err_t initialize_ethernet(esp_eth_handle_t* out_handle) {
    if (out_handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = kPhyAddress;
    phy_config.reset_gpio_num = kPhyResetGpio;

    esp_eth_mac_t* mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
    if (mac == nullptr) {
        return ESP_FAIL;
    }

    // Generic IEEE 802.3 PHY support is sufficient for the first IP101
    // transmit proof and avoids depending on vendor-only PHY features.
    esp_eth_phy_t* phy = esp_eth_phy_new_generic(&phy_config);
    if (phy == nullptr) {
        mac->del(mac);
        return ESP_FAIL;
    }

    esp_eth_config_t driver_config = ETH_DEFAULT_CONFIG(mac, phy);
    const auto installed = esp_eth_driver_install(&driver_config, out_handle);
    if (installed != ESP_OK) {
        mac->del(mac);
        phy->del(phy);
        return installed;
    }

    return ESP_OK;
}

sampled_values::SampledValuesFrame make_sv_frame(
    const std::array<std::uint8_t, 6U>& source_mac) {
    const std::array<std::uint8_t, 6U> destination_mac{
        0x01U, 0x0CU, 0xCDU, 0x04U, 0x00U, 0x01U};

    sampled_values::SampledValueAsdu asdu;
    asdu.sv_id = "ARSTACK61850_INJECTOR";
    asdu.data_set_reference = "ARSTACK61850/LLN0$PhsMeas1";
    asdu.configuration_revision = 1U;
    asdu.sample_synchronization = 0U;
    asdu.sample_rate = std::uint16_t{kSampleRateHz};
    asdu.sample_mode = std::uint16_t{1U};
    asdu.sample_payload.resize(kPayloadBytes, 0U);

    return {
        ethernet::MacAddress{destination_mac},
        ethernet::MacAddress{source_mac},
        std::nullopt,
        0x4001U,
        0U,
        0U,
        sampled_values::SampledValuesPdu{{asdu}}};
}

bool write_injector_payload(
    sampled_values::SampledValueAsdu& asdu,
    const sampled_values::InjectorSample& sample) noexcept {
    if (asdu.sample_payload.size() != kPayloadBytes) {
        return false;
    }

    const auto payload = std::span<std::uint8_t>{
        asdu.sample_payload.data(), asdu.sample_payload.size()};
    for (std::size_t channel = 0U;
         channel < sampled_values::injector_channel_count;
         ++channel) {
        if (!sampled_values::SampledValuesPayloadWriter::write_int32_quality_pair(
                payload,
                channel,
                sample.values[channel],
                sample.qualities[channel])) {
            return false;
        }
    }
    return true;
}

void sv_timer_callback(void* argument) {
    (void)argument;
    if (g_publisher_task != nullptr) {
        xTaskNotifyGive(g_publisher_task);
    }
}

void publisher_task(void* argument) {
    (void)argument;
    g_publisher_task = xTaskGetCurrentTaskHandle();

    ESP_LOGI(kTag, "Waiting for Ethernet link before starting SV publisher");
    (void)xEventGroupWaitBits(
        g_link_events,
        kEthernetLinkUpBit,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY);

    std::array<std::uint8_t, 6U> source_mac{};
    ESP_ERROR_CHECK(esp_eth_ioctl(g_eth_handle, ETH_CMD_G_MAC_ADDR, source_mac.data()));

    auto frame = make_sv_frame(source_mac);
    std::array<std::uint8_t, 1'536U> frame_buffer{};
    ports::esp_idf::RawEthernetContext ethernet_context{g_eth_handle};
    const auto raw_port = ports::esp_idf::make_raw_ethernet_port(ethernet_context);
    const auto clock = ports::esp_idf::make_monotonic_clock();

    sampled_values::SampledValuesPublisher publisher(
        frame,
        frame_buffer,
        raw_port,
        sampled_values::SampledValuesPublisherConfig{
            kSampleRateHz,
            std::uint16_t{kSampleCountWrap},
            0U,
            true});

    const auto profile = sampled_values::make_balanced_4i4v_profile();
    const std::array<sampled_values::InjectorScenarioSegment, 1U> scenario{
        sampled_values::make_hold_segment(profile)};
    sampled_values::DeterministicSvInjector injector{
        scenario,
        kSampleRateHz,
        true};

    if (!publisher.valid() || !injector.valid()) {
        ESP_LOGE(kTag, "SV publisher/injector configuration is invalid");
        g_injector_controller.set_fault();
        vTaskDelete(nullptr);
        return;
    }

    if (g_injector_controller.configure({
            kSampleRateHz,
            sampled_values::InjectorScenarioKind::normal}) !=
            sampled_values::InjectorControlStatus::ok ||
        g_injector_controller.arm() != sampled_values::InjectorControlStatus::ok ||
        g_injector_controller.start() != sampled_values::InjectorControlStatus::ok) {
        ESP_LOGE(kTag, "Injector lifecycle initialization failed");
        g_injector_controller.set_fault();
        vTaskDelete(nullptr);
        return;
    }

    esp_timer_handle_t timer{};
    esp_timer_create_args_t timer_args{};
    timer_args.callback = &sv_timer_callback;
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = "arstack_sv_4k";
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, kPublishIntervalUs));

    const auto control = g_injector_controller.snapshot();
    ESP_LOGI(
        kTag,
        "Deterministic SV injector active: 4000/s, 50Hz, 4I+4V, APPID=0x4001, rev=%u run=%" PRIu64,
        static_cast<unsigned>(control.configuration_revision),
        control.run_sequence);

    std::uint64_t timer_notifications{};
    std::uint64_t coalesced_notifications{};
    std::uint64_t report_notifications{};
    std::optional<sampled_values::InjectorSample> pending_sample;

    while (g_injector_controller.running()) {
        const auto notifications = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        timer_notifications += notifications;
        report_notifications += notifications;
        if (notifications > 1U) {
            coalesced_notifications += static_cast<std::uint64_t>(notifications - 1U);
        }

        if ((xEventGroupGetBits(g_link_events) & kEthernetLinkUpBit) == 0U) {
            continue;
        }

        if (!pending_sample.has_value()) {
            sampled_values::InjectorSample next_sample{};
            if (!injector.step(next_sample)) {
                ESP_LOGE(kTag, "Deterministic injector stopped unexpectedly");
                g_injector_controller.set_fault();
                break;
            }
            pending_sample = next_sample;

            const auto expected_sample_count = static_cast<std::uint16_t>(
                next_sample.sample_index % static_cast<std::uint64_t>(kSampleCountWrap));
            if (publisher.next_sample_count() != expected_sample_count) {
                ESP_LOGE(
                    kTag,
                    "Logical sample/smpCnt divergence: sample=%" PRIu64 " expected=%u publisher=%u",
                    next_sample.sample_index,
                    static_cast<unsigned>(expected_sample_count),
                    static_cast<unsigned>(publisher.next_sample_count()));
                g_injector_controller.set_fault();
                break;
            }

            if (!write_injector_payload(frame.pdu.asdus.front(), next_sample)) {
                ESP_LOGE(kTag, "Deterministic payload update failed");
                g_injector_controller.set_fault();
                break;
            }
        }

        const auto result = publisher.poll(clock.monotonic_us());
        if (result.status == sampled_values::SampledValuesPublishStatus::sent) {
            pending_sample.reset();
        } else if (result.status != sampled_values::SampledValuesPublishStatus::not_due) {
            ESP_LOGE(
                kTag,
                "SV publish fault: status=%u io=%u encode=%u",
                static_cast<unsigned>(result.status),
                static_cast<unsigned>(result.io_status),
                static_cast<unsigned>(result.encode_status));
            g_injector_controller.set_fault();
            break;
        }

        if (report_notifications >= kSampleRateHz) {
            report_notifications -= kSampleRateHz;
            const auto& stats = publisher.statistics();
            const auto snapshot = g_injector_controller.snapshot();
            ESP_LOGI(
                kTag,
                "QA state=%u run=%" PRIu64 " sample=%" PRIu64
                " sent=%" PRIu64 " txFail=%" PRIu64 " encFail=%" PRIu64
                " late=%" PRIu64 " maxLateUs=%" PRIu64
                " timerTicks=%" PRIu64 " coalesced=%" PRIu64
                " heap=%u minHeap=%u",
                static_cast<unsigned>(snapshot.state),
                snapshot.run_sequence,
                injector.sample_index(),
                stats.frames_sent,
                stats.transmit_failures,
                stats.encode_failures,
                stats.late_polls,
                stats.maximum_lateness_us,
                timer_notifications,
                coalesced_notifications,
                static_cast<unsigned>(esp_get_free_heap_size()),
                static_cast<unsigned>(esp_get_minimum_free_heap_size()));
        }
    }

    if (timer != nullptr) {
        (void)esp_timer_stop(timer);
        (void)esp_timer_delete(timer);
    }

    const auto final_state = g_injector_controller.snapshot();
    const auto& stats = publisher.statistics();
    ESP_LOGW(
        kTag,
        "SV injector stopped: state=%u sent=%" PRIu64 " txFail=%" PRIu64
        " encFail=%" PRIu64 " sample=%" PRIu64,
        static_cast<unsigned>(final_state.state),
        stats.frames_sent,
        stats.transmit_failures,
        stats.encode_failures,
        injector.sample_index());
    vTaskDelete(nullptr);
}

} // namespace

extern "C" void app_main(void) {
    g_link_events = xEventGroupCreate();
    if (g_link_events == nullptr) {
        ESP_LOGE(kTag, "Failed to create Ethernet link event group");
        return;
    }

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(initialize_ethernet(&g_eth_handle));
    ESP_ERROR_CHECK(esp_event_handler_register(
        ETH_EVENT,
        ESP_EVENT_ANY_ID,
        &ethernet_event_handler,
        nullptr));
    ESP_ERROR_CHECK(esp_eth_start(g_eth_handle));

    const auto created = xTaskCreate(
        &publisher_task,
        "arstack_sv_pub",
        kPublisherTaskStackBytes,
        nullptr,
        kPublisherTaskPriority,
        nullptr);
    if (created != pdPASS) {
        ESP_LOGE(kTag, "Failed to create SV publisher task");
    }
}
