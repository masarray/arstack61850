// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/sampled_values/publisher_profile.hpp"

#include <cctype>
#include <string>
#include <string_view>

namespace ar::iec61850::sampled_values {

enum class Esp32P4SvProfileSupport {
    ready,
    needs_counter_confirmation,
    unsupported_layout,
};

[[nodiscard]] inline std::string_view esp32p4_sv_profile_support_name(
    const Esp32P4SvProfileSupport support) noexcept {
    switch (support) {
    case Esp32P4SvProfileSupport::ready:
        return "ready";
    case Esp32P4SvProfileSupport::needs_counter_confirmation:
        return "needs-counter-confirmation";
    case Esp32P4SvProfileSupport::unsupported_layout:
        return "unsupported-layout";
    }
    return "unsupported-layout";
}

namespace detail {
[[nodiscard]] inline std::string sv_support_lower_copy(const std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char ch : text) {
        result.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))));
    }
    return result;
}

[[nodiscard]] inline bool esp32p4_4i4v_layout_matches(
    const SvPublisherProfile& profile) {
    if (profile.no_asdu != 1U || profile.payload_size_bytes != 64U ||
        profile.channels.size() != 16U || !profile.publisher_rate_hz.has_value() ||
        *profile.publisher_rate_hz == 0U || *profile.publisher_rate_hz > 65535U) {
        return false;
    }
    if (profile.asdu_options.refresh_time || profile.asdu_options.security ||
        profile.asdu_options.synch_source_id) {
        return false;
    }
    for (std::size_t i = 0U; i < profile.channels.size(); ++i) {
        const auto& channel = profile.channels[i];
        if (channel.wire_width_bytes != 4U) return false;
        if ((i % 2U) == 0U) {
            if (channel.is_quality || sv_support_lower_copy(channel.basic_type) != "int32") {
                return false;
            }
        } else if (!channel.is_quality) {
            return false;
        }
    }
    return true;
}
} // namespace detail

// Centralized deployment boundary for the currently proven ESP32-P4 runtime.
// Parsing may accept broader IEC 61850 SV structures; this classifier is only
// the embedded-device deployment gate and deliberately fails closed.
[[nodiscard]] inline Esp32P4SvProfileSupport classify_esp32p4_sv_profile(
    const SvPublisherProfile& profile) {
    if (!detail::esp32p4_4i4v_layout_matches(profile)) {
        return Esp32P4SvProfileSupport::unsupported_layout;
    }
    if (profile.sample_counter_policy != SvSampleCounterPolicy::explicit_modulus ||
        !profile.sample_counter_modulus.has_value() ||
        *profile.sample_counter_modulus == 0U) {
        return Esp32P4SvProfileSupport::needs_counter_confirmation;
    }
    return Esp32P4SvProfileSupport::ready;
}

} // namespace ar::iec61850::sampled_values
