// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/wire/encode_result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::osi {

enum class TpktPeekStatus : std::uint8_t {
    need_more,
    ready,
    invalid,
};

struct TpktPeekResult final {
    TpktPeekStatus status{TpktPeekStatus::need_more};
    std::size_t frame_bytes{};

    [[nodiscard]] constexpr bool ready() const noexcept {
        return status == TpktPeekStatus::ready;
    }
};

struct TpktFrameView final {
    std::uint8_t version{};
    std::uint16_t declared_length{};
    std::span<const std::uint8_t> payload{};
};

class TpktSpanCodec final {
public:
    static constexpr std::uint8_t supported_version = 0x03U;
    static constexpr std::size_t header_length = 4U;
    static constexpr std::size_t maximum_frame_bytes = 65'535U;
    static constexpr std::size_t maximum_payload_bytes =
        maximum_frame_bytes - header_length;

    [[nodiscard]] static wire::EncodeResult encode_into(
        std::span<const std::uint8_t> payload,
        std::span<std::uint8_t> destination) noexcept;

    // Inspect the first frame in a TCP receive window without copying it.
    // `ready` may be returned even when bytes contains coalesced frames;
    // frame_bytes identifies the exact prefix belonging to the first TPKT.
    [[nodiscard]] static TpktPeekResult peek_frame(
        std::span<const std::uint8_t> bytes) noexcept;

    // Decode one exact TPKT frame into non-owning spans. The supplied storage
    // must outlive the returned view.
    [[nodiscard]] static bool try_decode_view(
        std::span<const std::uint8_t> bytes,
        TpktFrameView& frame) noexcept;
};

} // namespace ar::iec61850::osi
