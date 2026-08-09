// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/asn1/ber_span_reader.hpp"
#include "ariec61850/mms/static_urcb_runtime.hpp"

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

[[nodiscard]] std::span<const std::uint8_t> as_bytes(
    const std::string_view text) noexcept {
    return {
        reinterpret_cast<const std::uint8_t*>(text.data()),
        text.size()};
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
    destination[0] = 0x83U;
    destination[1] = 0x01U;
    destination[2] = *static_cast<const bool*>(context) ? 0xFFU : 0x00U;
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] bool decode_unsigned(
    const mms::MmsReadAccessResultView& result,
    std::uint32_t& value) noexcept {
    value = 0U;
    if (!result.success) {
        return false;
    }
    asn1::BerTlvView tlv;
    if (!asn1::BerSpanReader::try_read_exact(result.encoded_data, tlv) ||
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

[[nodiscard]] bool decode_boolean(
    const mms::MmsReadAccessResultView& result,
    bool& value) noexcept {
    value = false;
    if (!result.success) {
        return false;
    }
    asn1::BerTlvView tlv;
    if (!asn1::BerSpanReader::try_read_exact(result.encoded_data, tlv) ||
        tlv.tag_class != asn1::BerClass::context_specific ||
        tlv.tag_number != 3 || tlv.constructed || tlv.value.size() != 1U) {
        return false;
    }
    value = tlv.value[0] != 0U;
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
            7U,
            {0x7CU, 0x80U},
            0U,
            0x0CU,
            1'000U}};
    std::array<mms::MmsStaticUrcbState, 1U> states{};
    mms::MmsStaticUrcbRuntime runtime{
        definitions, states, object_table, data_set_table};
    if (!runtime.initialize() || !runtime.valid() || runtime.size() != 1U) {
        return 1;
    }

    const auto* initial = runtime.state(0U);
    if (initial == nullptr || initial->enabled || initial->sequence_number != 0U ||
        initial->report_id() != "LD0/LLN0$RP$Events" ||
        initial->data_set_domain() != "LD0" ||
        initial->data_set_item() != "LLN0$Events") {
        return 2;
    }

    std::size_t found = 99U;
    const mms::MmsObjectNameView rcb_name{
        mms::MmsObjectNameViewKind::domain_specific,
        as_bytes("LD0"),
        as_bytes("LLN0$RP$Events")};
    if (!runtime.find_index(rcb_name, found) || found != 0U) {
        return 3;
    }

    mms::MmsStaticUrcbEmissionPlan plan;
    if (runtime.next_due(0U, plan) ||
        runtime.request_general_interrogation(0U) !=
            mms::MmsStaticUrcbStatus::temporarily_unavailable) {
        return 4;
    }

    if (runtime.set_enabled(0U, true, 100U) != mms::MmsStaticUrcbStatus::ok) {
        return 5;
    }
    const auto* enabled = runtime.state(0U);
    if (enabled == nullptr || !enabled->enabled || !enabled->integrity_armed ||
        enabled->next_integrity_due_ms != 1'100U || enabled->sequence_number != 0U) {
        return 6;
    }
    if (runtime.set_report_id(0U, "blocked") !=
            mms::MmsStaticUrcbStatus::object_access_denied ||
        runtime.set_data_set(0U, "LD0", "LLN0$Events") !=
            mms::MmsStaticUrcbStatus::object_access_denied ||
        runtime.set_integrity_period_ms(0U, 500U) !=
            mms::MmsStaticUrcbStatus::object_access_denied) {
        return 7;
    }

    if (runtime.request_general_interrogation(0U) != mms::MmsStaticUrcbStatus::ok ||
        !runtime.next_due(100U, plan) || plan.index != 0U ||
        plan.reason != mms::MmsStaticUrcbReportReason::general_interrogation ||
        plan.sequence_number != 1U) {
        return 8;
    }

    std::array<std::uint8_t, 8U> tiny{};
    std::array<std::uint8_t, 1'024U> output{};
    std::array<std::uint8_t, 128U> workspace{};
    auto encoded = runtime.encode(plan, kReportTime, tiny, workspace);
    if (encoded.status != mms::MmsStaticUrcbStatus::response_buffer_too_small ||
        encoded.required_bytes <= tiny.size()) {
        return 9;
    }
    const auto* before_commit = runtime.state(0U);
    if (before_commit == nullptr || before_commit->sequence_number != 0U ||
        !before_commit->general_interrogation_pending) {
        return 10;
    }

    encoded = runtime.encode(plan, kReportTime, output, workspace);
    if (!encoded.success() || encoded.member_count != 2U) {
        return 11;
    }
    mms::MmsInformationReportView report;
    if (!mms::MmsInformationReportSpanCodec::try_decode_information_report(
            std::span<const std::uint8_t>{output}.first(encoded.bytes_written), report) ||
        report.item_count != 13U) {
        return 12;
    }
    mms::MmsReadAccessResultView item;
    std::uint32_t sequence = 0U;
    if (!report.try_item(2U, item) || !decode_unsigned(item, sequence) || sequence != 1U) {
        return 13;
    }
    bool relay_value = false;
    bool alarm_value = true;
    if (!report.try_item(9U, item) || !decode_boolean(item, relay_value) || !relay_value ||
        !report.try_item(10U, item) || !decode_boolean(item, alarm_value) || alarm_value) {
        return 14;
    }

    if (runtime.commit(plan, 100U) != mms::MmsStaticUrcbStatus::ok) {
        return 15;
    }
    const auto* after_gi = runtime.state(0U);
    if (after_gi == nullptr || after_gi->sequence_number != 1U ||
        after_gi->general_interrogation_pending ||
        after_gi->next_integrity_due_ms != 1'100U ||
        runtime.commit(plan, 100U) != mms::MmsStaticUrcbStatus::stale_plan) {
        return 16;
    }

    if (runtime.next_due(1'099U, plan) || !runtime.next_due(1'100U, plan) ||
        plan.reason != mms::MmsStaticUrcbReportReason::integrity ||
        plan.sequence_number != 2U) {
        return 17;
    }
    encoded = runtime.encode(plan, kReportTime, output, workspace);
    if (!encoded.success() || runtime.commit(plan, 1'100U) != mms::MmsStaticUrcbStatus::ok) {
        return 18;
    }
    const auto* after_integrity = runtime.state(0U);
    if (after_integrity == nullptr || after_integrity->sequence_number != 2U ||
        after_integrity->next_integrity_due_ms != 2'100U) {
        return 19;
    }

    if (!runtime.next_due(5'500U, plan) ||
        plan.reason != mms::MmsStaticUrcbReportReason::integrity ||
        runtime.commit(plan, 5'500U) != mms::MmsStaticUrcbStatus::ok) {
        return 20;
    }
    const auto* no_catch_up = runtime.state(0U);
    if (no_catch_up == nullptr || no_catch_up->sequence_number != 3U ||
        no_catch_up->next_integrity_due_ms != 6'500U) {
        return 21;
    }

    if (runtime.request_general_interrogation(0U) != mms::MmsStaticUrcbStatus::ok ||
        !runtime.next_due(5'500U, plan) ||
        runtime.set_reserved(0U, true) != mms::MmsStaticUrcbStatus::ok) {
        return 22;
    }
    encoded = runtime.encode(plan, kReportTime, output, workspace);
    if (encoded.status != mms::MmsStaticUrcbStatus::stale_plan ||
        !runtime.next_due(5'500U, plan) ||
        plan.reason != mms::MmsStaticUrcbReportReason::general_interrogation) {
        return 23;
    }
    encoded = runtime.encode(plan, kReportTime, output, workspace);
    if (!encoded.success() || runtime.commit(plan, 5'500U) != mms::MmsStaticUrcbStatus::ok) {
        return 24;
    }

    if (runtime.set_enabled(0U, false, 6'000U) != mms::MmsStaticUrcbStatus::ok ||
        runtime.request_general_interrogation(0U) !=
            mms::MmsStaticUrcbStatus::temporarily_unavailable) {
        return 25;
    }
    const std::array<std::uint8_t, 2U> invalid_optional{0x7CU, 0x40U};
    const std::array<std::uint8_t, 2U> compact_optional{0x5CU, 0x80U};
    if (runtime.set_report_id(0U, "LD0/LLN0$RP$Runtime") !=
            mms::MmsStaticUrcbStatus::ok ||
        runtime.set_data_set(0U, "LD0", "Missing") !=
            mms::MmsStaticUrcbStatus::data_set_not_found ||
        runtime.set_optional_fields(0U, invalid_optional) !=
            mms::MmsStaticUrcbStatus::invalid_value ||
        runtime.set_optional_fields(0U, compact_optional) !=
            mms::MmsStaticUrcbStatus::ok ||
        runtime.set_trigger_options(0U, 0x08U) != mms::MmsStaticUrcbStatus::ok ||
        runtime.set_integrity_period_ms(0U, 50U) != mms::MmsStaticUrcbStatus::ok ||
        runtime.set_buffer_time_ms(0U, 10U) != mms::MmsStaticUrcbStatus::ok) {
        return 26;
    }

    if (runtime.set_enabled(0U, true, 7'000U) != mms::MmsStaticUrcbStatus::ok) {
        return 27;
    }
    const auto* clamped = runtime.state(0U);
    if (clamped == nullptr || clamped->sequence_number != 0U ||
        clamped->next_integrity_due_ms != 7'100U ||
        runtime.next_due(7'099U, plan) || !runtime.next_due(7'100U, plan)) {
        return 28;
    }

    // Keep time below the integrity deadline and exercise repeated GI generation.
    // The two-phase plan must preserve every report across buffer retries and wrap SqNum at 8 bits.
    for (std::uint32_t iteration = 0U; iteration < 50'000U; ++iteration) {
        if (runtime.request_general_interrogation(0U) != mms::MmsStaticUrcbStatus::ok ||
            !runtime.next_due(7'000U, plan) ||
            plan.reason != mms::MmsStaticUrcbReportReason::general_interrogation) {
            return 29;
        }
        encoded = runtime.encode(plan, kReportTime, output, workspace);
        if (!encoded.success() ||
            runtime.commit(plan, 7'000U) != mms::MmsStaticUrcbStatus::ok) {
            return 30;
        }
    }
    const auto* wrapped = runtime.state(0U);
    if (wrapped == nullptr || wrapped->sequence_number != 80U ||
        wrapped->next_integrity_due_ms != 7'100U) {
        return 31;
    }

    const std::array<mms::MmsStaticUrcbDefinition, 2U> duplicates{
        definitions[0], definitions[0]};
    std::array<mms::MmsStaticUrcbState, 2U> duplicate_states{};
    mms::MmsStaticUrcbRuntime duplicate_runtime{
        duplicates, duplicate_states, object_table, data_set_table};
    if (duplicate_runtime.initialize()) {
        return 32;
    }

    return 0;
}
