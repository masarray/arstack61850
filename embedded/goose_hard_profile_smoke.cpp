// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/goose/frame_codec.hpp"
#include "ariec61850/goose/pdu_codec.hpp"
#include "ariec61850/mms/data_value.hpp"
#include "ariec61850/mms/utc_time.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

using namespace ar::iec61850;

constexpr std::array<std::uint8_t, 86U> kExpectedPdu{
    0x61U, 0x54U, 0x80U, 0x10U, 0x4CU, 0x44U, 0x30U, 0x2FU, 0x4CU, 0x4CU,
    0x4EU, 0x30U, 0x24U, 0x47U, 0x4FU, 0x24U, 0x67U, 0x63U, 0x62U, 0x31U,
    0x81U, 0x02U, 0x03U, 0xE8U, 0x82U, 0x0CU, 0x4CU, 0x44U, 0x30U, 0x2FU,
    0x4CU, 0x4CU, 0x4EU, 0x30U, 0x24U, 0x44U, 0x53U, 0x31U, 0x83U, 0x06U,
    0x47U, 0x4FU, 0x4FU, 0x53U, 0x45U, 0x31U, 0x84U, 0x08U, 0x6AU, 0x2BU,
    0x77U, 0x25U, 0x40U, 0x00U, 0x00U, 0x0AU, 0x85U, 0x01U, 0x03U, 0x86U,
    0x01U, 0x09U, 0x87U, 0x01U, 0x00U, 0x88U, 0x01U, 0x02U, 0x89U, 0x01U,
    0x01U, 0x8AU, 0x01U, 0x03U, 0xABU, 0x0AU, 0x83U, 0x01U, 0x01U, 0x85U,
    0x01U, 0xFDU, 0x8AU, 0x02U, 0x4FU, 0x4BU};

constexpr std::array<std::uint8_t, 112U> kExpectedFrame{
    0x01U, 0x0CU, 0xCDU, 0x01U, 0x00U, 0x01U, 0x02U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x01U, 0x81U, 0x00U, 0x80U, 0x64U, 0x88U, 0xB8U, 0x10U, 0x01U,
    0x00U, 0x5EU, 0x00U, 0x00U, 0x00U, 0x00U, 0x61U, 0x54U, 0x80U, 0x10U,
    0x4CU, 0x44U, 0x30U, 0x2FU, 0x4CU, 0x4CU, 0x4EU, 0x30U, 0x24U, 0x47U,
    0x4FU, 0x24U, 0x67U, 0x63U, 0x62U, 0x31U, 0x81U, 0x02U, 0x03U, 0xE8U,
    0x82U, 0x0CU, 0x4CU, 0x44U, 0x30U, 0x2FU, 0x4CU, 0x4CU, 0x4EU, 0x30U,
    0x24U, 0x44U, 0x53U, 0x31U, 0x83U, 0x06U, 0x47U, 0x4FU, 0x4FU, 0x53U,
    0x45U, 0x31U, 0x84U, 0x08U, 0x6AU, 0x2BU, 0x77U, 0x25U, 0x40U, 0x00U,
    0x00U, 0x0AU, 0x85U, 0x01U, 0x03U, 0x86U, 0x01U, 0x09U, 0x87U, 0x01U,
    0x00U, 0x88U, 0x01U, 0x02U, 0x89U, 0x01U, 0x01U, 0x8AU, 0x01U, 0x03U,
    0xABU, 0x0AU, 0x83U, 0x01U, 0x01U, 0x85U, 0x01U, 0xFDU, 0x8AU, 0x02U,
    0x4FU, 0x4BU};

