// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/asn1/ber_span_reader.hpp"
#include "ariec61850/mms/information_report_span.hpp"
#include "ariec61850/mms/static_report_connection.hpp"
#include "ariec61850/mms/static_data_set_table.hpp"
#include "ariec61850/mms/static_object_table.hpp"
#include "ariec61850/osi/cotp_span.hpp"
#include "ariec61850/osi/presentation_span.hpp"
#include "ariec61850/osi/session_span.hpp"
#include "ariec61850/osi/tpkt_span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

using namespace ar::iec61850;

constexpr std::array<std::uint8_t, 2U> kBooleanType{0x83U, 0x00U};
constexpr std::array<std::uint8_t, 6U> kReportTime{
    0x00U, 0x00U, 0x12U, 0x34U, 0x00U, 0x01U};

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
    destination[0] = 0x83U;
    destination[1] = 0x01U;
    destination[2] = *static_cast<const bool*>(context) ? 0xFFU : 0x00U;
    return {wire::EncodeStatus::ok, required, required};
}

template <std::size_t N>
[[nodiscard]] wire::EncodeResult build_data_tpkt(
    const std::span<const std::uint8_t> session_payload,
    std::array<std::uint8_t, N>& output,
    std::array<std::uint8_t, N>& scratch) noexcept {
    const auto cotp = osi::CotpSpanCodec::encode_data_into(session_payload, scratch);
    if (!cotp.success()) {
        return cotp;
    }
    return osi::TpktSpanCodec::encode_into(
        std::span<const std::uint8_t>{scratch}.first(cotp.bytes_written), output);
}

[[nodiscard]] bool establish(
    mms::MmsStaticConnectionRuntime& connection,
    const std::span<std::uint8_t> request,
    const std::span<std::uint8_t> response,
    const std::span<std::uint8_t> workspace,
    const std::span<std::uint8_t> scratch) noexcept {
    constexpr std::array<std::uint8_t, 1U> tpdu_size{0x0AU};
    constexpr std::array<std::uint8_t, 2U> source_tsap{0x00U, 0x01U};
    constexpr std::array<std::uint8_t, 2U> destination_tsap{0x00U, 0x01U};
    const std::array<osi::CotpParameterView, 3U> parameters{
        osi::CotpParameterView{osi::CotpSpanCodec::tpdu_size_parameter, tpdu_size},
        osi::CotpParameterView{osi::CotpSpanCodec::source_tsap_parameter, source_tsap},
        osi::CotpParameterView{osi::CotpSpanCodec::destination_tsap_parameter, destination_tsap}};

    const auto cr = osi::CotpSpanCodec::encode_connection_request_into(
        0x0001U, parameters, scratch);
    if (!cr.success()) {
        return false;
    }
    const auto cr_tpkt = osi::TpktSpanCodec::encode_into(
        std::span<const std::uint8_t>{scratch}.first(cr.bytes_written), request);
    if (!cr_tpkt.success()) {
        return false;
    }
    auto result = connection.process_tcp_window(
        std::span<const std::uint8_t>{request.data(), cr_tpkt.bytes_written},
        response,
        workspace);
    if (!result.response_ready() ||
        connection.state() != mms::MmsStaticConnectionState::awaiting_association) {
        return false;
    }

    const auto cotp = osi::CotpSpanCodec::encode_data_into(kAssociationRequest, scratch);
    if (!cotp.success()) {
        return false;
    }
    const auto association = osi::TpktSpanCodec::encode_into(
        std::span<const std::uint8_t>{scratch}.first(cotp.bytes_written), request);
    if (!association.success()) {
        return false;
    }
    result = connection.process_tcp_window(
        std::span<const std::uint8_t>{request.data(), association.bytes_written},
        response,
        workspace);
    return result.response_ready() &&
        connection.state() == mms::MmsStaticConnectionState::established &&
        connection.mms_presentation_context_id() == 3U;
}

