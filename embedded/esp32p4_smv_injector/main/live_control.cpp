// SPDX-License-Identifier: GPL-3.0-or-later

#include "live_control.hpp"
#include "ethernet_port.h"
#include "profile_control.hpp"

#include "esp_log.h"
#include "esp_mac.h"
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
using ar::iec61850::sampled_values::live_channel_index;

constexpr char kTag[] = "ar_smv_ctrl";
constexpr std::array<std::string_view, 8> kChannelNames{
    "IA", "IB", "IC", "IN", "UA", "UB", "UC", "UN"};
constexpr std::string_view kTokenDelimiters{" \t\r\n"};

SvLiveSignalBank g_signal_bank;
std::atomic<bool> g_running{false};
std::atomic<bool> g_start_request{false};
std::atomic<TaskHandle_t> g_publisher_task{nullptr};

void wake_publisher() noexcept {
    const auto task = g_publisher_task.load(std::memory_order_acquire);
    if (task != nullptr) {
        xTaskNotifyGive(task);
    }
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

void print_help() noexcept {
    ESP_LOGI(kTag, "Live control commands (bench serial transport):");
    ESP_LOGI(kTag, "  IDENTIFY | START | STOP | SHOW | ZERO | HELP");
    ESP_LOGI(kTag, "  FREQ <millihertz>");
    ESP_LOGI(kTag, "  SET <IA|IB|IC|IN|UA|UB|UC|UN> <rms_wire_counts> <phase_mdeg> [quality]");
    ESP_LOGI(kTag, "  ENABLE <channel> <0|1>");
    ESP_LOGI(kTag, "  QUALITY <channel> <uint32/0xhex>");
    ESP_LOGI(kTag, "  SHAPE CT <0|1> <dc_permille> <harmonic_permille> <order> <clip_permille>");
    ESP_LOGI(kTag, "  PTP SHOW | PTP START | PTP STOP");
    ESP_LOGI(kTag, "  PTP CONFIG <domain> <transport> <vlan> <vid> <pcp> <announce_ms> <sync_ms> <pdelay>");
    ESP_LOGI(kTag, "GUI profile deployment uses bounded PROFILE subcommands while STOPPED.");
}

void print_identity() noexcept {
    std::array<std::uint8_t, 6> device_id{};
    if (esp_read_mac(device_id.data(), ESP_MAC_EFUSE_FACTORY) != ESP_OK) {
        ESP_LOGE(kTag, "ARSTACK identity unavailable: factory device ID read failed");
        return;
    }
    ESP_LOGI(kTag,
             "ARSTACK identity product=SMV-INJECTOR target=ESP32-P4 protocol=1 device_id=%02X%02X%02X%02X%02X%02X",
             static_cast<unsigned>(device_id[0]), static_cast<unsigned>(device_id[1]),
             static_cast<unsigned>(device_id[2]), static_cast<unsigned>(device_id[3]),
             static_cast<unsigned>(device_id[4]), static_cast<unsigned>(device_id[5]));
}

void print_ptp_state() noexcept {
#if CONFIG_AR_PTP_LAB_TX
    ar_ptp_lab_status_t status{};
    ar_ptp_lab_config_t config{};
    if (!ar_ptp_lab_get_status(&status) || !ar_ptp_lab_get_config(&config)) {
        ESP_LOGE(kTag, "PTP status unavailable");
        return;
    }
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
#if CONFIG_AR_PTP_LAB_TX
    if (std::strcmp(subcommand, "START") == 0) {
        if (has_extra_token(&save)) {
            ESP_LOGE(kTag, "Usage: PTP START");
            return;
        }
        if (!ar_esp32p4_ptp_start()) ESP_LOGE(kTag, "PTP start rejected: Ethernet is unavailable");
        else ESP_LOGI(kTag, "PTP start accepted");
        print_ptp_state();
        return;
    }
    if (std::strcmp(subcommand, "STOP") == 0) {
        if (has_extra_token(&save)) {
            ESP_LOGE(kTag, "Usage: PTP STOP");
            return;
        }
        ar_ptp_lab_stop();
        ESP_LOGI(kTag, "PTP stop requested");
        return;
    }
    if (std::strcmp(subcommand, "CONFIG") == 0) {
        std::array<std::uint32_t, 8> values{};
        for (auto& value : values) {
            if (!parse_u32(strtok_r(nullptr, kTokenDelimiters.data(), &save), value)) {
                ESP_LOGE(kTag, "Usage: PTP CONFIG <domain> <transport> <vlan> <vid> <pcp> <announce_ms> <sync_ms> <pdelay>");
                return;
            }
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
        if (!ar_ptp_lab_configure(&config)) {
            ESP_LOGE(kTag, "PTP configuration rejected: stop PTP first or correct the profile");
            return;
        }
        ESP_LOGI(kTag, "PTP configuration accepted");
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

    if (std::strcmp(command, "IDENTIFY") == 0) {
        if (has_extra_token(&save)) ESP_LOGE(kTag, "Usage: IDENTIFY");
        else print_identity();
        return;
    }

    if (std::strcmp(command, "START") == 0) {
        if (has_extra_token(&save)) {
            ESP_LOGE(kTag, "Usage: START");
            return;
        }
        g_start_request.store(true, std::memory_order_release);
        g_running.store(true, std::memory_order_release);
        wake_publisher();
        ESP_LOGI(kTag, "START accepted: profile armed, phase and smpCnt reset at first sample boundary");
        return;
    }
    if (std::strcmp(command, "STOP") == 0) {
        if (has_extra_token(&save)) {
            ESP_LOGE(kTag, "Usage: STOP");
            return;
        }
        g_running.store(false, std::memory_order_release);
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
            ESP_LOGE(kTag, "Usage: FREQ <millihertz>");
            return;
        }
        auto state = g_signal_bank.snapshot();
        state.frequency_millihz = frequency;
        static_cast<void>(publish_state(state));
        return;
    }

    if (std::strcmp(command, "SHAPE") == 0) {
        char* shape_text = strtok_r(nullptr, kTokenDelimiters.data(), &save);
        if (shape_text == nullptr) {
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
            ESP_LOGE(kTag, "Usage: SHAPE CT <0|1> <dc_permille> <harmonic_permille> <order> <clip_permille>");
            return;
        }
        auto state = g_signal_bank.snapshot();
        state.current_shape.enabled = enabled != 0U;
        state.current_shape.dc_offset_permille = dc_offset;
        state.current_shape.harmonic_permille = harmonic;
        state.current_shape.harmonic_order = static_cast<std::uint8_t>(order);
        state.current_shape.clip_permille = clip;
        static_cast<void>(publish_state(state));
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
            ESP_LOGE(kTag, "Channel name is required");
            return;
        }
        uppercase_ascii(channel_text);
        const auto channel_index = live_channel_index(channel_text);
        if (!channel_index.has_value()) {
            ESP_LOGE(kTag, "Unknown channel '%s'", channel_text);
            return;
        }

        auto state = g_signal_bank.snapshot();
        auto& channel = state.channels[*channel_index];

        if (std::strcmp(command, "SET") == 0) {
            char* rms_text = strtok_r(nullptr, kTokenDelimiters.data(), &save);
            char* phase_text = strtok_r(nullptr, kTokenDelimiters.data(), &save);
            char* quality_text = strtok_r(nullptr, kTokenDelimiters.data(), &save);
            std::int32_t rms{};
            std::int32_t phase{};
            if (!parse_i32(rms_text, rms) || !parse_i32(phase_text, phase)) {
                ESP_LOGE(kTag, "Usage: SET <channel> <rms_wire_counts> <phase_mdeg> [quality]");
                return;
            }
            std::uint32_t quality{};
            if (quality_text != nullptr && !parse_u32(quality_text, quality)) {
                ESP_LOGE(kTag, "Invalid quality value '%s'", quality_text);
                return;
            }
            if (has_extra_token(&save)) {
                ESP_LOGE(kTag, "Usage: SET <channel> <rms_wire_counts> <phase_mdeg> [quality]");
                return;
            }
            channel.rms_counts = rms;
            channel.phase_millidegrees = phase;
            if (quality_text != nullptr) channel.quality = quality;
            static_cast<void>(publish_state(state));
            return;
        }

        if (std::strcmp(command, "ENABLE") == 0) {
            char* enabled_text = strtok_r(nullptr, kTokenDelimiters.data(), &save);
            std::uint32_t enabled{};
            if (!parse_u32(enabled_text, enabled) || enabled > 1U || has_extra_token(&save)) {
                ESP_LOGE(kTag, "Usage: ENABLE <channel> <0|1>");
                return;
            }
            channel.enabled = enabled != 0U;
            static_cast<void>(publish_state(state));
            return;
        }

        char* quality_text = strtok_r(nullptr, kTokenDelimiters.data(), &save);
        std::uint32_t quality{};
        if (!parse_u32(quality_text, quality) || has_extra_token(&save)) {
            ESP_LOGE(kTag, "Usage: QUALITY <channel> <uint32/0xhex>");
            return;
        }
        channel.quality = quality;
        static_cast<void>(publish_state(state));
        return;
    }

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
    g_running.store(false, std::memory_order_release);
    g_start_request.store(false, std::memory_order_release);
}

void live_control_bind_publisher_task(const TaskHandle_t task) noexcept {
    g_publisher_task.store(task, std::memory_order_release);
}

void live_control_force_stop() noexcept {
    g_running.store(false, std::memory_order_release);
    g_start_request.store(false, std::memory_order_release);
    wake_publisher();
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
