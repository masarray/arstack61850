// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/sampled_values/frame_codec.hpp"

#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/sampled_values/pdu_codec.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#if !defined(ARIEC61850_NO_EXCEPTIONS)
#include <stdexcept>
#include <utility>
#include <vector>
#endif

namespace ar::iec61850::sampled_values {
namespace {

void write_u16_be(
    const std::span<std::uint8_t> destination,
    const std::size_t offset,
    const std::uint16_t value) noexcept {
    destination[offset] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    destination[offset + 1U] = static_cast<std::uint8_t>(value & 0xFFU);
}

[[nodiscard]] std::optional<std::uint16_t> vlan_tci(
    const ethernet::VlanTag& vlan) noexcept {
    if (vlan.priority_code_point > 7U || vlan.vlan_id > 4094U) {
        return std::nullopt;
    }
    const auto value =
        (static_cast<std::uint32_t>(vlan.priority_code_point) << 13U) |
        (vlan.drop_eligible ? 0x1000U : 0U) |
        static_cast<std::uint32_t>(vlan.vlan_id);
    return static_cast<std::uint16_t>(value);
}

} // namespace

std::optional<std::size_t> SampledValuesFrameCodec::encoded_size(
    const SampledValuesFrame& frame) noexcept {
    if (frame.vlan.has_value() && !vlan_tci(*frame.vlan).has_value()) {
        return std::nullopt;
    }

    const auto apdu_size = SampledValuesPduCodec::encoded_size(frame.pdu);
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

wire::EncodeResult SampledValuesFrameCodec::encode_into(
    const SampledValuesFrame& frame,
    const std::span<std::uint8_t> destination) noexcept {
    const auto required = encoded_size(frame);
    if (!required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (destination.size() < *required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, *required};
    }

    const auto apdu_size = SampledValuesPduCodec::encoded_size(frame.pdu);
    if (!apdu_size) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    std::copy(
        frame.destination.bytes().begin(), frame.destination.bytes().end(),
        destination.begin());
    std::copy(
        frame.source.bytes().begin(), frame.source.bytes().end(),
        destination.begin() + 6);

    std::size_t offset = 12U;
    if (frame.vlan.has_value()) {
        const auto tci = vlan_tci(*frame.vlan);
        if (!tci) {
            return {wire::EncodeStatus::value_out_of_range, 0U, *required};
        }
        write_u16_be(destination, offset, ethernet::vlan_tag_ethertype);
        write_u16_be(destination, offset + 2U, *tci);
        offset += 4U;
    }

    write_u16_be(destination, offset, ethernet::sampled_values_ethertype);
    offset += 2U;

    const auto declared_length = static_cast<std::uint16_t>(
        ethernet::ProcessBusFrameCodec::header_length + *apdu_size);
    write_u16_be(destination, offset, frame.app_id);
    write_u16_be(destination, offset + 2U, declared_length);
    write_u16_be(destination, offset + 4U, frame.reserved1);
    write_u16_be(destination, offset + 6U, frame.reserved2);
    offset += ethernet::ProcessBusFrameCodec::header_length;

    const auto pdu_result = SampledValuesPduCodec::encode_into(
        frame.pdu, destination.subspan(offset, *apdu_size));
    if (!pdu_result.success() || pdu_result.bytes_written != *apdu_size) {
        return {pdu_result.status, 0U, *required};
    }
    offset += pdu_result.bytes_written;
    return {wire::EncodeStatus::ok, offset, *required};
}

#if !defined(ARIEC61850_NO_EXCEPTIONS)
std::vector<std::uint8_t> SampledValuesFrameCodec::encode(
    const SampledValuesFrame& frame) {
    const auto required = encoded_size(frame);
    if (!required) {
        throw std::out_of_range("A Sampled Values Ethernet frame exceeds the supported wire range.");
    }

    std::vector<std::uint8_t> bytes(*required);
    const auto result = encode_into(frame, bytes);
    if (!result.success() || result.bytes_written != bytes.size()) {
        throw std::runtime_error("Failed to encode Sampled Values frame into sized buffer.");
    }
    return bytes;
}

bool SampledValuesFrameCodec::try_decode(
    const std::span<const std::uint8_t> bytes,
    SampledValuesFrame& frame) noexcept {
    frame = {};

    ethernet::EthernetFrame ethernet_frame;
    if (!ethernet::EthernetFrameCodec::try_decode(bytes, ethernet_frame) ||
        ethernet_frame.ether_type != ethernet::sampled_values_ethertype) {
        return false;
    }

    ethernet::ProcessBusFrame process_bus;
    if (!ethernet::ProcessBusFrameCodec::try_decode(ethernet_frame, process_bus) ||
        process_bus.declared_length < ethernet::ProcessBusFrameCodec::header_length) {
        return false;
    }

    SampledValuesPdu pdu;
    if (!SampledValuesPduCodec::try_decode(process_bus.apdu, pdu)) {
        return false;
    }

    frame.destination = ethernet_frame.destination;
    frame.source = ethernet_frame.source;
    frame.vlan = ethernet_frame.vlan;
    frame.app_id = process_bus.app_id;
    frame.reserved1 = process_bus.reserved1;
    frame.reserved2 = process_bus.reserved2;
    frame.pdu = std::move(pdu);
    return true;
}
#endif

} // namespace ar::iec61850::sampled_values
