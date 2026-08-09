// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/selective_information_report_span.hpp"

#include "ariec61850/asn1/ber_span_reader.hpp"
#include "ariec61850/asn1/ber_span_writer.hpp"
#include "ariec61850/mms/pdu_span.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {
namespace {

constexpr std::uint8_t kOptSequenceNumber = 0x40U;
constexpr std::uint8_t kOptReportTimeStamp = 0x20U;
constexpr std::uint8_t kOptReasonForInclusion = 0x10U;
constexpr std::uint8_t kOptDataSetName = 0x08U;
constexpr std::uint8_t kOptDataReference = 0x04U;
constexpr std::uint8_t kOptConfRevision = 0x80U;

[[nodiscard]] bool visible_ascii(
    const std::string_view text,
    const std::size_t maximum_bytes) noexcept {
    if (text.empty() || text.size() > maximum_bytes) {
        return false;
    }
    for (const auto character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte == 0U || byte > 0x7FU) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool optional_selected(
    const MmsSelectiveInformationReportSnapshotInput& report,
    const std::size_t byte_index,
    const std::uint8_t mask) noexcept {
    return report.optional_fields.size() > byte_index &&
        (report.optional_fields[byte_index] & mask) != 0U;
}

[[nodiscard]] std::size_t minimal_unsigned_size(std::uint32_t value) noexcept {
    std::size_t bytes = 1U;
    while (value > 0xFFU) {
        ++bytes;
        value >>= 8U;
    }
    return bytes;
}

[[nodiscard]] bool needs_positive_prefix(
    const std::uint32_t value,
    const std::size_t minimal_bytes) noexcept {
    const auto shift = static_cast<unsigned>((minimal_bytes - 1U) * 8U);
    return ((value >> shift) & 0x80U) != 0U;
}

[[nodiscard]] std::size_t positive_integer_size(const std::uint32_t value) noexcept {
    const auto minimal = minimal_unsigned_size(value);
    return minimal + (needs_positive_prefix(value, minimal) ? 1U : 0U);
}

[[nodiscard]] bool write_unsigned_content(
    asn1::BerSpanWriter& writer,
    const std::uint32_t value) noexcept {
    const auto bytes = minimal_unsigned_size(value);
    for (std::size_t index = bytes; index-- > 0U;) {
        const auto shift = static_cast<unsigned>(index * 8U);
        if (!writer.write_byte(static_cast<std::uint8_t>((value >> shift) & 0xFFU))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool write_positive_content(
    asn1::BerSpanWriter& writer,
    const std::uint32_t value) noexcept {
    const auto bytes = minimal_unsigned_size(value);
    if (needs_positive_prefix(value, bytes) && !writer.write_byte(0x00U)) {
        return false;
    }
    return write_unsigned_content(writer, value);
}

[[nodiscard]] bool valid_mms_data_tlv(const asn1::BerTlvView& tlv) noexcept {
    if (tlv.tag_class != asn1::BerClass::context_specific) {
        return false;
    }
    if (tlv.tag_number == 1 || tlv.tag_number == 2) {
        return tlv.constructed;
    }
    return tlv.tag_number >= 3 && tlv.tag_number <= 17 && !tlv.constructed;
}

[[nodiscard]] std::optional<std::size_t> access_result_size(
    const MmsReadAccessResultInput& result) noexcept {
    if (result.success) {
        asn1::BerTlvView data;
        if (!asn1::BerSpanReader::try_read_exact(result.encoded_data, data) ||
            !valid_mms_data_tlv(data)) {
            return std::nullopt;
        }
        return result.encoded_data.size();
    }
    return asn1::BerSpanWriter::tlv_size(
        0, positive_integer_size(result.failure_code));
}

[[nodiscard]] bool write_access_result(
    asn1::BerSpanWriter& writer,
    const MmsReadAccessResultInput& result) noexcept {
    if (result.success) {
        return writer.write_bytes(result.encoded_data);
    }
    const auto content = positive_integer_size(result.failure_code);
    return writer.write_tlv_header(
               asn1::BerClass::context_specific, false, 0, content) &&
        write_positive_content(writer, result.failure_code);
}

[[nodiscard]] std::optional<std::size_t> reference_content_size(
    const MmsInformationReportReferenceInput& reference) noexcept {
    if (!visible_ascii(reference.domain, MmsServiceSpanCodec::maximum_identifier_bytes) ||
        !visible_ascii(reference.item, MmsServiceSpanCodec::maximum_identifier_bytes)) {
        return std::nullopt;
    }
    if (reference.item.size() > MmsInformationReportSpanCodec::maximum_reference_bytes - 1U ||
        reference.domain.size() >
            MmsInformationReportSpanCodec::maximum_reference_bytes - 1U -
                reference.item.size()) {
        return std::nullopt;
    }
    return reference.domain.size() + 1U + reference.item.size();
}

[[nodiscard]] bool write_reference_data(
    asn1::BerSpanWriter& writer,
    const MmsInformationReportReferenceInput& reference) noexcept {
    const auto content = reference_content_size(reference);
    if (!content ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, false, 10, *content) ||
        !writer.write_bytes(std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(reference.domain.data()),
            reference.domain.size()}) ||
        !writer.write_byte(static_cast<std::uint8_t>('/')) ||
        !writer.write_bytes(std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(reference.item.data()),
            reference.item.size()})) {
        return false;
    }
    return true;
}

[[nodiscard]] bool write_visible_data(
    asn1::BerSpanWriter& writer,
    const std::string_view text) noexcept {
    return writer.write_tlv_header(
               asn1::BerClass::context_specific, false, 10, text.size()) &&
        writer.write_bytes(std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

[[nodiscard]] bool write_bit_string_data(
    asn1::BerSpanWriter& writer,
    const std::uint8_t unused_bits,
    const std::span<const std::uint8_t> bytes) noexcept {
    return unused_bits <= 7U &&
        writer.write_tlv_header(
            asn1::BerClass::context_specific, false, 4, 1U + bytes.size()) &&
        writer.write_byte(unused_bits) && writer.write_bytes(bytes);
}

[[nodiscard]] bool write_unsigned_data(
    asn1::BerSpanWriter& writer,
    const std::uint32_t value) noexcept {
    const auto content = minimal_unsigned_size(value);
    return writer.write_tlv_header(
               asn1::BerClass::context_specific, false, 6, content) &&
        write_unsigned_content(writer, value);
}

[[nodiscard]] bool validate_report(
    const MmsSelectiveInformationReportSnapshotInput& report) noexcept {
    if (!visible_ascii(
            report.report_id,
            MmsInformationReportSpanCodec::maximum_report_id_bytes) ||
        report.optional_fields.size() != MmsInformationReportSpanCodec::optional_field_bytes ||
        (report.optional_fields[0] & 0x83U) != 0U ||
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
    if (optional_selected(report, 0U, kOptReportTimeStamp) &&
        report.report_time.size() != MmsInformationReportSpanCodec::binary_time_bytes) {
        return false;
    }
    if (optional_selected(report, 0U, kOptReasonForInclusion)) {
        if (report.included_reason_for_inclusion.size() !=
            report.included_member_indices.size()) {
            return false;
        }
        for (const auto reason : report.included_reason_for_inclusion) {
            if (reason == 0U || (reason & 0x03U) != 0U) {
                return false;
            }
        }
    } else if (!report.included_reason_for_inclusion.empty()) {
        return false;
    }

    std::size_t previous = 0U;
    bool first = true;
    for (const auto index : report.included_member_indices) {
        if (index >= report.data_set_member_count || (!first && index <= previous)) {
            return false;
        }
        previous = index;
        first = false;
    }
    for (const auto& reference : report.included_member_references) {
        if (!reference_content_size(reference)) {
            return false;
        }
    }
    for (const auto& result : report.included_member_results) {
        if (!access_result_size(result)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool add_to(
    std::size_t& total,
    const std::optional<std::size_t> value) noexcept {
    if (!value || *value > std::numeric_limits<std::size_t>::max() - total) {
        return false;
    }
    total += *value;
    return true;
}

[[nodiscard]] std::optional<std::size_t> access_content_size(
    const MmsSelectiveInformationReportSnapshotInput& report) noexcept {
    std::size_t total = 0U;
    if (!add_to(total, asn1::BerSpanWriter::tlv_size(10, report.report_id.size())) ||
        !add_to(total, asn1::BerSpanWriter::tlv_size(
            4, 1U + report.optional_fields.size()))) {
        return std::nullopt;
    }
    if (optional_selected(report, 0U, kOptSequenceNumber) &&
        !add_to(total, asn1::BerSpanWriter::tlv_size(
            6, minimal_unsigned_size(report.sequence_number)))) {
        return std::nullopt;
    }
    if (optional_selected(report, 0U, kOptReportTimeStamp) &&
        !add_to(total, asn1::BerSpanWriter::tlv_size(12, report.report_time.size()))) {
        return std::nullopt;
    }
    if (optional_selected(report, 0U, kOptDataSetName)) {
        const auto content = reference_content_size(report.data_set_reference);
        if (!content || !add_to(total, asn1::BerSpanWriter::tlv_size(10, *content))) {
            return std::nullopt;
        }
    }
    if (optional_selected(report, 1U, kOptConfRevision) &&
        !add_to(total, asn1::BerSpanWriter::tlv_size(
            6, minimal_unsigned_size(report.conf_revision)))) {
        return std::nullopt;
    }

    const auto inclusion_bytes = (report.data_set_member_count + 7U) / 8U;
    if (!add_to(total, asn1::BerSpanWriter::tlv_size(4, 1U + inclusion_bytes))) {
        return std::nullopt;
    }
    if (optional_selected(report, 0U, kOptDataReference)) {
        for (const auto& reference : report.included_member_references) {
            const auto content = reference_content_size(reference);
            if (!content || !add_to(total, asn1::BerSpanWriter::tlv_size(10, *content))) {
                return std::nullopt;
            }
        }
    }
    for (const auto& result : report.included_member_results) {
        if (!add_to(total, access_result_size(result))) {
            return std::nullopt;
        }
    }
    if (optional_selected(report, 0U, kOptReasonForInclusion)) {
        const auto reason_tlv = asn1::BerSpanWriter::tlv_size(4, 2U);
        for (std::size_t index = 0U; index < report.included_member_indices.size(); ++index) {
            if (!add_to(total, reason_tlv)) {
                return std::nullopt;
            }
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
    if (!writer.write_tlv_header(
            asn1::BerClass::context_specific, false, 4, 1U + byte_count) ||
        !writer.write_byte(unused)) {
        return false;
    }

    std::size_t included_offset = 0U;
    for (std::size_t byte_index = 0U; byte_index < byte_count; ++byte_index) {
        std::uint8_t value = 0U;
        while (included_offset < included.size() &&
               included[included_offset] / 8U == byte_index) {
            const auto bit = included[included_offset] % 8U;
            value = static_cast<std::uint8_t>(
                value | static_cast<std::uint8_t>(0x80U >> bit));
            ++included_offset;
        }
        if (!writer.write_byte(value)) {
            return false;
        }
    }
    return included_offset == included.size();
}

} // namespace

wire::EncodeResult MmsSelectiveInformationReportSpanCodec::encode_snapshot_into(
    const MmsSelectiveInformationReportSnapshotInput& report,
    const std::span<std::uint8_t> destination) noexcept {
    if (!validate_report(report)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto access_content = access_content_size(report);
    const auto access_tlv = access_content
        ? asn1::BerSpanWriter::tlv_size(0, *access_content)
        : std::nullopt;
    const auto vmd_name_tlv = asn1::BerSpanWriter::tlv_size(0, 3U);
    const auto variable_access_tlv = vmd_name_tlv
        ? asn1::BerSpanWriter::tlv_size(1, *vmd_name_tlv)
        : std::nullopt;
    std::optional<std::size_t> information_content;
    if (access_tlv && variable_access_tlv &&
        *access_tlv <= std::numeric_limits<std::size_t>::max() - *variable_access_tlv) {
        information_content = *variable_access_tlv + *access_tlv;
    }
    const auto service_tlv = information_content
        ? asn1::BerSpanWriter::tlv_size(0, *information_content)
        : std::nullopt;
    const auto required = service_tlv
        ? asn1::BerSpanWriter::tlv_size(3, *service_tlv)
        : std::nullopt;
    if (!access_content || !access_tlv || !vmd_name_tlv || !variable_access_tlv ||
        !information_content || !service_tlv || !required ||
        *required > MmsPduSpanCodec::maximum_pdu_bytes) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (destination.size() < *required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, *required};
    }

    asn1::BerSpanWriter writer{destination.first(*required)};
    if (!writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 3, *service_tlv) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 0, *information_content) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 1, *vmd_name_tlv) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, false, 0, 3U) ||
        !writer.write_bytes(std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>("RPT"), 3U}) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 0, *access_content) ||
        !write_visible_data(writer, report.report_id) ||
        !write_bit_string_data(writer, 6U, report.optional_fields)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }

    if (optional_selected(report, 0U, kOptSequenceNumber) &&
        !write_unsigned_data(writer, report.sequence_number)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }
    if (optional_selected(report, 0U, kOptReportTimeStamp) &&
        (!writer.write_tlv_header(
             asn1::BerClass::context_specific, false, 12, report.report_time.size()) ||
         !writer.write_bytes(report.report_time))) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }
    if (optional_selected(report, 0U, kOptDataSetName) &&
        !write_reference_data(writer, report.data_set_reference)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }
    if (optional_selected(report, 1U, kOptConfRevision) &&
        !write_unsigned_data(writer, report.conf_revision)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }
    if (!write_inclusion(
            writer, report.data_set_member_count, report.included_member_indices)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }
    if (optional_selected(report, 0U, kOptDataReference)) {
        for (const auto& reference : report.included_member_references) {
            if (!write_reference_data(writer, reference)) {
                return {wire::EncodeStatus::value_out_of_range, 0U, *required};
            }
        }
    }
    for (const auto& result : report.included_member_results) {
        if (!write_access_result(writer, result)) {
            return {wire::EncodeStatus::value_out_of_range, 0U, *required};
        }
    }
    if (optional_selected(report, 0U, kOptReasonForInclusion)) {
        for (const auto reason : report.included_reason_for_inclusion) {
            if (!write_bit_string_data(
                    writer, 2U, std::span<const std::uint8_t>{&reason, 1U})) {
                return {wire::EncodeStatus::value_out_of_range, 0U, *required};
            }
        }
    }
    if (writer.size() != *required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }
    return {wire::EncodeStatus::ok, *required, *required};
}

} // namespace ar::iec61850::mms
