// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/connection_runtime.hpp"
#include "ariec61850/mms/static_brcb_connection.hpp"
#include "ariec61850/mms/static_brcb_control.hpp"
#include "ariec61850/mms/static_brcb_runtime.hpp"
#include "ariec61850/mms/static_data_set_table.hpp"
#include "ariec61850/mms/static_object_table.hpp"
#include "ariec61850/osi/cotp_span.hpp"
#include "ariec61850/osi/presentation_span.hpp"
#include "ariec61850/osi/tpkt_span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

using namespace ar::iec61850;

constexpr std::array<std::uint8_t, 3U> kOctet80Type{0x89U, 0x01U, 0x50U};
constexpr std::array<std::uint8_t, 184U> kAssociationRequest{
    0x0DU,0xB6U,0x05U,0x06U,0x13U,0x01U,0x00U,0x16U,0x01U,0x02U,0x14U,0x02U,
    0x00U,0x02U,0x33U,0x02U,0x00U,0x01U,0x34U,0x02U,0x00U,0x01U,0xC1U,0xA0U,
    0x31U,0x81U,0x9DU,0xA0U,0x03U,0x80U,0x01U,0x01U,0xA2U,0x81U,0x95U,0x81U,
    0x04U,0x00U,0x00U,0x00U,0x01U,0x82U,0x04U,0x00U,0x00U,0x00U,0x01U,0xA4U,
    0x23U,0x30U,0x0FU,0x02U,0x01U,0x01U,0x06U,0x04U,0x52U,0x01U,0x00U,0x01U,
    0x30U,0x04U,0x06U,0x02U,0x51U,0x01U,0x30U,0x10U,0x02U,0x01U,0x03U,0x06U,
    0x05U,0x28U,0xCAU,0x22U,0x02U,0x01U,0x30U,0x04U,0x06U,0x02U,0x51U,0x01U,
    0x61U,0x62U,0x30U,0x60U,0x02U,0x01U,0x01U,0xA0U,0x5BU,0x60U,0x59U,0xA1U,
    0x07U,0x06U,0x05U,0x28U,0xCAU,0x22U,0x02U,0x03U,0xA2U,0x07U,0x06U,0x05U,
    0x29U,0x01U,0x87U,0x67U,0x01U,0xA3U,0x03U,0x02U,0x01U,0x0CU,0xA6U,0x06U,
    0x06U,0x04U,0x29U,0x01U,0x87U,0x67U,0xA7U,0x03U,0x02U,0x01U,0x0CU,0xBEU,
    0x33U,0x28U,0x31U,0x06U,0x02U,0x51U,0x01U,0x02U,0x01U,0x03U,0xA0U,0x28U,
    0xA8U,0x26U,0x80U,0x03U,0x00U,0xFDU,0xE8U,0x81U,0x01U,0x0AU,0x82U,0x01U,
    0x0AU,0x83U,0x01U,0x05U,0xA4U,0x16U,0x80U,0x01U,0x01U,0x81U,0x03U,0x05U,
    0xF1U,0x00U,0x82U,0x0CU,0x03U,0xEEU,0x1CU,0x00U,0x00U,0x04U,0x08U,0x00U,
    0x00U,0x79U,0xEFU,0x18U};

constexpr std::array<std::uint8_t, 41U> kReadRequest{
    0xA0U,0x27U,0x02U,0x01U,0x0CU,
    0xA4U,0x22U,0xA1U,0x20U,0xA0U,0x1EU,
    0x30U,0x0DU,0xA0U,0x0BU,0xA1U,0x09U,
    0x1AU,0x03U,0x4CU,0x44U,0x30U,
    0x1AU,0x02U,0x52U,0x31U,
    0x30U,0x0DU,0xA0U,0x0BU,0xA1U,0x09U,
    0x1AU,0x03U,0x4CU,0x44U,0x30U,
    0x1AU,0x02U,0x4DU,0x31U};

