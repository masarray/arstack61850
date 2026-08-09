// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/goose/pdu.hpp"
#include "ariec61850/wire/encode_result.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ar::iec61850::goose {

class GoosePduCodec final {
public:
    // Allocation-free transmit path for embedded/steady-state publishers.
    [[nodiscard]] static std::optional<std::size_t> encoded_size(
        const GoosePdu& pdu) noexcept;
    [[nodiscard]] static wire::EncodeResult encode_into(
        const GoosePdu& pdu,
        std::span<std::uint8_t> destination) noexcept;

#if !defined(ARIEC61850_NO_EXCEPTIONS)
    // Host convenience/decode surface retained for compatibility.
    [[nodiscard]] static std::vector<std::uint8_t> encode(const GoosePdu& pdu);
    [[nodiscard]] static bool try_decode(
        std::span<const std::uint8_t> apdu, GoosePdu& pdu) noexcept;
#endif
};

} // namespace ar::iec61850::goose
