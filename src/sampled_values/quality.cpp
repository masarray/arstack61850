// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/sampled_values/quality.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace ar::iec61850::sampled_values {

std::uint32_t SampledValueQuality::to_uint32() const noexcept {
    auto value = static_cast<std::uint32_t>(validity);
    if (overflow) value |= 1U << 2U;
    if (out_of_range) value |= 1U << 3U;
    if (bad_reference) value |= 1U << 4U;
    if (oscillatory) value |= 1U << 5U;
    if (failure) value |= 1U << 6U;
    if (old_data) value |= 1U << 7U;
    if (inconsistent) value |= 1U << 8U;
    if (inaccurate) value |= 1U << 9U;
    if (test) value |= 1U << 11U;
    if (operator_blocked) value |= 1U << 12U;
    return value;
}

std::vector<std::uint8_t> SampledValueQuality::to_bytes(const std::size_t width) const {
    if (width == 0U) {
        return {};
    }

    const auto value = to_uint32();
    const std::array<std::uint8_t, 4> encoded{
        static_cast<std::uint8_t>((value >> 24U) & 0xFFU),
        static_cast<std::uint8_t>((value >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(value & 0xFFU)};

    std::vector<std::uint8_t> result(width, 0U);
    const auto count = std::min(width, encoded.size());
    std::copy_n(encoded.begin(), static_cast<std::ptrdiff_t>(count), result.begin());
    return result;
}

SampledValueQuality SampledValueQuality::from_uint32(const std::uint32_t encoded) noexcept {
    SampledValueQuality quality;
    quality.validity = static_cast<SampledValueValidity>(encoded & 0x03U);
    quality.overflow = (encoded & (1U << 2U)) != 0U;
    quality.out_of_range = (encoded & (1U << 3U)) != 0U;
    quality.bad_reference = (encoded & (1U << 4U)) != 0U;
    quality.oscillatory = (encoded & (1U << 5U)) != 0U;
    quality.failure = (encoded & (1U << 6U)) != 0U;
    quality.old_data = (encoded & (1U << 7U)) != 0U;
    quality.inconsistent = (encoded & (1U << 8U)) != 0U;
    quality.inaccurate = (encoded & (1U << 9U)) != 0U;
    quality.test = (encoded & (1U << 11U)) != 0U;
    quality.operator_blocked = (encoded & (1U << 12U)) != 0U;
    return quality;
}

SampledValueQuality SampledValueQuality::from_bytes(
    const std::span<const std::uint8_t> encoded) {
    std::uint32_t value = 0U;
    const auto count = std::min<std::size_t>(encoded.size(), 4U);
    for (std::size_t index = 0U; index < count; ++index) {
        value |= static_cast<std::uint32_t>(encoded[index]) <<
                 static_cast<unsigned>(24U - (index * 8U));
    }
    return from_uint32(value);
}

} // namespace ar::iec61850::sampled_values
