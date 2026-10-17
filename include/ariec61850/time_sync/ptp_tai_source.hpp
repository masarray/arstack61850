// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <limits>
#include <optional>

namespace ar::iec61850::time_sync {

// A device free-running clock is not a traceable reference. This conversion
// only expresses an already-plausible POSIX/UTC epoch in PTP/TAI seconds;
// it never by itself proves correct UTC, accuracy or leap-second knowledge.
// ESP32-P4 EMAC currently exposes a uint32 seconds register.
[[nodiscard]] constexpr std::optional<std::uint32_t>
try_seed_ptp_tai_seconds(const std::int64_t utc_seconds,
                         const std::int16_t configured_tai_utc_offset) noexcept {
    // Reject unset 1970-era RTC and clearly implausible offset settings.
    // A valid date is only a plausibility check, not a trusted time source.
    constexpr std::int64_t earliest_supported_utc = 1577836800LL; // 2020-01-01
    if (utc_seconds < earliest_supported_utc ||
        configured_tai_utc_offset < 0 || configured_tai_utc_offset > 200 ||
        utc_seconds > static_cast<std::int64_t>(
            std::numeric_limits<std::uint32_t>::max()) -
                configured_tai_utc_offset) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(
        utc_seconds + configured_tai_utc_offset);
}

} // namespace ar::iec61850::time_sync
