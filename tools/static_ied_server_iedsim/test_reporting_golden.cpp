// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/reporting.hpp"
#include "ariec61850/mms/static_brcb_runtime.hpp"
#include "ariec61850/mms/static_data_set_table.hpp"
#include "ariec61850/mms/static_object_table.hpp"
#include "ariec61850/mms/static_urcb_runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace ar::iec61850;

constexpr std::string_view kDomain{"AA1E1F06R4Application"};
constexpr std::string_view kAnalogDataSet{"LLN0$Analog"};
constexpr std::string_view kDigitalDataSet{"LLN0$Digital"};
constexpr std::string_view kUrcbRptId{
    "AA1E1F06R4/Application/LLN0$RP$Unbuffer"};
constexpr std::string_view kBrcbRptId{
    "AA1E1F06R4/Application/LLN0$BR$Buffer"};
constexpr std::uint32_t kConfRev = 100'001U;
constexpr std::array<std::uint8_t, 2U> kExpectedUrcbOptFlds{0x78U, 0x80U};
constexpr std::array<std::uint8_t, 2U> kExpectedBrcbOptFlds{0x79U, 0x80U};
constexpr std::array<std::uint8_t, 6U> kReportTime{
    0x00U, 0xACU, 0x36U, 0x52U, 0x3CU, 0xF2U};
constexpr std::array<std::uint8_t, 2U> kBooleanType{0x83U, 0x00U};

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
    destination[2] =
        *static_cast<const std::uint8_t*>(context) != 0U ? 0xFFU : 0x00U;
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] std::string two_digit(const std::size_t value) {
    return value < 10U
        ? "0" + std::to_string(value)
        : std::to_string(value);
}

[[nodiscard]] bool exact_optflds(
    const mms::MmsReportHeader& header,
    const std::array<std::uint8_t, 2U>& expected) {
    return header.optional_fields.raw ==
        std::vector<std::uint8_t>{6U, expected[0], expected[1]};
}

[[nodiscard]] bool full_indexes(
    const std::vector<std::size_t>& indexes,
    const std::size_t count) {
    if (indexes.size() != count) return false;
    for (std::size_t index = 0U; index < count; ++index) {
        if (indexes[index] != index) return false;
    }
    return true;
}

[[nodiscard]] bool all_reason(
    const mms::MmsReportFrame& frame,
    const std::string& name) {
    if (frame.values.empty()) return false;
    for (const auto& value : frame.values) {
        if (!value.reason_for_inclusion.has(name)) return false;
    }
    return true;
}

