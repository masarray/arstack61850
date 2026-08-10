// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/sampled_values/asdu.hpp"
#include "ariec61850/sampled_values/frame.hpp"
#include "ariec61850/sampled_values/frame_codec.hpp"
#include "ariec61850/sampled_values/live_signal_state.hpp"

#include "driver/gptimer.h"
#include "ethernet_port.h"
#include "live_control.hpp"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace {

using ar::esp32p4::smv::live_control_initialize;
using ar::esp32p4::smv::live_control_task;
using ar::esp32p4::smv::live_signal_snapshot;
using ar::esp32p4::smv::live_tx_running;
using ar::esp32p4::smv::take_start_request;
using ar::iec61850::ethernet::MacAddress;
using ar::iec61850::ethernet::VlanTag;
using ar::iec61850::sampled_values::SampledValueAsdu;
using ar::iec61850::sampled_values::SampledValuesFrame;
using ar::iec61850::sampled_values::SampledValuesFrameCodec;
using ar::iec61850::sampled_values::SampledValuesPdu;
using ar::iec61850::sampled_values::SvFixedPointSineEngine;
using ar::iec61850::sampled_values::SvLiveSignalState;

constexpr char kTag[] = "ar_smv_p3";
constexpr EventBits_t kLinkUpBit = BIT0;

// Canonical development identity remains fixed until the compiled-device
// profile work moves these fields behind the validated publisher profile.
constexpr std::uint16_t kCanonicalAppId = 0x4000U;
constexpr char kCanonicalSvId[] = "MU01_SV1";
constexpr std::array<std::uint8_t, 6> kSvMulticastMac{
    0x01U, 0x0CU, 0xCDU, 0x04U, 0x00U, 0x01U};

// The broadcast path is a separate diagnostic stream and never changes the
// canonical identity or sequence semantics.
constexpr std::uint16_t kMirrorAppId = 0x4F01U;
constexpr char kMirrorSvId[] = "AR_DIAG_SV1";

constexpr std::uint16_t kSampleCountWrap = 4000U;
constexpr std::uint32_t kSampleRateHz = 4000U;
constexpr std::int64_t kSamplePeriodUs = 250;
constexpr std::uint16_t kSampleModeSamplesPerSecond = 1U;
constexpr std::uint16_t kDiagnosticEtherType = 0x88B5U;
constexpr std::uint32_t kStatsMergeEvery = 400U;

EventGroupHandle_t g_link_events = nullptr;
portMUX_TYPE g_stats_mux = portMUX_INITIALIZER_UNLOCKED;

struct TimingStats final {
    std::uint64_t samples{};
    std::uint64_t canonical_ok{};
    std::uint64_t canonical_fail{};
    std::uint64_t mirror_ok{};
    std::uint64_t mirror_fail{};
    std::uint64_t missed_slots{};
    std::int64_t lateness_sum_us{};
    std::int32_t lateness_min_us{std::numeric_limits<std::int32_t>::max()};
    std::int32_t lateness_max_us{std::numeric_limits<std::int32_t>::min()};
    std::uint16_t last_sample_count{};
    std::uint64_t last_signal_generation{};
};

TimingStats g_interval_stats{};

struct PacketTemplate final {
    std::vector<std::uint8_t> bytes;
    std::size_t sample_count_offset{std::numeric_limits<std::size_t>::max()};
    std::size_t sample_payload_offset{std::numeric_limits<std::size_t>::max()};
};

using WaveformRow = SvFixedPointSineEngine::SampleRow;

void eth_event_handler(void*, esp_event_base_t, const std::int32_t event_id, void*) {
    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        xEventGroupSetBits(g_link_events, kLinkUpBit);
        ESP_LOGI(kTag, "Ethernet link up");
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        xEventGroupClearBits(g_link_events, kLinkUpBit);
        ESP_LOGW(kTag, "Ethernet link down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(kTag, "Ethernet driver started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(kTag, "Ethernet driver stopped");
        break;
    default:
        break;
    }
}

