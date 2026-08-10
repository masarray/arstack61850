// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/embedded/boards/waveshare_esp32s3_poe_eth_8di8do.hpp"
#include "ariec61850/embedded/io.hpp"
#include "ariec61850/embedded/profile.hpp"
#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/goose/frame_codec.hpp"
#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/osi/tpkt.hpp"
#include "ariec61850/sampled_values/frame_codec.hpp"
#include "ariec61850/wire/encode_result.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

ar::iec61850::embedded::IoResult discard_frame(
    void*, const std::span<const std::uint8_t> frame) noexcept {
    return {ar::iec61850::embedded::IoStatus::ok, frame.size()};
}

} // namespace

int main() {
    using namespace ar::iec61850;

    // The executable is linked from every embedded source file by CMake, so
    // the linker catches host-only dependencies even when this smoke program
    // does not exercise every protocol at runtime.
    std::array<std::uint8_t, 2> bytes{0x05U, 0x00U};
    std::size_t offset = 0U;
    asn1::BerTlv tlv;
    const bool parsed = asn1::BerReader::try_read_tlv(bytes, offset, tlv);

    static_assert(
        embedded::Esp32SmallProfile::ethernet_frame_bytes >= 1'522U);
    static_assert(
        embedded::boards::WaveshareEsp32S3PoeEth8Di8Do::w5500_macraw_socket == 0U);

    const std::array<std::uint8_t, 6> destination_bytes{
        0x01U, 0x0CU, 0xCDU, 0x04U, 0x00U, 0x01U};
    const std::array<std::uint8_t, 6> source_bytes{
        0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U};

    sampled_values::SampledValueAsdu asdu;
    asdu.sv_id = "ESP32S3IO/LLN0$MSVCB01";
    asdu.data_set_reference = "ESP32S3IO/LLN0$PhsMeas1";
    asdu.sample_count = 120U;
    asdu.configuration_revision = 1U;
    asdu.reference_time = mms::Iec61850UtcTime{
        std::chrono::system_clock::time_point{std::chrono::seconds{1'781'260'260}},
        0U};
    asdu.sample_synchronization = 2U;
    asdu.sample_rate = std::uint16_t{4'000U};
    asdu.sample_mode = std::uint16_t{1U};
    asdu.sample_payload = {
        0x00U, 0x00U, 0x00U, 0x64U,
        0x00U, 0x00U, 0x00U, 0x01U,
        0x00U, 0x00U, 0x00U, 0xC8U,
        0x00U, 0x00U, 0x00U, 0x03U};

    sampled_values::SampledValuesFrame sv_frame{
        ethernet::MacAddress{destination_bytes},
        ethernet::MacAddress{source_bytes},
        ethernet::VlanTag{4U, 200U},
        0x4001U,
        0U,
        0U,
        sampled_values::SampledValuesPdu{{asdu}}};

    std::array<std::uint8_t, embedded::Esp32SmallProfile::ethernet_frame_bytes>
        frame_buffer{};
    const auto first_encode = sampled_values::SampledValuesFrameCodec::encode_into(
        sv_frame, frame_buffer);
    if (!first_encode.success() || first_encode.bytes_written == 0U) {
        return 2;
    }

    // Reuse the same caller-owned frame buffer. The encoder itself performs no
    // heap allocation on this path; configuration objects are prepared once.
    sv_frame.pdu.asdus.front().sample_count = 121U;
    const auto second_encode = sampled_values::SampledValuesFrameCodec::encode_into(
        sv_frame, frame_buffer);
    if (!second_encode.success() ||
        second_encode.bytes_written != first_encode.bytes_written) {
        return 3;
    }

    if (second_encode.required_bytes <= 1U) {
        return 4;
    }
    const auto too_small = sampled_values::SampledValuesFrameCodec::encode_into(
        sv_frame,
        std::span<std::uint8_t>{frame_buffer}.first(
            second_encode.required_bytes - 1U));
    if (too_small.status != wire::EncodeStatus::buffer_too_small ||
        too_small.required_bytes != second_encode.required_bytes) {
        return 5;
    }

    embedded::RawEthernetPort ethernet_port;
    ethernet_port.transmit = &discard_frame;
    const auto sent = ethernet_port.send(
        std::span<const std::uint8_t>{frame_buffer}.first(
            second_encode.bytes_written));

    return parsed && sent.success() ? 0 : 1;
}
