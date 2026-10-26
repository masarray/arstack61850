// SPDX-License-Identifier: GPL-3.0-or-later
#include "multi_profile_control.hpp"
#include "multi_profile_staging.hpp"
#include "live_control.hpp"
#include "ariec61850/sampled_values/m4_three_sv_signals.hpp"
#include "ariec61850/sampled_values/live_signal_transaction.hpp"

#include "esp_log.h"
#include "esp_timer.h"

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
// Exactly three independent atomic waveform banks; only console task writes.
ar::iec61850::sampled_values::M4ThreeSvSignalBanks g_mu_signals{};
// Each slot independently stages a complete scenario. One UART writer; no
// transactional parsing or allocation in the 4-kHz data-plane.
std::array<ar::iec61850::sampled_values::SvScenarioTransaction, 3> g_mu_transactions{};
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


[[nodiscard]] bool parse_signed(const char* text, std::int32_t& output) noexcept {
    if (!text || !*text) return false;
    errno = 0;
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (errno || end == text || *end || value < std::numeric_limits<std::int32_t>::min() ||
        value > std::numeric_limits<std::int32_t>::max()) return false;
    output = static_cast<std::int32_t>(value);
    return true;
}

[[nodiscard]] int stream_channel(char* name) noexcept {
    if (!name) return -1;
    upper(name);
    static constexpr const char* names[] = {"IA", "IB", "IC", "IN", "UA", "UB", "UC", "UN"};
    for (int i = 0; i < 8; ++i)
        if (std::strcmp(name, names[i]) == 0) return i;
    return -1;
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


ar::iec61850::sampled_values::SvLiveSignalState
multi_profile_signal_snapshot(const std::size_t slot) noexcept {
    ar::iec61850::sampled_values::SvLiveSignalState value{};
    // Invalid slot is only reachable by programming error, never UI input.
    static_cast<void>(g_mu_signals.snapshot(slot, value));
    return value;
}


[[nodiscard]] bool parse_u64_token(const char* text, std::uint64_t& output,
                                   const int base) noexcept {
    if (!text || !*text || *text == '-') return false;
    errno = 0;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, base);
    if (errno || end == text || *end) return false;
    output = static_cast<std::uint64_t>(value);
    return true;
}

[[nodiscard]] std::uint32_t serial_now_ms() noexcept {
    return static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL);
}

void reset_mu_transactions() noexcept {
    for (auto& tx : g_mu_transactions) tx.reset();
}