void write_u16_be(std::uint8_t* destination, const std::uint16_t value) noexcept {
    destination[0] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    destination[1] = static_cast<std::uint8_t>(value & 0xFFU);
}

void write_u32_be(std::uint8_t* destination, const std::uint32_t value) noexcept {
    destination[0] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    destination[1] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    destination[2] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    destination[3] = static_cast<std::uint8_t>(value & 0xFFU);
}

void write_i32_be(std::uint8_t* destination, const std::int32_t value) noexcept {
    write_u32_be(destination, static_cast<std::uint32_t>(value));
}

std::vector<std::uint8_t> make_4i4v_payload() {
    // Current development layout: Ia, Ib, Ic, In, Ua, Ub, Uc, Un.
    // Each value is INT32 followed by UINT32 Quality.
    return std::vector<std::uint8_t>(8U * 8U, 0U);
}

SampledValuesFrame make_runtime_frame(
    const std::array<std::uint8_t, 6>& source_mac,
    const bool use_vlan,
    const bool diagnostic_mirror) {
    SampledValueAsdu asdu;
    asdu.sv_id = diagnostic_mirror ? kMirrorSvId : kCanonicalSvId;
    asdu.data_set_reference.clear();

    // Fixed-width placeholders let the realtime task patch fields in place
    // without BER rebuilds or packet-length changes.
    asdu.sample_count = 0x0100U;
    asdu.configuration_revision = 0x01000000U;
    asdu.reference_time.reset();
    asdu.sample_synchronization = 0U;

    if (diagnostic_mirror) {
        asdu.sample_rate = static_cast<std::uint16_t>(kSampleRateHz);
        asdu.sample_mode = kSampleModeSamplesPerSecond;
    } else {
        asdu.sample_rate.reset();
        asdu.sample_mode.reset();
    }

    asdu.sample_payload = make_4i4v_payload();

    SampledValuesFrame frame;
    frame.destination = MacAddress{std::span<const std::uint8_t>{kSvMulticastMac}};
    frame.source = MacAddress{std::span<const std::uint8_t>{source_mac}};
    if (use_vlan) {
        frame.vlan = VlanTag{4U, 0U};
    } else {
        frame.vlan.reset();
    }
    frame.app_id = diagnostic_mirror ? kMirrorAppId : kCanonicalAppId;
    frame.reserved1 = 0U;
    frame.reserved2 = 0U;
    frame.pdu = SampledValuesPdu{{std::move(asdu)}};
    return frame;
}

bool find_fixed_field(
    const std::vector<std::uint8_t>& bytes,
    const std::uint8_t tag,
    const std::uint8_t length,
    std::size_t& value_offset) noexcept {
    for (std::size_t index = 20U; index + 2U + length <= bytes.size(); ++index) {
        if (bytes[index] == tag && bytes[index + 1U] == length) {
            value_offset = index + 2U;
            return true;
        }
    }
    return false;
}

bool build_packet_template(
    const std::array<std::uint8_t, 6>& source_mac,
    const bool use_vlan,
    const bool broadcast_destination,
    const bool diagnostic_mirror,
    PacketTemplate& packet) {
    packet = {};
    packet.bytes = SampledValuesFrameCodec::encode(
        make_runtime_frame(source_mac, use_vlan, diagnostic_mirror));

    if (broadcast_destination) {
        std::fill_n(packet.bytes.begin(), 6U, std::uint8_t{0xFFU});
    }

    std::size_t conf_rev_offset = 0U;
    if (!find_fixed_field(packet.bytes, 0x82U, 2U, packet.sample_count_offset) ||
        !find_fixed_field(packet.bytes, 0x83U, 4U, conf_rev_offset) ||
        !find_fixed_field(packet.bytes, 0x87U, 64U, packet.sample_payload_offset)) {
        ESP_LOGE(kTag, "Failed to locate fixed SV patch fields in encoded template");
        return false;
    }

    if (packet.sample_payload_offset + 64U > packet.bytes.size()) {
        ESP_LOGE(kTag, "SV payload patch region exceeds encoded frame");
        return false;
    }

    write_u16_be(packet.bytes.data() + packet.sample_count_offset, 0U);
    write_u32_be(packet.bytes.data() + conf_rev_offset, 1U);
    return true;
}

