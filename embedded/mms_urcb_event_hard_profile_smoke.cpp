// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/asn1/ber_span_reader.hpp"
#include "ariec61850/mms/static_urcb_event_runtime.hpp"

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

[[nodiscard]] bool decode_bit_string(
    const mms::MmsReadAccessResultView& result,
    const std::uint8_t expected_unused,
    const std::uint8_t expected_value) noexcept {
    if (!result.success) {
        return false;
    }
    asn1::BerTlvView tlv;
    return asn1::BerSpanReader::try_read_exact(result.encoded_data, tlv) &&
        tlv.tag_class == asn1::BerClass::context_specific &&
        tlv.tag_number == 4 && !tlv.constructed && tlv.value.size() == 2U &&
        tlv.value[0] == expected_unused && tlv.value[1] == expected_value;
}

[[nodiscard]] bool decode_boolean(
    const mms::MmsReadAccessResultView& result,
    const bool expected) noexcept {
    if (!result.success) {
        return false;
    }
    asn1::BerTlvView tlv;
    return asn1::BerSpanReader::try_read_exact(result.encoded_data, tlv) &&
        tlv.tag_class == asn1::BerClass::context_specific &&
        tlv.tag_number == 3 && !tlv.constructed && tlv.value.size() == 1U &&
        (tlv.value[0] != 0U) == expected;
}

} // namespace

