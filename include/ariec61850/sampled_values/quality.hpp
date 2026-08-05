// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ar::iec61850::sampled_values {

enum class SampledValueValidity : std::uint32_t {
    good = 0U,
    invalid = 1U,
    reserved = 2U,
    questionable = 3U
};

struct SampledValueQuality final {
    SampledValueValidity validity{SampledValueValidity::good};
    bool overflow{};
    bool out_of_range{};
    bool bad_reference{};
    bool oscillatory{};
    bool failure{};
    bool old_data{};
    bool inconsistent{};
    bool inaccurate{};
    bool test{};
    bool operator_blocked{};

    [[nodiscard]] std::uint32_t to_uint32() const noexcept;
    [[nodiscard]] std::vector<std::uint8_t> to_bytes(std::size_t width = 4U) const;

    [[nodiscard]] static SampledValueQuality from_uint32(std::uint32_t encoded) noexcept;
    [[nodiscard]] static SampledValueQuality from_bytes(
        std::span<const std::uint8_t> encoded);

    friend bool operator==(const SampledValueQuality&, const SampledValueQuality&) = default;
};

} // namespace ar::iec61850::sampled_values
