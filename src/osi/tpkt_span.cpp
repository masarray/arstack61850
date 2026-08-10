// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/osi/tpkt_span.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::osi {
namespace {

[[nodiscard]] std::uint16_t read_be_u16(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8U) |
        static_cast<std::uint16_t>(bytes[offset + 1U]));
}

void write_be_u16(
    const std::span<std::uint8_t> bytes,
    const std::size_t offset,
    const std::uint16_t value) noexcept {
    bytes[offset] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value & 0xFFU);
}

} // namespace

wire::EncodeResult TpktSpanCodec::encode_into(
    const std::span<const std::uint8_t> payload,
    const std::span<std::uint8_t> destination) noexcept {
    if (payload.size() > maximum_payload_bytes) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    const auto required = payload.size() + header_length;
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }

    destination[0] = supported_version;
    destination[1] = 0x00U;
    write_be_u16(destination, 2U, static_cast<std::uint16_t>(required));
    std::copy(
        payload.begin(),
        payload.end(),
        destination.begin() + static_cast<std::ptrdiff_t>(header_length));
    return {wire::EncodeStatus::ok, required, required};
}

TpktPeekResult TpktSpanCodec::peek_frame(
    const std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() < header_length) {
        return {TpktPeekStatus::need_more, 0U};
    }
    if (bytes[0] != supported_version || bytes[1] != 0x00U) {
        return {TpktPeekStatus::invalid, 0U};
    }

    const auto declared = static_cast<std::size_t>(read_be_u16(bytes, 2U));
    if (declared < header_length) {
        return {TpktPeekStatus::invalid, 0U};
    }
    if (bytes.size() < declared) {
        return {TpktPeekStatus::need_more, declared};
    }
    return {TpktPeekStatus::ready, declared};
}

bool TpktSpanCodec::try_decode_view(
    const std::span<const std::uint8_t> bytes,
    TpktFrameView& frame) noexcept {
    frame = {};
    const auto peek = peek_frame(bytes);
    if (!peek.ready() || peek.frame_bytes != bytes.size()) {
        return false;
    }

    frame.version = bytes[0];
    frame.declared_length = static_cast<std::uint16_t>(peek.frame_bytes);
    frame.payload = bytes.subspan(header_length);
    return true;
}

} // namespace ar::iec61850::osi
