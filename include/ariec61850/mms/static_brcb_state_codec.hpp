// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::mms {

class MmsStaticBrcbRuntime;

enum class MmsStaticBrcbStateStatus : std::uint8_t {
    ok,
    invalid_runtime,
    buffer_too_small,
    invalid_state,
    definition_mismatch,
    capacity_mismatch,
};

struct MmsStaticBrcbStateResult final {
    MmsStaticBrcbStateStatus status{MmsStaticBrcbStateStatus::invalid_runtime};
    std::size_t bytes_written{};
    std::size_t required_bytes{};

    [[nodiscard]] constexpr bool success() const noexcept {
        return status == MmsStaticBrcbStateStatus::ok;
    }
};

// Storage-agnostic bounded BRCB recovery image.
//
// v1 persisted only the still-undelivered suffix. v2 persists the entire
// retained replay window plus the independent delivery cursor and replay-gap
// flag. Pending BufTm coalescing, live association ownership and RptEna remain
// deliberately transient and are never restored across reboot.
class MmsStaticBrcbStateCodec final {
public:
    static constexpr std::uint16_t legacy_format_version = 1U;
    static constexpr std::uint16_t format_version = 2U;

    [[nodiscard]] static MmsStaticBrcbStateResult encode(
        const MmsStaticBrcbRuntime& runtime,
        std::span<std::uint8_t> destination) noexcept;

    // Accepts both v2 and legacy v1 images. Legacy v1 restores as an
    // undelivered-only queue with delivery cursor at the oldest entry.
    [[nodiscard]] static MmsStaticBrcbStateResult restore(
        MmsStaticBrcbRuntime& runtime,
        std::span<const std::uint8_t> source) noexcept;
};

} // namespace ar::iec61850::mms
