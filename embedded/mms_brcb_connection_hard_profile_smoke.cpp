// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/asn1/ber_span_reader.hpp"
#include "ariec61850/mms/static_brcb_connection.hpp"
#include "ariec61850/mms/static_dispatcher.hpp"
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
constexpr std::array<std::uint8_t, 2U> kOwnerA{0xAAU, 0x01U};
constexpr std::array<std::uint8_t, 2U> kOwnerB{0xBBU, 0x01U};

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
    if (context == nullptr) return {wire::EncodeStatus::value_out_of_range, 0U, required};
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    destination[0] = 0x83U;
    destination[1] = 0x01U;
    destination[2] = *static_cast<const bool*>(context) ? 0xFFU : 0x00U;
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] mms::MmsStaticBrcbClientIdentity identity(
    const std::uint64_t association,
    const std::array<std::uint8_t, 2U>& owner) noexcept {
    mms::MmsStaticBrcbClientIdentity result;
    result.association_id = association;
    result.owner[0] = owner[0];
    result.owner[1] = owner[1];
    result.owner_size = owner.size();
    return result;
}

[[nodiscard]] mms::MmsStaticConnectionPolicy connection_policy(
    const std::uint64_t association,
    const std::array<std::uint8_t, 2U>& owner) noexcept {
    mms::MmsStaticConnectionPolicy result;
    result.association_id = association;
    result.owner[0] = owner[0];
    result.owner[1] = owner[1];
    result.owner_size = owner.size();
    return result;
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

[[nodiscard]] bool decode_boolean(
    const mms::MmsReadAccessResultView& item,
    bool& value) noexcept {
    value = false;
    if (!item.success) return false;
    asn1::BerTlvView tlv;
    if (!asn1::BerSpanReader::try_read_exact(item.encoded_data, tlv) ||
        tlv.tag_class != asn1::BerClass::context_specific ||
        tlv.tag_number != 3 || tlv.constructed || tlv.value.size() != 1U) return false;
    value = tlv.value[0] != 0U;
    return true;
}

} // namespace

