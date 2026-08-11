// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/buffered_selective_information_report_span.hpp"
#include "ariec61850/mms/buffered_selective_report_detail.hpp"

#include "ariec61850/asn1/ber_span_writer.hpp"
#include "ariec61850/mms/pdu_span.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace ar::iec61850::mms {
namespace {

[[nodiscard]] bool validate_report(
    const MmsBufferedSelectiveInformationReportSnapshotInput& report) noexcept {
    using namespace detail;
    if (!visible_ascii(report.report_id, MmsInformationReportSpanCodec::maximum_report_id_bytes) ||
        report.optional_fields.size() != MmsInformationReportSpanCodec::optional_field_bytes ||
        (report.optional_fields[0] & 0x80U) != 0U ||
        (report.optional_fields[1] & 0x7FU) != 0U ||
        report.data_set_member_count == 0U ||
        report.data_set_member_count > MmsInformationReportSpanCodec::maximum_members ||
        report.included_member_indices.empty() ||
        report.included_member_indices.size() > report.data_set_member_count ||
        report.included_member_references.size() != report.included_member_indices.size() ||
        report.included_member_results.size() != report.included_member_indices.size() ||
        !reference_content_size(report.data_set_reference)) {
        return false;
    }
    if (optional_selected(report, 0U, opt_report_timestamp) &&
        report.report_time.size() != MmsInformationReportSpanCodec::binary_time_bytes) {
        return false;
    }
    if (optional_selected(report, 0U, opt_entry_id)) {
        if (report.entry_id.size() != MmsInformationReportSpanCodec::entry_id_bytes) return false;
    } else if (!report.entry_id.empty()) {
        return false;
    }
    if (optional_selected(report, 0U, opt_reason_for_inclusion)) {
        if (report.included_reason_for_inclusion.size() !=
            report.included_member_indices.size()) return false;
        for (const auto reason : report.included_reason_for_inclusion) {
            if (reason == 0U || (reason & 0x03U) != 0U) return false;
        }
    } else if (!report.included_reason_for_inclusion.empty()) {
        return false;
    }

    std::size_t previous = 0U;
    bool first = true;
    for (const auto index : report.included_member_indices) {
        if (index >= report.data_set_member_count || (!first && index <= previous)) return false;
        previous = index;
        first = false;
    }
    for (const auto& reference : report.included_member_references) {
        if (!reference_content_size(reference)) return false;
    }
    for (const auto& result : report.included_member_results) {
        if (!access_result_size(result)) return false;
    }
    return true;
}

[[nodiscard]] std::optional<std::size_t> access_content_size(
    const MmsBufferedSelectiveInformationReportSnapshotInput& report) noexcept {
    using namespace detail;
    std::size_t total = 0U;
    if (!add_to(total, asn1::BerSpanWriter::tlv_size(10, report.report_id.size())) ||
        !add_to(total, asn1::BerSpanWriter::tlv_size(4, 1U + report.optional_fields.size()))) {
        return std::nullopt;
    }
    if (optional_selected(report, 0U, opt_sequence_number) &&
        !add_to(total, asn1::BerSpanWriter::tlv_size(
            6, minimal_unsigned_size(report.sequence_number)))) return std::nullopt;
    if (optional_selected(report, 0U, opt_report_timestamp) &&
        !add_to(total, asn1::BerSpanWriter::tlv_size(12, report.report_time.size()))) {
        return std::nullopt;
    }
    if (optional_selected(report, 0U, opt_data_set_name)) {
        const auto content = reference_content_size(report.data_set_reference);
        if (!content || !add_to(total, asn1::BerSpanWriter::tlv_size(10, *content))) {
            return std::nullopt;
        }
    }
    if (optional_selected(report, 0U, opt_buffer_overflow) &&
        !add_to(total, asn1::BerSpanWriter::tlv_size(3, 1U))) return std::nullopt;
    if (optional_selected(report, 0U, opt_entry_id) &&
        !add_to(total, asn1::BerSpanWriter::tlv_size(9, report.entry_id.size()))) {
        return std::nullopt;
    }
    if (optional_selected(report, 1U, opt_conf_revision) &&
        !add_to(total, asn1::BerSpanWriter::tlv_size(
            6, minimal_unsigned_size(report.conf_revision)))) return std::nullopt;

    const auto inclusion_bytes = (report.data_set_member_count + 7U) / 8U;
    if (!add_to(total, asn1::BerSpanWriter::tlv_size(4, 1U + inclusion_bytes))) {
        return std::nullopt;
    }
    if (optional_selected(report, 0U, opt_data_reference)) {
        for (const auto& reference : report.included_member_references) {
            const auto content = reference_content_size(reference);
            if (!content || !add_to(total, asn1::BerSpanWriter::tlv_size(10, *content))) {
                return std::nullopt;
            }
        }
    }
    for (const auto& result : report.included_member_results) {
        if (!add_to(total, access_result_size(result))) return std::nullopt;
    }
    if (optional_selected(report, 0U, opt_reason_for_inclusion)) {
        const auto reason_tlv = asn1::BerSpanWriter::tlv_size(4, 2U);
        for (std::size_t index = 0U; index < report.included_member_indices.size(); ++index) {
            if (!add_to(total, reason_tlv)) return std::nullopt;
        }
    }
    return total;
}

[[nodiscard]] bool write_inclusion(
    asn1::BerSpanWriter& writer,
    const std::size_t member_count,
    const std::span<const std::size_t> included) noexcept {
    const auto byte_count = (member_count + 7U) / 8U;
    const auto unused = static_cast<std::uint8_t>((byte_count * 8U) - member_count);
    if (!writer.write_tlv_header(asn1::BerClass::context_specific, false, 4, 1U + byte_count) ||
        !writer.write_byte(unused)) return false;

    std::size_t included_offset = 0U;
    for (std::size_t byte_index = 0U; byte_index < byte_count; ++byte_index) {
        std::uint8_t value = 0U;
        while (included_offset < included.size() && included[included_offset] / 8U == byte_index) {
            const auto bit = included[included_offset] % 8U;
            value = static_cast<std::uint8_t>(value | static_cast<std::uint8_t>(0x80U >> bit));
            ++included_offset;
        }
        if (!writer.write_byte(value)) return false;
    }
    return included_offset == included.size();
}

} // namespace

