// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/embedded/io.hpp"
#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/sampled_values/frame.hpp"
#include "ariec61850/sampled_values/publisher.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace {

using namespace ar::iec61850;

struct FakeEthernet final {
    std::array<std::uint8_t, 1'536U> last_frame{};
    std::size_t last_size{};
    std::uint64_t sends{};
};

embedded::IoResult capture_frame(
    void* context,
    const std::span<const std::uint8_t> frame) noexcept {
    auto* fake = static_cast<FakeEthernet*>(context);
    if (fake == nullptr || frame.size() > fake->last_frame.size()) {
        return {embedded::IoStatus::invalid_argument, 0U};
    }

    std::copy(frame.begin(), frame.end(), fake->last_frame.begin());
    fake->last_size = frame.size();
    ++fake->sends;
    return {embedded::IoStatus::ok, frame.size()};
}

sampled_values::SampledValuesFrame make_frame() {
    const std::array<std::uint8_t, 6> destination{
        0x01U, 0x0CU, 0xCDU, 0x04U, 0x00U, 0x01U};
    const std::array<std::uint8_t, 6> source{
        0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U};

    sampled_values::SampledValueAsdu asdu;
    asdu.sv_id = "ARSTACK61850_HARD_PROFILE";
    asdu.data_set_reference = "ARSTACK61850/LLN0$PhsMeas1";
    asdu.configuration_revision = 1U;
    asdu.sample_synchronization = 0U;
    asdu.sample_rate = std::uint16_t{4'000U};
    asdu.sample_mode = std::uint16_t{1U};
    asdu.sample_payload.resize(64U, 0U);

    return {
        ethernet::MacAddress{destination},
        ethernet::MacAddress{source},
        std::nullopt,
        0x4001U,
        0U,
        0U,
        sampled_values::SampledValuesPdu{{asdu}}};
}

} // namespace

int main() {
    FakeEthernet fake;
    embedded::RawEthernetPort port{&fake, &capture_frame};
    auto frame = make_frame();
    std::array<std::uint8_t, 1'536U> frame_buffer{};

    const auto asdu_capacity = frame.pdu.asdus.capacity();
    const auto payload_capacity = frame.pdu.asdus.front().sample_payload.capacity();
    const auto sv_id_capacity = frame.pdu.asdus.front().sv_id.capacity();
    const auto dataset_capacity = frame.pdu.asdus.front().data_set_reference.capacity();

    sampled_values::SampledValuesPublisher publisher(
        frame,
        frame_buffer,
        port,
        sampled_values::SampledValuesPublisherConfig{
            4'000U,
            std::uint16_t{4'000U},
            0U,
            true});
    if (!publisher.valid()) {
        return 1;
    }

    constexpr std::uint64_t start_us = 1'000'000U;
    constexpr std::uint64_t frame_count = 8'000U;
    for (std::uint64_t index = 0U; index < frame_count; ++index) {
        const auto result = publisher.poll(start_us + index * 250U);
        const auto expected_count = static_cast<std::uint16_t>(index % 4'000U);
        if (!result.sent() || result.sample_count != expected_count ||
            fake.sends != index + 1U) {
            return 2;
        }
    }

    const auto& stats = publisher.statistics();
    if (stats.frames_sent != frame_count || stats.encode_failures != 0U ||
        stats.transmit_failures != 0U || stats.late_polls != 0U ||
        stats.maximum_lateness_us != 0U || publisher.next_sample_count() != 0U) {
        return 3;
    }

    // The current hard-profile milestone permits setup-time dynamic storage in
    // the legacy frame model, but the 4 kHz steady-state path must not grow it.
    if (frame.pdu.asdus.capacity() != asdu_capacity ||
        frame.pdu.asdus.front().sample_payload.capacity() != payload_capacity ||
        frame.pdu.asdus.front().sv_id.capacity() != sv_id_capacity ||
        frame.pdu.asdus.front().data_set_reference.capacity() != dataset_capacity) {
        return 4;
    }

    if (fake.last_size < 22U ||
        fake.last_frame[12] != 0x88U || fake.last_frame[13] != 0xBAU ||
        fake.last_frame[14] != 0x40U || fake.last_frame[15] != 0x01U) {
        return 5;
    }

    return 0;
}
