// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/sampled_values/frame_codec.hpp"

#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/sampled_values/pdu_codec.hpp"

#include <utility>

namespace ar::iec61850::sampled_values {

std::vector<std::uint8_t> SampledValuesFrameCodec::encode(
    const SampledValuesFrame& frame) {
    const auto apdu = SampledValuesPduCodec::encode(frame.pdu);
    const auto ethernet_frame = ethernet::ProcessBusFrameCodec::encode_ethernet_frame(
        frame.destination,
        frame.source,
        ethernet::sampled_values_ethertype,
        frame.app_id,
        apdu,
        frame.vlan,
        frame.reserved1,
        frame.reserved2);
    return ethernet::EthernetFrameCodec::encode(ethernet_frame);
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

} // namespace ar::iec61850::sampled_values
