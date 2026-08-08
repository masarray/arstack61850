// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/ports/esp_idf/ethernet_port.hpp"
#include "ariec61850/sampled_values/deterministic_injector.hpp"
#include "ariec61850/sampled_values/frame.hpp"
#include "ariec61850/sampled_values/injector_control_protocol.hpp"
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
#include "freertos/queue.h"
#include "freertos/task.h"

#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string_view>

namespace {

using namespace ar::iec61850;

constexpr std::uint32_t kSampleRateHz = 4'000U;
constexpr std::uint64_t kPublishIntervalUs = 250U;
constexpr std::uint16_t kSampleCountWrap = 4'000U;
constexpr std::size_t kPayloadBytes =
    sampled_values::injector_channel_count *
    sampled_values::SampledValuesPayloadWriter::int32_quality_pair_bytes;
constexpr std::size_t kControlLineBytes = 256U;
constexpr std::size_t kControlQueueDepth = 16U;

// Waveshare ESP32-P4-ETH: IP101 PHY on RMII. ESP32-P4's
// ETH_ESP32_EMAC_DEFAULT_CONFIG() supplies the matching default RMII data-plane
// GPIOs; this board-specific layer supplies the PHY management values.
constexpr std::int32_t kPhyAddress = 1;
constexpr int kPhyResetGpio = 51;

constexpr EventBits_t kEthernetLinkUpBit = BIT0;
constexpr UBaseType_t kPublisherTaskPriority = 18U;
constexpr UBaseType_t kControlTaskPriority = 8U;
constexpr std::uint32_t kPublisherTaskStackBytes = 9'216U;
constexpr std::uint32_t kControlTaskStackBytes = 4'096U;

const char* kTag = "arstack_sv_trial";
EventGroupHandle_t g_link_events{};
TaskHandle_t g_publisher_task{};
esp_eth_handle_t g_eth_handle{};
QueueHandle_t g_control_queue{};
sampled_values::InjectorController g_injector_controller{};

[[nodiscard]] const char* state_name(
    const sampled_values::InjectorControlState state) noexcept {
    switch (state) {
    case sampled_values::InjectorControlState::idle:
        return "IDLE";
    case sampled_values::InjectorControlState::configured:
        return "CONFIGURED";
    case sampled_values::InjectorControlState::armed:
        return "ARMED";
    case sampled_values::InjectorControlState::running:
        return "RUNNING";
    case sampled_values::InjectorControlState::stopped:
        return "STOPPED";
    case sampled_values::InjectorControlState::fault:
        return "FAULT";
    }
    return "UNKNOWN";
}

[[nodiscard]] const char* scenario_name(
    const sampled_values::InjectorScenarioKind scenario) noexcept {
    return scenario == sampled_values::InjectorScenarioKind::protection_fault
        ? "protection-fault"
        : "normal";
}

[[nodiscard]] const char* command_name(
    const sampled_values::InjectorControlCommandKind command) noexcept {
    switch (command) {
    case sampled_values::InjectorControlCommandKind::capabilities:
        return "capabilities";
    case sampled_values::InjectorControlCommandKind::configure:
        return "configure";
    case sampled_values::InjectorControlCommandKind::arm:
        return "arm";
    case sampled_values::InjectorControlCommandKind::start:
        return "start";
    case sampled_values::InjectorControlCommandKind::stop:
        return "stop";
    case sampled_values::InjectorControlCommandKind::status:
        return "status";
    case sampled_values::InjectorControlCommandKind::stats:
        return "stats";
    }
    return "unknown";
}

void print_control_error(
    const char* command,
    const char* error) noexcept {
    std::printf(
        "ARCTRL {\"schemaVersion\":\"arstack-sv-injector-control-v1\","
        "\"command\":\"%s\",\"ok\":false,\"error\":\"%s\"}\n",
        command,
        error);
    std::fflush(stdout);
}

void print_control_ack(
    const sampled_values::InjectorControlCommandKind command) noexcept {
    const auto snapshot = g_injector_controller.snapshot();
    std::printf(
        "ARCTRL {\"schemaVersion\":\"arstack-sv-injector-control-v1\","
        "\"command\":\"%s\",\"ok\":true,\"state\":\"%s\","
        "\"scenario\":\"%s\",\"configurationRevision\":%u,"
        "\"armedRevision\":%u,\"runSequence\":%" PRIu64 "}\n",
        command_name(command),
        state_name(snapshot.state),
        scenario_name(snapshot.configuration.scenario),
        static_cast<unsigned>(snapshot.configuration_revision),
        static_cast<unsigned>(snapshot.armed_revision),
        snapshot.run_sequence);
    std::fflush(stdout);
}

void print_capabilities() noexcept {
    std::printf(
        "ARCTRL {\"schemaVersion\":\"arstack-sv-injector-control-v1\","
        "\"command\":\"capabilities\",\"ok\":true,"
        "\"transport\":\"usb-serial-jtag-stdio\","
        "\"engine\":\"sample-index-fixed-point\","
        "\"commands\":[\"capabilities\",\"configure\",\"arm\",\"start\","
        "\"stop\",\"status\",\"stats\"],"
        "\"scenarios\":[\"normal\",\"protection-fault\"],"
        "\"profile\":{\"sampleRateHz\":4000,\"sampleCountWrap\":4000,"
        "\"channels\":8,\"payload\":\"INT32+quality\",\"appid\":16385}}\n");
    std::fflush(stdout);
}

void print_status() noexcept {
    const auto snapshot = g_injector_controller.snapshot();
    const auto link_up =
        (xEventGroupGetBits(g_link_events) & kEthernetLinkUpBit) != 0U;
    std::printf(
        "ARCTRL {\"schemaVersion\":\"arstack-sv-injector-control-v1\","
        "\"command\":\"status\",\"ok\":true,\"state\":\"%s\","
        "\"scenario\":\"%s\",\"configurationRevision\":%u,"
        "\"armedRevision\":%u,\"runSequence\":%" PRIu64 ","
        "\"ethernetLinkUp\":%s}\n",
        state_name(snapshot.state),
        scenario_name(snapshot.configuration.scenario),
        static_cast<unsigned>(snapshot.configuration_revision),
        static_cast<unsigned>(snapshot.armed_revision),
        snapshot.run_sequence,
        link_up ? "true" : "false");
    std::fflush(stdout);
}

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

[[nodiscard]] std::array<sampled_values::InjectorScenarioSegment, 3U>
make_protection_fault_scenario() noexcept {
    const auto normal = sampled_values::make_balanced_4i4v_profile();
    auto fault = normal;
    for (std::size_t channel = 0U; channel < 3U; ++channel) {
        fault[channel].rms_counts *= 4;
    }
    for (std::size_t channel = 4U; channel < 7U; ++channel) {
        fault[channel].rms_counts /= 4;
    }
    return {
        sampled_values::make_hold_segment(normal, 2'000U),
        sampled_values::make_hold_segment(fault, 800U),
        sampled_values::make_hold_segment(normal, 2'000U)};
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

void control_task(void* argument) {
    (void)argument;
    std::array<char, kControlLineBytes> line{};
    std::size_t length{};

    ESP_LOGI(
        kTag,
        "Control plane ready on primary stdio; send one JSON command per line");
    print_capabilities();

    while (true) {
        bool consumed_character = false;
        while (true) {
            const auto character = std::getchar();
            if (character == EOF) {
                std::clearerr(stdin);
                break;
            }
            consumed_character = true;
            if (character == '\r') {
                continue;
            }
            if (character == '\n') {
                if (length == 0U) {
                    continue;
                }
                const auto parsed = sampled_values::parse_injector_control_command(
                    std::string_view{line.data(), length});
                length = 0U;
                if (!parsed.success()) {
                    std::printf(
                        "ARCTRL {\"schemaVersion\":\"arstack-sv-injector-control-v1\","
                        "\"command\":\"parse\",\"ok\":false,\"parseStatus\":%u}\n",
                        static_cast<unsigned>(parsed.status));
                    std::fflush(stdout);
                    continue;
                }
                if (xQueueSend(
                        g_control_queue,
                        &parsed.command,
                        pdMS_TO_TICKS(100)) != pdPASS) {
                    print_control_error(command_name(parsed.command.kind), "control-queue-full");
                }
                continue;
            }

            if (length + 1U >= line.size()) {
                length = 0U;
                print_control_error("parse", "line-too-long");
                continue;
            }
            line[length] = static_cast<char>(character);
            ++length;
        }

        if (!consumed_character) {
            vTaskDelay(pdMS_TO_TICKS(10));
        } else {
            taskYIELD();
        }
    }
}

void publisher_task(void* argument) {
    (void)argument;
    g_publisher_task = xTaskGetCurrentTaskHandle();

    std::array<std::uint8_t, 6U> source_mac{};
    while ((xEventGroupGetBits(g_link_events) & kEthernetLinkUpBit) == 0U) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
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

    const std::array<sampled_values::InjectorScenarioSegment, 1U> normal_scenario{
        sampled_values::make_hold_segment(
            sampled_values::make_balanced_4i4v_profile())};
    const auto protection_fault_scenario = make_protection_fault_scenario();
    std::optional<sampled_values::DeterministicSvInjector> injector;
    std::optional<sampled_values::InjectorSample> pending_sample;

    if (!publisher.valid()) {
        ESP_LOGE(kTag, "SV publisher configuration is invalid");
        g_injector_controller.set_fault();
        vTaskDelete(nullptr);
        return;
    }

    if (g_injector_controller.configure({
            kSampleRateHz,
            sampled_values::InjectorScenarioKind::normal}) !=
        sampled_values::InjectorControlStatus::ok) {
        ESP_LOGE(kTag, "Initial injector configuration failed");
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

    ESP_LOGI(
        kTag,
        "SV injector CONFIGURED and waiting for control: arm -> start");
    print_status();

    std::uint64_t timer_notifications{};
    std::uint64_t coalesced_notifications{};
    std::uint64_t report_notifications{};

    while (true) {
        const auto notifications = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        timer_notifications += notifications;
        report_notifications += notifications;
        if (notifications > 1U) {
            coalesced_notifications += static_cast<std::uint64_t>(notifications - 1U);
        }

        sampled_values::InjectorControlCommand command{};
        while (xQueueReceive(g_control_queue, &command, 0U) == pdPASS) {
            sampled_values::InjectorControlStatus control_status{
                sampled_values::InjectorControlStatus::ok};

            switch (command.kind) {
            case sampled_values::InjectorControlCommandKind::capabilities:
                print_capabilities();
                continue;
            case sampled_values::InjectorControlCommandKind::status:
                print_status();
                continue;
            case sampled_values::InjectorControlCommandKind::stats: {
                const auto& stats = publisher.statistics();
                const auto snapshot = g_injector_controller.snapshot();
                std::printf(
                    "ARCTRL {\"schemaVersion\":\"arstack-sv-injector-control-v1\","
                    "\"command\":\"stats\",\"ok\":true,\"state\":\"%s\","
                    "\"framesSent\":%" PRIu64 ",\"encodeFailures\":%" PRIu64 ","
                    "\"transmitFailures\":%" PRIu64 ",\"latePolls\":%" PRIu64 ","
                    "\"maximumLatenessUs\":%" PRIu64 ",\"timerTicks\":%" PRIu64 ","
                    "\"coalescedTicks\":%" PRIu64 "}\n",
                    state_name(snapshot.state),
                    stats.frames_sent,
                    stats.encode_failures,
                    stats.transmit_failures,
                    stats.late_polls,
                    stats.maximum_lateness_us,
                    timer_notifications,
                    coalesced_notifications);
                std::fflush(stdout);
                continue;
            }
            case sampled_values::InjectorControlCommandKind::configure:
                control_status = g_injector_controller.configure({
                    kSampleRateHz,
                    command.scenario});
                break;
            case sampled_values::InjectorControlCommandKind::arm:
                control_status = g_injector_controller.arm();
                break;
            case sampled_values::InjectorControlCommandKind::start:
                control_status = g_injector_controller.start();
                if (control_status == sampled_values::InjectorControlStatus::ok) {
                    const auto snapshot = g_injector_controller.snapshot();
                    publisher.reset(0U);
                    pending_sample.reset();
                    if (snapshot.configuration.scenario ==
                        sampled_values::InjectorScenarioKind::protection_fault) {
                        injector.emplace(
                            std::span<const sampled_values::InjectorScenarioSegment>{
                                protection_fault_scenario},
                            kSampleRateHz,
                            true);
                    } else {
                        injector.emplace(
                            std::span<const sampled_values::InjectorScenarioSegment>{
                                normal_scenario},
                            kSampleRateHz,
                            true);
                    }
                    if (!injector->valid()) {
                        g_injector_controller.set_fault();
                        injector.reset();
                        print_control_error("start", "invalid-injector-scenario");
                        continue;
                    }
                }
                break;
            case sampled_values::InjectorControlCommandKind::stop:
                control_status = g_injector_controller.stop();
                if (control_status == sampled_values::InjectorControlStatus::ok) {
                    pending_sample.reset();
                    injector.reset();
                    publisher.reset(0U);
                }
                break;
            }

            if (control_status == sampled_values::InjectorControlStatus::ok) {
                print_control_ack(command.kind);
            } else if (control_status ==
                       sampled_values::InjectorControlStatus::invalid_configuration) {
                print_control_error(command_name(command.kind), "invalid-configuration");
            } else {
                print_control_error(command_name(command.kind), "invalid-state");
            }
        }

        if (!g_injector_controller.running()) {
            continue;
        }
        if ((xEventGroupGetBits(g_link_events) & kEthernetLinkUpBit) == 0U) {
            continue;
        }
        if (!injector.has_value()) {
            ESP_LOGE(kTag, "RUNNING state without active deterministic injector");
            g_injector_controller.set_fault();
            print_control_error("runtime", "missing-injector");
            continue;
        }

        if (!pending_sample.has_value()) {
            sampled_values::InjectorSample next_sample{};
            if (!injector->step(next_sample)) {
                ESP_LOGE(kTag, "Deterministic injector stopped unexpectedly");
                g_injector_controller.set_fault();
                injector.reset();
                print_control_error("runtime", "injector-finished");
                continue;
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
                injector.reset();
                pending_sample.reset();
                print_control_error("runtime", "sample-counter-divergence");
                continue;
            }

            if (!write_injector_payload(frame.pdu.asdus.front(), next_sample)) {
                ESP_LOGE(kTag, "Deterministic payload update failed");
                g_injector_controller.set_fault();
                injector.reset();
                pending_sample.reset();
                print_control_error("runtime", "payload-write-failed");
                continue;
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
            injector.reset();
            pending_sample.reset();
            print_control_error("runtime", "publish-failed");
            continue;
        }

        if (report_notifications >= kSampleRateHz) {
            report_notifications -= kSampleRateHz;
            const auto& stats = publisher.statistics();
            const auto snapshot = g_injector_controller.snapshot();
            ESP_LOGI(
                kTag,
                "QA state=%s run=%" PRIu64 " sample=%" PRIu64
                " sent=%" PRIu64 " txFail=%" PRIu64 " encFail=%" PRIu64
                " late=%" PRIu64 " maxLateUs=%" PRIu64
                " timerTicks=%" PRIu64 " coalesced=%" PRIu64
                " heap=%u minHeap=%u",
                state_name(snapshot.state),
                snapshot.run_sequence,
                injector.has_value() ? injector->sample_index() : 0U,
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
}

} // namespace

extern "C" void app_main(void) {
    g_link_events = xEventGroupCreate();
    if (g_link_events == nullptr) {
        ESP_LOGE(kTag, "Failed to create Ethernet link event group");
        return;
    }

    g_control_queue = xQueueCreate(
        static_cast<UBaseType_t>(kControlQueueDepth),
        sizeof(sampled_values::InjectorControlCommand));
    if (g_control_queue == nullptr) {
        ESP_LOGE(kTag, "Failed to create injector control queue");
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

    const auto publisher_created = xTaskCreate(
        &publisher_task,
        "arstack_sv_pub",
        kPublisherTaskStackBytes,
        nullptr,
        kPublisherTaskPriority,
        nullptr);
    if (publisher_created != pdPASS) {
        ESP_LOGE(kTag, "Failed to create SV publisher task");
        return;
    }

    const auto control_created = xTaskCreate(
        &control_task,
        "arstack_sv_ctl",
        kControlTaskStackBytes,
        nullptr,
        kControlTaskPriority,
        nullptr);
    if (control_created != pdPASS) {
        ESP_LOGE(kTag, "Failed to create SV control task");
    }
}
