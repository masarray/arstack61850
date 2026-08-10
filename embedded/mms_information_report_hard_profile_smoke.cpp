// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/information_report_span.hpp"
#include "ariec61850/mms/static_data_set_table.hpp"
#include "ariec61850/mms/static_information_report.hpp"
#include "ariec61850/mms/static_object_table.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

using namespace ar::iec61850;

constexpr std::array<std::uint8_t, 2U> kBooleanType{0x83U, 0x00U};
constexpr std::array<std::uint8_t, 3U> kInt32Type{0x85U, 0x01U, 0x20U};
constexpr std::array<std::uint8_t, 3U> kBooleanTrue{0x83U, 0x01U, 0x01U};
constexpr std::array<std::uint8_t, 3U> kBooleanFalse{0x83U, 0x01U, 0x00U};
constexpr std::array<std::uint8_t, 3U> kIntData{0x85U, 0x01U, 0x2AU};
constexpr std::array<std::uint8_t, 8U> kFirstReference{
    0x8AU, 0x06U, 0x4CU, 0x44U, 0x30U, 0x2FU, 0x52U, 0x31U};
constexpr std::array<std::uint8_t, 4U> kReason{
    0x84U, 0x02U, 0x02U, 0x08U};
constexpr std::array<std::uint8_t, 2U> kOptionalFields{0x7CU, 0x80U};
constexpr std::array<std::uint8_t, 2U> kBadSegmentationFields{0x7CU, 0xC0U};
constexpr std::array<std::uint8_t, 6U> kReportTime{
    0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x01U};
constexpr std::array<std::uint8_t, 3U> kLd0{0x4CU, 0x44U, 0x30U};
constexpr std::array<std::uint8_t, 11U> kEvents{
    0x4CU, 0x4CU, 0x4EU, 0x30U, 0x24U,
    0x45U, 0x76U, 0x65U, 0x6EU, 0x74U, 0x73U};
constexpr std::array<std::uint8_t, 3U> kRpt{0x52U, 0x50U, 0x54U};

constexpr std::array<std::uint8_t, 88U> kExpectedReport{
    0xA3U, 0x56U, 0xA0U, 0x54U, 0xA1U, 0x05U, 0x80U, 0x03U,
    0x52U, 0x50U, 0x54U, 0xA0U, 0x4BU, 0x8AU, 0x03U, 0x72U,
    0x70U, 0x74U, 0x84U, 0x03U, 0x06U, 0x7CU, 0x80U, 0x86U,
    0x01U, 0x01U, 0x8CU, 0x06U, 0x00U, 0x00U, 0x00U, 0x01U,
    0x00U, 0x01U, 0x8AU, 0x0FU, 0x4CU, 0x44U, 0x30U, 0x2FU,
    0x4CU, 0x4CU, 0x4EU, 0x30U, 0x24U, 0x45U, 0x76U, 0x65U,
    0x6EU, 0x74U, 0x73U, 0x86U, 0x01U, 0x01U, 0x84U, 0x02U,
    0x06U, 0xC0U, 0x8AU, 0x06U, 0x4CU, 0x44U, 0x30U, 0x2FU,
    0x52U, 0x31U, 0x8AU, 0x06U, 0x4CU, 0x44U, 0x30U, 0x2FU,
    0x4DU, 0x31U, 0x83U, 0x01U, 0x01U, 0x85U, 0x01U, 0x2AU,
    0x84U, 0x02U, 0x02U, 0x08U, 0x84U, 0x02U, 0x02U, 0x08U};

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
    destination[0] = 0x83U;
    destination[1] = 0x01U;
    destination[2] = *static_cast<const bool*>(context) ? 0x01U : 0x00U;
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] wire::EncodeResult read_small_int32(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 3U;
    if (context == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    const auto value = *static_cast<const std::uint8_t*>(context);
    if (value > 0x7FU) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    destination[0] = 0x85U;
    destination[1] = 0x01U;
    destination[2] = value;
    return {wire::EncodeStatus::ok, required, required};
}

} // namespace

