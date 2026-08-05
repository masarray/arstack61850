// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/goose/pdu.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ar::iec61850::goose {

class GoosePduCodec final {
public:
    [[nodiscard]] static std::vector<std::uint8_t> encode(const GoosePdu& pdu);
    [[nodiscard]] static bool try_decode(
        std::span<const std::uint8_t> apdu, GoosePdu& pdu) noexcept;
};

} // namespace ar::iec61850::goose
