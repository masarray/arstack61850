// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/sampled_values/frame.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ar::iec61850::sampled_values {

class SampledValuesFrameCodec final {
public:
    [[nodiscard]] static std::vector<std::uint8_t> encode(const SampledValuesFrame& frame);
    [[nodiscard]] static bool try_decode(
        std::span<const std::uint8_t> bytes, SampledValuesFrame& frame) noexcept;
};

} // namespace ar::iec61850::sampled_values
