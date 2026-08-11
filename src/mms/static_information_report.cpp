// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_information_report.hpp"

#include "ariec61850/asn1/ber_span_reader.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {
namespace {

[[nodiscard]] std::span<const std::uint8_t> as_bytes(
    const std::string_view text) noexcept {
    return {
        reinterpret_cast<const std::uint8_t*>(text.data()),
        text.size()};
}

[[nodiscard]] bool valid_mms_data(
    const std::span<const std::uint8_t> encoded) noexcept {
    asn1::BerTlvView data;
    if (!asn1::BerSpanReader::try_read_exact(encoded, data) ||
        data.tag_class != asn1::BerClass::context_specific) {
        return false;
    }
    if (data.tag_number == 1 || data.tag_number == 2) {
        return data.constructed;
    }
    return data.tag_number >= 3 && data.tag_number <= 17 && !data.constructed;
}

[[nodiscard]] MmsStaticInformationReportEncodeResult make_status(
    const MmsStaticInformationReportStatus status,
    const std::size_t required_bytes = 0U,
    const std::size_t member_count = 0U) noexcept {
    return {status, 0U, required_bytes, member_count};
}

} // namespace

MmsStaticInformationReportEncodeResult
MmsStaticInformationReportEncoder::encode_data_set_snapshot_into(
    const MmsStaticObjectTable& objects,
    const MmsStaticDataSetTable& data_sets,
    const MmsObjectNameView& data_set_name,
    const MmsStaticInformationReportInput& report,
    const std::span<std::uint8_t> destination,
    const std::span<std::uint8_t> workspace) noexcept {
    if (!objects.valid()) {
        return make_status(MmsStaticInformationReportStatus::invalid_object_table);
    }
    if (!data_sets.valid()) {
        return make_status(MmsStaticInformationReportStatus::invalid_data_set_table);
    }

    const auto* data_set = data_sets.find(data_set_name);
    if (data_set == nullptr) {
        return make_status(MmsStaticInformationReportStatus::data_set_not_found);
    }
    if (data_set->members.empty() ||
        data_set->members.size() > MmsInformationReportSpanCodec::maximum_members) {
        return make_status(MmsStaticInformationReportStatus::invalid_data_set_table);
    }

    std::array<MmsInformationReportReferenceInput,
        MmsInformationReportSpanCodec::maximum_members> references{};
    std::array<MmsReadAccessResultInput,
        MmsInformationReportSpanCodec::maximum_members> results{};

    std::size_t workspace_offset = 0U;
    for (std::size_t index = 0U; index < data_set->members.size(); ++index) {
        const auto& member = data_set->members[index];
        references[index] = MmsInformationReportReferenceInput{
            member.domain, member.item};

        const MmsObjectNameView member_name{
            MmsObjectNameViewKind::domain_specific,
            as_bytes(member.domain),
            as_bytes(member.item)};
        const auto* object = objects.find(member_name);
        if (object == nullptr) {
            return make_status(
                MmsStaticInformationReportStatus::object_not_found,
                0U,
                data_set->members.size());
        }

        const auto remaining = workspace.subspan(workspace_offset);
        const auto read = object->read(object->context, remaining);
        if (read.status == wire::EncodeStatus::buffer_too_small) {
            if (read.required_bytes >
                std::numeric_limits<std::size_t>::max() - workspace_offset) {
                return make_status(
                    MmsStaticInformationReportStatus::backend_failure,
                    0U,
                    data_set->members.size());
            }
            return make_status(
                MmsStaticInformationReportStatus::workspace_too_small,
                workspace_offset + read.required_bytes,
                data_set->members.size());
        }
        if (!read.success() || read.bytes_written > remaining.size() ||
            !valid_mms_data(remaining.first(read.bytes_written))) {
            return make_status(
                MmsStaticInformationReportStatus::backend_failure,
                0U,
                data_set->members.size());
        }

        results[index] = MmsReadAccessResultInput{
            true,
            remaining.first(read.bytes_written),
            0U};
        workspace_offset += read.bytes_written;
    }

    MmsInformationReportSnapshotInput wire_report;
    wire_report.report_id = report.report_id;
    wire_report.optional_fields = report.optional_fields;
    wire_report.sequence_number = report.sequence_number;
    wire_report.report_time = report.report_time;
    wire_report.data_set_reference = MmsInformationReportReferenceInput{
        data_set->domain, data_set->item};
    wire_report.buffered = report.buffered;
    wire_report.buffer_overflow = report.buffer_overflow;
    wire_report.entry_id = report.entry_id;
    wire_report.conf_revision = report.conf_revision;
    wire_report.member_references =
        std::span<const MmsInformationReportReferenceInput>{references}.first(
            data_set->members.size());
    wire_report.member_results =
        std::span<const MmsReadAccessResultInput>{results}.first(
            data_set->members.size());
    wire_report.reason_for_inclusion = report.reason_for_inclusion;

    const auto encoded = MmsInformationReportSpanCodec::encode_snapshot_into(
        wire_report, destination);
    if (encoded.success()) {
        return {
            MmsStaticInformationReportStatus::response_ready,
            encoded.bytes_written,
            encoded.required_bytes,
            data_set->members.size()};
    }
    if (encoded.status == wire::EncodeStatus::buffer_too_small) {
        return make_status(
            MmsStaticInformationReportStatus::response_buffer_too_small,
            encoded.required_bytes,
            data_set->members.size());
    }
    return make_status(
        MmsStaticInformationReportStatus::invalid_report,
        encoded.required_bytes,
        data_set->members.size());
}

} // namespace ar::iec61850::mms
