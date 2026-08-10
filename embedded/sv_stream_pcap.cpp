// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/embedded/io.hpp"
#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/sampled_values/frame.hpp"
#include "ariec61850/sampled_values/payload_writer.hpp"
#include "ariec61850/sampled_values/publisher.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <span>

namespace {

using namespace ar::iec61850;

constexpr std::uint32_t kSampleRateHz = 4'000U;
constexpr std::uint64_t kPublishIntervalUs = 250U;
constexpr std::uint16_t kSampleCountWrap = 4'000U;
constexpr std::uint64_t kCaptureFrames = 8'000U;
constexpr std::uint64_t kCaptureStartUs = 1'000'000U;
constexpr std::size_t kChannelCount = 8U;
constexpr std::size_t kPayloadBytes =
    kChannelCount * sampled_values::SampledValuesPayloadWriter::int32_quality_pair_bytes;

bool write_bytes(
    std::ofstream& output,
    const std::span<const std::uint8_t> bytes) noexcept {
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(output);
}

bool write_u16_le(std::ofstream& output, const std::uint16_t value) noexcept {
    const std::array<std::uint8_t, 2U> bytes{
        static_cast<std::uint8_t>(value & 0xFFU),
        static_cast<std::uint8_t>((value >> 8U) & 0xFFU)};
    return write_bytes(output, bytes);
}

bool write_u32_le(std::ofstream& output, const std::uint32_t value) noexcept {
    const std::array<std::uint8_t, 4U> bytes{
        static_cast<std::uint8_t>(value & 0xFFU),
        static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
        static_cast<std::uint8_t>((value >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((value >> 24U) & 0xFFU)};
    return write_bytes(output, bytes);
}

bool write_pcap_header(std::ofstream& output) noexcept {
    // Classic PCAP, little-endian, microsecond timestamps, Ethernet link type.
    return write_u32_le(output, 0xA1B2C3D4U) &&
        write_u16_le(output, 2U) &&
        write_u16_le(output, 4U) &&
        write_u32_le(output, 0U) &&
        write_u32_le(output, 0U) &&
        write_u32_le(output, 65'535U) &&
        write_u32_le(output, 1U);
}

struct PcapEthernet final {
    std::ofstream* output{};
    std::uint64_t frame_index{};
    bool failed{};
};

embedded::IoResult capture_frame(
    void* context,
    const std::span<const std::uint8_t> frame) noexcept {
    auto* capture = static_cast<PcapEthernet*>(context);
    if (capture == nullptr || capture->output == nullptr || frame.empty() ||
        frame.size() > std::numeric_limits<std::uint32_t>::max()) {
        return {embedded::IoStatus::invalid_argument, 0U};
    }

    const auto timestamp_us =
        kCaptureStartUs + capture->frame_index * kPublishIntervalUs;
    const auto seconds = static_cast<std::uint32_t>(timestamp_us / 1'000'000U);
    const auto microseconds = static_cast<std::uint32_t>(timestamp_us % 1'000'000U);
    const auto frame_size = static_cast<std::uint32_t>(frame.size());

    auto& output = *capture->output;
    const auto written =
        write_u32_le(output, seconds) &&
        write_u32_le(output, microseconds) &&
        write_u32_le(output, frame_size) &&
        write_u32_le(output, frame_size) &&
        write_bytes(output, frame);
    if (!written) {
        capture->failed = true;
        return {embedded::IoStatus::io_error, 0U};
    }

    ++capture->frame_index;
    return {embedded::IoStatus::ok, frame.size()};
}

sampled_values::SampledValuesFrame make_frame() {
    const std::array<std::uint8_t, 6U> destination_mac{
        0x01U, 0x0CU, 0xCDU, 0x04U, 0x00U, 0x01U};
    const std::array<std::uint8_t, 6U> source_mac{
        0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U};

    sampled_values::SampledValueAsdu asdu;
    asdu.sv_id = "ARSTACK61850_P4_TRIAL";
    asdu.data_set_reference = "ARSTACK61850/LLN0$PhsMeas1";
    asdu.configuration_revision = 1U;
    asdu.sample_synchronization = 0U;
    asdu.sample_rate = std::uint16_t{kSampleRateHz};
    asdu.sample_mode = std::uint16_t{1U};
    asdu.sample_payload.resize(kPayloadBytes, 0U);

    return {
        ethernet::MacAddress{destination_mac},
        ethernet::MacAddress{source_mac},
        std::nullopt,
        0x4001U,
        0U,
        0U,
        sampled_values::SampledValuesPdu{{asdu}}};
}

bool update_synthetic_payload(
    sampled_values::SampledValueAsdu& asdu,
    const std::uint16_t sample_count) noexcept {
    if (asdu.sample_payload.size() != kPayloadBytes) {
        return false;
    }

    const auto payload = std::span<std::uint8_t>{
        asdu.sample_payload.data(), asdu.sample_payload.size()};
    const auto count = static_cast<std::int32_t>(sample_count);
    for (std::size_t channel = 0U; channel < kChannelCount; ++channel) {
        const auto value =
            count * 1'000 + static_cast<std::int32_t>(channel) * 100'000;
        if (!sampled_values::SampledValuesPayloadWriter::write_int32_quality_pair(
                payload, channel, value, 0U)) {
            return false;
        }
    }
    return true;
}

} // namespace

int main(const int argc, char** argv) {
    const char* output_path = argc > 1 ? argv[1] : "sv_trial_2s.pcap";
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output || !write_pcap_header(output)) {
        return 1;
    }

    PcapEthernet capture{&output};
    embedded::RawEthernetPort port{&capture, &capture_frame};
    auto frame = make_frame();
    std::array<std::uint8_t, 1'536U> frame_buffer{};
    sampled_values::SampledValuesPublisher publisher(
        frame,
        frame_buffer,
        port,
        sampled_values::SampledValuesPublisherConfig{
            kSampleRateHz,
            std::uint16_t{kSampleCountWrap},
            0U,
            true});

    if (!publisher.valid()) {
        return 2;
    }

    for (std::uint64_t index = 0U; index < kCaptureFrames; ++index) {
        const auto expected_count = static_cast<std::uint16_t>(index % kSampleCountWrap);
        if (publisher.next_sample_count() != expected_count ||
            !update_synthetic_payload(frame.pdu.asdus.front(), expected_count)) {
            return 3;
        }

        const auto now_us = kCaptureStartUs + index * kPublishIntervalUs;
        const auto result = publisher.poll(now_us);
        if (!result.sent() || result.sample_count != expected_count) {
            return 4;
        }
    }

    output.flush();
    if (!output || capture.failed || capture.frame_index != kCaptureFrames) {
        return 5;
    }

    const auto& statistics = publisher.statistics();
    if (statistics.frames_sent != kCaptureFrames ||
        statistics.encode_failures != 0U ||
        statistics.transmit_failures != 0U ||
        statistics.late_polls != 0U ||
        statistics.maximum_lateness_us != 0U ||
        publisher.next_sample_count() != 0U) {
        return 6;
    }

    return 0;
}
