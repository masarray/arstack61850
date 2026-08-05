// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <vector>

namespace ar::iec61850::mms {

struct Iec61850UtcTime final {
    std::chrono::system_clock::time_point value{};
    std::uint8_t quality{};

    [[nodiscard]] static Iec61850UtcTime from_bytes(std::span<const std::uint8_t> bytes);
    [[nodiscard]] std::vector<std::uint8_t> to_bytes() const;

    friend bool operator==(const Iec61850UtcTime&, const Iec61850UtcTime&) = default;
};

} // namespace ar::iec61850::mms
