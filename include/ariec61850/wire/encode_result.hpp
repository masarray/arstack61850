// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace ar::iec61850::wire {

enum class EncodeStatus : std::uint8_t {
    ok,
    buffer_too_small,
    value_out_of_range,
};

struct EncodeResult final {
    EncodeStatus status{EncodeStatus::ok};
    std::size_t bytes_written{};
    std::size_t required_bytes{};

    [[nodiscard]] constexpr bool success() const noexcept {
        return status == EncodeStatus::ok;
    }
};

} // namespace ar::iec61850::wire
