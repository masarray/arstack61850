// SPDX-License-Identifier: GPL-3.0-or-later
#include "multi_profile_control.hpp"
#include "multi_profile_staging.hpp"
#include "live_control.hpp"

#include "esp_log.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <span>

namespace ar::esp32p4::smv {
namespace {
constexpr char kTag[] = "ar_smv_multi";
constexpr char kDelimiters[] = " \t\r\n";

// One UART control-task owner; real-time publisher does NOT touch this bank
// until P0C2 adds a separately synchronized activation protocol.
MultiSvConfigStore g_multi{};
// Selection changes only in STOPPED control-plane commands; the active
// publisher reads the immutable committed roster once at START.
std::atomic<bool> g_multi_tx_selected{false};

void upper(char* text) noexcept {
    if (text == nullptr) return;
    for (; *text; ++text)
        *text = static_cast<char>(std::toupper(static_cast<unsigned char>(*text)));
}

[[nodiscard]] bool parse_uint(const char* text, std::uint32_t& output) noexcept {
    if (!text || !*text || *text == '-') return false;
    errno = 0;
    char* end = nullptr;
    const auto parsed = std::strtoul(text, &end, 10);
    if (errno || end == text || *end || parsed > std::numeric_limits<std::uint32_t>::max())
        return false;
    output = static_cast<std::uint32_t>(parsed);
    return true;
}

[[nodiscard]] int nibble(const char ch) noexcept {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

[[nodiscard]] bool decode_chunk(
    const char* hex,
    std::array<std::uint8_t, MultiSvConfigStore::max_chunk_bytes>& output,
    std::size_t& count) noexcept {
    count = 0U;
    if (!hex) return false;
    const auto length = std::strlen(hex);
    if (!length || (length % 2U) || length > 2U * output.size()) return false;
    for (std::size_t i = 0U; i < length; i += 2U) {
        const int hi = nibble(hex[i]), lo = nibble(hex[i + 1U]);
        if (hi < 0 || lo < 0) return false;
        output[count++] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return true;
}

[[nodiscard]] bool no_extra(char** save) noexcept {
    return strtok_r(nullptr, kDelimiters, save) == nullptr;
}

void print_status() noexcept {
    ESP_LOGI(kTag,
        "MULTISV STATUS staging=%u validated=%u transaction=%lu received=%lu expected=%lu last=%lu configured=%u generation=%llu hash=%016llX bytes=%lu output=OPT_IN_THREE_SV_TX_PREVIEW",
        g_multi.uploading() ? 1U : 0U,
        g_multi.validated() ? 1U : 0U,
        static_cast<unsigned long>(g_multi.transaction()),
        static_cast<unsigned long>(g_multi.received()),
        static_cast<unsigned long>(g_multi.expected()),
        static_cast<unsigned long>(g_multi.last_transaction()),
        g_multi.configured() ? 1U : 0U,
        static_cast<unsigned long long>(g_multi.generation()),
        static_cast<unsigned long long>(g_multi.committed_hash()),
        static_cast<unsigned long>(g_multi.committed_bytes()));
    ESP_LOGI(kTag, "MULTISV TXMODE selected=%u configured=%u (START uses selected roster; STOP ends TX)",
             g_multi_tx_selected.load(std::memory_order_acquire) ? 1U : 0U,
             g_multi.configured() ? 1U : 0U);
}

void print_roster() noexcept {
    print_status();
    if (!g_multi.configured()) return;
    const auto& profiles = g_multi.committed_profiles();
    for (std::size_t i = 0U; i < profiles.size(); ++i) {
        const auto& p = profiles[i];
        ESP_LOGI(kTag,
            "MULTISV STREAM slot=%u svID=%s APPID=0x%04X MAC=%02X:%02X:%02X:%02X:%02X:%02X VLAN=%u/%u/%u rate=%lu wrap=%u noASDU=%u confRev=%lu output=INACTIVE",
            static_cast<unsigned>(i + 1U),
            p.sv_id.data(),
            static_cast<unsigned>(p.app_id),
            static_cast<unsigned>(p.destination_mac[0]),
            static_cast<unsigned>(p.destination_mac[1]),
            static_cast<unsigned>(p.destination_mac[2]),
            static_cast<unsigned>(p.destination_mac[3]),
            static_cast<unsigned>(p.destination_mac[4]),
            static_cast<unsigned>(p.destination_mac[5]),
            p.vlan_present ? 1U : 0U,
            static_cast<unsigned>(p.vlan_id),
            static_cast<unsigned>(p.vlan_priority),
            static_cast<unsigned long>(p.frame_rate_hz),
            static_cast<unsigned>(p.sample_counter_modulus),
            static_cast<unsigned>(p.no_asdu),
            static_cast<unsigned long>(p.configuration_revision));
    }
}
} // namespace

void multi_profile_abort_staging() noexcept { g_multi.abort(); }

const ar::iec61850::sampled_values::CompiledSvDeviceProfile*
multi_profile_tx_roster_if_selected() noexcept {
    if (!g_multi_tx_selected.load(std::memory_order_acquire) ||
        !g_multi.configured()) return nullptr;
    // COMMIT is only accepted while STOPPED, and START is parsed by this
    // same serial command task; no writer can race a running publisher.
    return g_multi.committed_profiles().data();
}

void handle_multi_profile_command(char* arguments) noexcept {
    char* save = nullptr;
    char* command = strtok_r(arguments, kDelimiters, &save);
    if (!command) {
        ESP_LOGE(kTag, "MULTISV subcommand required; use STATUS/SHOW/BEGIN/CHUNK/VALIDATE/COMMIT/ABORT");
        return;
    }
    upper(command);

    if (!std::strcmp(command, "STATUS") || !std::strcmp(command, "SHOW")) {
        if (!no_extra(&save)) {
            ESP_LOGE(kTag, "MULTISV SHOW/STATUS takes no arguments");
            return;
        }
        if (!std::strcmp(command, "SHOW")) print_roster();
        else print_status();
        return;
    }

    // Opt-in: generic legacy START remains single-stream unless SELECT
    // explicitly arms a committed 3-SV roster. An incomplete transfer cannot
    // affect the active mode, and SELECT never starts transmission itself.
    if (!std::strcmp(command, "SELECT") || !std::strcmp(command, "DESELECT")) {
        if (!no_extra(&save) || live_tx_running() || g_multi.uploading()) {
            ESP_LOGE(kTag, "MULTISV SELECT rejected: STOP_SV_AND_UPLOAD_FIRST");
            return;
        }
        if (!std::strcmp(command, "SELECT") && !g_multi.configured()) {
            ESP_LOGE(kTag, "MULTISV SELECT rejected: no committed roster");
            return;
        }
        const bool selected = !std::strcmp(command, "SELECT");
        g_multi_tx_selected.store(selected, std::memory_order_release);
        ESP_LOGI(kTag, "MULTISV TXMODE selected=%u; next verified START uses %s",
                 selected ? 1U : 0U, selected ? "THREE_SV" : "LEGACY_ONE_SV");
        return;
    }

    std::uint32_t txn{};
    if (!parse_uint(strtok_r(nullptr, kDelimiters, &save), txn) || txn == 0U) {
        ESP_LOGE(kTag, "MULTISV rejected: missing or invalid transaction ID");
        return;
    }
    if (!std::strcmp(command, "ABORT")) {
        if (!no_extra(&save) || !g_multi.owns(txn)) {
            ESP_LOGE(kTag, "MULTISV ABORT rejected: unknown transaction");
            return;
        }
        g_multi.abort();
        ESP_LOGI(kTag, "MULTISV ABORT transaction=%lu accepted", static_cast<unsigned long>(txn));
        return;
    }

    // The old V1 publisher is never silently upgraded to a three-stream TX
    // runtime. M4.0 identity/layout mutations require a STOPPED board.
    if (live_tx_running()) {
        g_multi.abort();
        ESP_LOGE(kTag, "MULTISV rejected: STOP_SV_FIRST");
        return;
    }

    if (!std::strcmp(command, "BEGIN")) {
        std::uint32_t total{};
        if (!parse_uint(strtok_r(nullptr, kDelimiters, &save), total) ||
            !no_extra(&save) || !g_multi.begin(txn, total)) {
            ESP_LOGE(kTag, "MULTISV BEGIN rejected: busy, replay or invalid record size");
            return;
        }
        ESP_LOGI(kTag, "MULTISV BEGIN transaction=%lu bytes=%lu accepted",
                 static_cast<unsigned long>(txn), static_cast<unsigned long>(total));
        return;
    }
    if (!std::strcmp(command, "CHUNK")) {
        std::uint32_t offset{};
        const char* offset_text = strtok_r(nullptr, kDelimiters, &save);
        const char* chunk_text = strtok_r(nullptr, kDelimiters, &save);
        std::array<std::uint8_t, MultiSvConfigStore::max_chunk_bytes> buffer{};
        std::size_t count{};
        if (!parse_uint(offset_text, offset) || !no_extra(&save) ||
            !g_multi.owns(txn) || !decode_chunk(chunk_text, buffer, count) ||
            !g_multi.append(txn, offset, std::span<const std::uint8_t>{buffer}.first(count))) {
            g_multi.abort();
            ESP_LOGE(kTag, "MULTISV CHUNK rejected: invalid hex, offset or transaction");
            return;
        }
        ESP_LOGI(kTag, "MULTISV CHUNK transaction=%lu received=%lu",
                 static_cast<unsigned long>(txn), static_cast<unsigned long>(g_multi.received()));
        return;
    }
    if (!std::strcmp(command, "VALIDATE")) {
        if (!no_extra(&save) || !g_multi.validate(txn)) {
            ESP_LOGE(kTag, "MULTISV VALIDATE rejected: incomplete, bad CRC or unsupported stream layout");
            return;
        }
        ESP_LOGI(kTag, "MULTISV VALIDATE transaction=%lu accepted count=3 rate=4000",
                 static_cast<unsigned long>(txn));
        return;
    }
    if (!std::strcmp(command, "COMMIT")) {
        if (!no_extra(&save) || !g_multi.commit(txn)) {
            ESP_LOGE(kTag, "MULTISV COMMIT rejected: VALIDATE required or transaction invalid");
            return;
        }
        // A new committed profile set is never auto-activated by replacing
        // the old roster. Require an explicit SELECT after each COMMIT.
        g_multi_tx_selected.store(false, std::memory_order_release);
        ESP_LOGI(kTag,
            "MULTISV COMMITTED transaction=%lu generation=%llu hash=%016llX bytes=%lu output=INACTIVE_UNTIL_M4_P0C2",
            static_cast<unsigned long>(txn),
            static_cast<unsigned long long>(g_multi.generation()),
            static_cast<unsigned long long>(g_multi.committed_hash()),
            static_cast<unsigned long>(g_multi.committed_bytes()));
        return;
    }
    ESP_LOGE(kTag, "Unknown MULTISV subcommand");
}

} // namespace ar::esp32p4::smv
