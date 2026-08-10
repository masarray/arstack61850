// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/utc_time.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#if !defined(ARIEC61850_NO_EXCEPTIONS)
#include <stdexcept>
#endif

namespace ar::iec61850::mms {

bool Iec61850UtcTime::try_from_bytes(
    const std::span<const std::uint8_t> bytes,
    Iec61850UtcTime& result) noexcept {
    if (bytes.size() != 8U) {
        result = {};
        return false;
    }

    const auto seconds =
        (static_cast<std::uint32_t>(bytes[0]) << 24U) |
        (static_cast<std::uint32_t>(bytes[1]) << 16U) |
        (static_cast<std::uint32_t>(bytes[2]) << 8U) |
        static_cast<std::uint32_t>(bytes[3]);
    const auto fraction =
        (static_cast<std::uint32_t>(bytes[4]) << 16U) |
        (static_cast<std::uint32_t>(bytes[5]) << 8U) |
        static_cast<std::uint32_t>(bytes[6]);

    using fraction_duration = std::chrono::duration<std::uint32_t, std::ratio<1, 16'777'216>>;
    const auto timestamp = std::chrono::system_clock::time_point{std::chrono::seconds{seconds}} +
                           std::chrono::duration_cast<std::chrono::system_clock::duration>(
                               fraction_duration{fraction});
    result = {timestamp, bytes[7]};
    return true;
}

#if !defined(ARIEC61850_NO_EXCEPTIONS)
Iec61850UtcTime Iec61850UtcTime::from_bytes(const std::span<const std::uint8_t> bytes) {
    Iec61850UtcTime result{};
    if (!try_from_bytes(bytes, result)) {
        throw std::invalid_argument("IEC 61850 UTC time requires exactly 8 bytes.");
    }
    return result;
}
#endif

bool Iec61850UtcTime::try_write_bytes(
    const std::span<std::uint8_t, 8> destination) const noexcept {
    using seconds_type = std::chrono::seconds;
    const auto whole_seconds = std::chrono::floor<seconds_type>(value);
    auto seconds = whole_seconds.time_since_epoch().count();
    const auto fractional = value - whole_seconds;
    auto fraction = static_cast<std::uint32_t>(std::llround(
        std::chrono::duration<double>(fractional).count() * 16'777'216.0));

    if (fraction >= 16'777'216U) {
        ++seconds;
        fraction = 0U;
    }
    if (seconds < 0 ||
        static_cast<std::uint64_t>(seconds) >
            std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    const auto raw_seconds = static_cast<std::uint32_t>(seconds);
    destination[0] = static_cast<std::uint8_t>((raw_seconds >> 24U) & 0xFFU);
    destination[1] = static_cast<std::uint8_t>((raw_seconds >> 16U) & 0xFFU);
    destination[2] = static_cast<std::uint8_t>((raw_seconds >> 8U) & 0xFFU);
    destination[3] = static_cast<std::uint8_t>(raw_seconds & 0xFFU);
    destination[4] = static_cast<std::uint8_t>((fraction >> 16U) & 0xFFU);
    destination[5] = static_cast<std::uint8_t>((fraction >> 8U) & 0xFFU);
    destination[6] = static_cast<std::uint8_t>(fraction & 0xFFU);
    destination[7] = quality;
    return true;
}

#if !defined(ARIEC61850_NO_EXCEPTIONS)
std::vector<std::uint8_t> Iec61850UtcTime::to_bytes() const {
    std::array<std::uint8_t, 8> bytes{};
    if (!try_write_bytes(bytes)) {
        throw std::out_of_range("UTC time seconds are outside the IEC 61850 32-bit range.");
    }
    return {bytes.begin(), bytes.end()};
}
#endif

} // namespace ar::iec61850::mms
