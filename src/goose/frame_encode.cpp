// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/goose/frame_codec.hpp"

#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/goose/pdu_codec.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace ar::iec61850::goose {
namespace {

void write_u16_be(
    const std::span<std::uint8_t> destination,
    const std::size_t offset,
    const std::uint16_t value) noexcept {
    destination[offset] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    destination[offset + 1U] = static_cast<std::uint8_t>(value & 0xFFU);
}

} // namespace

std::optional<std::size_t> GooseFrameCodec::encoded_size(
    const GooseFrame& frame) noexcept {
    std::uint16_t tci{};
    if (frame.vlan.has_value() &&
        !frame.vlan->try_to_tag_control_information(tci)) {
        return std::nullopt;
    }

    const auto apdu_size = GoosePduCodec::encoded_size(frame.pdu);
    if (!apdu_size ||
        *apdu_size >
            static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) -
                ethernet::ProcessBusFrameCodec::header_length) {
        return std::nullopt;
    }

    const std::size_t ethernet_header = frame.vlan.has_value() ? 18U : 14U;
    const std::size_t process_bus_size =
        ethernet::ProcessBusFrameCodec::header_length + *apdu_size;
    if (process_bus_size > std::numeric_limits<std::size_t>::max() - ethernet_header) {
        return std::nullopt;
    }
    return ethernet_header + process_bus_size;
}

wire::EncodeResult GooseFrameCodec::encode_into(
    const GooseFrame& frame,
    const std::span<std::uint8_t> destination) noexcept {
    const auto required = encoded_size(frame);
    if (!required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (destination.size() < *required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, *required};
    }

    const auto apdu_size = GoosePduCodec::encoded_size(frame.pdu);
    if (!apdu_size) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    std::copy(
        frame.destination.bytes().begin(),
        frame.destination.bytes().end(),
        destination.begin());
    std::copy(
        frame.source.bytes().begin(),
        frame.source.bytes().end(),
        destination.begin() + 6);

    std::size_t offset = 12U;
    if (frame.vlan.has_value()) {
        std::uint16_t tci{};
        if (!frame.vlan->try_to_tag_control_information(tci)) {
            return {wire::EncodeStatus::value_out_of_range, 0U, *required};
        }
        write_u16_be(destination, offset, ethernet::vlan_tag_ethertype);
        write_u16_be(destination, offset + 2U, tci);
        offset += 4U;
    }

    write_u16_be(destination, offset, ethernet::goose_ethertype);
    offset += 2U;

    const auto declared_length = static_cast<std::uint16_t>(
        ethernet::ProcessBusFrameCodec::header_length + *apdu_size);
    write_u16_be(destination, offset, frame.app_id);
    write_u16_be(destination, offset + 2U, declared_length);
    write_u16_be(destination, offset + 4U, frame.reserved1);
    write_u16_be(destination, offset + 6U, frame.reserved2);
    offset += ethernet::ProcessBusFrameCodec::header_length;

    const auto pdu_result = GoosePduCodec::encode_into(
        frame.pdu,
        destination.subspan(offset, *apdu_size));
    if (!pdu_result.success() || pdu_result.bytes_written != *apdu_size) {
        return {pdu_result.status, 0U, *required};
    }
    offset += pdu_result.bytes_written;
    return {wire::EncodeStatus::ok, offset, *required};
}

} // namespace ar::iec61850::goose
