// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/sampled_values/frame.hpp"
#include "ariec61850/wire/encode_result.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#if !defined(ARIEC61850_NO_EXCEPTIONS)
#include <vector>
#endif

namespace ar::iec61850::sampled_values {

class SampledValuesFrameCodec final {
public:
    [[nodiscard]] static std::optional<std::size_t> encoded_size(
        const SampledValuesFrame& frame) noexcept;

    [[nodiscard]] static wire::EncodeResult encode_into(
        const SampledValuesFrame& frame,
        std::span<std::uint8_t> destination) noexcept;

#if !defined(ARIEC61850_NO_EXCEPTIONS)
    // Host/soft-profile convenience wrapper. Hard embedded publishers should
    // use encode_into with persistent caller-owned Ethernet storage.
    [[nodiscard]] static std::vector<std::uint8_t> encode(const SampledValuesFrame& frame);

    // Dynamic receive-side decode remains outside the first hard profile.
    [[nodiscard]] static bool try_decode(
        std::span<const std::uint8_t> bytes, SampledValuesFrame& frame) noexcept;
#endif
};

} // namespace ar::iec61850::sampled_values
