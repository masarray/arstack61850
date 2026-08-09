// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/ethernet/ethernet.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace ar::iec61850::ethernet {
namespace {

void write_u16_be(
    const std::span<std::uint8_t> destination,
    const std::size_t offset,
    const std::uint16_t value) noexcept {
    destination[offset] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    destination[offset + 1U] = static_cast<std::uint8_t>(value & 0xFFU);
}

} // namespace

bool MacAddress::try_from_bytes(
    const std::span<const std::uint8_t> bytes,
    MacAddress& address) noexcept {
    if (bytes.size() != address.bytes().size()) {
        address = {};
        return false;
    }
    std::array<std::uint8_t, 6U> copy{};
    std::copy(bytes.begin(), bytes.end(), copy.begin());
    address = MacAddress{copy};
    return true;
}

bool MacAddress::try_copy_to(
    const std::span<std::uint8_t> destination) const noexcept {
    if (destination.size() < bytes().size()) {
        return false;
    }
    std::copy(bytes().begin(), bytes().end(), destination.begin());
    return true;
}

bool VlanTag::try_to_tag_control_information(std::uint16_t& tci) const noexcept {
    if (priority_code_point > 7U || vlan_id > 4094U) {
        tci = 0U;
        return false;
    }

    const auto value =
        (static_cast<std::uint32_t>(priority_code_point) << 13U) |
        (drop_eligible ? 0x1000U : 0U) |
        static_cast<std::uint32_t>(vlan_id);
    tci = static_cast<std::uint16_t>(value);
    return true;
}

VlanTag VlanTag::from_tag_control_information(const std::uint16_t tci) noexcept {
    return {
        static_cast<std::uint8_t>((tci >> 13U) & 0x07U),
        (tci & 0x1000U) != 0U,
        static_cast<std::uint16_t>(tci & 0x0FFFU)};
}

std::optional<std::size_t> EthernetFrameCodec::encoded_size(
    const EthernetFrame& frame) noexcept {
    if (frame.vlan.has_value()) {
        std::uint16_t tci{};
        if (!frame.vlan->try_to_tag_control_information(tci)) {
            return std::nullopt;
        }
    }

    const std::size_t header_length = frame.vlan.has_value() ? 18U : 14U;
    if (frame.payload.size() > std::numeric_limits<std::size_t>::max() - header_length) {
        return std::nullopt;
    }
    return header_length + frame.payload.size();
}

wire::EncodeResult EthernetFrameCodec::encode_into(
    const EthernetFrame& frame,
    const std::span<std::uint8_t> destination) noexcept {
    const auto required = encoded_size(frame);
    if (!required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (destination.size() < *required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, *required};
    }

    std::copy(frame.destination.bytes().begin(), frame.destination.bytes().end(), destination.begin());
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
        write_u16_be(destination, offset, vlan_tag_ethertype);
        write_u16_be(destination, offset + 2U, tci);
        offset += 4U;
    }

    write_u16_be(destination, offset, frame.ether_type);
    offset += 2U;
    std::copy(
        frame.payload.begin(),
        frame.payload.end(),
        destination.begin() + static_cast<std::ptrdiff_t>(offset));
    offset += frame.payload.size();
    return {wire::EncodeStatus::ok, offset, *required};
}

std::optional<std::size_t> ProcessBusFrameCodec::encoded_payload_size(
    const std::span<const std::uint8_t> apdu) noexcept {
    if (apdu.size() >
        static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) - header_length) {
        return std::nullopt;
    }
    return header_length + apdu.size();
}

wire::EncodeResult ProcessBusFrameCodec::encode_payload_into(
    const std::uint16_t app_id,
    const std::span<const std::uint8_t> apdu,
    const std::span<std::uint8_t> destination,
    const std::uint16_t reserved1,
    const std::uint16_t reserved2) noexcept {
    const auto required = encoded_payload_size(apdu);
    if (!required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (destination.size() < *required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, *required};
    }

    write_u16_be(destination, 0U, app_id);
    write_u16_be(destination, 2U, static_cast<std::uint16_t>(*required));
    write_u16_be(destination, 4U, reserved1);
    write_u16_be(destination, 6U, reserved2);
    std::copy(
        apdu.begin(),
        apdu.end(),
        destination.begin() + static_cast<std::ptrdiff_t>(header_length));
    return {wire::EncodeStatus::ok, *required, *required};
}

} // namespace ar::iec61850::ethernet