void patch_packet(
    PacketTemplate& packet,
    const std::uint16_t sample_count,
    const WaveformRow& values,
    const SvLiveSignalState& signal_state) noexcept {
    write_u16_be(packet.bytes.data() + packet.sample_count_offset, sample_count);

    for (std::size_t channel = 0U; channel < values.size(); ++channel) {
        const auto base = packet.sample_payload_offset + channel * 8U;
        write_i32_be(packet.bytes.data() + base, values[channel]);
        write_u32_be(packet.bytes.data() + base + 4U, signal_state.channels[channel].quality);
    }
}

#if CONFIG_AR_SMV_DIAGNOSTIC_PROBES
std::array<std::uint8_t, 60> make_probe(
    const std::array<std::uint8_t, 6>& source_mac,
    const bool multicast) {
    std::array<std::uint8_t, 60> frame{};

    for (std::size_t index = 0U; index < 6U; ++index) {
        frame[index] = multicast ? kSvMulticastMac[index] : 0xFFU;
        frame[6U + index] = source_mac[index];
    }

    frame[12] = static_cast<std::uint8_t>((kDiagnosticEtherType >> 8U) & 0xFFU);
    frame[13] = static_cast<std::uint8_t>(kDiagnosticEtherType & 0xFFU);

    constexpr char payload[] = "ARSTACK-P3-LIVE-CONTROL-PROBE";
    for (std::size_t index = 0U;
         index < sizeof(payload) - 1U && 14U + index < frame.size();
         ++index) {
        frame[14U + index] = static_cast<std::uint8_t>(payload[index]);
    }

    return frame;
}
#endif

void merge_stats(TimingStats& local) noexcept {
    if (local.samples == 0U) {
        return;
    }

    portENTER_CRITICAL(&g_stats_mux);
    g_interval_stats.samples += local.samples;
    g_interval_stats.canonical_ok += local.canonical_ok;
    g_interval_stats.canonical_fail += local.canonical_fail;
    g_interval_stats.mirror_ok += local.mirror_ok;
    g_interval_stats.mirror_fail += local.mirror_fail;
    g_interval_stats.missed_slots += local.missed_slots;
    g_interval_stats.lateness_sum_us += local.lateness_sum_us;
    g_interval_stats.lateness_min_us =
        std::min(g_interval_stats.lateness_min_us, local.lateness_min_us);
    g_interval_stats.lateness_max_us =
        std::max(g_interval_stats.lateness_max_us, local.lateness_max_us);
    g_interval_stats.last_sample_count = local.last_sample_count;
    g_interval_stats.last_signal_generation = local.last_signal_generation;
    portEXIT_CRITICAL(&g_stats_mux);

    local = TimingStats{};
}

void update_local_timing(
    TimingStats& local,
    const std::int64_t lateness_us,
    const std::uint64_t missed_slots,
    const std::uint16_t sample_count,
    const std::uint64_t signal_generation) noexcept {
    const auto bounded_lateness = static_cast<std::int32_t>(std::clamp<std::int64_t>(
        lateness_us,
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max()));

    ++local.samples;
    local.missed_slots += missed_slots;
    local.lateness_sum_us += lateness_us;
    local.lateness_min_us = std::min(local.lateness_min_us, bounded_lateness);
    local.lateness_max_us = std::max(local.lateness_max_us, bounded_lateness);
    local.last_sample_count = sample_count;
    local.last_signal_generation = signal_generation;
}

bool IRAM_ATTR sample_alarm_callback(
    gptimer_handle_t,
    const gptimer_alarm_event_data_t*,
    void* user_context) {
    auto task = static_cast<TaskHandle_t>(user_context);
    BaseType_t high_priority_task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(task, &high_priority_task_woken);
    return high_priority_task_woken == pdTRUE;
}