[[nodiscard]] bool entry_id_is(
    const std::vector<std::uint8_t>& entry,
    const std::uint64_t expected) {
    if (entry.size() != mms::MmsInformationReportSpanCodec::entry_id_bytes) {
        return false;
    }
    for (std::size_t index = 0U; index < entry.size(); ++index) {
        const auto shift = static_cast<unsigned>(
            (entry.size() - 1U - index) * 8U);
        if (entry[index] !=
            static_cast<std::uint8_t>((expected >> shift) & 0xFFU)) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    constexpr std::size_t digital_count = 36U;
    constexpr std::size_t analog_count = 22U;
    constexpr std::size_t total_count = digital_count + analog_count;

    std::array<std::uint8_t, total_count> values{};
    std::vector<std::string> names;
    names.reserve(total_count);
    for (std::size_t index = 0U; index < digital_count; ++index) {
        names.push_back(
            "GGIO1$ST$Ind" + two_digit(index + 1U) + "$stVal");
    }
    for (std::size_t index = 0U; index < analog_count; ++index) {
        names.push_back(
            "MMXU1$MX$An" + two_digit(index + 1U) + "$mag");
    }

    std::vector<mms::MmsStaticObjectEntry> objects;
    objects.reserve(total_count);
    for (std::size_t index = 0U; index < total_count; ++index) {
        objects.push_back(mms::MmsStaticObjectEntry{
            kDomain,
            names[index],
            kBooleanType,
            read_boolean,
            &values[index],
            false});
    }
    const mms::MmsStaticObjectTable object_table{objects};
    if (!object_table.valid()) return 1;

    std::array<mms::MmsStaticDataSetMember, analog_count> analog_members{};
    for (std::size_t index = 0U; index < analog_count; ++index) {
        analog_members[index] = {
            kDomain,
            names[digital_count + index]};
    }
    std::array<mms::MmsStaticDataSetMember, digital_count> digital_members{};
    for (std::size_t index = 0U; index < digital_count; ++index) {
        digital_members[index] = {kDomain, names[index]};
    }
    const std::array<mms::MmsStaticDataSetEntry, 2U> data_sets{{
        {kDomain, kAnalogDataSet, analog_members, false},
        {kDomain, kDigitalDataSet, digital_members, false},
    }};
    const mms::MmsStaticDataSetTable data_set_table{data_sets};
    if (!data_set_table.valid() ||
        !data_set_table.valid_against(object_table)) {
        return 2;
    }

    // Real IEDScout static-URCB acceptance capture:
    // IEDScout did not rewrite OptFlds. The configured/effective report profile
    // emits OptFlds 78 80, SqNum=1, DataSet Analog, ConfRev=100001,
    // 22/22 included members and GI ReasonForInclusion.
    const std::array<mms::MmsStaticUrcbDefinition, 1U> urcb_definitions{{
        {
            kDomain,
            "LLN0$RP$Unbuffer01",
            kUrcbRptId,
            kDomain,
            kAnalogDataSet,
            kConfRev,
            kExpectedUrcbOptFlds,
            0U,
            0x7CU,
            0U,
        },
    }};
    std::array<mms::MmsStaticUrcbState, 1U> urcb_states{};
    mms::MmsStaticUrcbRuntime urcb{
        urcb_definitions, urcb_states, object_table, data_set_table};
    if (!urcb.initialize()) return 3;
    const auto* urcb_state = urcb.state(0U);
    if (urcb_state == nullptr ||
        urcb_state->optional_fields != kExpectedUrcbOptFlds ||
        urcb.set_enabled(0U, true, 1'000U) != mms::MmsStaticUrcbStatus::ok ||
        urcb.request_general_interrogation(0U) != mms::MmsStaticUrcbStatus::ok) {
        return 4;
    }
    mms::MmsStaticUrcbEmissionPlan urcb_plan;
    if (!urcb.next_due(1'000U, urcb_plan) ||
        urcb_plan.sequence_number != 1U ||
        urcb_plan.reason !=
            mms::MmsStaticUrcbReportReason::general_interrogation) {
        return 5;
    }
    std::array<std::uint8_t, 16'384U> urcb_pdu{};
    std::array<std::uint8_t, 16'384U> urcb_workspace{};
    const auto urcb_encoded = urcb.encode(
        urcb_plan, kReportTime, urcb_pdu, urcb_workspace);
    if (!urcb_encoded.success() ||
        urcb_encoded.member_count != analog_count) {
        return 6;
    }
    const auto urcb_report = mms::MmsInformationReportCodec::decode(
        std::span<const std::uint8_t>{urcb_pdu}.first(
            urcb_encoded.bytes_written));
    const auto urcb_frame =
        mms::MmsReportFrameMapper::map(urcb_report, {});
    if (urcb_frame.header.report_id != kUrcbRptId ||
        !exact_optflds(urcb_frame.header, kExpectedUrcbOptFlds) ||
        urcb_frame.header.sequence_number != 1U ||
        !urcb_frame.header.time_of_entry ||
        urcb_frame.header.data_set_reference !=
            std::string{kDomain} + "/" + std::string{kAnalogDataSet} ||
        !urcb_frame.header.entry_id.empty() ||
        urcb_frame.header.configuration_revision != kConfRev ||
        !full_indexes(
            urcb_frame.included_data_set_indexes, analog_count) ||
        urcb_frame.values.size() != analog_count ||
        !all_reason(urcb_frame, "general-interrogation") ||
        urcb_frame.raw_access_result_count !=
            7U + (2U * analog_count)) {
        return 7;
    }
    if (urcb.commit(urcb_plan, 1'000U) !=
        mms::MmsStaticUrcbStatus::ok) {
        return 8;
    }

    // Real IEDScout BRCB acceptance capture:
    // RptID ...$BR$Buffer, OptFlds 79 80, SqNum=1,
    // DataSet Digital, EntryID=1, ConfRev=100001, 36/36 included, GI reason.
    const mms::MmsStaticBrcbDefinition brcb_definition{
        kDomain,
        "LLN0$BR$Buffer01",
        kBrcbRptId,
        kDomain,
        kDigitalDataSet,
        kConfRev,
        kExpectedBrcbOptFlds,
        100U,
        0x7CU,
        0U,
    };
    std::array<std::array<std::uint8_t, 16'384U>, 4U> slot_storage{};
    std::array<mms::MmsStaticBrcbSlot, 4U> slots{{
        {slot_storage[0]},
        {slot_storage[1]},
        {slot_storage[2]},
        {slot_storage[3]},
    }};
    mms::MmsStaticBrcbPendingState brcb_pending{};
    mms::MmsStaticBrcbRuntime brcb{
        brcb_definition,
        brcb_pending,
        slots,
        object_table,
        data_set_table};
    if (!brcb.initialize() ||
        brcb.set_enabled(true) != mms::MmsStaticBrcbStatus::ok ||
        brcb.request_general_interrogation() !=
            mms::MmsStaticBrcbStatus::ok) {
        return 9;
    }
    mms::MmsStaticBrcbCapturePlan brcb_plan;
    if (!brcb.next_due(2'000U, brcb_plan) ||
        brcb_plan.sequence_number != 1U ||
        brcb_plan.entry_number != 1U ||
        brcb_plan.reason !=
            mms::MmsStaticBrcbCaptureReason::general_interrogation) {
        return 10;
    }
    std::array<std::uint8_t, 16'384U> brcb_staging{};
    std::array<std::uint8_t, 16'384U> brcb_workspace{};
    const auto brcb_gi = brcb.capture(
        brcb_plan, kReportTime, brcb_staging, brcb_workspace);
    if (!brcb_gi.success() ||
        brcb_gi.included_member_count != digital_count) {
        return 11;
    }
    mms::MmsStaticBrcbEntryView brcb_entry;
    if (!brcb.front(brcb_entry)) return 12;
    const auto brcb_gi_report =
        mms::MmsInformationReportCodec::decode(brcb_entry.mms_pdu);
    const auto brcb_gi_frame =
        mms::MmsReportFrameMapper::map(brcb_gi_report, {});
    if (brcb_gi_frame.header.report_id != kBrcbRptId ||
        !exact_optflds(brcb_gi_frame.header, kExpectedBrcbOptFlds) ||
        brcb_gi_frame.header.sequence_number != 1U ||
        !brcb_gi_frame.header.time_of_entry ||
        brcb_gi_frame.header.data_set_reference !=
            std::string{kDomain} + "/" + std::string{kDigitalDataSet} ||
        !entry_id_is(brcb_gi_frame.header.entry_id, 1U) ||
        brcb_gi_frame.header.configuration_revision != kConfRev ||
        !full_indexes(
            brcb_gi_frame.included_data_set_indexes, digital_count) ||
        brcb_gi_frame.values.size() != digital_count ||
        !all_reason(brcb_gi_frame, "general-interrogation") ||
        brcb_gi_frame.raw_access_result_count !=
            8U + (2U * digital_count)) {
        return 13;
    }
    if (brcb.commit_delivery(brcb_entry.entry_id) !=
        mms::MmsStaticBrcbStatus::ok) {
        return 14;
    }

    // The accepted OMICRON command capture produced a selective BRCB report
    // after process feedback. Its 36-bit inclusion field selects member 33 and
    // ReasonForInclusion is data-change. Lock SqNum/EntryID progression too.
    constexpr std::size_t changed_member = 33U;
    values[changed_member] = 1U;
    if (brcb.notify(
            changed_member,
            mms::MmsStaticBrcbEventReason::data_change,
            3'000U) != mms::MmsStaticBrcbStatus::ok ||
        brcb.next_due(3'099U, brcb_plan) ||
        !brcb.next_due(3'100U, brcb_plan) ||
        brcb_plan.sequence_number != 2U ||
        brcb_plan.entry_number != 2U ||
        brcb_plan.reason != mms::MmsStaticBrcbCaptureReason::event) {
        return 15;
    }
    const auto brcb_event = brcb.capture(
        brcb_plan, kReportTime, brcb_staging, brcb_workspace);
    if (!brcb_event.success() ||
        brcb_event.included_member_count != 1U ||
        !brcb.front(brcb_entry)) {
        return 16;
    }
    const auto brcb_event_report =
        mms::MmsInformationReportCodec::decode(brcb_entry.mms_pdu);
    const auto brcb_event_frame =
        mms::MmsReportFrameMapper::map(brcb_event_report, {});
    if (brcb_event_frame.header.sequence_number != 2U ||
        !entry_id_is(brcb_event_frame.header.entry_id, 2U) ||
        !exact_optflds(brcb_event_frame.header, kExpectedBrcbOptFlds) ||
        brcb_event_frame.included_data_set_indexes !=
            std::vector<std::size_t>{changed_member} ||
        brcb_event_frame.values.size() != 1U ||
        !brcb_event_frame.values[0].reason_for_inclusion.has(
            "data-change") ||
        brcb_event_frame.raw_access_result_count != 10U) {
        return 17;
    }
    if (brcb.commit_delivery(brcb_entry.entry_id) !=
        mms::MmsStaticBrcbStatus::ok) {
        return 18;
    }

    // The second accepted command toggles the same process member back and
    // produces the next buffered report. The real capture advanced both
    // sequence number and EntryID to 3 without resetting the RCB stream.
    values[changed_member] = 0U;
    if (brcb.notify(
            changed_member,
            mms::MmsStaticBrcbEventReason::data_change,
            4'000U) != mms::MmsStaticBrcbStatus::ok ||
        brcb.next_due(4'099U, brcb_plan) ||
        !brcb.next_due(4'100U, brcb_plan) ||
        brcb_plan.sequence_number != 3U ||
        brcb_plan.entry_number != 3U ||
        brcb_plan.reason != mms::MmsStaticBrcbCaptureReason::event) {
        return 19;
    }
    const auto brcb_event_two = brcb.capture(
        brcb_plan, kReportTime, brcb_staging, brcb_workspace);
    if (!brcb_event_two.success() ||
        brcb_event_two.included_member_count != 1U ||
        !brcb.front(brcb_entry)) {
        return 20;
    }
    const auto brcb_event_two_report =
        mms::MmsInformationReportCodec::decode(brcb_entry.mms_pdu);
    const auto brcb_event_two_frame =
        mms::MmsReportFrameMapper::map(brcb_event_two_report, {});
    if (brcb_event_two_frame.header.sequence_number != 3U ||
        !entry_id_is(brcb_event_two_frame.header.entry_id, 3U) ||
        !exact_optflds(brcb_event_two_frame.header, kExpectedBrcbOptFlds) ||
        brcb_event_two_frame.included_data_set_indexes !=
            std::vector<std::size_t>{changed_member} ||
        brcb_event_two_frame.values.size() != 1U ||
        !brcb_event_two_frame.values[0].reason_for_inclusion.has(
            "data-change") ||
        brcb_event_two_frame.raw_access_result_count != 10U) {
        return 21;
    }

    std::cout
        << "IEDSCOUT_REPORTING_GOLDEN_PASS "
        << "urcbMembers=22 urcbOptFlds=7880 urcbSqNum=1 "
        << "brcbMembers=36 brcbOptFlds=7980 "
        << "brcbGiSqNum=1 brcbGiEntryID=1 "
        << "eventIndex=33 eventReason=data-change "
        << "eventSqNum=2,3 eventEntryID=2,3\n";
    return 0;
}
