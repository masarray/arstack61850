// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/asn1/ber_span_reader.hpp"
#include "ariec61850/mms/static_urcb_event_connection.hpp"
#include "ariec61850/mms/static_dispatcher.hpp"
#include "ariec61850/osi/cotp_span.hpp"
#include "ariec61850/osi/presentation_span.hpp"
#include "ariec61850/osi/session_span.hpp"
#include "ariec61850/osi/tpkt_span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace {

using namespace ar::iec61850;

constexpr std::array<std::uint8_t, 2U> kBooleanType{0x83U, 0x00U};
constexpr std::array<std::uint8_t, 6U> kReportTime{
    0x00U, 0x00U, 0x12U, 0x34U, 0x00U, 0x01U};

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

[[nodiscard]] bool establish(
    mms::MmsStaticConnectionRuntime& connection,
    std::span<std::uint8_t> request,
    std::span<std::uint8_t> response,
    std::span<std::uint8_t> workspace,
    std::span<std::uint8_t> scratch) noexcept {
    constexpr std::array<std::uint8_t, 1U> tpdu_size{0x0AU};
    constexpr std::array<std::uint8_t, 2U> source_tsap{0x00U, 0x01U};
    constexpr std::array<std::uint8_t, 2U> destination_tsap{0x00U, 0x01U};
    const std::array<osi::CotpParameterView, 3U> parameters{
        osi::CotpParameterView{osi::CotpSpanCodec::tpdu_size_parameter, tpdu_size},
        osi::CotpParameterView{osi::CotpSpanCodec::source_tsap_parameter, source_tsap},
        osi::CotpParameterView{osi::CotpSpanCodec::destination_tsap_parameter, destination_tsap}};

    const auto cr = osi::CotpSpanCodec::encode_connection_request_into(
        0x0001U, parameters, scratch);
    if (!cr.success()) return false;
    const auto cr_tpkt = osi::TpktSpanCodec::encode_into(
        scratch.first(cr.bytes_written), request);
    if (!cr_tpkt.success()) return false;
    auto result = connection.process_tcp_window(
        request.first(cr_tpkt.bytes_written), response, workspace);
    if (!result.response_ready() ||
        connection.state() != mms::MmsStaticConnectionState::awaiting_association) {
        return false;
    }

    const auto cotp = osi::CotpSpanCodec::encode_data_into(kAssociationRequest, scratch);
    if (!cotp.success()) return false;
    const auto association = osi::TpktSpanCodec::encode_into(
        scratch.first(cotp.bytes_written), request);
    if (!association.success()) return false;
    result = connection.process_tcp_window(
        request.first(association.bytes_written), response, workspace);
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

[[nodiscard]] bool decode_bit_string(
    const mms::MmsReadAccessResultView& result,
    const std::uint8_t unused,
    const std::uint8_t value) noexcept {
    if (!result.success) return false;
    asn1::BerTlvView tlv;
    return asn1::BerSpanReader::try_read_exact(result.encoded_data, tlv) &&
        tlv.tag_class == asn1::BerClass::context_specific &&
        tlv.tag_number == 4 && !tlv.constructed && tlv.value.size() == 2U &&
        tlv.value[0] == unused && tlv.value[1] == value;
}

} // namespace

