// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/connection_runtime.hpp"
#include "ariec61850/mms/services_span.hpp"
#include "ariec61850/mms/static_object_table.hpp"
#include "ariec61850/osi/cotp_span.hpp"
#include "ariec61850/osi/presentation_span.hpp"
#include "ariec61850/osi/session_span.hpp"
#include "ariec61850/osi/tpkt_span.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

using namespace ar::iec61850;

constexpr std::array<std::uint8_t, 2U> kBooleanType{0x83U, 0x00U};
constexpr std::array<std::uint8_t, 3U> kInt32Type{0x85U, 0x01U, 0x20U};

constexpr std::array<std::uint8_t, 184U> kAssociationRequest{
    0x0DU, 0xB6U, 0x05U, 0x06U, 0x13U, 0x01U, 0x00U, 0x16U, 0x01U, 0x02U, 0x14U, 0x02U,
    0x00U, 0x02U, 0x33U, 0x02U, 0x00U, 0x01U, 0x34U, 0x02U, 0x00U, 0x01U, 0xC1U, 0xA0U,
    0x31U, 0x81U, 0x9DU, 0xA0U, 0x03U, 0x80U, 0x01U, 0x01U, 0xA2U, 0x81U, 0x95U, 0x81U,
    0x04U, 0x00U, 0x00U, 0x00U, 0x01U, 0x82U, 0x04U, 0x00U, 0x00U, 0x00U, 0x01U, 0xA4U,
    0x23U, 0x30U, 0x0FU, 0x02U, 0x01U, 0x01U, 0x06U, 0x04U, 0x52U, 0x01U, 0x00U, 0x01U,
    0x30U, 0x04U, 0x06U, 0x02U, 0x51U, 0x01U, 0x30U, 0x10U, 0x02U, 0x01U, 0x03U, 0x06U,
    0x05U, 0x28U, 0xCAU, 0x22U, 0x02U, 0x01U, 0x30U, 0x04U, 0x06U, 0x02U, 0x51U, 0x01U,
    0x61U, 0x62U, 0x30U, 0x60U, 0x02U, 0x01U, 0x01U, 0xA0U, 0x5BU, 0x60U, 0x59U, 0xA1U,
    0x07U, 0x06U, 0x05U, 0x28U, 0xCAU, 0x22U, 0x02U, 0x03U, 0xA2U, 0x07U, 0x06U, 0x05U,
    0x29U, 0x01U, 0x87U, 0x67U, 0x01U, 0xA3U, 0x03U, 0x02U, 0x01U, 0x0CU, 0xA6U, 0x06U,
    0x06U, 0x04U, 0x29U, 0x01U, 0x87U, 0x67U, 0xA7U, 0x03U, 0x02U, 0x01U, 0x0CU, 0xBEU,
    0x33U, 0x28U, 0x31U, 0x06U, 0x02U, 0x51U, 0x01U, 0x02U, 0x01U, 0x03U, 0xA0U, 0x28U,
    0xA8U, 0x26U, 0x80U, 0x03U, 0x00U, 0xFDU, 0xE8U, 0x81U, 0x01U, 0x0AU, 0x82U, 0x01U,
    0x0AU, 0x83U, 0x01U, 0x05U, 0xA4U, 0x16U, 0x80U, 0x01U, 0x01U, 0x81U, 0x03U, 0x05U,
    0xF1U, 0x00U, 0x82U, 0x0CU, 0x03U, 0xEEU, 0x1CU, 0x00U, 0x00U, 0x04U, 0x08U, 0x00U,
    0x00U, 0x79U, 0xEFU, 0x18U};

