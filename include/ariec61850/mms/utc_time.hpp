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

    // Allocation-free wire path for embedded publishers. Returns false when
    // the timestamp cannot be represented by IEC 61850's 32-bit seconds field.
    [[nodiscard]] bool try_write_bytes(std::span<std::uint8_t, 8> destination) const noexcept;

    // Host convenience wrapper retained for source compatibility.
    [[nodiscard]] std::vector<std::uint8_t> to_bytes() const;

    friend bool operator==(const Iec61850UtcTime&, const Iec61850UtcTime&) = default;
};

} // namespace ar::iec61850::mms
