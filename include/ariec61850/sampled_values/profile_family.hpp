// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace ar::iec61850::sampled_values {

enum class SvProfileFamily : std::uint8_t {
    unspecified,
    iec61850_9_2,
    legacy_9_2le,
    iec61869_9,
};

enum class SvProfileFamilyResolution : std::uint8_t {
    unresolved,
    resolved,
    incomplete,
};

enum class SvTransportMode : std::uint8_t {
    multicast,
    unicast,
};

inline constexpr std::uint16_t sv_app_id_minimum = 0x4000U;
inline constexpr std::uint16_t sv_app_id_maximum = 0x7FFFU;

[[nodiscard]] constexpr bool is_valid_sv_app_id(const std::uint16_t app_id) noexcept {
    return app_id >= sv_app_id_minimum && app_id <= sv_app_id_maximum;
}

[[nodiscard]] constexpr bool is_multicast_mac(
    const std::array<std::uint8_t, 6>& mac) noexcept {
    return (mac[0] & 0x01U) != 0U;
}

[[nodiscard]] constexpr bool is_zero_mac(
    const std::array<std::uint8_t, 6>& mac) noexcept {
    return mac[0] == 0U && mac[1] == 0U && mac[2] == 0U &&
           mac[3] == 0U && mac[4] == 0U && mac[5] == 0U;
}

// IEC 61850 Sampled Values multicast allocation:
// 01-0C-CD-04-00-00 .. 01-0C-CD-04-01-FF.
[[nodiscard]] constexpr bool is_iec_sv_multicast_mac(
    const std::array<std::uint8_t, 6>& mac) noexcept {
    return mac[0] == 0x01U && mac[1] == 0x0CU &&
           mac[2] == 0xCDU && mac[3] == 0x04U &&
           mac[4] <= 0x01U;
}

[[nodiscard]] constexpr bool is_valid_sv_unicast_mac(
    const std::array<std::uint8_t, 6>& mac) noexcept {
    return !is_zero_mac(mac) && !is_multicast_mac(mac);
}

[[nodiscard]] constexpr std::string_view sv_profile_family_name(
    const SvProfileFamily family) noexcept {
    switch (family) {
    case SvProfileFamily::iec61850_9_2: return "iec61850-9-2";
    case SvProfileFamily::legacy_9_2le: return "9-2le";
    case SvProfileFamily::iec61869_9: return "iec61869-9";
    case SvProfileFamily::unspecified: break;
    }
    return "unspecified";
}

[[nodiscard]] constexpr std::string_view sv_profile_family_resolution_name(
    const SvProfileFamilyResolution resolution) noexcept {
    switch (resolution) {
    case SvProfileFamilyResolution::resolved: return "resolved";
    case SvProfileFamilyResolution::incomplete: return "incomplete";
    case SvProfileFamilyResolution::unresolved: break;
    }
    return "unresolved";
}

[[nodiscard]] constexpr std::string_view sv_transport_mode_name(
    const SvTransportMode mode) noexcept {
    return mode == SvTransportMode::multicast ? "multicast" : "unicast";
}

} // namespace ar::iec61850::sampled_values
