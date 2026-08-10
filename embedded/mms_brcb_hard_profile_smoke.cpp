// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/asn1/ber_span_reader.hpp"
#include "ariec61850/mms/information_report_span.hpp"
#include "ariec61850/mms/static_brcb_runtime.hpp"

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

[[nodiscard]] bool decode_boolean(
    const mms::MmsReadAccessResultView& item,
    bool& value) noexcept {
    value = false;
    if (!item.success) return false;
    asn1::BerTlvView tlv;
    if (!asn1::BerSpanReader::try_read_exact(item.encoded_data, tlv) ||
        tlv.tag_class != asn1::BerClass::context_specific ||
        tlv.tag_number != 3 || tlv.constructed || tlv.value.size() != 1U) {
        return false;
    }
    value = tlv.value[0] != 0U;
    return true;
}

[[nodiscard]] bool decode_bit_string(
    const mms::MmsReadAccessResultView& item,
    const std::uint8_t unused,
    const std::uint8_t value) noexcept {
    if (!item.success) return false;
    asn1::BerTlvView tlv;
    return asn1::BerSpanReader::try_read_exact(item.encoded_data, tlv) &&
        tlv.tag_class == asn1::BerClass::context_specific &&
        tlv.tag_number == 4 && !tlv.constructed && tlv.value.size() == 2U &&
        tlv.value[0] == unused && tlv.value[1] == value;
}

[[nodiscard]] bool decode_octet_string(
    const mms::MmsReadAccessResultView& item,
    const std::span<const std::uint8_t> expected) noexcept {
    if (!item.success) return false;
    asn1::BerTlvView tlv;
    if (!asn1::BerSpanReader::try_read_exact(item.encoded_data, tlv) ||
        tlv.tag_class != asn1::BerClass::context_specific ||
        tlv.tag_number != 9 || tlv.constructed || tlv.value.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        if (tlv.value[index] != expected[index]) return false;
    }
    return true;
}

