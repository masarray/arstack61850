// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/sampled_values/asdu.hpp"
#include "ariec61850/sampled_values/frame.hpp"
#include "ariec61850/sampled_values/frame_codec.hpp"
#include "ariec61850/sampled_values/live_signal_state.hpp"

#include "driver/gptimer.h"
#include "ethernet_port.h"
#include "live_control.hpp"
#include "runtime_profile.hpp"
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

using ar::esp32p4::smv::RuntimePublisherProfile;
using ar::esp32p4::smv::live_control_bind_publisher_task;
using ar::esp32p4::smv::live_control_initialize;
using ar::esp32p4::smv::live_control_task;
using ar::esp32p4::smv::live_signal_snapshot;
using ar::esp32p4::smv::live_tx_running;
using ar::esp32p4::smv::runtime_profile_initialize;
using ar::esp32p4::smv::runtime_profile_snapshot;
using ar::esp32p4::smv::runtime_profile_validate;
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
constexpr std::uint16_t kMirrorAppId = 0x4F01U;
constexpr char kMirrorSvId[] = "AR_DIAG_SV1";
constexpr std::uint16_t kSampleModeSamplesPerSecond = 1U;
constexpr std::uint16_t kDiagnosticEtherType = 0x88B5U;
constexpr std::uint32_t kStatsMergeEvery = 400U;
constexpr std::uint32_t kTimerResolutionHz = 1000000U;

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

struct RationalScheduleCursor final {
    std::uint32_t rate_hz{};
    std::uint32_t base_ticks{};
    std::uint32_t remainder{};
    std::uint32_t accumulator{};

    void reset(const std::uint32_t rate) noexcept {
        rate_hz = rate;
        base_ticks = kTimerResolutionHz / rate_hz;
        remainder = kTimerResolutionHz % rate_hz;
        accumulator = 0U;
    }

    [[nodiscard]] std::uint32_t next_ticks() noexcept {
        std::uint32_t ticks = base_ticks;
        accumulator += remainder;
        if (accumulator >= rate_hz) {
            accumulator -= rate_hz;
            ++ticks;
        }
        return ticks;
    }
};

struct SampleClockContext final {
    TaskHandle_t publisher_task{};
    RationalScheduleCursor schedule{};
    gptimer_alarm_config_t next_alarm{};
};

SampleClockContext g_sample_clock_context{};

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
    return std::vector<std::uint8_t>(8U * 8U, 0U);
}

SampledValuesFrame make_runtime_frame(
    const std::array<std::uint8_t, 6>& source_mac,
    const RuntimePublisherProfile& profile,
    const bool diagnostic_mirror) {
    SampledValueAsdu asdu;
    asdu.sv_id = diagnostic_mirror ? kMirrorSvId : profile.sv_id.data();
    asdu.data_set_reference =
        (!diagnostic_mirror && profile.include_data_set) ? profile.data_set_reference.data() : "";

    // Keep BER widths fixed so the realtime path only patches bytes in place.
    asdu.sample_count = 0x0100U;
    asdu.configuration_revision = 0x01000000U;
    asdu.reference_time.reset();
    // No disciplined PTP clock exists yet, so never claim synchronization.
    asdu.sample_synchronization = 0U;

    if (diagnostic_mirror || profile.include_sample_rate) {
        asdu.sample_rate = static_cast<std::uint16_t>(profile.publisher_rate_hz);
        asdu.sample_mode = kSampleModeSamplesPerSecond;
    } else {
        asdu.sample_rate.reset();
        asdu.sample_mode.reset();
    }
    asdu.sample_payload = make_4i4v_payload();

    SampledValuesFrame frame;
    frame.destination = MacAddress{std::span<const std::uint8_t>{profile.destination_mac}};
    frame.source = MacAddress{std::span<const std::uint8_t>{source_mac}};
    if (!diagnostic_mirror && profile.vlan_present) {
        frame.vlan = VlanTag{profile.vlan_priority, profile.vlan_id};
    } else {
        frame.vlan.reset();
    }
    frame.app_id = diagnostic_mirror ? kMirrorAppId : profile.app_id;
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
    const RuntimePublisherProfile& profile,
    const bool broadcast_destination,
    const bool diagnostic_mirror,
    PacketTemplate& packet) {
    packet = {};
    packet.bytes = SampledValuesFrameCodec::encode(
        make_runtime_frame(source_mac, profile, diagnostic_mirror));

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
    write_u32_be(packet.bytes.data() + conf_rev_offset, profile.configuration_revision);
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
    const std::array<std::uint8_t, 6>& destination_mac,
    const bool multicast) {
    std::array<std::uint8_t, 60> frame{};
    for (std::size_t index = 0U; index < 6U; ++index) {
        frame[index] = multicast ? destination_mac[index] : 0xFFU;
        frame[6U + index] = source_mac[index];
    }
    frame[12] = static_cast<std::uint8_t>((kDiagnosticEtherType >> 8U) & 0xFFU);
    frame[13] = static_cast<std::uint8_t>(kDiagnosticEtherType & 0xFFU);
    constexpr char payload[] = "ARSTACK-P3-PROFILE-PROBE";
    for (std::size_t index = 0U;
         index < sizeof(payload) - 1U && 14U + index < frame.size(); ++index) {
        frame[14U + index] = static_cast<std::uint8_t>(payload[index]);
    }
    return frame;
}
#endif