int main() {
    bool value = true;
    const std::array<mms::MmsStaticObjectEntry, 1U> objects{
        mms::MmsStaticObjectEntry{"LD0", "X1", kBooleanType, read_boolean, &value, false}};
    const mms::MmsStaticObjectTable object_table{objects};
    const std::array<mms::MmsStaticDataSetMember, 1U> members{
        mms::MmsStaticDataSetMember{"LD0", "X1"}};
    const std::array<mms::MmsStaticDataSetEntry, 1U> data_sets{
        mms::MmsStaticDataSetEntry{"LD0", "LLN0$Events", members, false}};
    const mms::MmsStaticDataSetTable data_set_table{data_sets};
    const mms::MmsStaticApplicationDispatcher dispatcher{object_table, data_set_table};

    const auto owner_a1 = identity(101U, kOwnerA);
    const auto owner_a2 = identity(102U, kOwnerA);
    const auto foreign_b = identity(201U, kOwnerB);
    mms::MmsStaticConnectionRuntime connection{
        dispatcher, connection_policy(owner_a1.association_id, kOwnerA)};
    mms::MmsStaticConnectionRuntime foreign_connection{
        dispatcher, connection_policy(foreign_b.association_id, kOwnerB)};

    const mms::MmsStaticBrcbDefinition definition{
        "LD0", "LLN0$BR$Events", "LD0/LLN0$BR$Events",
        "LD0", "LLN0$Events", 4U, {0x7FU, 0x80U}, 0U, 0x70U};
    std::array<std::uint8_t, 1024U> slot_storage{};
    std::array<mms::MmsStaticBrcbSlot, 1U> slots{
        mms::MmsStaticBrcbSlot{slot_storage}};
    mms::MmsStaticBrcbPendingState pending{};
    mms::MmsStaticBrcbRuntime reports{
        definition, pending, slots, object_table, data_set_table};
    if (!reports.initialize()) {
        return 1;
    }
    mms::MmsStaticBrcbControl control{reports};
    if (control.reserve(owner_a1, 5U, 100U) != mms::MmsStaticBrcbControlStatus::ok ||
        control.set_report_enabled(owner_a1, true, 100U) !=
            mms::MmsStaticBrcbControlStatus::ok ||
        reports.notify(0U, mms::MmsStaticBrcbEventReason::data_change, 100U) !=
            mms::MmsStaticBrcbStatus::ok) {
        return 2;
    }

    mms::MmsStaticBrcbCapturePlan plan;
    std::array<std::uint8_t, 2048U> staging{};
    std::array<std::uint8_t, 128U> value_workspace{};
    if (!reports.next_due(100U, plan) ||
        !reports.capture(plan, kReportTime, staging, value_workspace).success() ||
        reports.queue_size() != 1U) {
        return 3;
    }

    value = false;
    std::array<std::uint8_t, 2048U> request{};
    std::array<std::uint8_t, 2048U> response{};
    std::array<std::uint8_t, 2048U> workspace{};
    std::array<std::uint8_t, 2048U> scratch{};
    auto delivery = mms::MmsStaticBrcbConnection::poll(
        connection, control, reports, 100U, response, workspace);
    if (delivery.status != mms::MmsStaticBrcbConnectionStatus::not_established ||
        reports.queue_size() != 1U) {
        return 4;
    }
    if (!establish(connection, request, response, workspace, scratch) ||
        !establish(foreign_connection, request, response, workspace, scratch)) {
        return 5;
    }

    // A foreign established association must not see or consume the owner's entry.
    delivery = mms::MmsStaticBrcbConnection::poll(
        foreign_connection, control, reports, 100U, response, workspace);
    if (delivery.status != mms::MmsStaticBrcbConnectionStatus::access_denied ||
        reports.queue_size() != 1U) {
        return 6;
    }

    mms::MmsStaticBrcbEntryView held_entry;
    if (!reports.front(held_entry) || held_entry.entry_id.size() != 8U ||
        held_entry.entry_id[7U] != 1U ||
        control.set_report_enabled(owner_a1, false, 100U) !=
            mms::MmsStaticBrcbControlStatus::ok) {
        return 7;
    }
    delivery = mms::MmsStaticBrcbConnection::poll(
        connection, control, reports, 100U, response, workspace);
    if (delivery.status != mms::MmsStaticBrcbConnectionStatus::reporting_disabled ||
        reports.queue_size() != 1U || !reports.front(held_entry) ||
        held_entry.entry_id[7U] != 1U ||
        control.set_report_enabled(owner_a1, true, 100U) !=
            mms::MmsStaticBrcbControlStatus::ok) {
        return 8;
    }

    std::array<std::uint8_t, 8U> tiny_response{};
    delivery = mms::MmsStaticBrcbConnection::poll(
        connection, control, reports, 100U, tiny_response, workspace);
    if (delivery.status != mms::MmsStaticBrcbConnectionStatus::response_buffer_too_small ||
        delivery.required_response_bytes <= tiny_response.size() ||
        delivery.entry_id[7U] != 1U || reports.queue_size() != 1U) {
        return 9;
    }

    std::array<std::uint8_t, 2U> tiny_workspace{};
    delivery = mms::MmsStaticBrcbConnection::poll(
        connection, control, reports, 100U, response, tiny_workspace);
    if (delivery.status != mms::MmsStaticBrcbConnectionStatus::workspace_too_small ||
        delivery.required_workspace_bytes <= tiny_workspace.size() ||
        delivery.entry_id[7U] != 1U || reports.queue_size() != 1U) {
        return 10;
    }

    // Staging is retry-safe: no delivery cursor movement until commit_sent().
    delivery = mms::MmsStaticBrcbConnection::poll(
        connection, control, reports, 100U, response, workspace);
    if (!delivery.response_ready() || delivery.bytes_written == 0U ||
        delivery.entry_id[7U] != 1U || delivery.sequence_number != 1U ||
        delivery.buffer_overflow || reports.queue_size() != 1U) {
        return 11;
    }
    const auto retry = mms::MmsStaticBrcbConnection::poll(
        connection, control, reports, 100U, response, workspace);
    if (!retry.response_ready() || retry.entry_id != delivery.entry_id ||
        reports.queue_size() != 1U) {
        return 12;
    }

    mms::MmsInformationReportView decoded;
    if (!decode_report_frame(
            std::span<const std::uint8_t>{response}.first(retry.bytes_written), decoded) ||
        decoded.item_count != 12U) {
        return 13;
    }
    mms::MmsReadAccessResultView item;
    bool historical_value = false;
    if (!decoded.try_item(10U, item) || !decode_boolean(item, historical_value) ||
        !historical_value) {
        return 14;
    }
    if (mms::MmsStaticBrcbConnection::commit_sent(reports, retry) !=
            mms::MmsStaticBrcbStatus::ok ||
        reports.queue_size() != 0U) {
        return 15;
    }

    // Queue another historical entry, close A1, then reclaim with the same stable
    // Owner on association A2. The retained entry must remain retryable.
    if (reports.notify(0U, mms::MmsStaticBrcbEventReason::data_update, 200U) !=
            mms::MmsStaticBrcbStatus::ok ||
        !reports.next_due(200U, plan) ||
        !reports.capture(plan, kReportTime, staging, value_workspace).success() ||
        reports.queue_size() != 1U) {
        return 16;
    }
    connection.reset();
    control.on_association_closed(owner_a1.association_id, 200U);
    delivery = mms::MmsStaticBrcbConnection::poll(
        connection, control, reports, 200U, response, workspace);
    if (delivery.status != mms::MmsStaticBrcbConnectionStatus::not_established ||
        reports.queue_size() != 1U ||
        control.reserve(owner_a2, 5U, 201U) != mms::MmsStaticBrcbControlStatus::ok ||
        control.set_report_enabled(owner_a2, true, 201U) !=
            mms::MmsStaticBrcbControlStatus::ok) {
        return 17;
    }

    mms::MmsStaticConnectionRuntime reconnect{
        dispatcher, connection_policy(owner_a2.association_id, kOwnerA)};
    if (!establish(reconnect, request, response, workspace, scratch)) {
        return 18;
    }
    delivery = mms::MmsStaticBrcbConnection::poll(
        reconnect, control, reports, 201U, response, workspace);
    if (!delivery.response_ready() || delivery.entry_id[7U] != 2U ||
        reports.queue_size() != 1U ||
        mms::MmsStaticBrcbConnection::commit_sent(reports, delivery) !=
            mms::MmsStaticBrcbStatus::ok ||
        reports.queue_size() != 0U ||
        mms::MmsStaticBrcbConnection::poll(
            reconnect, control, reports, 201U, response, workspace).status !=
            mms::MmsStaticBrcbConnectionStatus::no_report_available) {
        return 19;
    }

    return 0;
}