[[nodiscard]] bool expected_entry(
    const std::array<std::uint8_t, mms::MmsInformationReportSpanCodec::entry_id_bytes>& entry,
    const std::uint64_t value) noexcept {
    for (std::size_t index = 0U; index < entry.size(); ++index) {
        const auto shift = static_cast<unsigned>((entry.size() - 1U - index) * 8U);
        if (entry[index] != static_cast<std::uint8_t>((value >> shift) & 0xFFU)) {
            return false;
        }
    }
    return true;
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

    const mms::MmsStaticBrcbDefinition definition{
        "LD0",
        "LLN0$BR$Events",
        "LD0/LLN0$BR$Events",
        "LD0",
        "LLN0$Events",
        11U,
        {0x7FU, 0x80U},
        20U,
        0x70U};

    std::array<std::uint8_t, 1024U> slot0{};
    std::array<std::uint8_t, 1024U> slot1{};
    std::array<mms::MmsStaticBrcbSlot, 2U> slots{
        mms::MmsStaticBrcbSlot{slot0},
        mms::MmsStaticBrcbSlot{slot1}};
    mms::MmsStaticBrcbPendingState pending{};
    mms::MmsStaticBrcbRuntime runtime{
        definition, pending, slots, object_table, data_set_table};
    if (!runtime.initialize() || !runtime.valid() || runtime.enabled() ||
        runtime.queue_size() != 0U || runtime.queue_capacity() != 2U) {
        return 1;
    }
    if (runtime.notify(0U, mms::MmsStaticBrcbEventReason::data_change, 0U) !=
            mms::MmsStaticBrcbStatus::temporarily_unavailable ||
        runtime.set_enabled(true) != mms::MmsStaticBrcbStatus::ok) {
        return 2;
    }

    if (runtime.notify(0U, mms::MmsStaticBrcbEventReason::data_change, 100U) !=
            mms::MmsStaticBrcbStatus::ok ||
        runtime.notify(2U, mms::MmsStaticBrcbEventReason::quality_change, 105U) !=
            mms::MmsStaticBrcbStatus::ok) {
        return 3;
    }
    mms::MmsStaticBrcbCapturePlan plan;
    if (runtime.next_due(119U, plan) || !runtime.next_due(120U, plan) ||
        plan.entry_number != 1U || plan.sequence_number != 1U || plan.buffer_overflow) {
        return 4;
    }

    std::array<std::uint8_t, 8U> tiny{};
    std::array<std::uint8_t, 2048U> staging{};
    std::array<std::uint8_t, 256U> workspace{};
    auto capture = runtime.capture(plan, kReportTime, tiny, workspace);
    if (capture.status != mms::MmsStaticBrcbStatus::response_buffer_too_small ||
        capture.required_bytes <= tiny.size() || runtime.queue_size() != 0U ||
        !runtime.next_due(120U, plan) || plan.entry_number != 1U) {
        return 5;
    }

    capture = runtime.capture(plan, kReportTime, staging, workspace);
    if (!capture.success() || runtime.queue_size() != 1U ||
        capture.included_member_count != 2U || !expected_entry(capture.entry_id, 1U)) {
        return 6;
    }

    mms::MmsStaticBrcbEntryView entry;
    mms::MmsInformationReportView report;
    if (!runtime.front(entry) || entry.sequence_number != 1U || entry.buffer_overflow ||
        !mms::MmsInformationReportSpanCodec::try_decode_information_report(
            entry.mms_pdu, report) || report.item_count != 15U) {
        return 7;
    }
    mms::MmsReadAccessResultView item;
    bool boolean_value = false;
    if (!report.try_item(5U, item) || !decode_boolean(item, boolean_value) || boolean_value ||
        !report.try_item(6U, item) || !decode_octet_string(item, capture.entry_id) ||
        !report.try_item(8U, item) || !decode_bit_string(item, 5U, 0xA0U) ||
        !report.try_item(11U, item) || !decode_boolean(item, boolean_value) || !boolean_value ||
        !report.try_item(12U, item) || !decode_boolean(item, boolean_value) || !boolean_value ||
        !report.try_item(13U, item) || !decode_bit_string(item, 2U, 0x80U) ||
        !report.try_item(14U, item) || !decode_bit_string(item, 2U, 0x40U)) {
        return 8;
    }

    if (runtime.notify(1U, mms::MmsStaticBrcbEventReason::data_update, 200U) !=
            mms::MmsStaticBrcbStatus::ok ||
        !runtime.next_due(220U, plan)) {
        return 9;
    }
    capture = runtime.capture(plan, kReportTime, staging, workspace);
    if (!capture.success() || runtime.queue_size() != 2U ||
        !expected_entry(capture.entry_id, 2U)) {
        return 10;
    }

    if (runtime.notify(0U, mms::MmsStaticBrcbEventReason::data_change, 300U) !=
            mms::MmsStaticBrcbStatus::ok ||
        !runtime.next_due(320U, plan) || !plan.buffer_overflow) {
        return 11;
    }
    const auto stale_plan = plan;
    const auto first_entry_id = std::array<std::uint8_t,
        mms::MmsInformationReportSpanCodec::entry_id_bytes>{
            0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U};
    if (runtime.commit_delivery(first_entry_id) != mms::MmsStaticBrcbStatus::ok ||
        runtime.capture(stale_plan, kReportTime, staging, workspace).status !=
            mms::MmsStaticBrcbStatus::stale_plan ||
        !runtime.next_due(320U, plan) || plan.buffer_overflow) {
        return 12;
    }
    capture = runtime.capture(plan, kReportTime, staging, workspace);
    if (!capture.success() || runtime.queue_size() != 2U ||
        !expected_entry(capture.entry_id, 3U) || runtime.dropped_reports() != 0U) {
        return 13;
    }

    if (runtime.notify(2U, mms::MmsStaticBrcbEventReason::quality_change, 400U) !=
            mms::MmsStaticBrcbStatus::ok ||
        !runtime.next_due(420U, plan) || !plan.buffer_overflow) {
        return 14;
    }
    capture = runtime.capture(plan, kReportTime, staging, workspace);
    if (!capture.success() || runtime.queue_size() != 2U ||
        !expected_entry(capture.entry_id, 4U) || runtime.dropped_reports() != 1U) {
        return 15;
    }

    // Entry 2 was the oldest and must have been dropped. Entry 3 is now front.
    if (!runtime.front(entry) || entry.entry_id.size() != 8U || entry.entry_id[7] != 3U ||
        runtime.commit_delivery(entry.entry_id) != mms::MmsStaticBrcbStatus::ok ||
        !runtime.front(entry) || entry.entry_id[7] != 4U || !entry.buffer_overflow ||
        !mms::MmsInformationReportSpanCodec::try_decode_information_report(
            entry.mms_pdu, report) ||
        !report.try_item(5U, item) || !decode_boolean(item, boolean_value) || !boolean_value ||
        !report.try_item(6U, item)) {
        return 16;
    }
    const std::array<std::uint8_t, 8U> fourth_id{0U,0U,0U,0U,0U,0U,0U,4U};
    if (!decode_octet_string(item, fourth_id) ||
        runtime.commit_delivery(fourth_id) != mms::MmsStaticBrcbStatus::ok ||
        runtime.queue_size() != 0U) {
        return 17;
    }

    // Repeated capture/delivery proves bounded steady-state behavior and EntryID progression.
    for (std::uint32_t iteration = 0U; iteration < 10'000U; ++iteration) {
        const auto now = static_cast<std::uint64_t>(1'000U + (iteration * 25U));
        if (runtime.notify(0U, mms::MmsStaticBrcbEventReason::data_update, now) !=
                mms::MmsStaticBrcbStatus::ok ||
            !runtime.next_due(now + 20U, plan)) {
            return 18;
        }
        capture = runtime.capture(plan, kReportTime, staging, workspace);
        if (!capture.success() || !runtime.front(entry) ||
            runtime.commit_delivery(entry.entry_id) != mms::MmsStaticBrcbStatus::ok ||
            runtime.queue_size() != 0U) {
            return 19;
        }
    }

    if (runtime.set_enabled(false) != mms::MmsStaticBrcbStatus::ok || runtime.enabled() ||
        runtime.notify(0U, mms::MmsStaticBrcbEventReason::data_change, 999'999U) !=
            mms::MmsStaticBrcbStatus::temporarily_unavailable) {
        return 20;
    }

    return 0;
}