[[nodiscard]] wire::EncodeResult read_large_octets(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t payload_bytes = 80U;
    constexpr std::size_t required = payload_bytes + 2U;
    if (context == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    const auto& payload = *static_cast<const std::array<std::uint8_t, payload_bytes>*>(context);
    destination[0] = 0x89U;
    destination[1] = static_cast<std::uint8_t>(payload_bytes);
    for (std::size_t index = 0U; index < payload.size(); ++index) {
        destination[index + 2U] = payload[index];
    }
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] bool set_maximum_mms_pdu(
    std::array<std::uint8_t, kAssociationRequest.size()>& association,
    const std::uint32_t value) noexcept {
    if (value > 0x00FF'FFFFU) {
        return false;
    }
    constexpr std::array<std::uint8_t, 5U> marker{0x80U,0x03U,0x00U,0xFDU,0xE8U};
    for (std::size_t index = 0U; index + marker.size() <= association.size(); ++index) {
        bool match = true;
        for (std::size_t marker_index = 0U; marker_index < marker.size(); ++marker_index) {
            if (association[index + marker_index] != marker[marker_index]) {
                match = false;
                break;
            }
        }
        if (!match) {
            continue;
        }
        association[index + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
        association[index + 3U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
        association[index + 4U] = static_cast<std::uint8_t>(value & 0xFFU);
        return true;
    }
    return false;
}

template <std::size_t N>
[[nodiscard]] wire::EncodeResult build_data_tpkt(
    const std::span<const std::uint8_t> payload,
    std::array<std::uint8_t, N>& output,
    std::array<std::uint8_t, N>& scratch) noexcept {
    const auto cotp = osi::CotpSpanCodec::encode_data_into(payload, scratch);
    if (!cotp.success()) {
        return cotp;
    }
    return osi::TpktSpanCodec::encode_into(
        std::span<const std::uint8_t>{scratch}.first(cotp.bytes_written), output);
}

template <std::size_t N>
[[nodiscard]] wire::EncodeResult build_mms_tpkt(
    const std::span<const std::uint8_t> mms_pdu,
    std::array<std::uint8_t, N>& output,
    std::array<std::uint8_t, N>& presentation,
    std::array<std::uint8_t, N>& scratch) noexcept {
    const auto p_data = osi::PresentationSpanCodec::encode_p_data_into(
        mms_pdu, presentation, 3U, true);
    if (!p_data.success()) {
        return p_data;
    }
    return build_data_tpkt(
        std::span<const std::uint8_t>{presentation}.first(p_data.bytes_written),
        output,
        scratch);
}

[[nodiscard]] bool connect_cotp(
    mms::MmsStaticConnectionRuntime& runtime,
    const std::uint8_t tpdu_size_code,
    std::span<std::uint8_t> request,
    std::span<std::uint8_t> response,
    std::span<std::uint8_t> workspace,
    std::span<std::uint8_t> scratch) noexcept {
    const std::array<std::uint8_t, 1U> tpdu_size{tpdu_size_code};
    constexpr std::array<std::uint8_t, 2U> source_tsap{0x00U,0x01U};
    constexpr std::array<std::uint8_t, 2U> destination_tsap{0x00U,0x01U};
    const std::array<osi::CotpParameterView, 3U> parameters{
        osi::CotpParameterView{osi::CotpSpanCodec::tpdu_size_parameter, tpdu_size},
        osi::CotpParameterView{osi::CotpSpanCodec::source_tsap_parameter, source_tsap},
        osi::CotpParameterView{osi::CotpSpanCodec::destination_tsap_parameter, destination_tsap}};

    const auto cr = osi::CotpSpanCodec::encode_connection_request_into(
        0x0001U, parameters, scratch);
    if (!cr.success()) {
        return false;
    }
    const auto tpkt = osi::TpktSpanCodec::encode_into(
        scratch.first(cr.bytes_written), request);
    if (!tpkt.success()) {
        return false;
    }
    const auto result = runtime.process_tcp_window(
        request.first(tpkt.bytes_written), response, workspace);
    std::size_t expected{};
    return osi::CotpSpanCodec::try_tpdu_size_bytes(tpdu_size_code, expected) &&
        result.response_ready() &&
        runtime.state() == mms::MmsStaticConnectionState::awaiting_association &&
        runtime.negotiated_tpdu_size_bytes() == expected;
}

[[nodiscard]] mms::MmsStaticConnectionResult associate(
    mms::MmsStaticConnectionRuntime& runtime,
    const std::uint32_t maximum_mms_pdu,
    std::span<std::uint8_t> request,
    std::span<std::uint8_t> response,
    std::span<std::uint8_t> workspace,
    std::span<std::uint8_t> scratch) noexcept {
    auto association = kAssociationRequest;
    if (!set_maximum_mms_pdu(association, maximum_mms_pdu)) {
        return {};
    }
    const auto cotp = osi::CotpSpanCodec::encode_data_into(association, scratch);
    if (!cotp.success()) {
        return {};
    }
    const auto tpkt = osi::TpktSpanCodec::encode_into(
        scratch.first(cotp.bytes_written), request);
    if (!tpkt.success()) {
        return {};
    }
    return runtime.process_tcp_window(
        request.first(tpkt.bytes_written), response, workspace);
}

} // namespace

int main() {
    std::array<std::uint8_t, 80U> large_value{};
    for (std::size_t index = 0U; index < large_value.size(); ++index) {
        large_value[index] = static_cast<std::uint8_t>(index);
    }
    const std::array<mms::MmsStaticObjectEntry, 2U> objects{
        mms::MmsStaticObjectEntry{
            "LD0", "R1", kOctet80Type, read_large_octets, &large_value, false},
        mms::MmsStaticObjectEntry{
            "LD0", "M1", kOctet80Type, read_large_octets, &large_value, false}};
    const mms::MmsStaticObjectTable object_table{objects};
    const std::array<mms::MmsStaticDataSetMember, 1U> members{
        mms::MmsStaticDataSetMember{"LD0", "R1"}};
    const std::array<mms::MmsStaticDataSetEntry, 1U> data_sets{
        mms::MmsStaticDataSetEntry{"LD0", "LLN0$Events", members, false}};
    const mms::MmsStaticDataSetTable data_set_table{data_sets};
    const mms::MmsStaticApplicationDispatcher dispatcher{object_table, data_set_table};
    if (!object_table.valid() || !data_set_table.valid_against(object_table)) {
        return 1;
    }

    std::array<std::uint8_t, 2048U> request{};
    std::array<std::uint8_t, 2048U> response{};
    std::array<std::uint8_t, 2048U> workspace{};
    std::array<std::uint8_t, 2048U> scratch{};
    std::array<std::uint8_t, 2048U> presentation{};

    // A 128-byte TPDU cannot carry the fixed association-accept TSDU as one DT
    // TPDU. Until outbound COTP segmentation is implemented, reject it rather
    // than violate the negotiated limit.
    mms::MmsStaticConnectionRuntime tiny_tpdu{dispatcher};
    if (!connect_cotp(tiny_tpdu, 0x07U, request, response, workspace, scratch)) {
        return 2;
    }
    const auto tiny_association = associate(
        tiny_tpdu, 65'000U, request, response, workspace, scratch);
    if (tiny_association.status != mms::MmsStaticConnectionStatus::peer_limit_exceeded ||
        tiny_association.bytes_written != 0U ||
        tiny_tpdu.negotiated_tpdu_size_bytes() != 128U ||
        tiny_tpdu.state() != mms::MmsStaticConnectionState::awaiting_association) {
        return 3;
    }

    mms::MmsStaticConnectionPolicy policy;
    policy.association_id = 101U;
    policy.owner[0] = 0xAAU;
    policy.owner[1] = 0x01U;
    policy.owner_size = 2U;
    mms::MmsStaticConnectionRuntime runtime{dispatcher, policy};
    if (!connect_cotp(runtime, 0x09U, request, response, workspace, scratch)) {
        return 4;
    }
    const auto association = associate(runtime, 64U, request, response, workspace, scratch);
    if (!association.response_ready() ||
        runtime.state() != mms::MmsStaticConnectionState::established ||
        runtime.negotiated_tpdu_size_bytes() != 512U ||
        runtime.negotiated_mms_pdu_size() != 64U) {
        return 5;
    }

    // The two large Read results exceed the peer's 64-byte MMS PDU offer. No
    // oversized confirmed response may be placed on the wire.
    const auto read_tpkt = build_mms_tpkt(
        kReadRequest, request, presentation, scratch);
    if (!read_tpkt.success()) {
        return 6;
    }
    const auto read_result = runtime.process_tcp_window(
        std::span<const std::uint8_t>{request}.first(read_tpkt.bytes_written),
        response,
        workspace);
    if (read_result.status != mms::MmsStaticConnectionStatus::peer_limit_exceeded ||
        read_result.bytes_written != 0U ||
        read_result.consumed_bytes != read_tpkt.bytes_written ||
        runtime.state() != mms::MmsStaticConnectionState::established) {
        return 7;
    }

    // InformationReport has the same peer MMS/COTP bounds. Retain the entry and
    // cursor when delivery is refused so it can be retried on a compatible peer.
    const mms::MmsStaticBrcbDefinition definition{
        "LD0", "LLN0$BR$B1", "LD0/LLN0$BR$B1",
        "LD0", "LLN0$Events", 1U, {0x7FU,0x80U}, 0U, 0x70U};
    std::array<std::uint8_t, 1024U> slot_storage{};
    std::array<mms::MmsStaticBrcbSlot, 1U> slots{
        mms::MmsStaticBrcbSlot{slot_storage}};
    mms::MmsStaticBrcbPendingState pending{};
    mms::MmsStaticBrcbRuntime reports{
        definition, pending, slots, object_table, data_set_table};
    if (!reports.initialize()) {
        return 8;
    }
    mms::MmsStaticBrcbControl control{reports};
    mms::MmsStaticBrcbClientIdentity client;
    client.association_id = 101U;
    client.owner[0] = 0xAAU;
    client.owner[1] = 0x01U;
    client.owner_size = 2U;
    if (control.reserve(client, 0U, 100U) != mms::MmsStaticBrcbControlStatus::ok ||
        control.set_report_enabled(client, true, 100U) !=
            mms::MmsStaticBrcbControlStatus::ok ||
        reports.notify(0U, mms::MmsStaticBrcbEventReason::data_change, 100U) !=
            mms::MmsStaticBrcbStatus::ok) {
        return 9;
    }
    mms::MmsStaticBrcbCapturePlan plan;
    std::array<std::uint8_t, 2048U> encode_buffer{};
    std::array<std::uint8_t, 512U> capture_workspace{};
    constexpr std::array<std::uint8_t, 6U> report_time{0U,0U,0x12U,0x34U,0U,0x01U};
    if (!reports.next_due(100U, plan) ||
        !reports.capture(plan, report_time, encode_buffer, capture_workspace).success() ||
        reports.queue_size() != 1U) {
        return 10;
    }
    const auto report = mms::MmsStaticBrcbConnection::poll(
        runtime, control, reports, 100U, response, workspace);
    if (report.status != mms::MmsStaticBrcbConnectionStatus::peer_limit_exceeded ||
        report.bytes_written != 0U || reports.queue_size() != 1U) {
        return 11;
    }

    runtime.reset();
    if (runtime.negotiated_tpdu_size_bytes() != 0U ||
        runtime.negotiated_mms_pdu_size() != 0U) {
        return 12;
    }
    return 0;
}