// The existing V1 SvScenarioTransaction is deliberately reused per MU:
// complete 8-channel staging, generation guard, 8s timeout, canonical
// wire-state hash and explicit readback. Only this UART task writes a bank.
void handle_svstream_cfg(const std::size_t slot, char* save) noexcept {
    using Tx = ar::iec61850::sampled_values::SvScenarioTransaction;
    auto& tx = g_mu_transactions[slot];
    auto* bank = g_mu_signals.control_bank_for_serial_writer(slot);
    char* verb = strtok_r(nullptr, kDelimiters, &save);
    if (!verb || bank == nullptr) {
        tx.reject();
        ESP_LOGE(kTag, "SVSTREAM CFG requires BEGIN|FREQ|SET|SHAPE|COMMIT|READBACK|ABORT");
        return;
    }
    upper(verb);
    const auto now = serial_now_ms();
    const auto log_slot = static_cast<unsigned>(slot + 1U);
    if (std::strcmp(verb, "ABORT") == 0) {
        if (!no_extra(&save)) { tx.reject(); return; }
        tx.reset();
        ESP_LOGI(kTag, "SVSTREAM CFG ABORT slot=%u", log_slot);
        return;
    }
    if (std::strcmp(verb, "BEGIN") == 0) {
        std::uint64_t token{};
        if (!parse_u64_token(strtok_r(nullptr, kDelimiters, &save), token, 10) ||
            !no_extra(&save) ||
            !tx.begin(token, bank->snapshot(), now, live_tx_running())) {
            tx.reset();
            ESP_LOGE(kTag, "SVSTREAM CFG BEGIN rejected slot=%u", log_slot);
            return;
        }
        ESP_LOGI(kTag, "SVSTREAM CFG BEGIN slot=%u token=%llu",
                 log_slot, static_cast<unsigned long long>(token));
        return;
    }
    if (std::strcmp(verb, "READBACK") == 0) {
        std::uint64_t token{};
        if (!parse_u64_token(strtok_r(nullptr, kDelimiters, &save), token, 10) ||
            !no_extra(&save) || !tx.confirm_readback(*bank, token, now)) {
            ESP_LOGE(kTag, "SVSTREAM CFG READBACK rejected slot=%u", log_slot);
            return;
        }
        ESP_LOGI(kTag,
            "SVSTREAM CFG READBACK slot=%u token=%llu hash=%016llX generation=%llu",
            log_slot, static_cast<unsigned long long>(token),
            static_cast<unsigned long long>(tx.checksum()),
            static_cast<unsigned long long>(tx.applied_generation()));
        return;
    }
    if (std::strcmp(verb, "COMMIT") == 0) {
        std::uint64_t token{}, expected_hash{};
        if (!parse_u64_token(strtok_r(nullptr, kDelimiters, &save), token, 10) ||
            !parse_u64_token(strtok_r(nullptr, kDelimiters, &save), expected_hash, 16) ||
            !no_extra(&save) || tx.began_running() != live_tx_running()) {
            tx.reject();
            ESP_LOGE(kTag, "SVSTREAM CFG COMMIT rejected slot=%u malformed or RUN state changed",
                     log_slot);
            return;
        }
        const auto result = tx.commit(*bank, token, expected_hash, now);
        if (result != Tx::Result::accepted) {
            ESP_LOGE(kTag, "SVSTREAM CFG REJECT slot=%u code=%u", log_slot,
                     static_cast<unsigned>(result));
            return;
        }
        ESP_LOGI(kTag,
            "SVSTREAM CFG COMMITTED slot=%u token=%llu hash=%016llX generation=%llu",
            log_slot, static_cast<unsigned long long>(token),
            static_cast<unsigned long long>(tx.checksum()),
            static_cast<unsigned long long>(tx.applied_generation()));
        return;
    }

    if (!tx.staging() || tx.began_running() != live_tx_running()) {
        tx.reject();
        ESP_LOGE(kTag, "SVSTREAM CFG rejected slot=%u: BEGIN required or RUN state changed",
                 log_slot);
        return;
    }
    auto& draft = tx.draft();
    if (std::strcmp(verb, "FREQ") == 0) {
        std::uint32_t frequency{};
        if (!parse_uint(strtok_r(nullptr, kDelimiters, &save), frequency) ||
            !no_extra(&save)) { tx.reject(); return; }
        draft.frequency_millihz = frequency;
        tx.mark_frequency();
    } else if (std::strcmp(verb, "SET") == 0) {
        const int index = stream_channel(strtok_r(nullptr, kDelimiters, &save));
        std::int32_t rms{}, phase{};
        std::uint32_t quality{}, enabled{};
        if (index < 0 ||
            !parse_signed(strtok_r(nullptr, kDelimiters, &save), rms) ||
            !parse_signed(strtok_r(nullptr, kDelimiters, &save), phase) ||
            !parse_uint(strtok_r(nullptr, kDelimiters, &save), quality) ||
            !parse_uint(strtok_r(nullptr, kDelimiters, &save), enabled) ||
            enabled > 1U || !no_extra(&save)) { tx.reject(); return; }
        auto& ch = draft.channels[static_cast<std::size_t>(index)];
        ch.rms_counts = rms;
        ch.phase_millidegrees = phase;
        ch.quality = quality;
        ch.enabled = enabled != 0U;
        tx.mark_set(static_cast<std::size_t>(index));
        tx.mark_enabled(static_cast<std::size_t>(index));
    } else if (std::strcmp(verb, "SHAPE") == 0) {
        std::uint32_t enabled{}, harmonic{}, order{}, clip{};
        std::int32_t dc{};
        if (!parse_uint(strtok_r(nullptr, kDelimiters, &save), enabled) ||
            enabled > 1U ||
            !parse_signed(strtok_r(nullptr, kDelimiters, &save), dc) ||
            !parse_uint(strtok_r(nullptr, kDelimiters, &save), harmonic) ||
            !parse_uint(strtok_r(nullptr, kDelimiters, &save), order) ||
            !parse_uint(strtok_r(nullptr, kDelimiters, &save), clip) ||
            order > 255U || !no_extra(&save)) { tx.reject(); return; }
        draft.current_shape = {enabled != 0U, dc, harmonic,
                               static_cast<std::uint8_t>(order), clip};
        tx.mark_shape();
    } else {
        tx.reject();
        ESP_LOGE(kTag, "SVSTREAM CFG invalid command slot=%u", log_slot);
        return;
    }
    // Even before COMMIT, reject an invalid individual field without exposing
    // this draft to the publisher; full validation repeats on COMMIT.
    if (!ar::iec61850::sampled_values::SvLiveSignalBank::validate(draft)) {
        tx.reject();
        ESP_LOGE(kTag, "SVSTREAM CFG invalid staged value slot=%u", log_slot);
        return;
    }
    ESP_LOGI(kTag, "SVSTREAM CFG STAGED slot=%u verb=%s", log_slot, verb);
}

