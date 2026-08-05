// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/goose/frame_codec.hpp"

#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/goose/pdu_codec.hpp"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace ar::iec61850::goose {

std::vector<std::uint8_t> GooseFrameCodec::encode(const GooseFrame& frame) {
    const auto apdu = GoosePduCodec::encode(frame.pdu);
    const auto ethernet_frame = ethernet::ProcessBusFrameCodec::encode_ethernet_frame(
        frame.destination,
        frame.source,
        ethernet::goose_ethertype,
        frame.app_id,
        apdu,
        frame.vlan,
        frame.reserved1,
        frame.reserved2);
    return ethernet::EthernetFrameCodec::encode(ethernet_frame);
}

bool GooseFrameCodec::try_decode(
    const std::span<const std::uint8_t> frame_bytes, GooseFrame& frame) noexcept {
    frame = {};

    ethernet::EthernetFrame ethernet_frame;
    if (!ethernet::EthernetFrameCodec::try_decode(frame_bytes, ethernet_frame) ||
        ethernet_frame.ether_type != ethernet::goose_ethertype) {
        return false;
    }

    ethernet::ProcessBusFrame process_bus;
    if (!ethernet::ProcessBusFrameCodec::try_decode(ethernet_frame, process_bus)) {
        return false;
    }

    GoosePdu pdu;
    if (!GoosePduCodec::try_decode(process_bus.apdu, pdu)) {
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

} // namespace ar::iec61850::goose
