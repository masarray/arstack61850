// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/embedded/io.hpp"
#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/sampled_values/frame.hpp"
#include "ariec61850/sampled_values/frame_codec.hpp"
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
    bool fail_next{};
};

embedded::IoResult capture_frame(
    void* context,
    const std::span<const std::uint8_t> frame) noexcept {
    auto* fake = static_cast<FakeEthernet*>(context);
    if (fake == nullptr || frame.size() > fake->last_frame.size()) {
        return {embedded::IoStatus::invalid_argument, 0U};
    }
    if (fake->fail_next) {
        fake->fail_next = false;
        return {embedded::IoStatus::timeout, 0U};
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
    asdu.sv_id = "ESP32P4/LLN0$MSVCB01";
    asdu.data_set_reference = "ESP32P4/LLN0$PhsMeas1";
    asdu.configuration_revision = 1U;
    asdu.sample_synchronization = 0U;
    asdu.sample_rate = std::uint16_t{4'000U};
    asdu.sample_mode = std::uint16_t{1U};
    asdu.sample_payload = {
        0x00U, 0x00U, 0x00U, 0x64U,
        0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0xC8U,
        0x00U, 0x00U, 0x00U, 0x00U};

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
    using namespace ar::iec61850;

    FakeEthernet fake;
    embedded::RawEthernetPort port{&fake, &capture_frame};
    auto frame = make_frame();
    std::array<std::uint8_t, 1'536U> frame_buffer{};

    sampled_values::SampledValuesPublisher publisher(
        frame,
        frame_buffer,
        port,
        sampled_values::SampledValuesPublisherConfig{
            4'000U,
            std::uint16_t{4'000U},
            3'999U,
            true});

    if (!publisher.valid()) {
        return 1;
    }

    const auto first = publisher.poll(1'000U);
    if (!first.sent() || first.sample_count != 3'999U ||
        publisher.next_sample_count() != 0U || first.next_due_us != 1'250U ||
        fake.sends != 1U) {
        return 2;
    }

    sampled_values::SampledValuesFrame decoded;
    if (!sampled_values::SampledValuesFrameCodec::try_decode(
            std::span<const std::uint8_t>{fake.last_frame}.first(fake.last_size),
            decoded) ||
        decoded.pdu.asdus.size() != 1U ||
        decoded.pdu.asdus.front().sample_count != 3'999U) {
        return 3;
    }

    const auto early = publisher.poll(1'249U);
    if (early.status != sampled_values::SampledValuesPublishStatus::not_due ||
        fake.sends != 1U) {
        return 4;
    }

    const auto wrapped = publisher.poll(1'250U);
    if (!wrapped.sent() || wrapped.sample_count != 0U ||
        publisher.next_sample_count() != 1U || fake.sends != 2U) {
        return 5;
    }

    // Arrive very late. Exactly one frame may be sent, then the next deadline is
    // anchored 250 us after the actual send. This prevents scheduler catch-up bursts.
    const auto late = publisher.poll(2'000U);
    if (!late.sent() || late.lateness_us != 500U ||
        late.next_due_us != 2'250U || fake.sends != 3U) {
        return 6;
    }
    const auto no_catch_up = publisher.poll(2'001U);
    if (no_catch_up.status != sampled_values::SampledValuesPublishStatus::not_due ||
        fake.sends != 3U) {
        return 7;
    }

    const auto before_failure = publisher.next_sample_count();
    fake.fail_next = true;
    const auto failed = publisher.publish_now(2'100U);
    if (failed.status != sampled_values::SampledValuesPublishStatus::transmit_failed ||
        failed.io_status != embedded::IoStatus::timeout ||
        publisher.next_sample_count() != before_failure ||
        fake.sends != 3U) {
        return 8;
    }

    auto small_frame = make_frame();
    std::array<std::uint8_t, 32U> small_buffer{};
    sampled_values::SampledValuesPublisher too_small(
        small_frame,
        small_buffer,
        port,
        sampled_values::SampledValuesPublisherConfig{});
    const auto encoding_failure = too_small.publish_now(3'000U);
    if (encoding_failure.status != sampled_values::SampledValuesPublishStatus::encode_failed ||
        encoding_failure.encode_status != wire::EncodeStatus::buffer_too_small) {
        return 9;
    }

    const auto& statistics = publisher.statistics();
    if (statistics.frames_sent != 3U || statistics.transmit_failures != 1U ||
        statistics.late_polls != 1U || statistics.maximum_lateness_us != 500U) {
        return 10;
    }

    return 0;
}
