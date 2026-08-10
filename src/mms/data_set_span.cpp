// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/data_set_span.hpp"

#include "ariec61850/asn1/ber_span_reader.hpp"
#include "ariec61850/asn1/ber_span_writer.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {
namespace {

[[nodiscard]] bool valid_identifier(const std::string_view value) noexcept {
    if (value.empty() || value.size() > MmsServiceSpanCodec::maximum_identifier_bytes) {
        return false;
    }
    for (const char ch : value) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte == 0U || byte > 0x7FU) {
            return false;
        }
    }
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

[[nodiscard]] bool write_positive_integer(
    asn1::BerSpanWriter& writer,
    const std::uint32_t value) noexcept {
    const auto minimal = minimal_unsigned_size(value);
    if (needs_positive_prefix(value, minimal) && !writer.write_byte(0x00U)) {
        return false;
    }
    for (std::size_t index = minimal; index-- > 0U;) {
        const auto shift = static_cast<unsigned>(index * 8U);
        if (!writer.write_byte(static_cast<std::uint8_t>((value >> shift) & 0xFFU))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::size_t> checked_add(
    const std::optional<std::size_t> left,
    const std::optional<std::size_t> right) noexcept {
    if (!left || !right || *right > std::numeric_limits<std::size_t>::max() - *left) {
        return std::nullopt;
    }
    return *left + *right;
}

[[nodiscard]] std::optional<std::size_t> member_encoded_size(
    const MmsNamedVariableListMemberInput& member) noexcept {
    if (!valid_identifier(member.domain) || !valid_identifier(member.item)) {
        return std::nullopt;
    }
    const auto domain = asn1::BerSpanWriter::tlv_size(26, member.domain.size());
    const auto item = asn1::BerSpanWriter::tlv_size(26, member.item.size());
    const auto object_content = checked_add(domain, item);
    if (!object_content) {
        return std::nullopt;
    }
    const auto object_name = asn1::BerSpanWriter::tlv_size(1, *object_content);
    if (!object_name) {
        return std::nullopt;
    }
    const auto specification = asn1::BerSpanWriter::tlv_size(0, *object_name);
    if (!specification) {
        return std::nullopt;
    }
    return asn1::BerSpanWriter::tlv_size(16, *specification);
}

struct ResponseSizing final {
    std::size_t invoke_value{};
    std::size_t list_content{};
    std::size_t service_content{};
    std::size_t outer_content{};
    std::size_t required{};
};

[[nodiscard]] std::optional<ResponseSizing> response_sizing(
    const std::uint32_t invoke_id,
    const std::span<const MmsNamedVariableListMemberInput> members) noexcept {
    if (invoke_id > MmsPduSpanCodec::maximum_invoke_id ||
        members.size() > MmsDataSetSpanCodec::maximum_members) {
        return std::nullopt;
    }

    std::size_t list_content = 0U;
    for (const auto& member : members) {
        const auto encoded = member_encoded_size(member);
        if (!encoded || *encoded > std::numeric_limits<std::size_t>::max() - list_content) {
            return std::nullopt;
        }
        list_content += *encoded;
    }

    const auto list_tlv = asn1::BerSpanWriter::tlv_size(1, list_content);
    const auto deletable_tlv = asn1::BerSpanWriter::tlv_size(0, 1U);
    const auto service_content = checked_add(deletable_tlv, list_tlv);
    if (!list_tlv || !deletable_tlv || !service_content) {
        return std::nullopt;
    }

    const auto invoke_value = positive_integer_size(invoke_id);
    const auto invoke_tlv = asn1::BerSpanWriter::tlv_size(2, invoke_value);
    const auto service_tlv = asn1::BerSpanWriter::tlv_size(12, *service_content);
    const auto outer_content = checked_add(invoke_tlv, service_tlv);
    if (!invoke_tlv || !service_tlv || !outer_content) {
        return std::nullopt;
    }
    const auto required = asn1::BerSpanWriter::tlv_size(1, *outer_content);
    if (!required || *required > MmsPduSpanCodec::maximum_pdu_bytes) {
        return std::nullopt;
    }

    return ResponseSizing{
        invoke_value,
        list_content,
        *service_content,
        *outer_content,
        *required};
}

[[nodiscard]] bool decode_member_definition(
    const asn1::BerTlvView& definition,
    MmsObjectNameView& name) noexcept {
    name = {};
    if (definition.tag_class != asn1::BerClass::universal ||
        definition.tag_number != 16 || !definition.constructed) {
        return false;
    }
    asn1::BerTlvView specification;
    if (!asn1::BerSpanReader::try_read_exact(definition.value, specification) ||
        specification.tag_class != asn1::BerClass::context_specific ||
        specification.tag_number != 0 || !specification.constructed) {
        return false;
    }
    if (!MmsServiceSpanCodec::try_decode_object_name_view(specification.value, name)) {
        return false;
    }
    return name.kind == MmsObjectNameViewKind::domain_specific &&
        !name.domain.empty() && !name.item.empty();
}

[[nodiscard]] bool validate_member_list(
    const std::span<const std::uint8_t> bytes,
    std::size_t& count) noexcept {
    count = 0U;
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        if (count >= MmsDataSetSpanCodec::maximum_members) {
            count = 0U;
            return false;
        }
        asn1::BerTlvView definition;
        MmsObjectNameView name;
        if (!asn1::BerSpanReader::try_read_tlv(bytes, offset, definition) ||
            !decode_member_definition(definition, name)) {
            count = 0U;
            return false;
        }
        ++count;
    }
    return true;
}

} // namespace

bool MmsNamedVariableListAttributesResponseView::try_member(
    const std::size_t index,
    MmsObjectNameView& name) const noexcept {
    name = {};
    if (index >= member_count) {
        return false;
    }
    std::size_t offset = 0U;
    std::size_t current = 0U;
    while (offset < variable_list.size()) {
        asn1::BerTlvView definition;
        if (!asn1::BerSpanReader::try_read_tlv(variable_list, offset, definition)) {
            return false;
        }
        if (current == index) {
            return decode_member_definition(definition, name);
        }
        ++current;
    }
    return false;
}

bool MmsDataSetSpanCodec::try_decode_get_named_variable_list_attributes_request(
    const MmsConfirmedPduView& confirmed,
    MmsNamedVariableListAttributesRequestView& request) noexcept {
    request = {};
    if (confirmed.kind != MmsWirePduKind::confirmed_request ||
        confirmed.service_tag != 12 || !confirmed.service_constructed) {
        return false;
    }
    MmsObjectNameView name;
    if (!MmsServiceSpanCodec::try_decode_object_name_view(confirmed.service_value, name) ||
        name.kind != MmsObjectNameViewKind::domain_specific) {
        return false;
    }
    request.invoke_id = confirmed.invoke_id;
    request.name = name;
    return true;
}

bool MmsDataSetSpanCodec::try_decode_get_named_variable_list_attributes_request(
    const std::span<const std::uint8_t> mms_pdu,
    MmsNamedVariableListAttributesRequestView& request) noexcept {
    MmsConfirmedPduView confirmed;
    return MmsPduSpanCodec::try_decode_confirmed_request_view(mms_pdu, confirmed) &&
        try_decode_get_named_variable_list_attributes_request(confirmed, request);
}

bool MmsDataSetSpanCodec::try_decode_get_named_variable_list_attributes_response(
    const MmsConfirmedPduView& confirmed,
    MmsNamedVariableListAttributesResponseView& response) noexcept {
    response = {};
    if (confirmed.kind != MmsWirePduKind::confirmed_response ||
        confirmed.service_tag != 12 || !confirmed.service_constructed) {
        return false;
    }

    std::size_t offset = 0U;
    asn1::BerTlvView deletable;
    asn1::BerTlvView list;
    if (!asn1::BerSpanReader::try_read_tlv(confirmed.service_value, offset, deletable) ||
        !asn1::BerSpanReader::try_read_tlv(confirmed.service_value, offset, list) ||
        offset != confirmed.service_value.size() ||
        deletable.tag_class != asn1::BerClass::context_specific ||
        deletable.tag_number != 0 || deletable.constructed || deletable.value.size() != 1U ||
        list.tag_class != asn1::BerClass::context_specific ||
        list.tag_number != 1 || !list.constructed) {
        return false;
    }

    std::size_t member_count = 0U;
    if (!validate_member_list(list.value, member_count)) {
        return false;
    }
    response.invoke_id = confirmed.invoke_id;
    response.mms_deletable = deletable.value.front() != 0U;
    response.variable_list = list.value;
    response.member_count = member_count;
    return true;
}

bool MmsDataSetSpanCodec::try_decode_get_named_variable_list_attributes_response(
    const std::span<const std::uint8_t> mms_pdu,
    MmsNamedVariableListAttributesResponseView& response) noexcept {
    MmsConfirmedPduView confirmed;
    return MmsPduSpanCodec::try_decode_confirmed_response_view(mms_pdu, confirmed) &&
        try_decode_get_named_variable_list_attributes_response(confirmed, response);
}

wire::EncodeResult MmsDataSetSpanCodec::encode_get_named_variable_list_attributes_response_into(
    const std::uint32_t invoke_id,
    const bool mms_deletable,
    const std::span<const MmsNamedVariableListMemberInput> members,
    const std::span<std::uint8_t> destination) noexcept {
    const auto sizing = response_sizing(invoke_id, members);
    if (!sizing) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (destination.size() < sizing->required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, sizing->required};
    }

    asn1::BerSpanWriter writer{destination.first(sizing->required)};
    if (!writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 1, sizing->outer_content) ||
        !writer.write_tlv_header(
            asn1::BerClass::universal, false, 2, sizing->invoke_value) ||
        !write_positive_integer(writer, invoke_id) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 12, sizing->service_content) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, false, 0, 1U) ||
        !writer.write_byte(mms_deletable ? 0xFFU : 0x00U) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 1, sizing->list_content)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, sizing->required};
    }

    for (const auto& member : members) {
        const auto domain_tlv = asn1::BerSpanWriter::tlv_size(26, member.domain.size());
        const auto item_tlv = asn1::BerSpanWriter::tlv_size(26, member.item.size());
        const auto object_content = checked_add(domain_tlv, item_tlv);
        const auto object_name = object_content
            ? asn1::BerSpanWriter::tlv_size(1, *object_content)
            : std::nullopt;
        const auto specification = object_name
            ? asn1::BerSpanWriter::tlv_size(0, *object_name)
            : std::nullopt;
        if (!domain_tlv || !item_tlv || !object_content || !object_name || !specification ||
            !writer.write_tlv_header(
                asn1::BerClass::universal, true, 16, *specification) ||
            !writer.write_tlv_header(
                asn1::BerClass::context_specific, true, 0, *object_name) ||
            !writer.write_tlv_header(
                asn1::BerClass::context_specific, true, 1, *object_content) ||
            !writer.write_tlv(
                asn1::BerClass::universal,
                false,
                26,
                std::span<const std::uint8_t>{
                    reinterpret_cast<const std::uint8_t*>(member.domain.data()),
                    member.domain.size()}) ||
            !writer.write_tlv(
                asn1::BerClass::universal,
                false,
                26,
                std::span<const std::uint8_t>{
                    reinterpret_cast<const std::uint8_t*>(member.item.data()),
                    member.item.size()})) {
            return {wire::EncodeStatus::value_out_of_range, 0U, sizing->required};
        }
    }

    if (writer.size() != sizing->required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, sizing->required};
    }
    return {wire::EncodeStatus::ok, sizing->required, sizing->required};
}

} // namespace ar::iec61850::mms
