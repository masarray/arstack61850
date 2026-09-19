// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/asn1/ber_span_reader.hpp"
#include "ariec61850/embedded/boards/waveshare_esp32s3_poe_eth_8di8do.hpp"
#include "ariec61850/embedded/profile.hpp"
#include "ariec61850/osi/tpkt_span.hpp"

#include <array>
#include <cstdint>
#include <span>

int main() {
    using namespace ar::iec61850;

    // CMake links every source in the bounded embedded profile into this
    // executable. Keep the smoke itself on the same span-only boundary so
    // missing transitive dependencies are exposed without pulling legacy
    // heap-owning protocol implementations back into the embedded profile.
    static_assert(
        embedded::Esp32SmallProfile::ethernet_frame_bytes >= 1'522U);
    static_assert(
        embedded::boards::WaveshareEsp32S3PoeEth8Di8Do::w5500_macraw_socket == 0U);

    constexpr std::array<std::uint8_t, 2U> ber_null{0x05U, 0x00U};
    asn1::BerTlvView tlv;
    if (!asn1::BerSpanReader::try_read_exact(ber_null, tlv) ||
        tlv.encoded_bytes != ber_null.size()) {
        return 1;
    }

    constexpr std::array<std::uint8_t, 1U> payload{0xAAU};
    std::array<std::uint8_t, 8U> tpkt{};
    const auto encoded = osi::TpktSpanCodec::encode_into(payload, tpkt);
    if (!encoded.success() ||
        encoded.bytes_written != osi::TpktSpanCodec::header_length + payload.size()) {
        return 2;
    }

    osi::TpktFrameView decoded;
    if (!osi::TpktSpanCodec::try_decode_view(
            std::span<const std::uint8_t>{tpkt}.first(encoded.bytes_written),
            decoded) ||
        decoded.version != osi::TpktSpanCodec::supported_version ||
        decoded.payload.size() != payload.size() ||
        decoded.payload[0] != payload[0]) {
        return 3;
    }

    return 0;
}
