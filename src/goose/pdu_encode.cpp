// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/goose/pdu_codec.hpp"

#include "ariec61850/asn1/ber_span_writer.hpp"
#include "ariec61850/mms/data_span_codec.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>

namespace ar::iec61850::goose {
namespace {

constexpr std::int32_t kGooseApplicationTag = 1;

[[nodiscard]] std::size_t unsigned_integer_size(std::uint64_t value) noexcept {
    std::size_t size = 1U;
    while (value > 0xFFU) {
        ++size;
        value >>= 8U;
    }
    return size;
}

[[nodiscard]] std::optional<std::size_t> add_size(
    const std::size_t left,
    const std::optional<std::size_t> right) noexcept {
    if (!right || *right > std::numeric_limits<std::size_t>::max() - left) {
        return std::nullopt;
    }
    return left + *right;
}

[[nodiscard]] std::optional<std::size_t> primitive_size(
    const std::int32_t tag,
    const std::size_t value_size) noexcept {
    return asn1::BerSpanWriter::tlv_size(tag, value_size);
}

[[nodiscard]] std::optional<std::size_t> unsigned_field_size(
    const std::int32_t tag,
    const std::uint64_t value) noexcept {
    return primitive_size(tag, unsigned_integer_size(value));
}

[[nodiscard]] bool write_unsigned_value(
    asn1::BerSpanWriter& writer,
    const std::uint64_t value) noexcept {
    const auto size = unsigned_integer_size(value);
    for (std::size_t index = size; index-- > 0U;) {
        const auto shift = static_cast<unsigned>(index * 8U);
        if (!writer.write_byte(
                static_cast<std::uint8_t>((value >> shift) & 0xFFU))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool write_unsigned_field(
    asn1::BerSpanWriter& writer,
    const std::int32_t tag,
    const std::uint64_t value) noexcept {
    const auto size = unsigned_integer_size(value);
    return writer.write_tlv_header(
               asn1::BerClass::context_specific, false, tag, size) &&
        write_unsigned_value(writer, value);
}

[[nodiscard]] bool write_string_field(
    asn1::BerSpanWriter& writer,
    const std::int32_t tag,
    const std::string& value) noexcept {
    if (!writer.write_tlv_header(
            asn1::BerClass::context_specific, false, tag, value.size())) {
        return false;
    }
    for (const auto character : value) {
        if (!writer.write_byte(static_cast<std::uint8_t>(character))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::size_t> content_size(
    const GoosePdu& pdu) noexcept {
    std::array<std::uint8_t, 8U> timestamp{};
    if (!pdu.timestamp.try_write_bytes(timestamp)) {
        return std::nullopt;
    }

    const auto all_data_size = mms::MmsDataSpanCodec::encoded_all_size(pdu.values);
    if (!all_data_size ||
        pdu.values.size() > static_cast<std::size_t>(
            std::numeric_limits<std::uint32_t>::max())) {
        return std::nullopt;
    }

    std::size_t total = 0U;
    const auto add = [&total](const std::optional<std::size_t> size) noexcept {
        const auto next = add_size(total, size);
        if (!next) {
            return false;
        }
        total = *next;
        return true;
    };

    return add(primitive_size(0, pdu.go_cb_ref.size())) &&
            add(unsigned_field_size(1, pdu.time_allowed_to_live_milliseconds)) &&
            add(primitive_size(2, pdu.data_set_reference.size())) &&
            add(primitive_size(3, pdu.go_id.size())) &&
            add(primitive_size(4, timestamp.size())) &&
            add(unsigned_field_size(5, pdu.state_number)) &&
            add(unsigned_field_size(6, pdu.sequence_number)) &&
            add(primitive_size(7, 1U)) &&
            add(unsigned_field_size(8, pdu.configuration_revision)) &&
            add(primitive_size(9, 1U)) &&
            add(unsigned_field_size(
                10,
                static_cast<std::uint64_t>(pdu.values.size()))) &&
            add(asn1::BerSpanWriter::tlv_size(11, *all_data_size))
        ? std::optional<std::size_t>{total}
        : std::nullopt;
}

} // namespace

std::optional<std::size_t> GoosePduCodec::encoded_size(
    const GoosePdu& pdu) noexcept {
    const auto content = content_size(pdu);
    return content
        ? asn1::BerSpanWriter::tlv_size(kGooseApplicationTag, *content)
        : std::nullopt;
}

wire::EncodeResult GoosePduCodec::encode_into(
    const GoosePdu& pdu,
    const std::span<std::uint8_t> destination) noexcept {
    const auto required = encoded_size(pdu);
    if (!required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (destination.size() < *required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, *required};
    }

    const auto content = content_size(pdu);
    const auto all_data_size = mms::MmsDataSpanCodec::encoded_all_size(pdu.values);
    std::array<std::uint8_t, 8U> timestamp{};
    if (!content || !all_data_size || !pdu.timestamp.try_write_bytes(timestamp)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }

    asn1::BerSpanWriter writer{destination.first(*required)};
    if (!writer.write_tlv_header(
            asn1::BerClass::application,
            true,
            kGooseApplicationTag,
            *content) ||
        !write_string_field(writer, 0, pdu.go_cb_ref) ||
        !write_unsigned_field(writer, 1, pdu.time_allowed_to_live_milliseconds) ||
        !write_string_field(writer, 2, pdu.data_set_reference) ||
        !write_string_field(writer, 3, pdu.go_id) ||
        !writer.write_tlv(
            asn1::BerClass::context_specific,
            false,
            4,
            timestamp) ||
        !write_unsigned_field(writer, 5, pdu.state_number) ||
        !write_unsigned_field(writer, 6, pdu.sequence_number) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, false, 7, 1U) ||
        !writer.write_byte(pdu.test ? 0x01U : 0x00U) ||
        !write_unsigned_field(writer, 8, pdu.configuration_revision) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, false, 9, 1U) ||
        !writer.write_byte(pdu.needs_commissioning ? 0x01U : 0x00U) ||
        !write_unsigned_field(
            writer,
            10,
            static_cast<std::uint64_t>(pdu.values.size())) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific,
            true,
            11,
            *all_data_size)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }

    const auto all_data_result = mms::MmsDataSpanCodec::encode_all_into(
        pdu.values,
        destination.first(*required).subspan(writer.size(), *all_data_size));
    if (!all_data_result.success() ||
        all_data_result.bytes_written != *all_data_size) {
        return {all_data_result.status, 0U, *required};
    }

    const auto written = writer.size() + all_data_result.bytes_written;
    if (written != *required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }
    return {wire::EncodeStatus::ok, written, *required};
}

} // namespace ar::iec61850::goose
