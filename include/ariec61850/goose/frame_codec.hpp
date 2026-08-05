// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/goose/frame.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ar::iec61850::goose {

class GooseFrameCodec final {
public:
    [[nodiscard]] static std::vector<std::uint8_t> encode(const GooseFrame& frame);
    [[nodiscard]] static bool try_decode(
        std::span<const std::uint8_t> frame_bytes, GooseFrame& frame) noexcept;
};

} // namespace ar::iec61850::goose
