// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/embedded/io.hpp"
#include "ariec61850/embedded/profile.hpp"
#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/goose/frame_codec.hpp"
#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/osi/tpkt.hpp"
#include "ariec61850/sampled_values/frame_codec.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

ar::iec61850::embedded::IoResult discard_frame(
    void*, const std::span<const std::uint8_t> frame) noexcept {
    return {ar::iec61850::embedded::IoStatus::ok, frame.size()};
}

} // namespace

int main() {
    // The executable is linked from every embedded source file by CMake, so
    // the linker catches host-only dependencies even when this smoke program
    // does not exercise every protocol at runtime.
    std::array<std::uint8_t, 2> bytes{0x05U, 0x00U};
    std::size_t offset = 0U;
    ar::iec61850::asn1::BerTlv tlv;
    const bool parsed = ar::iec61850::asn1::BerReader::try_read_tlv(bytes, offset, tlv);

    ar::iec61850::embedded::RawEthernetPort ethernet;
    ethernet.transmit = &discard_frame;
    const auto sent = ethernet.send(bytes);

    static_assert(
        ar::iec61850::embedded::Esp32SmallProfile::ethernet_frame_bytes >= 1'522U);
    return parsed && sent.success() ? 0 : 1;
}
