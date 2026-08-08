// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/ethernet/ethernet.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace ar::iec61850::ethernet {
namespace {

void write_u16_be(std::span<std::uint8_t> destination, const std::size_t offset,
                  const std::uint16_t value) {
    destination[offset] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    destination[offset + 1U] = static_cast<std::uint8_t>(value & 0xFFU);
}

std::uint16_t read_u16_be(const std::span<const std::uint8_t> source,
                          const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(source[offset]) << 8U) |
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

MacAddress::MacAddress(const std::span<const std::uint8_t> bytes) {
    if (bytes.size() != bytes_.size()) {
        throw std::invalid_argument("A MAC address must contain exactly 6 bytes.");
    }
    std::copy(bytes.begin(), bytes.end(), bytes_.begin());
}

MacAddress MacAddress::parse(const std::string& text) {
    MacAddress address;
    if (!try_parse(text, address)) {
        throw std::invalid_argument("Invalid MAC address '" + text + "'.");
    }
    return address;
}

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
        return false;
    }

    for (auto it = begin; it != end; ++it) {
        normalized.push_back(*it == '-' ? ':' : *it);
    }
    if (normalized.size() != 17U) {
        return false;
    }

    std::array<std::uint8_t, 6> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const auto offset = index * 3U;
        if (index != bytes.size() - 1U && normalized[offset + 2U] != ':') {
            return false;
        }
        const auto high = hex_value(normalized[offset]);
        const auto low = hex_value(normalized[offset + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
    }

    address.bytes_ = bytes;
    return true;
}

void MacAddress::copy_to(const std::span<std::uint8_t> destination) const {
    if (destination.size() < bytes_.size()) {
        throw std::invalid_argument("Destination span must be at least 6 bytes.");
    }
    std::copy(bytes_.begin(), bytes_.end(), destination.begin());
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

std::uint16_t VlanTag::to_tag_control_information() const {
    if (priority_code_point > 7U) {
        throw std::out_of_range("VLAN priority must be 0..7.");
    }
    if (vlan_id > 4094U) {
        throw std::out_of_range("VLAN ID must be 0..4094.");
    }

    const auto tci = (static_cast<std::uint32_t>(priority_code_point) << 13U) |
                     (drop_eligible ? 0x1000U : 0U) |
                     static_cast<std::uint32_t>(vlan_id);
    return static_cast<std::uint16_t>(tci);
}

VlanTag VlanTag::from_tag_control_information(const std::uint16_t tci) noexcept {
    return {
        static_cast<std::uint8_t>((tci >> 13U) & 0x07U),
        (tci & 0x1000U) != 0U,
        static_cast<std::uint16_t>(tci & 0x0FFFU)};
}

std::vector<std::uint8_t> EthernetFrameCodec::encode(const EthernetFrame& frame) {
    const std::size_t header_length = frame.vlan ? 18U : 14U;
    std::vector<std::uint8_t> bytes(header_length + frame.payload.size());
    const std::span<std::uint8_t> span{bytes};

    frame.destination.copy_to(span.first(6U));
    frame.source.copy_to(span.subspan(6U, 6U));

    std::size_t offset = 12U;
    if (frame.vlan) {
        write_u16_be(span, offset, vlan_tag_ethertype);
        write_u16_be(span, offset + 2U, frame.vlan->to_tag_control_information());
        offset += 4U;
    }

    write_u16_be(span, offset, frame.ether_type);
    offset += 2U;
    std::copy(frame.payload.begin(), frame.payload.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    return bytes;
}

bool EthernetFrameCodec::try_decode(const std::span<const std::uint8_t> bytes,
                                    EthernetFrame& frame) noexcept {
    frame = {};
    if (bytes.size() < 14U) {
        return false;
    }

    try {
        frame.destination = MacAddress(bytes.first(6U));
        frame.source = MacAddress(bytes.subspan(6U, 6U));
    } catch (...) {
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
        // IEEE 802.1Q reserves VID 4095. Keep try_decode closed under encode:
        // every successfully decoded VlanTag must be valid for re-encoding.
        if (decoded_vlan.vlan_id > 4094U) {
            return false;
        }
        vlan = decoded_vlan;
        ether_type = read_u16_be(bytes, 16U);
        payload_offset = 18U;
    }

    frame.ether_type = ether_type;
    frame.vlan = vlan;
    frame.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(payload_offset), bytes.end());
    return true;
}

std::vector<std::uint8_t> ProcessBusFrameCodec::encode_payload(
    const std::uint16_t app_id,
    const std::span<const std::uint8_t> apdu,
    const std::uint16_t reserved1,
    const std::uint16_t reserved2) {
    if (apdu.size() > std::numeric_limits<std::uint16_t>::max() - header_length) {
        throw std::out_of_range("Process-bus APDU exceeds the 16-bit declared length.");
    }

    const auto declared_length = static_cast<std::uint16_t>(header_length + apdu.size());
    std::vector<std::uint8_t> bytes(declared_length);
    const std::span<std::uint8_t> span{bytes};
    write_u16_be(span, 0U, app_id);
    write_u16_be(span, 2U, declared_length);
    write_u16_be(span, 4U, reserved1);
    write_u16_be(span, 6U, reserved2);
    std::copy(apdu.begin(), apdu.end(), bytes.begin() + static_cast<std::ptrdiff_t>(header_length));
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
    return {destination, source, ether_type, vlan,
            encode_payload(app_id, apdu, reserved1, reserved2)};
}

bool ProcessBusFrameCodec::try_decode(const EthernetFrame& ethernet,
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
    frame.apdu.assign(payload.begin() + static_cast<std::ptrdiff_t>(header_length),
                      payload.begin() + static_cast<std::ptrdiff_t>(header_length + declared_apdu_length));
    return true;
}

} // namespace ar::iec61850::ethernet