void merge_stats(TimingStats& local) noexcept {
    if (local.samples == 0U) return;
    portENTER_CRITICAL(&g_stats_mux);
    g_interval_stats.samples += local.samples;
    g_interval_stats.canonical_ok += local.canonical_ok;
    g_interval_stats.canonical_fail += local.canonical_fail;
    g_interval_stats.mirror_ok += local.mirror_ok;
    g_interval_stats.mirror_fail += local.mirror_fail;
    g_interval_stats.missed_slots += local.missed_slots;
    g_interval_stats.lateness_sum_us += local.lateness_sum_us;
    g_interval_stats.lateness_min_us = std::min(g_interval_stats.lateness_min_us, local.lateness_min_us);
    g_interval_stats.lateness_max_us = std::max(g_interval_stats.lateness_max_us, local.lateness_max_us);
    g_interval_stats.last_sample_count = local.last_sample_count;
    g_interval_stats.last_signal_generation = local.last_signal_generation;
    portEXIT_CRITICAL(&g_stats_mux);
    local = TimingStats{};
}

void reset_interval_stats() noexcept {
    portENTER_CRITICAL(&g_stats_mux);
    g_interval_stats = TimingStats{};
    portEXIT_CRITICAL(&g_stats_mux);
}

void update_local_timing(
    TimingStats& local,
    const std::int64_t lateness_us,
    const std::uint64_t missed_slots,
    const std::uint16_t sample_count,
    const std::uint64_t signal_generation) noexcept {
    const auto bounded = static_cast<std::int32_t>(std::clamp<std::int64_t>(
        lateness_us,
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max()));
    ++local.samples;
    local.missed_slots += missed_slots;
    local.lateness_sum_us += lateness_us;
    local.lateness_min_us = std::min(local.lateness_min_us, bounded);
    local.lateness_max_us = std::max(local.lateness_max_us, bounded);
    local.last_sample_count = sample_count;
    local.last_signal_generation = signal_generation;
}

bool sample_alarm_callback(
    gptimer_handle_t timer,
    const gptimer_alarm_event_data_t* event,
    void* user_context) {
    auto* context = static_cast<SampleClockContext*>(user_context);
    const auto interval = context->schedule.next_ticks();
    context->next_alarm = {};
    context->next_alarm.alarm_count = event->alarm_value + interval;
    context->next_alarm.flags.auto_reload_on_alarm = false;
    static_cast<void>(gptimer_set_alarm_action(timer, &context->next_alarm));

    BaseType_t high_priority_task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(context->publisher_task, &high_priority_task_woken);
    return high_priority_task_woken == pdTRUE;
}

gptimer_handle_t start_sample_clock(
    TaskHandle_t publisher_task_handle,
    const std::uint32_t publisher_rate_hz) {
    g_sample_clock_context = {};
    g_sample_clock_context.publisher_task = publisher_task_handle;
    g_sample_clock_context.schedule.reset(publisher_rate_hz);

    gptimer_config_t timer_config{};
    timer_config.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    timer_config.direction = GPTIMER_COUNT_UP;
    timer_config.resolution_hz = kTimerResolutionHz;

    gptimer_handle_t timer = nullptr;
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &timer));

    gptimer_event_callbacks_t callbacks{};
    callbacks.on_alarm = &sample_alarm_callback;
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(
        timer, &callbacks, &g_sample_clock_context));

    g_sample_clock_context.next_alarm = {};
    g_sample_clock_context.next_alarm.alarm_count =
        g_sample_clock_context.schedule.next_ticks();
    g_sample_clock_context.next_alarm.flags.auto_reload_on_alarm = false;
    ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &g_sample_clock_context.next_alarm));
    ESP_ERROR_CHECK(gptimer_enable(timer));
    ESP_ERROR_CHECK(gptimer_start(timer));
    return timer;
}

