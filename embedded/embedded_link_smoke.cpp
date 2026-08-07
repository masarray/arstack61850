// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/goose/frame_codec.hpp"
#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/osi/tpkt.hpp"
#include "ariec61850/sampled_values/frame_codec.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

int main() {
    // Intentionally tiny: the executable is linked from every embedded source
    // file by CMake, so the linker catches host-only dependencies even when
    // this smoke program does not exercise every protocol at runtime.
    std::array<std::uint8_t, 2> bytes{0x05U, 0x00U};
    std::size_t offset = 0U;
    ar::iec61850::asn1::BerTlv tlv;
    const bool parsed = ar::iec61850::asn1::BerReader::try_read_tlv(bytes, offset, tlv);
    return parsed ? 0 : 0;
}