wire::EncodeResult MmsBufferedSelectiveInformationReportSpanCodec::encode_snapshot_into(
    const MmsBufferedSelectiveInformationReportSnapshotInput& report,
    const std::span<std::uint8_t> destination) noexcept {
    using namespace detail;
    if (!validate_report(report)) return {wire::EncodeStatus::value_out_of_range, 0U, 0U};

    const auto access_content = access_content_size(report);
    const auto access_tlv = access_content ? asn1::BerSpanWriter::tlv_size(0, *access_content) : std::nullopt;
    const auto vmd_name_tlv = asn1::BerSpanWriter::tlv_size(0, 3U);
    const auto variable_access_tlv = vmd_name_tlv ? asn1::BerSpanWriter::tlv_size(1, *vmd_name_tlv) : std::nullopt;
    std::optional<std::size_t> information_content;
    if (access_tlv && variable_access_tlv &&
        *access_tlv <= std::numeric_limits<std::size_t>::max() - *variable_access_tlv) {
        information_content = *variable_access_tlv + *access_tlv;
    }
    const auto service_tlv = information_content ? asn1::BerSpanWriter::tlv_size(0, *information_content) : std::nullopt;
    const auto required = service_tlv ? asn1::BerSpanWriter::tlv_size(3, *service_tlv) : std::nullopt;
    if (!access_content || !access_tlv || !vmd_name_tlv || !variable_access_tlv ||
        !information_content || !service_tlv || !required ||
        *required > MmsPduSpanCodec::maximum_pdu_bytes) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (destination.size() < *required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, *required};
    }

    asn1::BerSpanWriter writer{destination.first(*required)};
    if (!writer.write_tlv_header(asn1::BerClass::context_specific, true, 3, *service_tlv) ||
        !writer.write_tlv_header(asn1::BerClass::context_specific, true, 0, *information_content) ||
        !writer.write_tlv_header(asn1::BerClass::context_specific, true, 1, *vmd_name_tlv) ||
        !writer.write_tlv_header(asn1::BerClass::context_specific, false, 0, 3U) ||
        !writer.write_bytes(std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>("RPT"), 3U}) ||
        !writer.write_tlv_header(asn1::BerClass::context_specific, true, 0, *access_content) ||
        !write_visible(writer, report.report_id) ||
        !write_bit_string(writer, 6U, report.optional_fields)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }

    if (optional_selected(report, 0U, opt_sequence_number) &&
        !write_unsigned(writer, report.sequence_number)) return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    if (optional_selected(report, 0U, opt_report_timestamp) &&
        (!writer.write_tlv_header(asn1::BerClass::context_specific, false, 12, report.report_time.size()) ||
         !writer.write_bytes(report.report_time))) return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    if (optional_selected(report, 0U, opt_data_set_name) && !write_reference(writer, report.data_set_reference)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }
    if (optional_selected(report, 0U, opt_buffer_overflow) && !write_boolean(writer, report.buffer_overflow)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }
    if (optional_selected(report, 0U, opt_entry_id) &&
        (!writer.write_tlv_header(asn1::BerClass::context_specific, false, 9, report.entry_id.size()) ||
         !writer.write_bytes(report.entry_id))) return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    if (optional_selected(report, 1U, opt_conf_revision) && !write_unsigned(writer, report.conf_revision)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }
    if (!write_inclusion(writer, report.data_set_member_count, report.included_member_indices)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }
    if (optional_selected(report, 0U, opt_data_reference)) {
        for (const auto& reference : report.included_member_references) {
            if (!write_reference(writer, reference)) return {wire::EncodeStatus::value_out_of_range, 0U, *required};
        }
    }
    for (const auto& result : report.included_member_results) {
        if (!write_access_result(writer, result)) return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }
    if (optional_selected(report, 0U, opt_reason_for_inclusion)) {
        for (const auto reason : report.included_reason_for_inclusion) {
            if (!write_bit_string(writer, 2U, std::span<const std::uint8_t>{&reason, 1U})) {
                return {wire::EncodeStatus::value_out_of_range, 0U, *required};
            }
        }
    }
    if (writer.size() != *required) return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    return {wire::EncodeStatus::ok, *required, *required};
}

} // namespace ar::iec61850::mms
