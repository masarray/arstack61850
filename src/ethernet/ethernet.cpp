// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/ethernet/ethernet.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <string>
#if !defined(ARIEC61850_NO_EXCEPTIONS)
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>
#endif

namespace ar::iec61850::ethernet {
namespace {

void write_u16_be(
    const std::span<std::uint8_t> destination,
    const std::size_t offset,
    const std::uint16_t value) noexcept {
    destination[offset] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    destination[offset + 1U] = static_cast<std::uint8_t>(value & 0xFFU);
}

std::uint16_t read_u16_be(
    const std::span<const std::uint8_t> source,
    const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(source[offset]) << 8U) |
        static_cast<std::uint16_t>(source[offset + 1U]));
}

int hex_value(const char ch) noexcept {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

} // namespace

bool MacAddress::try_from_bytes(
    const std::span<const std::uint8_t> bytes,
    MacAddress& address) noexcept {
    if (bytes.size() != address.bytes_.size()) {
        address = {};
        return false;
    }
    std::copy(bytes.begin(), bytes.end(), address.bytes_.begin());
    return true;
}

#if !defined(ARIEC61850_NO_EXCEPTIONS)
MacAddress::MacAddress(const std::span<const std::uint8_t> bytes) {
    if (!try_from_bytes(bytes, *this)) {
        throw std::invalid_argument("A MAC address must contain exactly 6 bytes.");
    }
}
#endif

bool MacAddress::try_parse(const std::string& text, MacAddress& address) noexcept {
    std::string normalized;
    normalized.reserve(text.size());

    auto begin = text.begin();
    while (begin != text.end() && std::isspace(static_cast<unsigned char>(*begin)) != 0) {
        ++begin;
    }
    auto end = text.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1))) != 0) {
        --end;
    }
    if (begin == end) {
        address = {};
        return false;
    }

    for (auto it = begin; it != end; ++it) {
        normalized.push_back(*it == '-' ? ':' : *it);
    }
    if (normalized.size() != 17U) {
        address = {};
        return false;
    }

    std::array<std::uint8_t, 6> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const auto offset = index * 3U;
        if (index != bytes.size() - 1U && normalized[offset + 2U] != ':') {
            address = {};
            return false;
        }
        const auto high = hex_value(normalized[offset]);
        const auto low = hex_value(normalized[offset + 1U]);
        if (high < 0 || low < 0) {
            address = {};
            return false;
        }
        bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
    }

    address.bytes_ = bytes;
    return true;
}

#if !defined(ARIEC61850_NO_EXCEPTIONS)
MacAddress MacAddress::parse(const std::string& text) {
    MacAddress address;
    if (!try_parse(text, address)) {
        throw std::invalid_argument("Invalid MAC address '" + text + "'.");
    }
    return address;
}
#endif

bool MacAddress::try_copy_to(
    const std::span<std::uint8_t> destination) const noexcept {
    if (destination.size() < bytes_.size()) {
        return false;
    }
    std::copy(bytes_.begin(), bytes_.end(), destination.begin());
    return true;
}

#if !defined(ARIEC61850_NO_EXCEPTIONS)
void MacAddress::copy_to(const std::span<std::uint8_t> destination) const {
    if (!try_copy_to(destination)) {
        throw std::invalid_argument("Destination span must be at least 6 bytes.");
    }
}

std::string MacAddress::to_string() const {
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < bytes_.size(); ++index) {
        if (index != 0U) {
            stream << ':';
        }
        stream << std::setw(2) << static_cast<unsigned>(bytes_[index]);
    }
    return stream.str();
}
#endif

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

#if !defined(ARIEC61850_NO_EXCEPTIONS)
std::uint16_t VlanTag::to_tag_control_information() const {
    std::uint16_t tci{};
    if (!try_to_tag_control_information(tci)) {
        if (priority_code_point > 7U) {
            throw std::out_of_range("VLAN priority must be 0..7.");
        }
        throw std::out_of_range("VLAN ID must be 0..4094.");
    }
    return tci;
}
#endif

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

#if !defined(ARIEC61850_NO_EXCEPTIONS)
std::vector<std::uint8_t> EthernetFrameCodec::encode(const EthernetFrame& frame) {
    const auto required = encoded_size(frame);
    if (!required) {
        throw std::out_of_range("Ethernet frame exceeds the supported wire range.");
    }
    std::vector<std::uint8_t> bytes(*required);
    const auto result = encode_into(frame, bytes);
    if (!result.success() || result.bytes_written != bytes.size()) {
        throw std::runtime_error("Failed to encode Ethernet frame into sized buffer.");
    }
    return bytes;
}

