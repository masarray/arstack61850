// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/sampled_values/frame.hpp"
#include "ariec61850/wire/encode_result.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ar::iec61850::sampled_values {

class SampledValuesFrameCodec final {
public:
    [[nodiscard]] static std::optional<std::size_t> encoded_size(
        const SampledValuesFrame& frame) noexcept;

    [[nodiscard]] static wire::EncodeResult encode_into(
        const SampledValuesFrame& frame,
        std::span<std::uint8_t> destination) noexcept;

    // Host convenience wrapper. Embedded SV publishers should use encode_into
    // with a persistent Ethernet-sized buffer.
    [[nodiscard]] static std::vector<std::uint8_t> encode(const SampledValuesFrame& frame);

    [[nodiscard]] static bool try_decode(
        std::span<const std::uint8_t> bytes, SampledValuesFrame& frame) noexcept;
};

} // namespace ar::iec61850::sampled_values
