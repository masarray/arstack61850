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

    // No-throw decode primitive for constrained/embedded profiles.
    [[nodiscard]] static bool try_from_bytes(
        std::span<const std::uint8_t> bytes,
        Iec61850UtcTime& result) noexcept;

#if !defined(ARIEC61850_NO_EXCEPTIONS)
    // Host convenience wrapper retained when C++ exceptions are available.
    [[nodiscard]] static Iec61850UtcTime from_bytes(std::span<const std::uint8_t> bytes);
#endif

    // Allocation-free wire path for embedded publishers. Returns false when
    // the timestamp cannot be represented by IEC 61850's 32-bit seconds field.
    [[nodiscard]] bool try_write_bytes(std::span<std::uint8_t, 8> destination) const noexcept;

#if !defined(ARIEC61850_NO_EXCEPTIONS)
    // Host convenience wrapper retained for source compatibility.
    [[nodiscard]] std::vector<std::uint8_t> to_bytes() const;
#endif

    friend bool operator==(const Iec61850UtcTime&, const Iec61850UtcTime&) = default;
};

} // namespace ar::iec61850::mms
