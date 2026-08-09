// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/osi/presentation_span.hpp"

#include "ariec61850/asn1/ber_span_reader.hpp"
#include "ariec61850/asn1/ber_span_writer.hpp"
#include "ariec61850/osi/session_span.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace ar::iec61850::osi {
namespace {

[[nodiscard]] std::size_t unsigned_integer_size(std::uint32_t value) noexcept {
    std::size_t size = 1U;
    while (value > 0xFFU) {
        ++size;
        value >>= 8U;
    }
    return size;
}

[[nodiscard]] bool write_unsigned_integer(
    asn1::BerSpanWriter& writer,
    const std::uint32_t value) noexcept {
    const auto size = unsigned_integer_size(value);
    for (std::size_t index = size; index-- > 0U;) {
        const auto shift = static_cast<unsigned>(index * 8U);
        if (!writer.write_byte(static_cast<std::uint8_t>(
                (value >> shift) & 0xFFU))) {
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

struct FullyEncodedSizes final {
    std::size_t id_value{};
    std::size_t id_tlv{};
    std::size_t payload_tlv{};
    std::size_t sequence_content{};
    std::size_t sequence_tlv{};
    std::size_t outer_tlv{};
};

[[nodiscard]] std::optional<FullyEncodedSizes> calculate_sizes(
    const std::uint32_t context_id,
    const std::size_t payload_bytes) noexcept {
    if (context_id == 0U || payload_bytes > PresentationSpanCodec::maximum_ppdu_bytes) {
        return std::nullopt;
    }

    FullyEncodedSizes sizes;
    sizes.id_value = unsigned_integer_size(context_id);
    const auto id_tlv = asn1::BerSpanWriter::tlv_size(2, sizes.id_value);
    const auto payload_tlv = asn1::BerSpanWriter::tlv_size(0, payload_bytes);
    const auto sequence_content = add_size(id_tlv, payload_tlv);
    if (!id_tlv || !payload_tlv || !sequence_content) {
        return std::nullopt;
    }
    const auto sequence_tlv = asn1::BerSpanWriter::tlv_size(16, *sequence_content);
    if (!sequence_tlv) {
        return std::nullopt;
    }
    const auto outer_tlv = asn1::BerSpanWriter::tlv_size(1, *sequence_tlv);
    if (!outer_tlv || *outer_tlv > PresentationSpanCodec::maximum_ppdu_bytes) {
        return std::nullopt;
    }

    sizes.id_tlv = *id_tlv;
    sizes.payload_tlv = *payload_tlv;
    sizes.sequence_content = *sequence_content;
    sizes.sequence_tlv = *sequence_tlv;
    sizes.outer_tlv = *outer_tlv;
    return sizes;
}

} // namespace

std::optional<std::size_t> PresentationSpanCodec::fully_encoded_data_size(
    const std::uint32_t context_id,
    const std::size_t single_asn1_type_bytes) noexcept {
    const auto sizes = calculate_sizes(context_id, single_asn1_type_bytes);
    return sizes ? std::optional<std::size_t>{sizes->outer_tlv} : std::nullopt;
}

wire::EncodeResult PresentationSpanCodec::encode_fully_encoded_data_into(
    const std::uint32_t context_id,
    const std::span<const std::uint8_t> single_asn1_type,
    const std::span<std::uint8_t> destination) noexcept {
    const auto sizes = calculate_sizes(context_id, single_asn1_type.size());
    if (!sizes) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (destination.size() < sizes->outer_tlv) {
        return {wire::EncodeStatus::buffer_too_small, 0U, sizes->outer_tlv};
    }

    asn1::BerSpanWriter writer{destination.first(sizes->outer_tlv)};
    if (!writer.write_tlv_header(
            asn1::BerClass::application, true, 1, sizes->sequence_tlv) ||
        !writer.write_tlv_header(
            asn1::BerClass::universal, true, 16, sizes->sequence_content) ||
        !writer.write_tlv_header(
            asn1::BerClass::universal, false, 2, sizes->id_value) ||
        !write_unsigned_integer(writer, context_id) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific,
            true,
            0,
            single_asn1_type.size()) ||
        !writer.write_bytes(single_asn1_type) ||
        writer.size() != sizes->outer_tlv) {
        return {wire::EncodeStatus::value_out_of_range, 0U, sizes->outer_tlv};
    }
    return {wire::EncodeStatus::ok, sizes->outer_tlv, sizes->outer_tlv};
}

bool PresentationSpanCodec::try_decode_fully_encoded_data_view(
    const std::span<const std::uint8_t> bytes,
    PresentationPdvView& pdv) noexcept {
    pdv = {};
    if (bytes.empty() || bytes.size() > maximum_ppdu_bytes) {
        return false;
    }

    asn1::BerTlvView outer;
    if (!asn1::BerSpanReader::try_read_exact(bytes, outer) ||
        outer.tag_class != asn1::BerClass::application ||
        outer.tag_number != 1 || !outer.constructed) {
        return false;
    }

    asn1::BerTlvView sequence;
    if (!asn1::BerSpanReader::try_read_exact(outer.value, sequence) ||
        sequence.tag_class != asn1::BerClass::universal ||
        sequence.tag_number != 16 || !sequence.constructed) {
        return false;
    }

    bool saw_context_id = false;
    bool saw_payload = false;
    std::size_t offset = 0U;
    while (offset < sequence.value.size()) {
        asn1::BerTlvView field;
        if (!asn1::BerSpanReader::try_read_tlv(sequence.value, offset, field)) {
            pdv = {};
            return false;
        }

        if (field.tag_class == asn1::BerClass::universal &&
            field.tag_number == 2 && !field.constructed) {
            if (saw_context_id) {
                pdv = {};
                return false;
            }
            const auto value = asn1::BerSpanReader::read_uint32(field);
            if (!value || *value == 0U) {
                pdv = {};
                return false;
            }
            pdv.context_id = *value;
            saw_context_id = true;
        } else if (field.tag_class == asn1::BerClass::context_specific &&
                   field.tag_number == 0 && field.constructed) {
            if (saw_payload) {
                pdv = {};
                return false;
            }
            pdv.single_asn1_type = field.value;
            saw_payload = true;
        } else {
            pdv = {};
            return false;
        }
    }

    if (!saw_context_id || !saw_payload) {
        pdv = {};
        return false;
    }
    return true;
}

wire::EncodeResult PresentationSpanCodec::encode_p_data_into(
    const std::span<const std::uint8_t> abstract_syntax_payload,
    const std::span<std::uint8_t> destination,
    const std::uint32_t presentation_context_id,
    const bool include_give_tokens_prefix) noexcept {
    const auto fully_encoded = fully_encoded_data_size(
        presentation_context_id, abstract_syntax_payload.size());
    if (!fully_encoded) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    const auto prefix = include_give_tokens_prefix ? 4U : 2U;
    if (*fully_encoded > std::numeric_limits<std::size_t>::max() - prefix) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto required = prefix + *fully_encoded;
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }

    std::size_t offset = 0U;
    if (include_give_tokens_prefix) {
        destination[offset++] = SessionSpanCodec::data_transfer_code;
        destination[offset++] = 0x00U;
    }
    destination[offset++] = SessionSpanCodec::data_transfer_code;
    destination[offset++] = 0x00U;

    const auto result = encode_fully_encoded_data_into(
        presentation_context_id,
        abstract_syntax_payload,
        destination.subspan(offset, *fully_encoded));
    if (!result.success() || result.bytes_written != *fully_encoded) {
        return {result.status, 0U, required};
    }
    return {wire::EncodeStatus::ok, required, required};
}

bool PresentationSpanCodec::try_decode_p_data_view(
    const std::span<const std::uint8_t> bytes,
    PresentationPdvView& pdv) noexcept {
    pdv = {};
    std::span<const std::uint8_t> presentation = bytes;
    SessionDataTransferView transfer;
    if (!bytes.empty() && bytes.front() == SessionSpanCodec::data_transfer_code) {
        if (!SessionSpanCodec::try_decode_data_transfer_view(bytes, transfer)) {
            return false;
        }
        presentation = transfer.presentation_payload;
    }
    return try_decode_fully_encoded_data_view(presentation, pdv);
}

} // namespace ar::iec61850::osi