gptimer_handle_t start_sample_clock(TaskHandle_t publisher_task_handle) {
    gptimer_config_t timer_config{};
    timer_config.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    timer_config.direction = GPTIMER_COUNT_UP;
    timer_config.resolution_hz = 1000000U;

    gptimer_handle_t timer = nullptr;
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &timer));

    gptimer_event_callbacks_t callbacks{};
    callbacks.on_alarm = &sample_alarm_callback;
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(
        timer, &callbacks, publisher_task_handle));

    gptimer_alarm_config_t alarm_config{};
    alarm_config.alarm_count = static_cast<std::uint64_t>(kSamplePeriodUs);
    alarm_config.reload_count = 0U;
    alarm_config.flags.auto_reload_on_alarm = true;
    ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarm_config));
    ESP_ERROR_CHECK(gptimer_enable(timer));
    ESP_ERROR_CHECK(gptimer_start(timer));
    return timer;
}

void publisher_task(void* argument) {
#if CONFIG_AR_SMV_LIVE_TX && CONFIG_AR_SMV_P2_REALTIME_50HZ
    const auto eth_handle = static_cast<esp_eth_handle_t>(argument);

    ESP_LOGW(kTag, "LIVE SV TX ENABLED: isolated lab network only");
    ESP_LOGI(kTag, "Waiting for Ethernet link...");
    xEventGroupWaitBits(g_link_events, kLinkUpBit, pdFALSE, pdTRUE, portMAX_DELAY);

    std::array<std::uint8_t, 6> source_mac{};
    ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, source_mac.data()));

    ESP_LOGI(kTag,
             "EMAC source MAC=%02X:%02X:%02X:%02X:%02X:%02X",
             source_mac[0], source_mac[1], source_mac[2],
             source_mac[3], source_mac[4], source_mac[5]);

    PacketTemplate canonical;
    PacketTemplate mirror;

    if (!build_packet_template(
            source_mac,
#if CONFIG_AR_SMV_CANONICAL_VLAN
            true,
#else
            false,
#endif
            false,
            false,
            canonical)) {
        ESP_LOGE(kTag, "Cannot start publisher: canonical template build failed");
        vTaskDelete(nullptr);
        return;
    }

#if CONFIG_AR_SMV_BROADCAST_MIRROR
    if (!build_packet_template(source_mac, false, true, true, mirror)) {
        ESP_LOGE(kTag, "Cannot start publisher: diagnostic mirror template build failed");
        vTaskDelete(nullptr);
        return;
    }
#endif

#if CONFIG_AR_SMV_DIAGNOSTIC_PROBES
    const auto probe_broadcast = make_probe(source_mac, false);
    const auto probe_multicast = make_probe(source_mac, true);
