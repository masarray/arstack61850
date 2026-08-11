// SPDX-License-Identifier: GPL-3.0-or-later

#include "live_control.hpp"
#include "profile_control.hpp"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
    ESP_LOGI(kTag, "  START | STOP | SHOW | ZERO | HELP");
    ESP_LOGI(kTag, "  FREQ <millihertz>");
    ESP_LOGI(kTag, "  SET <IA|IB|IC|IN|UA|UB|UC|UN> <rms_wire_counts> <phase_mdeg> [quality]");
    ESP_LOGI(kTag, "  ENABLE <channel> <0|1>");
    ESP_LOGI(kTag, "  QUALITY <channel> <uint32/0xhex>");
    ESP_LOGI(kTag, "GUI profile deployment uses bounded PROFILE subcommands while STOPPED.");
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