bool EthernetFrameCodec::try_decode(
    const std::span<const std::uint8_t> bytes,
    EthernetFrame& frame) noexcept {
    frame = {};
    if (bytes.size() < 14U ||
        !MacAddress::try_from_bytes(bytes.first(6U), frame.destination) ||
        !MacAddress::try_from_bytes(bytes.subspan(6U, 6U), frame.source)) {
        return false;
    }

    auto ether_type = read_u16_be(bytes, 12U);
    std::size_t payload_offset = 14U;
    std::optional<VlanTag> vlan;

    if (ether_type == vlan_tag_ethertype) {
        if (bytes.size() < 18U) {
            return false;
        }
        const auto decoded_vlan =
            VlanTag::from_tag_control_information(read_u16_be(bytes, 14U));
        if (decoded_vlan.vlan_id > 4094U) {
            return false;
        }
        vlan = decoded_vlan;
        ether_type = read_u16_be(bytes, 16U);
        payload_offset = 18U;
    }

    frame.ether_type = ether_type;
    frame.vlan = vlan;
    frame.payload.assign(
        bytes.begin() + static_cast<std::ptrdiff_t>(payload_offset),
        bytes.end());
    return true;
}
#endif

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

    const auto declared_length = static_cast<std::uint16_t>(*required);
    write_u16_be(destination, 0U, app_id);
    write_u16_be(destination, 2U, declared_length);
    write_u16_be(destination, 4U, reserved1);
    write_u16_be(destination, 6U, reserved2);
    std::copy(
        apdu.begin(),
        apdu.end(),
        destination.begin() + static_cast<std::ptrdiff_t>(header_length));
    return {wire::EncodeStatus::ok, *required, *required};
}

#if !defined(ARIEC61850_NO_EXCEPTIONS)
std::vector<std::uint8_t> ProcessBusFrameCodec::encode_payload(
    const std::uint16_t app_id,
    const std::span<const std::uint8_t> apdu,
    const std::uint16_t reserved1,
    const std::uint16_t reserved2) {
    const auto required = encoded_payload_size(apdu);
    if (!required) {
        throw std::out_of_range("Process-bus APDU exceeds the 16-bit declared length.");
    }

    std::vector<std::uint8_t> bytes(*required);
    const auto result = encode_payload_into(
        app_id, apdu, bytes, reserved1, reserved2);
    if (!result.success() || result.bytes_written != bytes.size()) {
        throw std::runtime_error("Failed to encode process-bus payload into sized buffer.");
    }
    return bytes;
}

EthernetFrame ProcessBusFrameCodec::encode_ethernet_frame(
    const MacAddress& destination,
    const MacAddress& source,
    const std::uint16_t ether_type,
    const std::uint16_t app_id,
    const std::span<const std::uint8_t> apdu,
    const std::optional<VlanTag> vlan,
    const std::uint16_t reserved1,
    const std::uint16_t reserved2) {
    return {
        destination,
        source,
        ether_type,
        vlan,
        encode_payload(app_id, apdu, reserved1, reserved2)};
}

bool ProcessBusFrameCodec::try_decode(
    const EthernetFrame& ethernet,
    ProcessBusFrame& frame) noexcept {
    frame = {};
    if (ethernet.payload.size() < header_length) {
        return false;
    }

    const std::span<const std::uint8_t> payload{ethernet.payload};
    const auto app_id = read_u16_be(payload, 0U);
    const auto declared_length = read_u16_be(payload, 2U);
    const auto reserved1 = read_u16_be(payload, 4U);
    const auto reserved2 = read_u16_be(payload, 6U);

    const auto available_apdu_length = payload.size() - header_length;
    const auto declared_apdu_length = declared_length >= header_length
        ? static_cast<std::size_t>(declared_length) - header_length
        : available_apdu_length;

    if (declared_apdu_length > available_apdu_length) {
        return false;
    }

    frame.ethernet = ethernet;
    frame.app_id = app_id;
    frame.declared_length = declared_length;
    frame.reserved1 = reserved1;
    frame.reserved2 = reserved2;
    frame.apdu.assign(
        payload.begin() + static_cast<std::ptrdiff_t>(header_length),
        payload.begin() + static_cast<std::ptrdiff_t>(header_length + declared_apdu_length));
    return true;
}
#endif

} // namespace ar::iec61850::ethernet