#endif

    const auto initial_signal = live_signal_snapshot();
    ESP_LOGI(kTag,
             "Live signal engine ready: publisher=%u fps, signal=%u mHz, generation=%llu; type START to transmit",
             static_cast<unsigned>(kSampleRateHz),
             static_cast<unsigned>(initial_signal.frequency_millihz),
             static_cast<unsigned long long>(initial_signal.generation));
    ESP_LOGI(kTag,
             "Development profile: svID=%s APPID=0x%04X canonical smpCnt wrap=%u %s",
             kCanonicalSvId,
             static_cast<unsigned>(kCanonicalAppId),
             static_cast<unsigned>(kSampleCountWrap),
#if CONFIG_AR_SMV_CANONICAL_VLAN
             "VLAN PCP4/VID0"
#else
             "untagged"
#endif
    );
    ESP_LOGI(kTag,
             "Fixed template: canonical len=%u smpCnt@%u seqData@%u",
             static_cast<unsigned>(canonical.bytes.size()),
             static_cast<unsigned>(canonical.sample_count_offset),
             static_cast<unsigned>(canonical.sample_payload_offset));

    static SvFixedPointSineEngine signal_engine;

    vTaskDelay(pdMS_TO_TICKS(100));
    const gptimer_handle_t sample_clock = start_sample_clock(xTaskGetCurrentTaskHandle());
    (void)sample_clock;
    ESP_LOGI(kTag,
             "Sample clock: GPTimer 1 MHz, periodic alarm every %lld us; publisher blocks between alarms",
             static_cast<long long>(kSamplePeriodUs));

    std::uint16_t canonical_sample_count = 0U;
    std::uint16_t mirror_sample_count = 0U;
    TimingStats local{};
    bool schedule_anchored = false;
    std::int64_t expected_wake_us = 0;

    while (true) {
        const std::uint32_t pending_ticks = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (pending_ticks == 0U) {
            continue;
        }

        if (take_start_request()) {
            canonical_sample_count = 0U;
            mirror_sample_count = 0U;
            signal_engine.reset_phase();
            schedule_anchored = false;
            local = TimingStats{};
        }

        if (!live_tx_running()) {
            schedule_anchored = false;
            continue;
        }

        const std::int64_t actual_wake_us = esp_timer_get_time();
        std::uint64_t missed_slots = 0U;

        if (!schedule_anchored) {
            expected_wake_us = actual_wake_us;
            schedule_anchored = true;
        } else {
            expected_wake_us +=
                static_cast<std::int64_t>(pending_ticks) * kSamplePeriodUs;
        }

        const auto signal_state = live_signal_snapshot();

        if (pending_ticks > 1U) {
            missed_slots = static_cast<std::uint64_t>(pending_ticks - 1U);
            canonical_sample_count = static_cast<std::uint16_t>(
                (static_cast<std::uint32_t>(canonical_sample_count) + missed_slots) %
                kSampleCountWrap);
            mirror_sample_count = static_cast<std::uint16_t>(
                static_cast<std::uint32_t>(mirror_sample_count) +
                static_cast<std::uint32_t>(missed_slots));
            signal_engine.advance(
                signal_state,
                kSampleRateHz,
                static_cast<std::uint32_t>(missed_slots));
        }

        const std::int64_t lateness_us = actual_wake_us - expected_wake_us;
        const auto row = signal_engine.next(signal_state, kSampleRateHz);

        if ((xEventGroupGetBits(g_link_events) & kLinkUpBit) == 0U) {
            ++local.canonical_fail;
#if CONFIG_AR_SMV_BROADCAST_MIRROR
            ++local.mirror_fail;
#endif
        } else {
            patch_packet(canonical, canonical_sample_count, row, signal_state);

            const esp_err_t canonical_result =
                esp_eth_transmit(eth_handle, canonical.bytes.data(), canonical.bytes.size());
            if (canonical_result == ESP_OK) {
                ++local.canonical_ok;
            } else {
                ++local.canonical_fail;
            }

#if CONFIG_AR_SMV_BROADCAST_MIRROR
            patch_packet(mirror, mirror_sample_count, row, signal_state);
            const esp_err_t mirror_result =
                esp_eth_transmit(eth_handle, mirror.bytes.data(), mirror.bytes.size());
            if (mirror_result == ESP_OK) {
                ++local.mirror_ok;
            } else {
                ++local.mirror_fail;
            }
#endif

#if CONFIG_AR_SMV_DIAGNOSTIC_PROBES
            if ((canonical_sample_count % kSampleCountWrap) == 0U) {
                (void)esp_eth_transmit(
                    eth_handle,
                    const_cast<std::uint8_t*>(probe_broadcast.data()),
                    probe_broadcast.size());
                (void)esp_eth_transmit(
                    eth_handle,
                    const_cast<std::uint8_t*>(probe_multicast.data()),
                    probe_multicast.size());
            }
#endif
        }

        update_local_timing(
            local,
            lateness_us,
            missed_slots,
            canonical_sample_count,
            signal_state.generation);

        canonical_sample_count = static_cast<std::uint16_t>(
            (canonical_sample_count + 1U) % kSampleCountWrap);
        mirror_sample_count = static_cast<std::uint16_t>(mirror_sample_count + 1U);

        if (local.samples >= kStatsMergeEvery) {
            merge_stats(local);
        }
    }
