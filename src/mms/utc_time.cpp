// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/utc_time.hpp"

#include "ariec61850/asn1/ber.hpp"

#include <chrono>
#include <cstdint>
#include <stdexcept>

namespace ar::iec61850::mms {

Iec61850UtcTime Iec61850UtcTime::from_bytes(const std::span<const std::uint8_t> bytes) {
    if (bytes.size() != 8U) {
        throw std::invalid_argument("IEC 61850 UTC time requires exactly 8 bytes.");
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
    return {timestamp, bytes[7]};
}

std::vector<std::uint8_t> Iec61850UtcTime::to_bytes() const {
    return asn1::BerWriter::encode_utc_time(value, quality);
}

} // namespace ar::iec61850::mms
