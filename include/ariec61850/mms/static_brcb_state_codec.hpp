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

// Storage-agnostic bounded state image for the already-buffered BRCB queue.
// Pending BufTm coalescing is intentionally transient and is not restored.
class MmsStaticBrcbStateCodec final {
public:
    static constexpr std::uint16_t format_version = 1U;

    [[nodiscard]] static MmsStaticBrcbStateResult encode(
        const MmsStaticBrcbRuntime& runtime,
        std::span<std::uint8_t> destination) noexcept;

    [[nodiscard]] static MmsStaticBrcbStateResult restore(
        MmsStaticBrcbRuntime& runtime,
        std::span<const std::uint8_t> source) noexcept;
};

} // namespace ar::iec61850::mms