[[nodiscard]] bool decode_report_frame(
    const std::span<const std::uint8_t> frame,
    mms::MmsInformationReportView& report) noexcept {
    osi::TpktFrameView tpkt;
    osi::CotpTpduView cotp;
    osi::SessionDataTransferView session;
    osi::PresentationPdvView pdv;
    return osi::TpktSpanCodec::try_decode_view(frame, tpkt) &&
        osi::CotpSpanCodec::try_decode_view(tpkt.payload, cotp) &&
        cotp.kind == osi::CotpWireKind::data && cotp.end_of_transmission &&
        osi::SessionSpanCodec::try_decode_data_transfer_view(cotp.user_data, session) &&
        osi::PresentationSpanCodec::try_decode_fully_encoded_data_view(
            session.presentation_payload, pdv) &&
        pdv.context_id == 3U &&
        mms::MmsInformationReportSpanCodec::try_decode_information_report(
            pdv.single_asn1_type, report);
}

[[nodiscard]] bool decode_unsigned_item(
    const mms::MmsReadAccessResultView& item,
    std::uint32_t& value) noexcept {
    value = 0U;
    if (!item.success) {
        return false;
    }
    asn1::BerTlvView tlv;
    if (!asn1::BerSpanReader::try_read_exact(item.encoded_data, tlv) ||
        tlv.tag_class != asn1::BerClass::context_specific ||
        tlv.tag_number != 6 || tlv.constructed) {
        return false;
    }
    const auto decoded = asn1::BerSpanReader::read_uint32(tlv);
    if (!decoded) {
        return false;
    }
    value = *decoded;
    return true;
}

} // namespace

