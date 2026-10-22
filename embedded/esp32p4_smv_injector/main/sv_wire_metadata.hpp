// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "runtime_profile.hpp"

#include <cstdint>
#include <optional>

namespace ar::esp32p4::smv {

// The physical sample cadence is always publisher_rate_hz; smpRate has units
// determined by smpMod. Do not confuse 80 samples/cycle with 4000 samples/s.
// No optional fields are emitted in legacy/disabled mode.
struct SvWireSampleRate final {
    std::uint16_t sample_rate{};
    std::uint16_t sample_mode{};
};

[[nodiscard]] inline std::optional<SvWireSampleRate> sv_wire_sample_rate(
    const RuntimePublisherProfile& profile) noexcept {
    if (!profile.include_sample_rate) return std::nullopt;
    const auto value = profile.wire_sample_rate != 0U
        ? static_cast<std::uint32_t>(profile.wire_sample_rate)
        : profile.publisher_rate_hz;
    if (value == 0U || value > 65535U) return std::nullopt;
    if (profile.wire_sample_mode == 1U) {
        if (value != profile.publisher_rate_hz) return std::nullopt;
    } else if (profile.wire_sample_mode == 0U) {
        // Current ESP32-P4 4I+4V single-ASDU capabilities: nominal 50/60 Hz.
        // Prevent a contradictory smpMod=0,smpRate=4000 for 4 kfps.
        const auto fps = static_cast<std::uint64_t>(profile.publisher_rate_hz);
        const auto per_period = static_cast<std::uint64_t>(value);
        if (fps != per_period * 50U && fps != per_period * 60U) {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }
    return SvWireSampleRate{static_cast<std::uint16_t>(value),
                            profile.wire_sample_mode};
}

} // namespace ar::esp32p4::smv