void handle_svstream_command(char* arguments) noexcept {
    char* save = nullptr;
    const char* slot_text = strtok_r(arguments, kDelimiters, &save);
    std::uint32_t slot_number{};
    if (!parse_uint(slot_text, slot_number) || slot_number < 1U || slot_number > 3U ||
        !g_multi.configured() ||
        !g_multi_tx_selected.load(std::memory_order_acquire)) {
        ESP_LOGE(kTag, "SVSTREAM rejected: select committed MULTISV roster first; slot 1..3");
        return;
    }
    const std::size_t slot = slot_number - 1U;
    char* verb = strtok_r(nullptr, kDelimiters, &save);
    if (!verb) { ESP_LOGE(kTag, "SVSTREAM usage: <1..3> SHOW|SET|FREQ"); return; }
    upper(verb);
    if (std::strcmp(verb, "CFG") == 0) {
        handle_svstream_cfg(slot, save);
        return;
    }
    auto state = multi_profile_signal_snapshot(slot);
    if (std::strcmp(verb, "SHOW") == 0) {
        if (!no_extra(&save)) { ESP_LOGE(kTag, "SVSTREAM SHOW takes no extras"); return; }
        ESP_LOGI(kTag, "SVSTREAM slot=%lu generation=%llu frequency_millihz=%lu",
                 static_cast<unsigned long>(slot_number),
                 static_cast<unsigned long long>(state.generation),
                 static_cast<unsigned long>(state.frequency_millihz));
        static constexpr const char* names[] = {"IA", "IB", "IC", "IN", "UA", "UB", "UC", "UN"};
        for (std::size_t i = 0; i < state.channels.size(); ++i) {
            const auto& ch = state.channels[i];
            ESP_LOGI(kTag, "SVSTREAM slot=%lu channel=%s rms=%ld phase=%ld quality=%lu enabled=%u",
                     static_cast<unsigned long>(slot_number), names[i],
                     static_cast<long>(ch.rms_counts),
                     static_cast<long>(ch.phase_millidegrees),
                     static_cast<unsigned long>(ch.quality), ch.enabled ? 1U : 0U);
        }
        return;
    }
    if (std::strcmp(verb, "FREQ") == 0) {
        std::uint32_t frequency{};
        if (!parse_uint(strtok_r(nullptr, kDelimiters, &save), frequency) ||
            !no_extra(&save)) { ESP_LOGE(kTag, "SVSTREAM FREQ <millihertz>"); return; }
        state.frequency_millihz = frequency;
    } else if (std::strcmp(verb, "SET") == 0) {
        const int index = stream_channel(strtok_r(nullptr, kDelimiters, &save));
        std::int32_t rms{}, phase{};
        std::uint32_t quality{};
        if (index < 0 ||
            !parse_signed(strtok_r(nullptr, kDelimiters, &save), rms) ||
            !parse_signed(strtok_r(nullptr, kDelimiters, &save), phase) ||
            !parse_uint(strtok_r(nullptr, kDelimiters, &save), quality) ||
            !no_extra(&save)) {
            ESP_LOGE(kTag, "SVSTREAM SET <IA|IB|IC|IN|UA|UB|UC|UN> <rms> <phase_mdeg> <quality>");
            return;
        }
        auto& channel = state.channels[static_cast<std::size_t>(index)];
        channel.rms_counts = rms;
        channel.phase_millidegrees = phase;
        channel.quality = quality;
    } else {
        ESP_LOGE(kTag, "SVSTREAM unsupported verb; use SHOW/SET/FREQ");
        return;
    }
    if (!g_mu_signals.publish(slot, state)) {
        ESP_LOGE(kTag, "SVSTREAM rejected: invalid signal values or busy bank");
        return;
    }
    ESP_LOGI(kTag, "SVSTREAM committed slot=%lu generation=%llu",
             static_cast<unsigned long>(slot_number),
             static_cast<unsigned long long>(g_mu_signals.generation(slot)));
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
        reset_mu_transactions();
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
        // Roster identity changed: pending MU tickets belong to the old set.
        reset_mu_transactions();
        // A new committed profile set is never auto-activated by replacing
        // the old roster. Require an explicit SELECT after each COMMIT.
        g_multi_tx_selected.store(false, std::memory_order_release);
        ESP_LOGI(kTag,
            "MULTISV COMMITTED transaction=%lu generation=%llu hash=%016llX bytes=%lu output=CONFIG_COMMITTED_TX_REQUIRES_SELECT_AND_VERIFIED_START",
            static_cast<unsigned long>(txn),
            static_cast<unsigned long long>(g_multi.generation()),
            static_cast<unsigned long long>(g_multi.committed_hash()),
            static_cast<unsigned long>(g_multi.committed_bytes()));
        return;
    }
    ESP_LOGE(kTag, "Unknown MULTISV subcommand");
}

} // namespace ar::esp32p4::smv