int main() {
    bool first = true;
    bool second = false;
    bool third = true;
    const std::array<mms::MmsStaticObjectEntry, 3U> objects{
        mms::MmsStaticObjectEntry{"LD0", "X1", kBooleanType, read_boolean, &first, false},
        mms::MmsStaticObjectEntry{"LD0", "X2", kBooleanType, read_boolean, &second, false},
        mms::MmsStaticObjectEntry{"LD0", "X3", kBooleanType, read_boolean, &third, false}};
    const mms::MmsStaticObjectTable object_table{objects};

    const std::array<mms::MmsStaticDataSetMember, 3U> members{
        mms::MmsStaticDataSetMember{"LD0", "X1"},
        mms::MmsStaticDataSetMember{"LD0", "X2"},
        mms::MmsStaticDataSetMember{"LD0", "X3"}};
    const std::array<mms::MmsStaticDataSetEntry, 1U> data_sets{
        mms::MmsStaticDataSetEntry{"LD0", "LLN0$Events", members, false}};
    const mms::MmsStaticDataSetTable data_set_table{data_sets};

    const std::array<mms::MmsStaticUrcbDefinition, 1U> definitions{
        mms::MmsStaticUrcbDefinition{
            "LD0",
            "LLN0$RP$Events",
            "LD0/LLN0$RP$Events",
            "LD0",
            "LLN0$Events",
            9U,
            {0x7CU, 0x80U},
            20U,
            0x70U,
            0U}};
    std::array<mms::MmsStaticUrcbState, 1U> urcb_states{};
    mms::MmsStaticUrcbRuntime urcb{
        definitions, urcb_states, object_table, data_set_table};
    if (!urcb.initialize()) {
        return 1;
    }

    std::array<mms::MmsStaticUrcbEventState, 1U> event_states{};
    mms::MmsStaticUrcbEventRuntime events{
        urcb, event_states, object_table, data_set_table};
    if (!events.initialize() || !events.valid()) {
        return 2;
    }
    if (events.notify(0U, 0U, mms::MmsStaticUrcbEventReason::data_change, 100U) !=
        mms::MmsStaticUrcbEventStatus::temporarily_unavailable) {
        return 3;
    }
    if (urcb.set_enabled(0U, true, 100U) != mms::MmsStaticUrcbStatus::ok) {
        return 4;
    }

    if (events.notify(0U, 0U, mms::MmsStaticUrcbEventReason::data_change, 110U) !=
            mms::MmsStaticUrcbEventStatus::ok ||
        events.notify(0U, 0U, mms::MmsStaticUrcbEventReason::quality_change, 115U) !=
            mms::MmsStaticUrcbEventStatus::ok ||
        events.notify(0U, 2U, mms::MmsStaticUrcbEventReason::data_update, 119U) !=
            mms::MmsStaticUrcbEventStatus::ok) {
        return 5;
    }
    const auto* pending = events.state(0U);
    if (pending == nullptr || !pending->pending || pending->pending_member_count != 2U ||
        pending->due_ms != 130U || pending->member_reason_masks[0] != 0xC0U ||
        pending->member_reason_masks[1] != 0U || pending->member_reason_masks[2] != 0x20U) {
        return 6;
    }

    mms::MmsStaticUrcbEventPlan plan;
    if (events.next_due(129U, plan) || !events.next_due(130U, plan) ||
        plan.index != 0U || plan.sequence_number != 1U) {
        return 7;
    }

    std::array<std::uint8_t, 8U> tiny{};
    std::array<std::uint8_t, 1'024U> output{};
    std::array<std::uint8_t, 128U> workspace{};
    auto encoded = events.encode(plan, kReportTime, tiny, workspace);
    if (encoded.status != mms::MmsStaticUrcbEventStatus::response_buffer_too_small ||
        encoded.required_bytes <= tiny.size()) {
        return 8;
    }
    encoded = events.encode(plan, kReportTime, output, workspace);
    if (!encoded.success() || encoded.data_set_member_count != 3U ||
        encoded.included_member_count != 2U) {
        return 9;
    }

    mms::MmsInformationReportView report;
    if (!mms::MmsInformationReportSpanCodec::try_decode_information_report(
            std::span<const std::uint8_t>{output}.first(encoded.bytes_written), report) ||
        report.item_count != 13U) {
        return 10;
    }
    mms::MmsReadAccessResultView item;
    // Full DataSet cardinality is three; only members 0 and 2 are included.
    if (!report.try_item(6U, item) || !decode_bit_string(item, 5U, 0xA0U)) {
        return 11;
    }
    if (!report.try_item(9U, item) || !decode_boolean(item, true) ||
        !report.try_item(10U, item) || !decode_boolean(item, true)) {
        return 12;
    }
    // Member 0 coalesced dchg + qchg; member 2 is dupd.
    if (!report.try_item(11U, item) || !decode_bit_string(item, 2U, 0xC0U) ||
        !report.try_item(12U, item) || !decode_bit_string(item, 2U, 0x20U)) {
        return 13;
    }

    if (events.commit(plan) != mms::MmsStaticUrcbEventStatus::ok ||
        events.commit(plan) != mms::MmsStaticUrcbEventStatus::stale_plan) {
        return 14;
    }
    const auto* control = urcb.state(0U);
    pending = events.state(0U);
    if (control == nullptr || control->sequence_number != 1U ||
        pending == nullptr || pending->pending || pending->pending_member_count != 0U) {
        return 15;
    }

    // A new event after planning invalidates the old event revision.
    if (events.notify(0U, 1U, mms::MmsStaticUrcbEventReason::data_change, 200U) !=
            mms::MmsStaticUrcbEventStatus::ok ||
        !events.next_due(220U, plan) ||
        events.notify(0U, 0U, mms::MmsStaticUrcbEventReason::quality_change, 220U) !=
            mms::MmsStaticUrcbEventStatus::ok ||
        events.encode(plan, kReportTime, output, workspace).status !=
            mms::MmsStaticUrcbEventStatus::stale_plan ||
        !events.next_due(220U, plan)) {
        return 16;
    }
    encoded = events.encode(plan, kReportTime, output, workspace);
    if (!encoded.success() || encoded.included_member_count != 2U ||
        events.commit(plan) != mms::MmsStaticUrcbEventStatus::ok) {
        return 17;
    }

    // Reconfiguration invalidates pending source revision rather than leaking
    // a pre-disable event into a later enable interval.
    if (events.notify(0U, 0U, mms::MmsStaticUrcbEventReason::data_change, 300U) !=
            mms::MmsStaticUrcbEventStatus::ok ||
        urcb.set_enabled(0U, false, 301U) != mms::MmsStaticUrcbStatus::ok ||
        urcb.set_enabled(0U, true, 302U) != mms::MmsStaticUrcbStatus::ok ||
        events.next_due(400U, plan) ||
        events.notify(0U, 2U, mms::MmsStaticUrcbEventReason::data_update, 400U) !=
            mms::MmsStaticUrcbEventStatus::ok ||
        !events.next_due(420U, plan)) {
        return 18;
    }
    encoded = events.encode(plan, kReportTime, output, workspace);
    if (!encoded.success() || encoded.included_member_count != 1U ||
        events.commit(plan) != mms::MmsStaticUrcbEventStatus::ok) {
        return 19;
    }

    // Secure trigger selection: a qchg event cannot bypass TrgOps.
    if (urcb.set_enabled(0U, false, 500U) != mms::MmsStaticUrcbStatus::ok ||
        urcb.set_trigger_options(0U, 0x40U) != mms::MmsStaticUrcbStatus::ok ||
        urcb.set_buffer_time_ms(0U, 0U) != mms::MmsStaticUrcbStatus::ok ||
        urcb.set_enabled(0U, true, 500U) != mms::MmsStaticUrcbStatus::ok ||
        events.notify(0U, 0U, mms::MmsStaticUrcbEventReason::quality_change, 500U) !=
            mms::MmsStaticUrcbEventStatus::trigger_not_selected ||
        events.notify(0U, 0U, mms::MmsStaticUrcbEventReason::data_change, 500U) !=
            mms::MmsStaticUrcbEventStatus::ok ||
        !events.next_due(500U, plan)) {
        return 20;
    }
    encoded = events.encode(plan, kReportTime, output, workspace);
    if (!encoded.success() || events.commit(plan) != mms::MmsStaticUrcbEventStatus::ok) {
        return 21;
    }

    // Repeated immediate events prove deterministic bounded replay and 8-bit
    // SqNum wrapping without heap-backed pending queues.
    for (std::uint32_t iteration = 0U; iteration < 10'000U; ++iteration) {
        if (events.notify(0U, 0U, mms::MmsStaticUrcbEventReason::data_change, 600U) !=
                mms::MmsStaticUrcbEventStatus::ok ||
            !events.next_due(600U, plan)) {
            return 22;
        }
        encoded = events.encode(plan, kReportTime, output, workspace);
        if (!encoded.success() ||
            events.commit(plan) != mms::MmsStaticUrcbEventStatus::ok) {
            return 23;
        }
    }

    if (events.notify(0U, 3U, mms::MmsStaticUrcbEventReason::data_change, 700U) !=
        mms::MmsStaticUrcbEventStatus::member_out_of_range) {
        return 24;
    }

    return 0;
}
