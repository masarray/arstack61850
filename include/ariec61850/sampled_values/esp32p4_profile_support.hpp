// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/sampled_values/publisher_profile.hpp"

#include <string_view>

namespace ar::iec61850::sampled_values {

enum class Esp32P4SvProfileSupport {
    ready,
    needs_profile_family_confirmation,
    needs_counter_confirmation,
    unsupported_profile_family,
    unsupported_layout,
};

[[nodiscard]] inline std::string_view esp32p4_sv_profile_support_name(
    const Esp32P4SvProfileSupport support) noexcept {
    switch (support) {
    case Esp32P4SvProfileSupport::ready:
        return "ready";
    case Esp32P4SvProfileSupport::needs_profile_family_confirmation:
        return "needs-profile-family-confirmation";
    case Esp32P4SvProfileSupport::needs_counter_confirmation:
        return "needs-counter-confirmation";
    case Esp32P4SvProfileSupport::unsupported_profile_family:
        return "unsupported-profile-family";
    case Esp32P4SvProfileSupport::unsupported_layout:
        return "unsupported-layout";
    }
    return "unsupported-layout";
}

namespace detail {
[[nodiscard]] inline bool esp32p4_4i4v_layout_matches(
    const SvPublisherProfile& profile) {
    const auto frame_rate_hz = profile.timing.exact_frame_rate_hz();
    if (profile.timing.sampling_basis != SvSampleMode::samples_per_second ||
        profile.timing.asdus_per_frame != 1U ||
        !legacy_9_2le_4i4v_layout_matches(profile) ||
        !frame_rate_hz.has_value() || *frame_rate_hz == 0U ||
        *frame_rate_hz > 65535U) {
        return false;
    }
    if (profile.asdu_options.refresh_time || profile.asdu_options.security ||
        profile.asdu_options.synch_source_id) {
        return false;
    }
    return true;
}
} // namespace detail

// Centralized deployment boundary for the currently proven ESP32-P4 runtime.
// Parsing may accept broader IEC 61850 SV structures; this classifier is only
// the embedded-device deployment gate and deliberately fails closed.
[[nodiscard]] inline Esp32P4SvProfileSupport classify_esp32p4_sv_profile(
    const SvPublisherProfile& profile) {
    if (profile.profile_family == SvProfileFamily::unspecified ||
        profile.profile_family_resolution == SvProfileFamilyResolution::unresolved) {
        return Esp32P4SvProfileSupport::needs_profile_family_confirmation;
    }
    if (profile.profile_family == SvProfileFamily::iec61869_9 ||
        profile.profile_family_resolution != SvProfileFamilyResolution::resolved) {
        return Esp32P4SvProfileSupport::unsupported_profile_family;
    }
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