int main() {
    const std::array<mms::MmsInformationReportReferenceInput, 2U> references{
        mms::MmsInformationReportReferenceInput{"LD0", "R1"},
        mms::MmsInformationReportReferenceInput{"LD0", "M1"}};
    const std::array<mms::MmsReadAccessResultInput, 2U> member_results{
        mms::MmsReadAccessResultInput{true, kBooleanTrue, 0U},
        mms::MmsReadAccessResultInput{true, kIntData, 0U}};

    mms::MmsInformationReportSnapshotInput report;
    report.report_id = "rpt";
    report.optional_fields = kOptionalFields;
    report.sequence_number = 1U;
    report.report_time = kReportTime;
    report.data_set_reference = {"LD0", "LLN0$Events"};
    report.buffered = false;
    report.conf_revision = 1U;
    report.member_references = references;
    report.member_results = member_results;
    report.reason_for_inclusion = 0x08U;

    std::array<std::uint8_t, 512U> buffer{};
    const auto encoded = mms::MmsInformationReportSpanCodec::encode_snapshot_into(
        report, buffer);
    if (!encoded.success() || encoded.bytes_written != kExpectedReport.size() ||
        !matches(
            std::span<const std::uint8_t>{buffer}.first(encoded.bytes_written),
            kExpectedReport)) {
        return 1;
    }

    mms::MmsInformationReportView decoded;
    if (!mms::MmsInformationReportSpanCodec::try_decode_information_report(
            std::span<const std::uint8_t>{buffer}.first(encoded.bytes_written),
            decoded) ||
        decoded.variable_list_name.kind != mms::MmsObjectNameViewKind::vmd_specific ||
        !matches(decoded.variable_list_name.item, kRpt) || decoded.item_count != 13U) {
        return 2;
    }

    mms::MmsReadAccessResultView item;
    if (!decoded.try_item(7U, item) || !item.success ||
        !matches(item.encoded_data, kFirstReference) ||
        !decoded.try_item(9U, item) || !item.success ||
        !matches(item.encoded_data, kBooleanTrue) ||
        !decoded.try_item(10U, item) || !item.success ||
        !matches(item.encoded_data, kIntData) ||
        !decoded.try_item(11U, item) || !item.success ||
        !matches(item.encoded_data, kReason) || decoded.try_item(13U, item)) {
        return 3;
    }

    std::array<std::uint8_t, 16U> small_response{};
    const auto too_small = mms::MmsInformationReportSpanCodec::encode_snapshot_into(
        report, small_response);
    if (too_small.status != wire::EncodeStatus::buffer_too_small ||
        too_small.required_bytes != kExpectedReport.size()) {
        return 4;
    }

    auto invalid_report = report;
    invalid_report.optional_fields = kBadSegmentationFields;
    if (mms::MmsInformationReportSpanCodec::encode_snapshot_into(
            invalid_report, buffer).status != wire::EncodeStatus::value_out_of_range) {
        return 5;
    }

    std::array<mms::MmsReadAccessResultInput, 2U> failure_results{
        member_results[0],
        mms::MmsReadAccessResultInput{false, {}, 10U}};
    auto failure_report = report;
    failure_report.member_results = failure_results;
    const auto failure_encoded = mms::MmsInformationReportSpanCodec::encode_snapshot_into(
        failure_report, buffer);
    if (!failure_encoded.success() ||
        !mms::MmsInformationReportSpanCodec::try_decode_information_report(
            std::span<const std::uint8_t>{buffer}.first(failure_encoded.bytes_written),
            decoded) ||
        !decoded.try_item(10U, item) || item.success || item.failure_code != 10U) {
        return 6;
    }

    bool relay_state = true;
    std::uint8_t meter_value = 42U;
    const std::array<mms::MmsStaticObjectEntry, 2U> objects{
        mms::MmsStaticObjectEntry{
            "LD0", "R1", kBooleanType, read_boolean, &relay_state, false},
        mms::MmsStaticObjectEntry{
            "LD0", "M1", kInt32Type, read_small_int32, &meter_value, false}};
    const mms::MmsStaticObjectTable object_table{objects};
    const std::array<mms::MmsStaticDataSetMember, 2U> event_members{
        mms::MmsStaticDataSetMember{"LD0", "R1"},
        mms::MmsStaticDataSetMember{"LD0", "M1"}};
    const std::array<mms::MmsStaticDataSetEntry, 1U> data_sets{
        mms::MmsStaticDataSetEntry{"LD0", "LLN0$Events", event_members, false}};
    const mms::MmsStaticDataSetTable data_set_table{data_sets};
    if (!object_table.valid() || !data_set_table.valid() ||
        !data_set_table.valid_against(object_table)) {
        return 7;
    }

    const mms::MmsObjectNameView data_set_name{
        mms::MmsObjectNameViewKind::domain_specific,
        kLd0,
        kEvents};
    mms::MmsStaticInformationReportInput static_report;
    static_report.report_id = "rpt";
    static_report.optional_fields = kOptionalFields;
    static_report.sequence_number = 1U;
    static_report.report_time = kReportTime;
    static_report.buffered = false;
    static_report.conf_revision = 1U;
    static_report.reason_for_inclusion = 0x08U;

    std::array<std::uint8_t, 16U> workspace{};
    const auto static_encoded =
        mms::MmsStaticInformationReportEncoder::encode_data_set_snapshot_into(
            object_table,
            data_set_table,
            data_set_name,
            static_report,
            buffer,
            workspace);
    if (!static_encoded.success() || static_encoded.member_count != 2U ||
        static_encoded.bytes_written != kExpectedReport.size() ||
        !matches(
            std::span<const std::uint8_t>{buffer}.first(static_encoded.bytes_written),
            kExpectedReport)) {
        return 8;
    }

    // The report has no shadow value store: changing the callback-owned source is
    // visible in the next DataSet snapshot without rebuilding the DataSet model.
    relay_state = false;
    const auto changed =
        mms::MmsStaticInformationReportEncoder::encode_data_set_snapshot_into(
            object_table,
            data_set_table,
            data_set_name,
            static_report,
            buffer,
            workspace);
    if (!changed.success() ||
        !mms::MmsInformationReportSpanCodec::try_decode_information_report(
            std::span<const std::uint8_t>{buffer}.first(changed.bytes_written),
            decoded) ||
        !decoded.try_item(9U, item) || !item.success ||
        !matches(item.encoded_data, kBooleanFalse)) {
        return 9;
    }

    std::array<std::uint8_t, 2U> tiny_workspace{};
    const auto workspace_shortage =
        mms::MmsStaticInformationReportEncoder::encode_data_set_snapshot_into(
            object_table,
            data_set_table,
            data_set_name,
            static_report,
            buffer,
            tiny_workspace);
    if (workspace_shortage.status !=
            mms::MmsStaticInformationReportStatus::workspace_too_small ||
        workspace_shortage.required_bytes != 3U) {
        return 10;
    }

    for (std::uint32_t iteration = 0U; iteration < 50'000U; ++iteration) {
        report.sequence_number = iteration;
        const auto repeated = mms::MmsInformationReportSpanCodec::encode_snapshot_into(
            report, buffer);
        mms::MmsInformationReportView repeated_decoded;
        if (!repeated.success() ||
            !mms::MmsInformationReportSpanCodec::try_decode_information_report(
                std::span<const std::uint8_t>{buffer}.first(repeated.bytes_written),
                repeated_decoded) ||
            repeated_decoded.item_count != 13U) {
            return 11;
        }
    }

    return 0;
}