[[nodiscard]] goose::GoosePdu make_pdu() {
    goose::GoosePdu pdu;
    pdu.go_cb_ref = "LD0/LLN0$GO$gcb1";
    pdu.time_allowed_to_live_milliseconds = 1'000U;
    pdu.data_set_reference = "LD0/LLN0$DS1";
    pdu.go_id = "GOOSE1";
    pdu.timestamp = mms::Iec61850UtcTime{
        std::chrono::system_clock::time_point{std::chrono::seconds{1'781'233'445}} +
            std::chrono::milliseconds{250},
        0x0AU};
    pdu.state_number = 3U;
    pdu.sequence_number = 9U;
    pdu.test = false;
    pdu.configuration_revision = 2U;
    pdu.needs_commissioning = true;
    pdu.values = {
        mms::MmsDataValue::boolean(true),
        mms::MmsDataValue::integer(-3),
        mms::MmsDataValue::visible_string("OK")};
    return pdu;
}

[[nodiscard]] goose::GooseFrame make_frame() {
    const std::array<std::uint8_t, 6U> destination{
        0x01U, 0x0CU, 0xCDU, 0x01U, 0x00U, 0x01U};
    const std::array<std::uint8_t, 6U> source{
        0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U};

    goose::GooseFrame frame;
    frame.destination = ethernet::MacAddress{destination};
    frame.source = ethernet::MacAddress{source};
    frame.vlan = ethernet::VlanTag{4U, 100U};
    frame.app_id = 0x1001U;
    frame.pdu = make_pdu();
    return frame;
}

[[nodiscard]] bool equals(
    const std::span<const std::uint8_t> left,
    const std::span<const std::uint8_t> right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    auto pdu = make_pdu();
    const auto pdu_size = goose::GoosePduCodec::encoded_size(pdu);
    if (!pdu_size || *pdu_size != kExpectedPdu.size()) {
        return 1;
    }

    std::array<std::uint8_t, 256U> pdu_buffer{};
    const auto pdu_result = goose::GoosePduCodec::encode_into(pdu, pdu_buffer);
    if (!pdu_result.success() || pdu_result.bytes_written != kExpectedPdu.size() ||
        !equals(
            std::span<const std::uint8_t>{pdu_buffer}.first(pdu_result.bytes_written),
            kExpectedPdu)) {
        return 2;
    }

    std::array<std::uint8_t, 16U> small{};
    const auto small_result = goose::GoosePduCodec::encode_into(pdu, small);
    if (small_result.status != wire::EncodeStatus::buffer_too_small ||
        small_result.required_bytes != kExpectedPdu.size()) {
        return 3;
    }

    auto frame = make_frame();
    const auto frame_size = goose::GooseFrameCodec::encoded_size(frame);
    if (!frame_size || *frame_size != kExpectedFrame.size()) {
        return 4;
    }

    std::array<std::uint8_t, 1'536U> frame_buffer{};
    const auto frame_result = goose::GooseFrameCodec::encode_into(frame, frame_buffer);
    if (!frame_result.success() || frame_result.bytes_written != kExpectedFrame.size() ||
        !equals(
            std::span<const std::uint8_t>{frame_buffer}.first(frame_result.bytes_written),
            kExpectedFrame)) {
        return 5;
    }

    const auto values_capacity = frame.pdu.values.capacity();
    const auto go_cb_capacity = frame.pdu.go_cb_ref.capacity();
    const auto dataset_capacity = frame.pdu.data_set_reference.capacity();
    const auto go_id_capacity = frame.pdu.go_id.capacity();

    // Simulate a sustained retransmission workload. Only scalar state/sequence
    // fields change; the bounded encoder must not grow configured storage.
    constexpr std::uint32_t retransmissions = 20'000U;
    for (std::uint32_t index = 0U; index < retransmissions; ++index) {
        frame.pdu.sequence_number = index;
        const auto result = goose::GooseFrameCodec::encode_into(frame, frame_buffer);
        if (!result.success() || result.bytes_written == 0U) {
            return 6;
        }
    }

    if (frame.pdu.values.capacity() != values_capacity ||
        frame.pdu.go_cb_ref.capacity() != go_cb_capacity ||
        frame.pdu.data_set_reference.capacity() != dataset_capacity ||
        frame.pdu.go_id.capacity() != go_id_capacity) {
        return 7;
    }

    return 0;
}