int main() {
    bool first = true;
    bool second = false;
    bool third = true;
    const std::array<mms::MmsStaticObjectEntry, 3U> objects{
        mms::MmsStaticObjectEntry{"LD0","X1",kBooleanType,read_boolean,&first,false},
        mms::MmsStaticObjectEntry{"LD0","X2",kBooleanType,read_boolean,&second,false},
        mms::MmsStaticObjectEntry{"LD0","X3",kBooleanType,read_boolean,&third,false}};
    const mms::MmsStaticObjectTable object_table{objects};
    const std::array<mms::MmsStaticDataSetMember, 3U> members{
        mms::MmsStaticDataSetMember{"LD0","X1"},
        mms::MmsStaticDataSetMember{"LD0","X2"},
        mms::MmsStaticDataSetMember{"LD0","X3"}};
    const std::array<mms::MmsStaticDataSetEntry,1U> data_sets{
        mms::MmsStaticDataSetEntry{"LD0","LLN0$Events",members,false}};
    const mms::MmsStaticDataSetTable data_set_table{data_sets};
    const mms::MmsStaticApplicationDispatcher dispatcher{object_table,data_set_table};
    mms::MmsStaticConnectionRuntime connection{dispatcher};

    const std::array<mms::MmsStaticUrcbDefinition,1U> definitions{
        mms::MmsStaticUrcbDefinition{
            "LD0","LLN0$RP$Events","LD0/LLN0$RP$Events",
            "LD0","LLN0$Events",9U,{0x7CU,0x80U},20U,0x70U,0U}};
    std::array<mms::MmsStaticUrcbState,1U> urcb_states{};
    mms::MmsStaticUrcbRuntime urcb{definitions,urcb_states,object_table,data_set_table};
    if (!urcb.initialize()) return 1;
    std::array<mms::MmsStaticUrcbEventState,1U> event_states{};
    mms::MmsStaticUrcbEventRuntime events{urcb,event_states,object_table,data_set_table};
    if (!events.initialize() || urcb.set_enabled(0U,true,100U) != mms::MmsStaticUrcbStatus::ok) {
        return 2;
    }
    if (events.notify(0U,0U,mms::MmsStaticUrcbEventReason::data_change,100U) !=
            mms::MmsStaticUrcbEventStatus::ok ||
        events.notify(0U,2U,mms::MmsStaticUrcbEventReason::quality_change,105U) !=
            mms::MmsStaticUrcbEventStatus::ok) {
        return 3;
    }

    std::array<std::uint8_t,2048U> request{};
    std::array<std::uint8_t,2048U> response{};
    std::array<std::uint8_t,2048U> workspace{};
    std::array<std::uint8_t,2048U> scratch{};
    auto poll = mms::MmsStaticUrcbEventConnection::poll(
        connection,events,120U,kReportTime,response,workspace);
    if (poll.status != mms::MmsStaticUrcbEventConnectionStatus::not_established) return 4;
    const auto* pending = events.state(0U);
    if (pending == nullptr || !pending->pending || pending->pending_member_count != 2U) return 5;

    if (!establish(connection,request,response,workspace,scratch)) return 6;

    std::array<std::uint8_t,8U> tiny_response{};
    poll = mms::MmsStaticUrcbEventConnection::poll(
        connection,events,120U,kReportTime,tiny_response,workspace);
    pending = events.state(0U);
    const auto* control = urcb.state(0U);
    if (poll.status != mms::MmsStaticUrcbEventConnectionStatus::response_buffer_too_small ||
        poll.required_response_bytes <= tiny_response.size() ||
        poll.sequence_number != 1U || pending == nullptr || !pending->pending ||
        control == nullptr || control->sequence_number != 0U) {
        return 7;
    }

    std::array<std::uint8_t,2U> tiny_workspace{};
    poll = mms::MmsStaticUrcbEventConnection::poll(
        connection,events,120U,kReportTime,response,tiny_workspace);
    pending = events.state(0U);
    control = urcb.state(0U);
    if (poll.status != mms::MmsStaticUrcbEventConnectionStatus::workspace_too_small ||
        poll.required_workspace_bytes <= tiny_workspace.size() ||
        pending == nullptr || !pending->pending || control == nullptr ||
        control->sequence_number != 0U) {
        return 8;
    }

    poll = mms::MmsStaticUrcbEventConnection::poll(
        connection,events,120U,kReportTime,response,workspace);
    pending = events.state(0U);
    control = urcb.state(0U);
    if (!poll.response_ready() || poll.bytes_written == 0U || poll.sequence_number != 1U ||
        pending == nullptr || pending->pending || control == nullptr ||
        control->sequence_number != 1U) {
        return 9;
    }

    mms::MmsInformationReportView report;
    if (!decode_report_frame(response.first(poll.bytes_written), report) ||
        report.item_count != 13U) {
        return 10;
    }
    mms::MmsReadAccessResultView item;
    if (!report.try_item(6U,item) || !decode_bit_string(item,5U,0xA0U) ||
        !report.try_item(11U,item) || !decode_bit_string(item,2U,0x80U) ||
        !report.try_item(12U,item) || !decode_bit_string(item,2U,0x40U)) {
        return 11;
    }
    if (mms::MmsStaticUrcbEventConnection::poll(
            connection,events,120U,kReportTime,response,workspace).status !=
        mms::MmsStaticUrcbEventConnectionStatus::no_report_due) {
        return 12;
    }

    connection.reset();
    if (events.notify(0U,1U,mms::MmsStaticUrcbEventReason::data_update,200U) !=
            mms::MmsStaticUrcbEventStatus::ok) {
        return 13;
    }
    poll = mms::MmsStaticUrcbEventConnection::poll(
        connection,events,220U,kReportTime,response,workspace);
    pending = events.state(0U);
    if (poll.status != mms::MmsStaticUrcbEventConnectionStatus::not_established ||
        pending == nullptr || !pending->pending || pending->pending_member_count != 1U) {
        return 14;
    }

    return 0;
}