constexpr std::array<std::uint8_t, 138U> kAssociationResponse{
    0x0EU, 0x88U, 0x05U, 0x06U, 0x13U, 0x01U, 0x00U, 0x16U, 0x01U, 0x02U, 0x14U, 0x02U,
    0x00U, 0x02U, 0x33U, 0x02U, 0x00U, 0x01U, 0x34U, 0x02U, 0x00U, 0x01U, 0xC1U, 0x72U,
    0x31U, 0x70U, 0xA0U, 0x03U, 0x80U, 0x01U, 0x01U, 0xA2U, 0x69U, 0xA5U, 0x12U, 0x30U,
    0x07U, 0x80U, 0x01U, 0x00U, 0x81U, 0x02U, 0x51U, 0x01U, 0x30U, 0x07U, 0x80U, 0x01U,
    0x00U, 0x81U, 0x02U, 0x51U, 0x01U, 0x61U, 0x53U, 0x30U, 0x51U, 0x02U, 0x01U, 0x01U,
    0xA0U, 0x4CU, 0x61U, 0x4AU, 0xA1U, 0x07U, 0x06U, 0x05U, 0x28U, 0xCAU, 0x22U, 0x02U,
    0x03U, 0xA2U, 0x03U, 0x02U, 0x01U, 0x00U, 0xA3U, 0x05U, 0xA1U, 0x03U, 0x02U, 0x01U,
    0x00U, 0xBEU, 0x33U, 0x28U, 0x31U, 0x06U, 0x02U, 0x51U, 0x01U, 0x02U, 0x01U, 0x03U,
    0xA0U, 0x28U, 0xA9U, 0x26U, 0x80U, 0x03U, 0x00U, 0xFDU, 0xE8U, 0x81U, 0x01U, 0x0AU,
    0x82U, 0x01U, 0x0AU, 0x83U, 0x01U, 0x05U, 0xA4U, 0x16U, 0x80U, 0x01U, 0x01U, 0x81U,
    0x03U, 0x05U, 0xF1U, 0x00U, 0x82U, 0x0CU, 0x03U, 0xEEU, 0x1CU, 0x00U, 0x00U, 0x04U,
    0x08U, 0x00U, 0x00U, 0x79U, 0xEFU, 0x18U};

constexpr std::array<std::uint8_t, 41U> kReadRequest{
    0xA0U, 0x27U, 0x02U, 0x01U, 0x0CU,
    0xA4U, 0x22U, 0xA1U, 0x20U, 0xA0U, 0x1EU,
    0x30U, 0x0DU, 0xA0U, 0x0BU, 0xA1U, 0x09U,
    0x1AU, 0x03U, 0x4CU, 0x44U, 0x30U,
    0x1AU, 0x02U, 0x52U, 0x31U,
    0x30U, 0x0DU, 0xA0U, 0x0BU, 0xA1U, 0x09U,
    0x1AU, 0x03U, 0x4CU, 0x44U, 0x30U,
    0x1AU, 0x02U, 0x4DU, 0x31U};

constexpr std::array<std::uint8_t, 15U> kReadResponse{
    0xA1U, 0x0DU, 0x02U, 0x01U, 0x0CU,
    0xA4U, 0x08U, 0xA1U, 0x06U,
    0x83U, 0x01U, 0xFFU,
    0x85U, 0x01U, 0x2AU};

constexpr std::array<std::uint8_t, 29U> kWriteRequest{
    0xA0U, 0x1BU, 0x02U, 0x01U, 0x0EU,
    0xA5U, 0x16U,
    0xA0U, 0x0FU,
    0x30U, 0x0DU, 0xA0U, 0x0BU, 0xA1U, 0x09U,
    0x1AU, 0x03U, 0x4CU, 0x44U, 0x30U,
    0x1AU, 0x02U, 0x52U, 0x31U,
    0xA0U, 0x03U, 0x83U, 0x01U, 0x00U};

constexpr std::array<std::uint8_t, 9U> kWriteResponse{
    0xA1U, 0x07U, 0x02U, 0x01U, 0x0EU,
    0xA5U, 0x02U, 0x81U, 0x00U};

constexpr std::array<std::uint8_t, 2U> kConcludeRequest{0x8BU, 0x00U};
constexpr std::array<std::uint8_t, 2U> kConcludeResponse{0x8CU, 0x00U};

template <std::size_t N>
[[nodiscard]] bool matches(
    const std::span<const std::uint8_t> actual,
    const std::array<std::uint8_t, N>& expected) noexcept {
    return actual.size() == expected.size() &&
        std::equal(actual.begin(), actual.end(), expected.begin());
}