int main() {
    bool relay_state = true;
    bool alarm_state = false;
    const std::array<mms::MmsStaticObjectEntry, 2U> objects{
        mms::MmsStaticObjectEntry{
            "LD0", "R1", kBooleanType, read_boolean, &relay_state, false},
        mms::MmsStaticObjectEntry{
            "LD0", "A1", kBooleanType, read_boolean, &alarm_state, false}};
    const mms::MmsStaticObjectTable object_table{objects};
    const std::array<mms::MmsStaticDataSetMember, 2U> members{
        mms::MmsStaticDataSetMember{"LD0", "R1"},
        mms::MmsStaticDataSetMember{"LD0", "A1"}};
    const std::array<mms::MmsStaticDataSetEntry, 1U> data_set_entries{
        mms::MmsStaticDataSetEntry{"LD0", "LLN0$Events", members, false}};
    const mms::MmsStaticDataSetTable data_sets{data_set_entries};
    const mms::MmsStaticApplicationDispatcher dispatcher{object_table, data_sets};
    mms::MmsStaticConnectionRuntime connection{dispatcher};

    const std::array<mms::MmsStaticUrcbDefinition, 1U> definitions{
        mms::MmsStaticUrcbDefinition{
            "LD0",
            "LLN0$RP$Events",
            "LD0/LLN0$RP$Events",
            "LD0",
            "LLN0$Events",
            7U,
            {0x7CU, 0x80U},
            0U,
            0x08U,
            1'000U}};
    std::array<mms::MmsStaticUrcbState, 1U> states{};
    mms::MmsStaticUrcbRuntime reports{
        definitions, states, object_table, data_sets};
    if (!reports.initialize()) {
        return 1;
    }

    std::array<std::uint8_t, 2048U> request{};
    std::array<std::uint8_t, 2048U> response{};
    std::array<std::uint8_t, 2048U> workspace{};
    std::array<std::uint8_t, 2048U> scratch{};

    auto poll = mms::MmsStaticReportConnection::poll(
        connection, reports, 100U, kReportTime, response, workspace);
    if (poll.status != mms::MmsStaticReportConnectionStatus::not_established) {
        return 2;
    }
    if (!establish(connection, request, response, workspace, scratch)) {
        return 3;
    }
    if (mms::MmsStaticReportConnection::poll(
            connection, reports, 100U, kReportTime, response, workspace).status !=
        mms::MmsStaticReportConnectionStatus::no_report_due) {
        return 4;
    }

    if (reports.set_enabled(0U, true, 100U) != mms::MmsStaticUrcbStatus::ok ||
        reports.request_general_interrogation(0U) != mms::MmsStaticUrcbStatus::ok) {
        return 5;
    }
    const auto* state = reports.state(0U);
    if (state == nullptr || state->sequence_number != 0U ||
        !state->general_interrogation_pending) {
        return 6;
    }

    std::array<std::uint8_t, 8U> tiny_response{};
    poll = mms::MmsStaticReportConnection::poll(
        connection, reports, 100U, kReportTime, tiny_response, workspace);
    state = reports.state(0U);
    if (poll.status != mms::MmsStaticReportConnectionStatus::response_buffer_too_small ||
        poll.required_response_bytes <= tiny_response.size() ||
        poll.reason != mms::MmsStaticUrcbReportReason::general_interrogation ||
        poll.sequence_number != 1U || state == nullptr || state->sequence_number != 0U ||
        !state->general_interrogation_pending) {
        return 7;
    }

    poll = mms::MmsStaticReportConnection::poll(
        connection, reports, 100U, kReportTime, response, workspace);
    state = reports.state(0U);
    if (!poll.response_ready() || poll.bytes_written == 0U ||
        poll.control_block_index != 0U || poll.sequence_number != 1U ||
        poll.reason != mms::MmsStaticUrcbReportReason::general_interrogation ||
        state == nullptr || state->sequence_number != 1U ||
        state->general_interrogation_pending) {
        return 8;
    }

    mms::MmsInformationReportView report;
    if (!decode_report_frame(
            std::span<const std::uint8_t>{response}.first(poll.bytes_written), report) ||
        report.item_count != 13U) {
        return 9;
    }
    mms::MmsReadAccessResultView item;
    std::uint32_t sequence = 0U;
    if (!report.try_item(2U, item) || !decode_unsigned_item(item, sequence) || sequence != 1U) {
        return 10;
    }

    poll = mms::MmsStaticReportConnection::poll(
        connection, reports, 100U, kReportTime, response, workspace);
    if (poll.status != mms::MmsStaticReportConnectionStatus::no_report_due) {
        return 11;
    }

    // Integrity is due at t=1100. A workspace failure must preserve SqNum and due state.
    std::array<std::uint8_t, 2U> tiny_workspace{};
    poll = mms::MmsStaticReportConnection::poll(
        connection, reports, 1'100U, kReportTime, response, tiny_workspace);
    state = reports.state(0U);
    if (poll.status != mms::MmsStaticReportConnectionStatus::workspace_too_small ||
        poll.required_workspace_bytes <= tiny_workspace.size() ||
        poll.reason != mms::MmsStaticUrcbReportReason::integrity ||
        poll.sequence_number != 2U || state == nullptr || state->sequence_number != 1U ||
        state->next_integrity_due_ms != 1'100U) {
        return 12;
    }

    poll = mms::MmsStaticReportConnection::poll(
        connection, reports, 1'100U, kReportTime, response, workspace);
    state = reports.state(0U);
    if (!poll.response_ready() || poll.sequence_number != 2U ||
        poll.reason != mms::MmsStaticUrcbReportReason::integrity ||
        state == nullptr || state->sequence_number != 2U ||
        state->next_integrity_due_ms != 2'100U ||
        !decode_report_frame(
            std::span<const std::uint8_t>{response}.first(poll.bytes_written), report) ||
        !report.try_item(2U, item) || !decode_unsigned_item(item, sequence) || sequence != 2U) {
        return 13;
    }

    connection.reset();
    poll = mms::MmsStaticReportConnection::poll(
        connection, reports, 2'100U, kReportTime, response, workspace);
    state = reports.state(0U);
    if (poll.status != mms::MmsStaticReportConnectionStatus::not_established ||
        state == nullptr || state->sequence_number != 2U ||
        state->next_integrity_due_ms != 2'100U) {
        return 14;
    }

    return 0;
}
