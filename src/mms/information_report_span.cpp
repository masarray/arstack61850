// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/information_report_span.hpp"

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
constexpr std::uint8_t kOptBufferOverflow = 0x02U;
constexpr std::uint8_t kOptEntryId = 0x01U;
constexpr std::uint8_t kOptConfRevision = 0x80U;
constexpr std::uint8_t kOptSegmentation = 0x40U;

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

[[nodiscard]] bool span_equals(
    const std::span<const std::uint8_t> bytes,
    const std::string_view text) noexcept {
    if (bytes.size() != text.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        if (bytes[index] != static_cast<std::uint8_t>(
                static_cast<unsigned char>(text[index]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::size_t> add_size(
    const std::optional<std::size_t> left,
    const std::optional<std::size_t> right) noexcept {
    if (!left || !right || *right > std::numeric_limits<std::size_t>::max() - *left) {
        return std::nullopt;
    }
    return *left + *right;
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

[[nodiscard]] bool decode_access_result(
    const std::span<const std::uint8_t> encoded,
    MmsReadAccessResultView& result) noexcept {
    result = {};
    asn1::BerTlvView tlv;
    if (!asn1::BerSpanReader::try_read_exact(encoded, tlv)) {
        return false;
    }
    if (tlv.tag_class == asn1::BerClass::context_specific &&
        tlv.tag_number == 0 && !tlv.constructed) {
        const auto failure = asn1::BerSpanReader::read_uint32(tlv);
        if (!failure) {
            return false;
        }
        result.success = false;
        result.failure_code = *failure;
        return true;
    }
    if (!valid_mms_data_tlv(tlv)) {
        return false;
    }
    result.success = true;
    result.encoded_data = encoded;
    return true;
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
    return asn1::BerSpanWriter::tlv_size(0, positive_integer_size(result.failure_code));
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
    if (reference.item.size() >
        MmsInformationReportSpanCodec::maximum_reference_bytes - 1U ||
        reference.domain.size() >
        MmsInformationReportSpanCodec::maximum_reference_bytes - 1U - reference.item.size()) {
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

[[nodiscard]] bool write_boolean_data(
    asn1::BerSpanWriter& writer,
    const bool value) noexcept {
    return writer.write_tlv_header(
               asn1::BerClass::context_specific, false, 3, 1U) &&
        writer.write_byte(value ? 0x01U : 0x00U);
}

[[nodiscard]] bool optional_selected(
    const MmsInformationReportSnapshotInput& report,
    const std::size_t byte_index,
    const std::uint8_t mask) noexcept {
    return report.optional_fields.size() > byte_index &&
        (report.optional_fields[byte_index] & mask) != 0U;
}

[[nodiscard]] bool validate_report(
    const MmsInformationReportSnapshotInput& report) noexcept {
    if (!visible_ascii(
            report.report_id,
            MmsInformationReportSpanCodec::maximum_report_id_bytes) ||
        report.optional_fields.size() !=
            MmsInformationReportSpanCodec::optional_field_bytes ||
        report.member_results.empty() ||
        report.member_results.size() > MmsInformationReportSpanCodec::maximum_members ||
        report.member_references.size() != report.member_results.size()) {
        return false;
    }
    if ((report.optional_fields[0] & 0x80U) != 0U ||
        (report.optional_fields[1] & kOptSegmentation) != 0U ||
        (report.optional_fields[1] & 0x3FU) != 0U) {
        return false;
    }
    if (!reference_content_size(report.data_set_reference)) {
        return false;
    }
    for (const auto& reference : report.member_references) {
        if (!reference_content_size(reference)) {
            return false;
        }
    }
    if (optional_selected(report, 0U, kOptReportTimeStamp) &&
        report.report_time.size() != MmsInformationReportSpanCodec::binary_time_bytes) {
        return false;
    }
    if ((optional_selected(report, 0U, kOptBufferOverflow) ||
         optional_selected(report, 0U, kOptEntryId)) && !report.buffered) {
        return false;
    }
    if (optional_selected(report, 0U, kOptEntryId) &&
        report.entry_id.size() != MmsInformationReportSpanCodec::entry_id_bytes) {
        return false;
    }
    if (optional_selected(report, 0U, kOptReasonForInclusion) &&
        (report.reason_for_inclusion & 0x03U) != 0U) {
        return false;
    }
    for (const auto& result : report.member_results) {
        if (!access_result_size(result)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::size_t> access_list_content_size(
    const MmsInformationReportSnapshotInput& report) noexcept {
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
    if (optional_selected(report, 0U, kOptBufferOverflow) &&
        !add_to(total, asn1::BerSpanWriter::tlv_size(3, 1U))) {
        return std::nullopt;
    }
    if (optional_selected(report, 0U, kOptEntryId) &&
        !add_to(total, asn1::BerSpanWriter::tlv_size(9, report.entry_id.size()))) {
        return std::nullopt;
    }
    if (optional_selected(report, 1U, kOptConfRevision) &&
        !add_to(total, asn1::BerSpanWriter::tlv_size(
            6, minimal_unsigned_size(report.conf_revision)))) {
        return std::nullopt;
    }

    const auto inclusion_bytes = (report.member_results.size() + 7U) / 8U;
    if (!add_to(total, asn1::BerSpanWriter::tlv_size(4, 1U + inclusion_bytes))) {
        return std::nullopt;
    }

    if (optional_selected(report, 0U, kOptDataReference)) {
        for (const auto& reference : report.member_references) {
            const auto content = reference_content_size(reference);
            if (!content || !add_to(total, asn1::BerSpanWriter::tlv_size(10, *content))) {
                return std::nullopt;
            }
        }
    }

    for (const auto& result : report.member_results) {
        if (!add_to(total, access_result_size(result))) {
            return std::nullopt;
        }
    }

    if (optional_selected(report, 0U, kOptReasonForInclusion)) {
        const auto reason_tlv = asn1::BerSpanWriter::tlv_size(4, 2U);
        for (std::size_t index = 0U; index < report.member_results.size(); ++index) {
            if (!add_to(total, reason_tlv)) {
                return std::nullopt;
            }
        }
    }
    return total;
}

[[nodiscard]] bool write_inclusion(
    asn1::BerSpanWriter& writer,
    const std::size_t member_count) noexcept {
    const auto bytes = (member_count + 7U) / 8U;
    const auto unused = static_cast<std::uint8_t>((bytes * 8U) - member_count);
    if (!writer.write_tlv_header(
            asn1::BerClass::context_specific, false, 4, 1U + bytes) ||
        !writer.write_byte(unused)) {
        return false;
    }
    for (std::size_t index = 0U; index < bytes; ++index) {
        const auto first_member = index * 8U;
        const auto remaining = member_count - first_member;
        const auto used = remaining >= 8U ? 8U : remaining;
        const auto value = used == 8U
            ? std::uint8_t{0xFFU}
            : static_cast<std::uint8_t>(0xFFU << (8U - used));
        if (!writer.write_byte(value)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool MmsInformationReportView::try_item(
    const std::size_t index,
    MmsReadAccessResultView& result) const noexcept {
    result = {};
    if (index >= item_count) {
        return false;
    }
    std::size_t offset = 0U;
    std::size_t current = 0U;
    while (offset < access_result_list.size()) {
        const auto start = offset;
        asn1::BerTlvView item;
        if (!asn1::BerSpanReader::try_read_tlv(access_result_list, offset, item)) {
            return false;
        }
        if (current == index) {
            return decode_access_result(
                access_result_list.subspan(start, offset - start), result);
        }
        ++current;
    }
    return false;
}

bool MmsInformationReportSpanCodec::try_decode_information_report(
    const std::span<const std::uint8_t> mms_pdu,
    MmsInformationReportView& report) noexcept {
    report = {};
    asn1::BerTlvView outer;
    if (!asn1::BerSpanReader::try_read_exact(mms_pdu, outer) ||
        outer.tag_class != asn1::BerClass::context_specific ||
        outer.tag_number != 3 || !outer.constructed) {
        return false;
    }

    asn1::BerTlvView service;
    if (!asn1::BerSpanReader::try_read_exact(outer.value, service) ||
        service.tag_class != asn1::BerClass::context_specific ||
        service.tag_number != 0 || !service.constructed) {
        return false;
    }

    std::size_t offset = 0U;
    asn1::BerTlvView variable_access;
    asn1::BerTlvView access_results;
    if (!asn1::BerSpanReader::try_read_tlv(service.value, offset, variable_access) ||
        !asn1::BerSpanReader::try_read_tlv(service.value, offset, access_results) ||
        offset != service.value.size() ||
        variable_access.tag_class != asn1::BerClass::context_specific ||
        variable_access.tag_number != 1 || !variable_access.constructed ||
        access_results.tag_class != asn1::BerClass::context_specific ||
        access_results.tag_number != 0 || !access_results.constructed) {
        return false;
    }

    MmsObjectNameView list_name;
    if (!MmsServiceSpanCodec::try_decode_object_name_view(
            variable_access.value, list_name) ||
        list_name.kind != MmsObjectNameViewKind::vmd_specific ||
        !span_equals(list_name.item, "RPT")) {
        return false;
    }

    std::size_t result_offset = 0U;
    std::size_t count = 0U;
    while (result_offset < access_results.value.size()) {
        if (count >= maximum_access_results) {
            return false;
        }
        const auto start = result_offset;
        asn1::BerTlvView item;
        if (!asn1::BerSpanReader::try_read_tlv(
                access_results.value, result_offset, item)) {
            return false;
        }
        MmsReadAccessResultView decoded;
        if (!decode_access_result(
                access_results.value.subspan(start, result_offset - start), decoded)) {
            return false;
        }
        ++count;
    }
    if (count == 0U) {
        return false;
    }

    report.variable_list_name = list_name;
    report.access_result_list = access_results.value;
    report.item_count = count;
    return true;
}

wire::EncodeResult MmsInformationReportSpanCodec::encode_snapshot_into(
    const MmsInformationReportSnapshotInput& report,
    const std::span<std::uint8_t> destination) noexcept {
    if (!validate_report(report)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    const auto access_content = access_list_content_size(report);
    const auto access_tlv = access_content
        ? asn1::BerSpanWriter::tlv_size(0, *access_content)
        : std::nullopt;
    const auto vmd_name_tlv = asn1::BerSpanWriter::tlv_size(0, 3U);
    const auto variable_access_tlv = vmd_name_tlv
        ? asn1::BerSpanWriter::tlv_size(1, *vmd_name_tlv)
        : std::nullopt;
    const auto information_content = add_size(variable_access_tlv, access_tlv);
    const auto service_tlv = information_content
        ? asn1::BerSpanWriter::tlv_size(0, *information_content)
        : std::nullopt;
    const auto required = service_tlv
        ? asn1::BerSpanWriter::tlv_size(3, *service_tlv)
        : std::nullopt;
    if (!access_content || !access_tlv || !vmd_name_tlv ||
        !variable_access_tlv || !information_content || !service_tlv || !required ||
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
    if (optional_selected(report, 0U, kOptBufferOverflow) &&
        !write_boolean_data(writer, report.buffer_overflow)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }
    if (optional_selected(report, 0U, kOptEntryId) &&
        (!writer.write_tlv_header(
             asn1::BerClass::context_specific, false, 9, report.entry_id.size()) ||
         !writer.write_bytes(report.entry_id))) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }
    if (optional_selected(report, 1U, kOptConfRevision) &&
        !write_unsigned_data(writer, report.conf_revision)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }
    if (!write_inclusion(writer, report.member_results.size())) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }

    if (optional_selected(report, 0U, kOptDataReference)) {
        for (const auto& reference : report.member_references) {
            if (!write_reference_data(writer, reference)) {
                return {wire::EncodeStatus::value_out_of_range, 0U, *required};
            }
        }
    }
    for (const auto& result : report.member_results) {
        if (!write_access_result(writer, result)) {
            return {wire::EncodeStatus::value_out_of_range, 0U, *required};
        }
    }
    if (optional_selected(report, 0U, kOptReasonForInclusion)) {
        const std::uint8_t reason = report.reason_for_inclusion;
        for (std::size_t index = 0U; index < report.member_results.size(); ++index) {
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
