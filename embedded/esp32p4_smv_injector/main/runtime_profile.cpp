// SPDX-License-Identifier: GPL-3.0-or-later

#include "runtime_profile.hpp"

#include "freertos/FreeRTOS.h"

#include <algorithm>
#include <cstring>

namespace ar::esp32p4::smv {
namespace {

portMUX_TYPE g_profile_mux = portMUX_INITIALIZER_UNLOCKED;
RuntimePublisherProfile g_profile{};

void copy_text(std::array<char, 96>& destination, const char* source) noexcept {
    destination.fill('\0');
    if (source == nullptr) return;
    std::strncpy(destination.data(), source, destination.size() - 1U);
}

} // namespace

void runtime_profile_initialize() noexcept {
    RuntimePublisherProfile initial;
    copy_text(initial.sv_id, "MU01_SV1");
    initial.data_set_reference.fill('\0');
    initial.generation = 1U;
    portENTER_CRITICAL(&g_profile_mux);
    g_profile = initial;
    portEXIT_CRITICAL(&g_profile_mux);
}

RuntimePublisherProfile runtime_profile_snapshot() noexcept {
    RuntimePublisherProfile snapshot;
    portENTER_CRITICAL(&g_profile_mux);
    snapshot = g_profile;
    portEXIT_CRITICAL(&g_profile_mux);
    return snapshot;
}

bool runtime_profile_validate(const RuntimePublisherProfile& profile) noexcept {
    if (profile.schema_version != 1U || profile.app_id == 0U ||
        profile.publisher_rate_hz == 0U || profile.publisher_rate_hz > 65535U ||
        profile.sample_counter_modulus == 0U || profile.no_asdu != 1U ||
        profile.sv_id[0] == '\0') {
        return false;
    }
    if (profile.vlan_present && (profile.vlan_id > 4095U || profile.vlan_priority > 7U)) {
        return false;
    }
    if (profile.include_data_set && profile.data_set_reference[0] == '\0') {
        return false;
    }
    return true;
}

bool runtime_profile_commit(const RuntimePublisherProfile& requested) noexcept {
    if (!runtime_profile_validate(requested)) return false;
    portENTER_CRITICAL(&g_profile_mux);
    auto next = requested;
    next.generation = g_profile.generation + 1U;
    g_profile = next;
    portEXIT_CRITICAL(&g_profile_mux);
    return true;
}

} // namespace ar::esp32p4::smv