#else
    (void)argument;
    ESP_LOGW(kTag, "Realtime publisher is disabled in menuconfig");
    vTaskDelete(nullptr);
#endif
}

void telemetry_task(void*) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        TimingStats snapshot{};
        portENTER_CRITICAL(&g_stats_mux);
        snapshot = g_interval_stats;
        g_interval_stats = TimingStats{};
        portEXIT_CRITICAL(&g_stats_mux);

        if (snapshot.samples == 0U) {
            continue;
        }

        const std::int64_t mean_lateness_us =
            snapshot.lateness_sum_us / static_cast<std::int64_t>(snapshot.samples);

        ESP_LOGI(kTag,
                 "timing: samples=%llu (~%llu fps) MC ok=%llu fail=%llu mirror ok=%llu fail=%llu missed=%llu wake_late_us[min/mean/max]=%ld/%lld/%ld smpCnt=%u signal_gen=%llu",
                 static_cast<unsigned long long>(snapshot.samples),
                 static_cast<unsigned long long>(snapshot.samples),
                 static_cast<unsigned long long>(snapshot.canonical_ok),
                 static_cast<unsigned long long>(snapshot.canonical_fail),
                 static_cast<unsigned long long>(snapshot.mirror_ok),
                 static_cast<unsigned long long>(snapshot.mirror_fail),
                 static_cast<unsigned long long>(snapshot.missed_slots),
                 static_cast<long>(snapshot.lateness_min_us),
                 static_cast<long long>(mean_lateness_us),
                 static_cast<long>(snapshot.lateness_max_us),
                 static_cast<unsigned>(snapshot.last_sample_count),
                 static_cast<unsigned long long>(snapshot.last_signal_generation));
    }
}

} // namespace

extern "C" void app_main(void) {
    g_link_events = xEventGroupCreate();
    if (g_link_events == nullptr) {
        ESP_LOGE(kTag, "Failed to allocate Ethernet event group");
        return;
    }

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(
        ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, nullptr));

    esp_eth_handle_t eth_handle = ar_esp32p4_eth_init();
    if (eth_handle == nullptr) {
        ESP_LOGE(kTag, "ESP32-P4 Ethernet bring-up failed");
        return;
    }

    ESP_ERROR_CHECK(esp_eth_start(eth_handle));

#if CONFIG_AR_SMV_LIVE_TX && CONFIG_AR_SMV_P2_REALTIME_50HZ
    // Existing menuconfig values seed the first live generation. Current
    // development wire scaling is 1 mA/count for I and 10 mV/count for U.
    live_control_initialize(
        static_cast<std::int32_t>(CONFIG_AR_SMV_CURRENT_RMS_MA),
        static_cast<std::int32_t>(CONFIG_AR_SMV_VOLTAGE_RMS_MV / 10));

    if (xTaskCreatePinnedToCore(
            &live_control_task,
            "ar_smv_control",
            6144,
            nullptr,
            1,
            nullptr,
            0) != pdPASS) {
        ESP_LOGE(kTag, "Failed to create live control task on CPU0");
        return;
    }

    if (xTaskCreatePinnedToCore(
            &publisher_task,
            "ar_smv_publisher",
            8192,
            eth_handle,
            configMAX_PRIORITIES - 2,
            nullptr,
            1) != pdPASS) {
        ESP_LOGE(kTag, "Failed to create realtime publisher task on CPU1");
        return;
    }

    if (xTaskCreatePinnedToCore(
            &telemetry_task,
            "ar_smv_telemetry",
            4096,
            nullptr,
            2,
            nullptr,
            0) != pdPASS) {
        ESP_LOGE(kTag, "Failed to create telemetry task on CPU0");
        return;
    }
#else
    ESP_LOGW(kTag, "Live transmission disabled; Ethernet bring-up only");
#endif
}