void stop_sample_clock(gptimer_handle_t& timer) noexcept {
    if (timer == nullptr) return;
    static_cast<void>(gptimer_stop(timer));
    static_cast<void>(gptimer_disable(timer));
    static_cast<void>(gptimer_del_timer(timer));
    timer = nullptr;
}

void publisher_task(void* argument) {
#if CONFIG_AR_SMV_LIVE_TX && CONFIG_AR_SMV_P2_REALTIME_50HZ
    const auto eth_handle = static_cast<esp_eth_handle_t>(argument);
    const auto task_handle = xTaskGetCurrentTaskHandle();
    live_control_bind_publisher_task(task_handle);

    ESP_LOGW(kTag, "LIVE SV TX ENABLED: isolated lab network only");
    ESP_LOGI(kTag, "Waiting for Ethernet link...");
    xEventGroupWaitBits(g_link_events, kLinkUpBit, pdFALSE, pdTRUE, portMAX_DELAY);

    std::array<std::uint8_t, 6> source_mac{};
    ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, source_mac.data()));
    ESP_LOGI(kTag, "EMAC source MAC=%02X:%02X:%02X:%02X:%02X:%02X",
             source_mac[0], source_mac[1], source_mac[2],
             source_mac[3], source_mac[4], source_mac[5]);

    const auto initial_signal = live_signal_snapshot();
    const auto initial_profile = runtime_profile_snapshot();
    ESP_LOGI(kTag,
             "Live signal engine ready: profile=%s rate=%lu fps signal=%u mHz generation=%llu; use GUI START",
             initial_profile.sv_id.data(),
             static_cast<unsigned long>(initial_profile.publisher_rate_hz),
             static_cast<unsigned>(initial_signal.frequency_millihz),
             static_cast<unsigned long long>(initial_signal.generation));

    PacketTemplate canonical;
    PacketTemplate mirror;
    RuntimePublisherProfile active_profile = initial_profile;
    static SvFixedPointSineEngine signal_engine;
    gptimer_handle_t sample_clock = nullptr;
    RationalScheduleCursor expected_schedule{};
    std::uint16_t canonical_sample_count = 0U;
    std::uint16_t mirror_sample_count = 0U;
    TimingStats local{};
    bool schedule_anchored = false;
    std::int64_t expected_wake_us = 0;

#if CONFIG_AR_SMV_DIAGNOSTIC_PROBES
    std::array<std::uint8_t, 60> probe_broadcast{};
    std::array<std::uint8_t, 60> probe_multicast{};
