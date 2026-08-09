// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/goose/frame.hpp"
#include "ariec61850/wire/encode_result.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ar::iec61850::goose {

class GooseFrameCodec final {
public:
    [[nodiscard]] static std::optional<std::size_t> encoded_size(
        const GooseFrame& frame) noexcept;
    [[nodiscard]] static wire::EncodeResult encode_into(
        const GooseFrame& frame,
        std::span<std::uint8_t> destination) noexcept;

#if !defined(ARIEC61850_NO_EXCEPTIONS)
    [[nodiscard]] static std::vector<std::uint8_t> encode(const GooseFrame& frame);
    [[nodiscard]] static bool try_decode(
        std::span<const std::uint8_t> frame_bytes, GooseFrame& frame) noexcept;
#endif
};

} // namespace ar::iec61850::goose