[[nodiscard]] wire::EncodeResult read_boolean(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 3U;
    if (context == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    const auto value = *static_cast<const bool*>(context);
    destination[0] = 0x83U;
    destination[1] = 0x01U;
    destination[2] = value ? 0xFFU : 0x00U;
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] wire::EncodeResult read_small_int32(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 3U;
    if (context == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    const auto value = *static_cast<const std::uint8_t*>(context);
    if (value > 0x7FU) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    destination[0] = 0x85U;
    destination[1] = 0x01U;
    destination[2] = value;
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] mms::MmsStaticWriteResult write_boolean(
    void* context,
    const std::span<const std::uint8_t> encoded_data) noexcept {
    if (context == nullptr || encoded_data.size() != 3U ||
        encoded_data[0] != 0x83U || encoded_data[1] != 0x01U ||
        (encoded_data[2] != 0x00U && encoded_data[2] != 0xFFU)) {
        return {false, 7U};
    }
    *static_cast<bool*>(context) = encoded_data[2] != 0x00U;
    return {true, 0U};
}

template <std::size_t N>
[[nodiscard]] wire::EncodeResult build_data_tpkt(
    const std::span<const std::uint8_t> session_payload,
    std::array<std::uint8_t, N>& output,
    std::array<std::uint8_t, N>& scratch,
    const bool eot = true) noexcept {
    const auto cotp = osi::CotpSpanCodec::encode_data_into(
        session_payload, scratch, eot, 0U);
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
    std::array<std::uint8_t, N>& scratch,
    const bool eot = true) noexcept {
    const auto p_data = osi::PresentationSpanCodec::encode_p_data_into(
        mms_pdu, presentation, 3U, true);
    if (!p_data.success()) {
        return p_data;
    }
    return build_data_tpkt(
        std::span<const std::uint8_t>{presentation}.first(p_data.bytes_written),
        output,
        scratch,
        eot);
}

[[nodiscard]] bool extract_cotp(
    const std::span<const std::uint8_t> frame,
    osi::CotpTpduView& cotp) noexcept {
    osi::TpktFrameView tpkt;
    return osi::TpktSpanCodec::try_decode_view(frame, tpkt) &&
        osi::CotpSpanCodec::try_decode_view(tpkt.payload, cotp);
}

[[nodiscard]] bool extract_mms(
    const std::span<const std::uint8_t> frame,
    osi::PresentationPdvView& pdv) noexcept {
    osi::CotpTpduView cotp;
    osi::SessionDataTransferView session;
    return extract_cotp(frame, cotp) &&
        cotp.kind == osi::CotpWireKind::data &&
        osi::SessionSpanCodec::try_decode_data_transfer_view(cotp.user_data, session) &&
        osi::PresentationSpanCodec::try_decode_fully_encoded_data_view(
            session.presentation_payload, pdv);
}

} // namespace

int main() {
    bool relay_state = true;
    const std::uint8_t meter_value = 42U;
    const std::array<mms::MmsStaticObjectEntry, 2U> objects{
        mms::MmsStaticObjectEntry{
            "LD0", "R1", kBooleanType,
            read_boolean, &relay_state, false,
            write_boolean, &relay_state},
        mms::MmsStaticObjectEntry{
            "LD0", "M1", kInt32Type,
            read_small_int32, &meter_value, false}};
    const mms::MmsStaticObjectTable table{objects};
    const mms::MmsStaticApplicationDispatcher dispatcher{table};
    mms::MmsStaticConnectionRuntime runtime{dispatcher};
    if (!table.valid() ||
        runtime.state() != mms::MmsStaticConnectionState::awaiting_cotp_connect) {
        return 1;
    }

    constexpr std::array<std::uint8_t, 1U> tpdu_size{0x0AU};
    constexpr std::array<std::uint8_t, 2U> source_tsap{0x00U, 0x01U};
    constexpr std::array<std::uint8_t, 2U> destination_tsap{0x00U, 0x01U};
    const std::array<osi::CotpParameterView, 3U> parameters{
        osi::CotpParameterView{osi::CotpSpanCodec::tpdu_size_parameter, tpdu_size},
        osi::CotpParameterView{osi::CotpSpanCodec::source_tsap_parameter, source_tsap},
        osi::CotpParameterView{osi::CotpSpanCodec::destination_tsap_parameter, destination_tsap}};

    std::array<std::uint8_t, 1024U> request{};
    std::array<std::uint8_t, 1024U> response{};
    std::array<std::uint8_t, 1024U> workspace{};
    std::array<std::uint8_t, 1024U> scratch{};
    std::array<std::uint8_t, 1024U> presentation{};

    const auto cr = osi::CotpSpanCodec::encode_connection_request_into(
        0x0001U, parameters, scratch);
    const auto cr_tpkt = cr.success()
        ? osi::TpktSpanCodec::encode_into(
            std::span<const std::uint8_t>{scratch}.first(cr.bytes_written), request)
        : wire::EncodeResult{};
    if (!cr.success() || !cr_tpkt.success()) {
        return 2;
    }

    auto result = runtime.process_tcp_window(
        std::span<const std::uint8_t>{request}.first(3U), response, workspace);
    if (result.status != mms::MmsStaticConnectionStatus::need_more ||
        result.consumed_bytes != 0U ||
        runtime.state() != mms::MmsStaticConnectionState::awaiting_cotp_connect) {
        return 3;
    }

    result = runtime.process_tcp_window(
        std::span<const std::uint8_t>{request}.first(cr_tpkt.bytes_written),
        response,
        workspace);
    osi::CotpTpduView cc;
    if (!result.response_ready() || result.consumed_bytes != cr_tpkt.bytes_written ||
        runtime.state() != mms::MmsStaticConnectionState::awaiting_association ||
        !extract_cotp(
            std::span<const std::uint8_t>{response}.first(result.bytes_written), cc) ||
        cc.kind != osi::CotpWireKind::connection_confirm ||
        cc.destination_reference != 0x0001U || cc.source_reference != 0x1001U) {
        return 4;
    }

    const auto association_tpkt = build_data_tpkt(
        kAssociationRequest, request, scratch);
    result = runtime.process_tcp_window(
        std::span<const std::uint8_t>{request}.first(association_tpkt.bytes_written),
        response,
        workspace);
    osi::CotpTpduView association_response;
    if (!association_tpkt.success() || !result.response_ready() ||
        runtime.state() != mms::MmsStaticConnectionState::established ||
        runtime.mms_presentation_context_id() != 3U ||
        !extract_cotp(
            std::span<const std::uint8_t>{response}.first(result.bytes_written),
            association_response) ||
        !matches(association_response.user_data, kAssociationResponse)) {
        return 5;
    }

    const auto read_tpkt = build_mms_tpkt(
        kReadRequest, request, presentation, scratch);
    if (!read_tpkt.success()) {
        return 6;
    }

    std::array<std::uint8_t, 2048U> coalesced{};
    std::copy_n(request.begin(), read_tpkt.bytes_written, coalesced.begin());
    std::copy_n(
        request.begin(),
        read_tpkt.bytes_written,
        coalesced.begin() + static_cast<std::ptrdiff_t>(read_tpkt.bytes_written));
    const auto coalesced_bytes = read_tpkt.bytes_written * 2U;

    result = runtime.process_tcp_window(
        std::span<const std::uint8_t>{coalesced}.first(coalesced_bytes),
        response,
        workspace);
    osi::PresentationPdvView pdv;
    if (!result.response_ready() || result.consumed_bytes != read_tpkt.bytes_written ||
        result.application_service != mms::MmsWireConfirmedService::read ||
        !extract_mms(
            std::span<const std::uint8_t>{response}.first(result.bytes_written), pdv) ||
        pdv.context_id != 3U || !matches(pdv.single_asn1_type, kReadResponse)) {
        return 7;
    }

    const auto second_window = std::span<const std::uint8_t>{coalesced}
        .subspan(result.consumed_bytes, read_tpkt.bytes_written);
    result = runtime.process_tcp_window(second_window, response, workspace);
    if (!result.response_ready() || result.consumed_bytes != read_tpkt.bytes_written ||
        !extract_mms(
            std::span<const std::uint8_t>{response}.first(result.bytes_written), pdv) ||
        !matches(pdv.single_asn1_type, kReadResponse)) {
        return 8;
    }

    for (std::uint32_t iteration = 0U; iteration < 10'000U; ++iteration) {
        result = runtime.process_tcp_window(
            std::span<const std::uint8_t>{request}.first(read_tpkt.bytes_written),
            response,
            workspace);
        if (!result.response_ready() ||
            result.application_service != mms::MmsWireConfirmedService::read ||
            result.invoke_id != 12U) {
            return 9;
        }
    }

    std::array<std::uint8_t, 64U> identify_mms{};
    const auto identify = mms::MmsPduSpanCodec::encode_confirmed_request_into(
        21U,
        static_cast<std::int32_t>(mms::MmsWireConfirmedService::identify),
        false,
        {},
        identify_mms);
    const auto identify_tpkt = identify.success()
        ? build_mms_tpkt(
            std::span<const std::uint8_t>{identify_mms}.first(identify.bytes_written),
            request,
            presentation,
            scratch)
        : wire::EncodeResult{};
    if (!identify.success() || !identify_tpkt.success()) {
        return 10;
    }
    result = runtime.process_tcp_window(
        std::span<const std::uint8_t>{request}.first(identify_tpkt.bytes_written),
        response,
        workspace);
    mms::MmsConfirmedPduView identify_response;
    if (!result.response_ready() ||
        result.application_service != mms::MmsWireConfirmedService::identify ||
        result.invoke_id != 21U || result.consumed_bytes != identify_tpkt.bytes_written ||
        runtime.state() != mms::MmsStaticConnectionState::established ||
        !extract_mms(
            std::span<const std::uint8_t>{response}.first(result.bytes_written), pdv) ||
        !mms::MmsPduSpanCodec::try_decode_confirmed_response_view(
            pdv.single_asn1_type, identify_response) ||
        identify_response.invoke_id != 21U ||
        identify_response.service() != mms::MmsWireConfirmedService::identify) {
        return 11;
    }

    const auto conclude_tpkt = build_mms_tpkt(
        kConcludeRequest, request, presentation, scratch);
    if (!conclude_tpkt.success()) {
        return 18;
    }
    result = runtime.process_tcp_window(
        std::span<const std::uint8_t>{request}.first(conclude_tpkt.bytes_written),
        response,
        workspace);
    if (!result.response_ready() ||
        runtime.state() != mms::MmsStaticConnectionState::established ||
        !extract_mms(
            std::span<const std::uint8_t>{response}.first(result.bytes_written), pdv) ||
        !matches(pdv.single_asn1_type, kConcludeResponse)) {
        return 19;
    }

    const auto write_tpkt = build_mms_tpkt(
        kWriteRequest, request, presentation, scratch);
    if (!write_tpkt.success() || !relay_state) {
        return 12;
    }
    std::array<std::uint8_t, 16U> small_response{};
    result = runtime.process_tcp_window(
        std::span<const std::uint8_t>{request}.first(write_tpkt.bytes_written),
        small_response,
        workspace);
    if (result.status != mms::MmsStaticConnectionStatus::response_buffer_too_small ||
        result.consumed_bytes != 0U || result.required_response_bytes <= small_response.size() ||
        !relay_state || runtime.state() != mms::MmsStaticConnectionState::established) {
        return 13;
    }

    result = runtime.process_tcp_window(
        std::span<const std::uint8_t>{request}.first(write_tpkt.bytes_written),
        response,
        workspace);
    if (!result.response_ready() || relay_state ||
        result.application_service != mms::MmsWireConfirmedService::write ||
        !extract_mms(
            std::span<const std::uint8_t>{response}.first(result.bytes_written), pdv) ||
        !matches(pdv.single_asn1_type, kWriteResponse)) {
        return 14;
    }

    const auto segmented_tpkt = build_mms_tpkt(
        kReadRequest, request, presentation, scratch, false);
    result = runtime.process_tcp_window(
        std::span<const std::uint8_t>{request}.first(segmented_tpkt.bytes_written),
        response,
        workspace);
    if (!segmented_tpkt.success() ||
        result.status != mms::MmsStaticConnectionStatus::protocol_violation ||
        runtime.state() != mms::MmsStaticConnectionState::fault) {
        return 15;
    }

    runtime.reset();
    if (runtime.state() != mms::MmsStaticConnectionState::awaiting_cotp_connect ||
        runtime.mms_presentation_context_id() != 0U) {
        return 16;
    }

    mms::MmsStaticConnectionRuntime wrong_order{dispatcher};
    const auto out_of_order = build_mms_tpkt(
        kReadRequest, request, presentation, scratch);
    result = wrong_order.process_tcp_window(
        std::span<const std::uint8_t>{request}.first(out_of_order.bytes_written),
        response,
        workspace);
    if (!out_of_order.success() ||
        result.status != mms::MmsStaticConnectionStatus::protocol_violation ||
        wrong_order.state() != mms::MmsStaticConnectionState::fault) {
        return 17;
    }

    return 0;
}