#endif

    while (true) {
        const std::uint32_t pending_ticks = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (pending_ticks == 0U) continue;

        if (take_start_request()) {
            stop_sample_clock(sample_clock);
            active_profile = runtime_profile_snapshot();
            if (!runtime_profile_validate(active_profile)) {
                ESP_LOGE(kTag, "START rejected: active runtime profile is invalid");
                continue;
            }
            if (!build_packet_template(source_mac, active_profile, false, false, canonical)) {
                ESP_LOGE(kTag, "START rejected: canonical template build failed");
                continue;
            }
#if CONFIG_AR_SMV_BROADCAST_MIRROR
            if (!build_packet_template(source_mac, active_profile, true, true, mirror)) {
                ESP_LOGE(kTag, "START rejected: diagnostic mirror template build failed");
                continue;
            }
#endif
#if CONFIG_AR_SMV_DIAGNOSTIC_PROBES
            probe_broadcast = make_probe(source_mac, active_profile.destination_mac, false);
            probe_multicast = make_probe(source_mac, active_profile.destination_mac, true);
#endif
            canonical_sample_count = 0U;
            mirror_sample_count = 0U;
            signal_engine.reset_phase();
            local = TimingStats{};
            reset_interval_stats();
            schedule_anchored = false;
            expected_schedule.reset(active_profile.publisher_rate_hz);
            static_cast<void>(expected_schedule.next_ticks()); // first alarm interval
            sample_clock = start_sample_clock(task_handle, active_profile.publisher_rate_hz);

            ESP_LOGI(kTag,
                     "PROFILE armed generation=%llu svID=%s APPID=0x%04X rate=%lu wrap=%u confRev=%lu VLAN=%u/%u/%u frameLen=%u",
                     static_cast<unsigned long long>(active_profile.generation),
                     active_profile.sv_id.data(),
                     static_cast<unsigned>(active_profile.app_id),
                     static_cast<unsigned long>(active_profile.publisher_rate_hz),
                     static_cast<unsigned>(active_profile.sample_counter_modulus),
                     static_cast<unsigned long>(active_profile.configuration_revision),
                     active_profile.vlan_present ? 1U : 0U,
                     static_cast<unsigned>(active_profile.vlan_id),
                     static_cast<unsigned>(active_profile.vlan_priority),
                     static_cast<unsigned>(canonical.bytes.size()));
            continue;
        }

        if (!live_tx_running()) {
            stop_sample_clock(sample_clock);
            schedule_anchored = false;
            continue;
        }
        if (sample_clock == nullptr) continue;

        const std::int64_t actual_wake_us = esp_timer_get_time();
        std::uint64_t missed_slots = 0U;
        if (!schedule_anchored) {
            expected_wake_us = actual_wake_us;
            schedule_anchored = true;
        } else {
            std::uint64_t elapsed_ticks = 0U;
            for (std::uint32_t i = 0U; i < pending_ticks; ++i) {
                elapsed_ticks += expected_schedule.next_ticks();
            }
            expected_wake_us += static_cast<std::int64_t>(elapsed_ticks);
        }

        const auto signal_state = live_signal_snapshot();
        if (pending_ticks > 1U) {
            missed_slots = static_cast<std::uint64_t>(pending_ticks - 1U);
            canonical_sample_count = static_cast<std::uint16_t>(
                (static_cast<std::uint32_t>(canonical_sample_count) + missed_slots) %
                active_profile.sample_counter_modulus);
            mirror_sample_count = static_cast<std::uint16_t>(
                static_cast<std::uint32_t>(mirror_sample_count) +
                static_cast<std::uint32_t>(missed_slots));
            signal_engine.advance(
                signal_state,
                active_profile.publisher_rate_hz,
                static_cast<std::uint32_t>(missed_slots));
        }

        const std::int64_t lateness_us = actual_wake_us - expected_wake_us;
        const auto row = signal_engine.next(signal_state, active_profile.publisher_rate_hz);

        if ((xEventGroupGetBits(g_link_events) & kLinkUpBit) == 0U) {
            ++local.canonical_fail;
#if CONFIG_AR_SMV_BROADCAST_MIRROR
            ++local.mirror_fail;
#endif
        } else {
            patch_packet(canonical, canonical_sample_count, row, signal_state);
            const esp_err_t canonical_result =
                esp_eth_transmit(eth_handle, canonical.bytes.data(), canonical.bytes.size());
            if (canonical_result == ESP_OK) ++local.canonical_ok;
            else ++local.canonical_fail;

#if CONFIG_AR_SMV_BROADCAST_MIRROR
            patch_packet(mirror, mirror_sample_count, row, signal_state);
            const esp_err_t mirror_result =
                esp_eth_transmit(eth_handle, mirror.bytes.data(), mirror.bytes.size());
            if (mirror_result == ESP_OK) ++local.mirror_ok;
            else ++local.mirror_fail;
#endif

#if CONFIG_AR_SMV_DIAGNOSTIC_PROBES
            if (canonical_sample_count == 0U) {
                static_cast<void>(esp_eth_transmit(
                    eth_handle, probe_broadcast.data(), probe_broadcast.size()));
                static_cast<void>(esp_eth_transmit(
                    eth_handle, probe_multicast.data(), probe_multicast.size()));
            }
#endif
        }

        update_local_timing(
            local, lateness_us, missed_slots, canonical_sample_count, signal_state.generation);
        canonical_sample_count = static_cast<std::uint16_t>(
            (canonical_sample_count + 1U) % active_profile.sample_counter_modulus);
        mirror_sample_count = static_cast<std::uint16_t>(mirror_sample_count + 1U);
        if (local.samples >= kStatsMergeEvery) merge_stats(local);
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
        if (snapshot.samples == 0U) continue;

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
    runtime_profile_initialize();
    live_control_initialize(
        static_cast<std::int32_t>(CONFIG_AR_SMV_CURRENT_RMS_MA),
        static_cast<std::int32_t>(CONFIG_AR_SMV_VOLTAGE_RMS_MV / 10));

    if (xTaskCreatePinnedToCore(
            &live_control_task, "ar_smv_control", 7168, nullptr, 1, nullptr, 0) != pdPASS) {
        ESP_LOGE(kTag, "Failed to create live control task on CPU0");
        return;
    }
    if (xTaskCreatePinnedToCore(
            &publisher_task, "ar_smv_publisher", 9216, eth_handle,
            configMAX_PRIORITIES - 2, nullptr, 1) != pdPASS) {
        ESP_LOGE(kTag, "Failed to create realtime publisher task on CPU1");
        return;
    }
    if (xTaskCreatePinnedToCore(
            &telemetry_task, "ar_smv_telemetry", 4096, nullptr, 2, nullptr, 0) != pdPASS) {
        ESP_LOGE(kTag, "Failed to create telemetry task on CPU0");
        return;
    }
#else
    ESP_LOGW(kTag, "Live transmission disabled; Ethernet bring-up only");
#endif
}
