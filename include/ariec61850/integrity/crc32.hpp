// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

namespace ar::iec61850::integrity {

inline constexpr std::uint32_t crc32_initial_state = 0xFFFF'FFFFU;

[[nodiscard]] inline std::uint32_t crc32_update(
    std::uint32_t state,
    const std::span<const std::uint8_t> bytes) noexcept {
    for (const auto byte : bytes) {
        state ^= byte;
        for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
            const auto mask = static_cast<std::uint32_t>(
                0U - static_cast<std::uint32_t>(state & 1U));
            state = (state >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return state;
}

[[nodiscard]] constexpr std::uint32_t crc32_finalize(
    const std::uint32_t state) noexcept {
    return ~state;
}

[[nodiscard]] inline std::uint32_t crc32(
    const std::span<const std::uint8_t> bytes) noexcept {
    return crc32_finalize(crc32_update(crc32_initial_state, bytes));
}

} // namespace ar::iec61850::integrity
