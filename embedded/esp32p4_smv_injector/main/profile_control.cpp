// SPDX-License-Identifier: GPL-3.0-or-later

#include "profile_control.hpp"

#include "live_control.hpp"
#include "runtime_profile.hpp"
#include "smp_synch_lab.hpp"

#include "esp_log.h"

#include <array>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace ar::esp32p4::smv {
namespace {

constexpr char kTag[] = "ar_smv_profile";
constexpr char kDelimiters[] = " \t\r\n";
RuntimePublisherProfile g_staging{};
bool g_staging_active = false;

void uppercase_ascii(char* text) noexcept {
    if (text == nullptr) return;
    for (; *text != '\0'; ++text) {
        *text = static_cast<char>(std::toupper(static_cast<unsigned char>(*text)));
    }
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

int hex_nibble(const char ch) noexcept {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
    if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
    return -1;
}

template <std::size_t N>
bool decode_hex_text(const char* text, std::array<char, N>& destination) noexcept {
    destination.fill('\0');
    if (text == nullptr) return false;
    if (std::strcmp(text, "-") == 0) return true;
    const std::size_t length = std::strlen(text);
    if (length == 0U || (length % 2U) != 0U || length / 2U >= N) return false;
    for (std::size_t i = 0U; i < length; i += 2U) {
        const int high = hex_nibble(text[i]);
        const int low = hex_nibble(text[i + 1U]);
        if (high < 0 || low < 0) return false;
        const unsigned char byte = static_cast<unsigned char>((high << 4) | low);
        if (byte == 0U) return false;
        destination[i / 2U] = static_cast<char>(byte);
    }
    return true;
}

bool decode_mac(const char* text, std::array<std::uint8_t, 6>& mac) noexcept {
    if (text == nullptr || std::strlen(text) != 12U) return false;
    for (std::size_t i = 0U; i < mac.size(); ++i) {
        const int high = hex_nibble(text[i * 2U]);
        const int low = hex_nibble(text[i * 2U + 1U]);
        if (high < 0 || low < 0) return false;
        mac[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

bool no_extra(char** save) noexcept {
    return strtok_r(nullptr, kDelimiters, save) == nullptr;
}

bool parse_smp_synch_mode(char* text, SvSyncPolicyMode& mode) noexcept {
    if (text == nullptr) return false;
    uppercase_ascii(text);
    if (std::strcmp(text, "AUTO") == 0 || std::strcmp(text, "EXTERNAL") == 0) {
        mode = SvSyncPolicyMode::external_ptp_auto;
        return true;
    }
    if (std::strcmp(text, "0") == 0 || std::strcmp(text, "UNSYNC") == 0 ||
        std::strcmp(text, "FORCE0") == 0 || std::strcmp(text, "FORCE_0") == 0) {
        mode = SvSyncPolicyMode::honest_unsynchronized;
        return true;
    }
    if (std::strcmp(text, "1") == 0 || std::strcmp(text, "LOCAL") == 0 ||
        std::strcmp(text, "FORCE1") == 0 || std::strcmp(text, "FORCE_1") == 0) {
        mode = SvSyncPolicyMode::local_compatibility;
        return true;
    }
    if (std::strcmp(text, "2") == 0 || std::strcmp(text, "GLOBAL") == 0 ||
        std::strcmp(text, "FORCE2") == 0 || std::strcmp(text, "FORCE_2") == 0) {
        mode = SvSyncPolicyMode::global_compatibility;
        return true;
    }
    return false;
}

void print_smp_synch_policy() noexcept {
    const auto status = smp_synch_lab_status();
    const auto mode_name = ar::iec61850::time_sync::sv_sync_policy_name(status.decision.mode);
    const auto source_name = ar::iec61850::time_sync::smp_synch_source_name(status.decision.source);
    ESP_LOGI(kTag,
             "SMPSYNCH mode=%.*s advertised=%u source=%.*s simulated=%u measured=%u",
             static_cast<int>(mode_name.size()), mode_name.data(),
             static_cast<unsigned>(status.decision.value),
             static_cast<int>(source_name.size()), source_name.data(),
             status.decision.simulated() ? 1U : 0U,
             status.measured_input_valid ? 1U : 0U);
}

void print_profile(const RuntimePublisherProfile& profile) noexcept {
    ESP_LOGI(kTag,
             "PROFILE generation=%llu svID=%s APPID=0x%04X rate=%lu wrap=%u confRev=%lu VLAN=%u/%u/%u dataSetField=%u sampleRateField=%u",
             static_cast<unsigned long long>(profile.generation),
             profile.sv_id.data(),
             static_cast<unsigned>(profile.app_id),
             static_cast<unsigned long>(profile.publisher_rate_hz),
             static_cast<unsigned>(profile.sample_counter_modulus),
             static_cast<unsigned long>(profile.configuration_revision),
             profile.vlan_present ? 1U : 0U,
             static_cast<unsigned>(profile.vlan_id),
             static_cast<unsigned>(profile.vlan_priority),
             profile.include_data_set ? 1U : 0U,
             profile.include_sample_rate ? 1U : 0U);
}

} // namespace

void handle_profile_command(char* arguments) noexcept {
    char* save{};
    char* subcommand = strtok_r(arguments, kDelimiters, &save);
    if (subcommand == nullptr) {
        ESP_LOGE(kTag, "PROFILE subcommand required");
        return;
    }
    uppercase_ascii(subcommand);

    if (std::strcmp(subcommand, "SHOW") == 0) {
        if (!no_extra(&save)) {
            ESP_LOGE(kTag, "Usage: PROFILE SHOW");
            return;
        }
        print_profile(runtime_profile_snapshot());
        print_smp_synch_policy();
        return;
    }

    // smpSynch is a lab stimulus, not stream identity/layout. It may therefore
    // change live without rebuilding the deterministic SV packet template.
    if (std::strcmp(subcommand, "SMPSYNCH") == 0) {
        char* value = strtok_r(nullptr, kDelimiters, &save);
        SvSyncPolicyMode mode{};
        if (!parse_smp_synch_mode(value, mode) || !no_extra(&save)) {
            ESP_LOGE(kTag, "Usage: PROFILE SMPSYNCH <AUTO|0|1|2>");
            return;
        }
        smp_synch_lab_set_mode(mode);
        ESP_LOGI(kTag, "Live smpSynch lab policy accepted");
        print_smp_synch_policy();
        return;
    }

    if (live_tx_running()) {
        ESP_LOGE(kTag, "PROFILE rejected: stop the publisher before changing stream identity/layout");
        return;
    }

    if (std::strcmp(subcommand, "BEGIN") == 0) {
        if (!no_extra(&save)) {
            ESP_LOGE(kTag, "Usage: PROFILE BEGIN");
            return;
        }
        g_staging = runtime_profile_snapshot();
        g_staging_active = true;
        ESP_LOGI(kTag, "PROFILE staging started");
        return;
    }

    if (!g_staging_active) {
        ESP_LOGE(kTag, "PROFILE staging is not active; send PROFILE BEGIN first");
        return;
    }

    if (std::strcmp(subcommand, "ID") == 0) {
        char* value = strtok_r(nullptr, kDelimiters, &save);
        if (!decode_hex_text(value, g_staging.sv_id) || !no_extra(&save)) {
            ESP_LOGE(kTag, "Invalid PROFILE ID payload");
            return;
        }
        return;
    }

    if (std::strcmp(subcommand, "DATASET") == 0) {
        char* value = strtok_r(nullptr, kDelimiters, &save);
        if (!decode_hex_text(value, g_staging.data_set_reference) || !no_extra(&save)) {
            ESP_LOGE(kTag, "Invalid PROFILE DATASET payload");
            return;
        }
        return;
    }

    if (std::strcmp(subcommand, "L2") == 0) {
        char* appid_text = strtok_r(nullptr, kDelimiters, &save);
        char* mac_text = strtok_r(nullptr, kDelimiters, &save);
        char* vlan_present_text = strtok_r(nullptr, kDelimiters, &save);
        char* vlan_id_text = strtok_r(nullptr, kDelimiters, &save);
        char* pcp_text = strtok_r(nullptr, kDelimiters, &save);
        std::uint32_t appid{};
        std::uint32_t vlan_present{};
        std::uint32_t vlan_id{};
        std::uint32_t pcp{};
        std::array<std::uint8_t, 6> mac{};
        if (!parse_u32(appid_text, appid) || appid == 0U || appid > 65535U ||
            !decode_mac(mac_text, mac) ||
            !parse_u32(vlan_present_text, vlan_present) || vlan_present > 1U ||
            !parse_u32(vlan_id_text, vlan_id) || vlan_id > 4095U ||
            !parse_u32(pcp_text, pcp) || pcp > 7U || !no_extra(&save)) {
            ESP_LOGE(kTag, "Invalid PROFILE L2 payload");
            return;
        }
        g_staging.app_id = static_cast<std::uint16_t>(appid);
        g_staging.destination_mac = mac;
        g_staging.vlan_present = vlan_present != 0U;
        g_staging.vlan_id = static_cast<std::uint16_t>(vlan_id);
        g_staging.vlan_priority = static_cast<std::uint8_t>(pcp);
        return;
    }

    if (std::strcmp(subcommand, "SV") == 0) {
        char* confrev_text = strtok_r(nullptr, kDelimiters, &save);
        char* rate_text = strtok_r(nullptr, kDelimiters, &save);
        char* modulus_text = strtok_r(nullptr, kDelimiters, &save);
        char* no_asdu_text = strtok_r(nullptr, kDelimiters, &save);
        char* flags_text = strtok_r(nullptr, kDelimiters, &save);
        std::uint32_t confrev{};
        std::uint32_t rate{};
        std::uint32_t modulus{};
        std::uint32_t no_asdu{};
        std::uint32_t flags{};
        if (!parse_u32(confrev_text, confrev) ||
            !parse_u32(rate_text, rate) || rate == 0U || rate > 65535U ||
            !parse_u32(modulus_text, modulus) || modulus == 0U || modulus > 65535U ||
            !parse_u32(no_asdu_text, no_asdu) || no_asdu != 1U ||
            !parse_u32(flags_text, flags) || flags > 3U || !no_extra(&save)) {
            ESP_LOGE(kTag, "Invalid PROFILE SV payload");
            return;
        }
        g_staging.configuration_revision = confrev;
        g_staging.publisher_rate_hz = rate;
        g_staging.sample_counter_modulus = static_cast<std::uint16_t>(modulus);
        g_staging.no_asdu = static_cast<std::uint16_t>(no_asdu);
        g_staging.include_data_set = (flags & 0x1U) != 0U;
        g_staging.include_sample_rate = (flags & 0x2U) != 0U;
        return;
    }

    if (std::strcmp(subcommand, "COMMIT") == 0) {
        if (!no_extra(&save)) {
            ESP_LOGE(kTag, "Usage: PROFILE COMMIT");
            return;
        }
        if (!runtime_profile_commit(g_staging)) {
            ESP_LOGE(kTag, "PROFILE commit rejected by runtime validation");
            return;
        }
        g_staging_active = false;
        const auto active = runtime_profile_snapshot();
        ESP_LOGI(kTag,
                 "PROFILE committed generation=%llu svID=%s APPID=0x%04X rate=%lu wrap=%u",
                 static_cast<unsigned long long>(active.generation),
                 active.sv_id.data(),
                 static_cast<unsigned>(active.app_id),
                 static_cast<unsigned long>(active.publisher_rate_hz),
                 static_cast<unsigned>(active.sample_counter_modulus));
        return;
    }

    ESP_LOGE(kTag, "Unknown PROFILE subcommand");
}

} // namespace ar::esp32p4::smv
