// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/asn1/ber_span_reader.hpp"
#include "ariec61850/asn1/ber_span_writer.hpp"
#include "ariec61850/mms/buffered_selective_information_report_span.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace ar::iec61850::mms::detail {

inline constexpr std::uint8_t opt_sequence_number = 0x40U;
inline constexpr std::uint8_t opt_report_timestamp = 0x20U;
inline constexpr std::uint8_t opt_reason_for_inclusion = 0x10U;
inline constexpr std::uint8_t opt_data_set_name = 0x08U;
inline constexpr std::uint8_t opt_data_reference = 0x04U;
inline constexpr std::uint8_t opt_buffer_overflow = 0x02U;
inline constexpr std::uint8_t opt_entry_id = 0x01U;
inline constexpr std::uint8_t opt_conf_revision = 0x80U;

[[nodiscard]] inline bool optional_selected(
    const MmsBufferedSelectiveInformationReportSnapshotInput& report,
    const std::size_t byte_index,
    const std::uint8_t mask) noexcept {
    return report.optional_fields.size() > byte_index &&
        (report.optional_fields[byte_index] & mask) != 0U;
}

[[nodiscard]] inline bool visible_ascii(
    const std::string_view text,
    const std::size_t maximum_bytes) noexcept {
    if (text.empty() || text.size() > maximum_bytes) return false;
    for (const auto character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte == 0U || byte > 0x7FU) return false;
    }
    return true;
}

[[nodiscard]] inline std::size_t minimal_unsigned_size(std::uint32_t value) noexcept {
    std::size_t bytes = 1U;
    while (value > 0xFFU) {
        ++bytes;
        value >>= 8U;
    }
    return bytes;
}

[[nodiscard]] inline bool needs_positive_prefix(
    const std::uint32_t value,
    const std::size_t minimal_bytes) noexcept {
    const auto shift = static_cast<unsigned>((minimal_bytes - 1U) * 8U);
    return ((value >> shift) & 0x80U) != 0U;
}

[[nodiscard]] inline std::size_t positive_integer_size(const std::uint32_t value) noexcept {
    const auto minimal = minimal_unsigned_size(value);
    return minimal + (needs_positive_prefix(value, minimal) ? 1U : 0U);
}

[[nodiscard]] inline bool write_unsigned_content(
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

[[nodiscard]] inline bool write_positive_content(
    asn1::BerSpanWriter& writer,
    const std::uint32_t value) noexcept {
    const auto bytes = minimal_unsigned_size(value);
    if (needs_positive_prefix(value, bytes) && !writer.write_byte(0x00U)) return false;
    return write_unsigned_content(writer, value);
}

[[nodiscard]] inline bool valid_mms_data(const std::span<const std::uint8_t> encoded) noexcept {
    asn1::BerTlvView tlv;
    if (!asn1::BerSpanReader::try_read_exact(encoded, tlv) ||
        tlv.tag_class != asn1::BerClass::context_specific) return false;
    if (tlv.tag_number == 1 || tlv.tag_number == 2) return tlv.constructed;
    return tlv.tag_number >= 3 && tlv.tag_number <= 17 && !tlv.constructed;
}

[[nodiscard]] inline std::optional<std::size_t> access_result_size(
    const MmsReadAccessResultInput& result) noexcept {
    if (result.success) {
        return valid_mms_data(result.encoded_data)
            ? std::optional<std::size_t>{result.encoded_data.size()}
            : std::nullopt;
    }
    return asn1::BerSpanWriter::tlv_size(0, positive_integer_size(result.failure_code));
}

[[nodiscard]] inline bool write_access_result(
    asn1::BerSpanWriter& writer,
    const MmsReadAccessResultInput& result) noexcept {
    if (result.success) return writer.write_bytes(result.encoded_data);
    const auto content = positive_integer_size(result.failure_code);
    return writer.write_tlv_header(asn1::BerClass::context_specific, false, 0, content) &&
        write_positive_content(writer, result.failure_code);
}

[[nodiscard]] inline std::optional<std::size_t> reference_content_size(
    const MmsInformationReportReferenceInput& reference) noexcept {
    if (!visible_ascii(reference.domain, MmsServiceSpanCodec::maximum_identifier_bytes) ||
        !visible_ascii(reference.item, MmsServiceSpanCodec::maximum_identifier_bytes)) {
        return std::nullopt;
    }
    if (reference.item.size() > MmsInformationReportSpanCodec::maximum_reference_bytes - 1U ||
        reference.domain.size() >
            MmsInformationReportSpanCodec::maximum_reference_bytes - 1U - reference.item.size()) {
        return std::nullopt;
    }
    return reference.domain.size() + 1U + reference.item.size();
}

[[nodiscard]] inline bool write_reference(
    asn1::BerSpanWriter& writer,
    const MmsInformationReportReferenceInput& reference) noexcept {
    const auto content = reference_content_size(reference);
    return content &&
        writer.write_tlv_header(asn1::BerClass::context_specific, false, 10, *content) &&
        writer.write_bytes(std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(reference.domain.data()),
            reference.domain.size()}) &&
        writer.write_byte(static_cast<std::uint8_t>('/')) &&
        writer.write_bytes(std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(reference.item.data()),
            reference.item.size()});
}

[[nodiscard]] inline bool write_visible(
    asn1::BerSpanWriter& writer,
    const std::string_view text) noexcept {
    return writer.write_tlv_header(asn1::BerClass::context_specific, false, 10, text.size()) &&
        writer.write_bytes(std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

[[nodiscard]] inline bool write_bit_string(
    asn1::BerSpanWriter& writer,
    const std::uint8_t unused_bits,
    const std::span<const std::uint8_t> bytes) noexcept {
    return unused_bits <= 7U &&
        writer.write_tlv_header(asn1::BerClass::context_specific, false, 4, 1U + bytes.size()) &&
        writer.write_byte(unused_bits) && writer.write_bytes(bytes);
}

[[nodiscard]] inline bool write_unsigned(
    asn1::BerSpanWriter& writer,
    const std::uint32_t value) noexcept {
    const auto content = minimal_unsigned_size(value);
    return writer.write_tlv_header(asn1::BerClass::context_specific, false, 6, content) &&
        write_unsigned_content(writer, value);
}

[[nodiscard]] inline bool write_boolean(
    asn1::BerSpanWriter& writer,
    const bool value) noexcept {
    return writer.write_tlv_header(asn1::BerClass::context_specific, false, 3, 1U) &&
        writer.write_byte(value ? 0x01U : 0x00U);
}

[[nodiscard]] inline bool add_to(
    std::size_t& total,
    const std::optional<std::size_t> value) noexcept {
    if (!value || *value > std::numeric_limits<std::size_t>::max() - total) return false;
    total += *value;
    return true;
}

} // namespace ar::iec61850::mms::detail
