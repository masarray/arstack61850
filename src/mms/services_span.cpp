// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/services_span.hpp"

#include "ariec61850/asn1/ber_span_reader.hpp"
#include "ariec61850/asn1/ber_span_writer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {
namespace {

[[nodiscard]] bool visible_ascii(
    const std::span<const std::uint8_t> value,
    const bool allow_empty = false) noexcept {
    if ((!allow_empty && value.empty()) ||
        value.size() > MmsServiceSpanCodec::maximum_identifier_bytes) {
        return false;
    }
    for (const auto byte : value) {
        if (byte == 0U || byte > 0x7FU) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool visible_ascii(
    const std::string_view value,
    const bool allow_empty = false) noexcept {
    if ((!allow_empty && value.empty()) ||
        value.size() > MmsServiceSpanCodec::maximum_identifier_bytes) {
        return false;
    }
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte == 0U || byte > 0x7FU) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool read_u32(
    const asn1::BerTlvView& tlv,
    std::uint32_t& value) noexcept {
    const auto decoded = asn1::BerSpanReader::read_uint32(tlv);
    if (!decoded) {
        value = 0U;
        return false;
    }
    value = *decoded;
    return true;
}

[[nodiscard]] bool valid_identifier_tlv(
    const asn1::BerTlvView& tlv) noexcept {
    return !tlv.constructed &&
        tlv.tag_class == asn1::BerClass::universal &&
        (tlv.tag_number == 22 || tlv.tag_number == 26) &&
        visible_ascii(tlv.value);
}

[[nodiscard]] bool valid_mms_data_tlv(
    const asn1::BerTlvView& tlv) noexcept {
    if (tlv.tag_class != asn1::BerClass::context_specific) {
        return false;
    }
    if (tlv.tag_number == 1 || tlv.tag_number == 2) {
        return tlv.constructed;
    }
    return tlv.tag_number >= 3 && tlv.tag_number <= 17 && !tlv.constructed;
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

struct ConfirmedResponseSizing final {
    std::size_t invoke_value{};
    std::size_t outer_content{};
    std::size_t required{};
};

[[nodiscard]] std::optional<ConfirmedResponseSizing> confirmed_response_sizing(
    const std::uint32_t invoke_id,
    const std::int32_t service_tag,
    const std::size_t service_content) noexcept {
    if (invoke_id > MmsPduSpanCodec::maximum_invoke_id ||
        service_tag < 0 || service_tag > 1'000'000) {
        return std::nullopt;
    }
    const auto invoke_value = positive_integer_size(invoke_id);
    const auto invoke_tlv = asn1::BerSpanWriter::tlv_size(2, invoke_value);
    const auto service_tlv = asn1::BerSpanWriter::tlv_size(service_tag, service_content);
    const auto outer_content = add_size(invoke_tlv, service_tlv);
    if (!outer_content) {
        return std::nullopt;
    }
    const auto required = asn1::BerSpanWriter::tlv_size(1, *outer_content);
    if (!required || *required > MmsPduSpanCodec::maximum_pdu_bytes) {
        return std::nullopt;
    }
    return ConfirmedResponseSizing{invoke_value, *outer_content, *required};
}

[[nodiscard]] bool write_confirmed_response_prefix(
    asn1::BerSpanWriter& writer,
    const ConfirmedResponseSizing& sizing,
    const std::uint32_t invoke_id,
    const std::int32_t service_tag,
    const std::size_t service_content) noexcept {
    return writer.write_tlv_header(
               asn1::BerClass::context_specific, true, 1, sizing.outer_content) &&
        writer.write_tlv_header(
               asn1::BerClass::universal, false, 2, sizing.invoke_value) &&
        write_positive_integer(writer, invoke_id) &&
        writer.write_tlv_header(
               asn1::BerClass::context_specific, true, service_tag, service_content);
}

[[nodiscard]] bool try_read_variable_definition(
    const std::span<const std::uint8_t> variable_list,
    const std::size_t wanted,
    MmsObjectNameView& name) noexcept {
    name = {};
    std::size_t offset = 0U;
    std::size_t current = 0U;
    while (offset < variable_list.size()) {
        asn1::BerTlvView definition;
        if (!asn1::BerSpanReader::try_read_tlv(variable_list, offset, definition) ||
            definition.tag_class != asn1::BerClass::universal ||
            definition.tag_number != 16 || !definition.constructed) {
            return false;
        }
        asn1::BerTlvView wrapper;
        if (!asn1::BerSpanReader::try_read_exact(definition.value, wrapper) ||
            wrapper.tag_class != asn1::BerClass::context_specific ||
            wrapper.tag_number != 0 || !wrapper.constructed) {
            return false;
        }
        MmsObjectNameView decoded;
        if (!MmsServiceSpanCodec::try_decode_object_name_view(wrapper.value, decoded)) {
            return false;
        }
        if (current == wanted) {
            name = decoded;
            return true;
        }
        ++current;
    }
    return false;
}

[[nodiscard]] bool try_read_access_result(
    const std::span<const std::uint8_t> list,
    const std::size_t wanted,
    MmsReadAccessResultView& result) noexcept {
    result = {};
    std::size_t offset = 0U;
    std::size_t current = 0U;
    while (offset < list.size()) {
        const auto start = offset;
        asn1::BerTlvView access;
        if (!asn1::BerSpanReader::try_read_tlv(list, offset, access)) {
            return false;
        }
        MmsReadAccessResultView decoded;
        if (access.tag_class == asn1::BerClass::context_specific &&
            access.tag_number == 0 && !access.constructed) {
            if (!read_u32(access, decoded.failure_code)) {
                return false;
            }
            decoded.success = false;
        } else if (valid_mms_data_tlv(access)) {
            decoded.success = true;
            decoded.encoded_data = list.subspan(start, offset - start);
        } else {
            return false;
        }
        if (current == wanted) {
            result = decoded;
            return true;
        }
        ++current;
    }
    return false;
}

} // namespace

bool MmsServiceSpanCodec::try_decode_object_name_view(
    const std::span<const std::uint8_t> encoded_object_name,
    MmsObjectNameView& name) noexcept {
    name = {};
    asn1::BerTlvView outer;
    if (!asn1::BerSpanReader::try_read_exact(encoded_object_name, outer) ||
        outer.tag_class != asn1::BerClass::context_specific) {
        return false;
    }
    if (outer.tag_number == 0 && !outer.constructed) {
        if (!visible_ascii(outer.value)) {
            return false;
        }
        name.kind = MmsObjectNameViewKind::vmd_specific;
        name.item = outer.value;
        return true;
    }
    if (outer.tag_number == 2 && !outer.constructed) {
        if (!visible_ascii(outer.value)) {
            return false;
        }
        name.kind = MmsObjectNameViewKind::aa_specific;
        name.item = outer.value;
        return true;
    }
    if (outer.tag_number != 1 || !outer.constructed) {
        return false;
    }

    std::size_t offset = 0U;
    asn1::BerTlvView domain;
    asn1::BerTlvView item;
    if (!asn1::BerSpanReader::try_read_tlv(outer.value, offset, domain) ||
        !asn1::BerSpanReader::try_read_tlv(outer.value, offset, item) ||
        offset != outer.value.size() ||
        !valid_identifier_tlv(domain) || !valid_identifier_tlv(item)) {
        return false;
    }
    name.kind = MmsObjectNameViewKind::domain_specific;
    name.domain = domain.value;
    name.item = item.value;
    return true;
}

bool MmsGetNameListResponseView::try_identifier(
    const std::size_t index,
    std::span<const std::uint8_t>& identifier) const noexcept {
    identifier = {};
    if (index >= identifier_count) {
        return false;
    }
    std::size_t offset = 0U;
    std::size_t current = 0U;
    while (offset < identifier_list.size()) {
        asn1::BerTlvView item;
        if (!asn1::BerSpanReader::try_read_tlv(identifier_list, offset, item) ||
            !valid_identifier_tlv(item)) {
            return false;
        }
        if (current == index) {
            identifier = item.value;
            return true;
        }
        ++current;
    }
    return false;
}

bool MmsReadRequestView::try_variable(
    const std::size_t index,
    MmsObjectNameView& name) const noexcept {
    if (index >= variable_count) {
        name = {};
        return false;
    }
    return try_read_variable_definition(variable_list, index, name);
}

bool MmsReadResponseView::try_result(
    const std::size_t index,
    MmsReadAccessResultView& result) const noexcept {
    if (index >= result_count) {
        result = {};
        return false;
    }
    return try_read_access_result(access_result_list, index, result);
}

bool MmsServiceSpanCodec::try_decode_get_name_list_request(
    const MmsConfirmedPduView& confirmed,
    MmsGetNameListRequestView& request) noexcept {
    request = {};
    if (confirmed.kind != MmsWirePduKind::confirmed_request ||
        confirmed.service_tag != 1 || !confirmed.service_constructed) {
        return false;
    }

    bool have_class = false;
    bool have_scope = false;
    bool have_continuation = false;
    std::size_t count = 0U;
    std::size_t offset = 0U;
    while (offset < confirmed.service_value.size()) {
        if (count >= 3U) {
            request = {};
            return false;
        }
        ++count;
        asn1::BerTlvView field;
        if (!asn1::BerSpanReader::try_read_tlv(
                confirmed.service_value, offset, field) ||
            field.tag_class != asn1::BerClass::context_specific) {
            request = {};
            return false;
        }

        if (field.tag_number == 0 && field.constructed) {
            if (have_class) {
                request = {};
                return false;
            }
            asn1::BerTlvView inner;
            std::uint32_t value{};
            if (!asn1::BerSpanReader::try_read_exact(field.value, inner) ||
                inner.tag_class != asn1::BerClass::context_specific ||
                inner.tag_number != 0 || inner.constructed ||
                !read_u32(inner, value) ||
                (value != 0U && value != 2U && value != 9U)) {
                request = {};
                return false;
            }
            request.object_class = static_cast<MmsNameListObjectClass>(value);
            have_class = true;
        } else if (field.tag_number == 1 && field.constructed) {
            if (have_scope) {
                request = {};
                return false;
            }
            asn1::BerTlvView inner;
            if (!asn1::BerSpanReader::try_read_exact(field.value, inner) ||
                inner.tag_class != asn1::BerClass::context_specific ||
                inner.constructed) {
                request = {};
                return false;
            }
            switch (inner.tag_number) {
            case 0:
                if (!inner.value.empty()) {
                    request = {};
                    return false;
                }
                request.scope = MmsNameScopeKind::vmd_specific;
                break;
            case 1:
                if (!visible_ascii(inner.value)) {
                    request = {};
                    return false;
                }
                request.scope = MmsNameScopeKind::domain_specific;
                request.domain_id = inner.value;
                break;
            case 2:
                if (!inner.value.empty()) {
                    request = {};
                    return false;
                }
                request.scope = MmsNameScopeKind::aa_specific;
                break;
            default:
                request = {};
                return false;
            }
            have_scope = true;
        } else if (field.tag_number == 2 && !field.constructed) {
            if (have_continuation || !visible_ascii(field.value)) {
                request = {};
                return false;
            }
            request.continue_after = field.value;
            have_continuation = true;
        } else {
            request = {};
            return false;
        }
    }

    if (!have_class || !have_scope) {
        request = {};
        return false;
    }
    request.invoke_id = confirmed.invoke_id;
    return true;
}

bool MmsServiceSpanCodec::try_decode_get_name_list_request(
    const std::span<const std::uint8_t> mms_pdu,
    MmsGetNameListRequestView& request) noexcept {
    MmsConfirmedPduView confirmed;
    return MmsPduSpanCodec::try_decode_confirmed_request_view(mms_pdu, confirmed) &&
        try_decode_get_name_list_request(confirmed, request);
}

bool MmsServiceSpanCodec::try_decode_get_name_list_response(
    const MmsConfirmedPduView& confirmed,
    MmsGetNameListResponseView& response) noexcept {
    response = {};
    if (confirmed.kind != MmsWirePduKind::confirmed_response ||
        confirmed.service_tag != 1 || !confirmed.service_constructed) {
        return false;
    }

    bool have_list = false;
    bool have_more = false;
    std::size_t offset = 0U;
    while (offset < confirmed.service_value.size()) {
        asn1::BerTlvView field;
        if (!asn1::BerSpanReader::try_read_tlv(
                confirmed.service_value, offset, field) ||
            field.tag_class != asn1::BerClass::context_specific) {
            response = {};
            return false;
        }

        if (field.tag_number == 0 && field.constructed) {
            if (have_list) {
                response = {};
                return false;
            }
            std::size_t name_offset = 0U;
            std::size_t name_count = 0U;
            while (name_offset < field.value.size()) {
                if (name_count >= maximum_identifiers) {
                    response = {};
                    return false;
                }
                asn1::BerTlvView name;
                if (!asn1::BerSpanReader::try_read_tlv(
                        field.value, name_offset, name) ||
                    !valid_identifier_tlv(name)) {
                    response = {};
                    return false;
                }
                ++name_count;
            }
            response.identifier_list = field.value;
            response.identifier_count = name_count;
            have_list = true;
        } else if (field.tag_number == 1 && !field.constructed) {
            if (have_more || field.value.size() != 1U) {
                response = {};
                return false;
            }
            response.more_follows = field.value[0] != 0U;
            have_more = true;
        } else {
            response = {};
            return false;
        }
    }

    if (!have_list) {
        response = {};
        return false;
    }
    response.invoke_id = confirmed.invoke_id;
    return true;
}

bool MmsServiceSpanCodec::try_decode_get_name_list_response(
    const std::span<const std::uint8_t> mms_pdu,
    MmsGetNameListResponseView& response) noexcept {
    MmsConfirmedPduView confirmed;
    return MmsPduSpanCodec::try_decode_confirmed_response_view(mms_pdu, confirmed) &&
        try_decode_get_name_list_response(confirmed, response);
}

wire::EncodeResult MmsServiceSpanCodec::encode_get_name_list_response_into(
    const std::uint32_t invoke_id,
    const std::span<const std::string_view> names,
    const bool more_follows,
    const std::span<std::uint8_t> destination) noexcept {
    if (invoke_id > MmsPduSpanCodec::maximum_invoke_id ||
        names.size() > maximum_identifiers) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    std::size_t names_content = 0U;
    for (const auto name : names) {
        if (!visible_ascii(name)) {
            return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
        }
        const auto encoded = asn1::BerSpanWriter::tlv_size(26, name.size());
        if (!encoded || *encoded > std::numeric_limits<std::size_t>::max() - names_content) {
            return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
        }
        names_content += *encoded;
    }

    const auto list_tlv = asn1::BerSpanWriter::tlv_size(0, names_content);
    const auto more_tlv = asn1::BerSpanWriter::tlv_size(1, 1U);
    const auto service_content = add_size(list_tlv, more_tlv);
    if (!service_content) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto service_tlv = asn1::BerSpanWriter::tlv_size(1, *service_content);

    const auto invoke_value = positive_integer_size(invoke_id);
    const auto invoke_tlv = asn1::BerSpanWriter::tlv_size(2, invoke_value);
    const auto outer_content = add_size(invoke_tlv, service_tlv);
    if (!outer_content) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto required = asn1::BerSpanWriter::tlv_size(1, *outer_content);
    if (!required || *required > MmsPduSpanCodec::maximum_pdu_bytes) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (destination.size() < *required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, *required};
    }

    asn1::BerSpanWriter writer{destination.first(*required)};
    if (!writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 1, *outer_content) ||
        !writer.write_tlv_header(
            asn1::BerClass::universal, false, 2, invoke_value) ||
        !write_positive_integer(writer, invoke_id) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 1, *service_content) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 0, names_content)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }

    for (const auto name : names) {
        if (!writer.write_tlv_header(
                asn1::BerClass::universal, false, 26, name.size())) {
            return {wire::EncodeStatus::value_out_of_range, 0U, *required};
        }
        for (const auto character : name) {
            if (!writer.write_byte(static_cast<std::uint8_t>(
                    static_cast<unsigned char>(character)))) {
                return {wire::EncodeStatus::value_out_of_range, 0U, *required};
            }
        }
    }

    if (!writer.write_tlv_header(
            asn1::BerClass::context_specific, false, 1, 1U) ||
        !writer.write_byte(more_follows ? 0xFFU : 0x00U) ||
        writer.size() != *required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }
    return {wire::EncodeStatus::ok, *required, *required};
}

bool MmsServiceSpanCodec::try_decode_variable_access_attributes_request(
    const MmsConfirmedPduView& confirmed,
    MmsVariableAccessAttributesRequestView& request) noexcept {
    request = {};
    if (confirmed.kind != MmsWirePduKind::confirmed_request ||
        confirmed.service_tag != 6 || !confirmed.service_constructed) {
        return false;
    }
    asn1::BerTlvView wrapper;
    if (!asn1::BerSpanReader::try_read_exact(confirmed.service_value, wrapper) ||
        wrapper.tag_class != asn1::BerClass::context_specific ||
        wrapper.tag_number != 0 || !wrapper.constructed ||
        !try_decode_object_name_view(wrapper.value, request.name)) {
        request = {};
        return false;
    }
    request.invoke_id = confirmed.invoke_id;
    return true;
}

bool MmsServiceSpanCodec::try_decode_variable_access_attributes_request(
    const std::span<const std::uint8_t> mms_pdu,
    MmsVariableAccessAttributesRequestView& request) noexcept {
    MmsConfirmedPduView confirmed;
    return MmsPduSpanCodec::try_decode_confirmed_request_view(mms_pdu, confirmed) &&
        try_decode_variable_access_attributes_request(confirmed, request);
}

bool MmsServiceSpanCodec::try_decode_variable_access_attributes_response(
    const MmsConfirmedPduView& confirmed,
    MmsVariableAccessAttributesResponseView& response) noexcept {
    response = {};
    if (confirmed.kind != MmsWirePduKind::confirmed_response ||
        confirmed.service_tag != 6 || !confirmed.service_constructed) {
        return false;
    }
    bool have_deletable = false;
    bool have_type = false;
    std::size_t offset = 0U;
    while (offset < confirmed.service_value.size()) {
        asn1::BerTlvView field;
        if (!asn1::BerSpanReader::try_read_tlv(confirmed.service_value, offset, field) ||
            field.tag_class != asn1::BerClass::context_specific) {
            response = {};
            return false;
        }
        if (field.tag_number == 0 && !field.constructed) {
            if (have_deletable || field.value.size() != 1U) {
                response = {};
                return false;
            }
            response.mms_deletable = field.value[0] != 0U;
            have_deletable = true;
        } else if (field.tag_number == 2 && field.constructed) {
            if (have_type) {
                response = {};
                return false;
            }
            asn1::BerTlvView type;
            if (!asn1::BerSpanReader::try_read_exact(field.value, type) ||
                type.tag_class != asn1::BerClass::context_specific) {
                response = {};
                return false;
            }
            response.type_specification = field.value;
            have_type = true;
        } else {
            response = {};
            return false;
        }
    }
    if (!have_type) {
        response = {};
        return false;
    }
    response.invoke_id = confirmed.invoke_id;
    return true;
}

bool MmsServiceSpanCodec::try_decode_variable_access_attributes_response(
    const std::span<const std::uint8_t> mms_pdu,
    MmsVariableAccessAttributesResponseView& response) noexcept {
    MmsConfirmedPduView confirmed;
    return MmsPduSpanCodec::try_decode_confirmed_response_view(mms_pdu, confirmed) &&
        try_decode_variable_access_attributes_response(confirmed, response);
}

wire::EncodeResult MmsServiceSpanCodec::encode_variable_access_attributes_response_into(
    const std::uint32_t invoke_id,
    const bool mms_deletable,
    const std::span<const std::uint8_t> encoded_type_specification,
    const std::span<std::uint8_t> destination) noexcept {
    asn1::BerTlvView type;
    if (!asn1::BerSpanReader::try_read_exact(encoded_type_specification, type) ||
        type.tag_class != asn1::BerClass::context_specific) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto deletable_tlv = asn1::BerSpanWriter::tlv_size(0, 1U);
    const auto type_wrapper = asn1::BerSpanWriter::tlv_size(
        2, encoded_type_specification.size());
    const auto service_content = add_size(deletable_tlv, type_wrapper);
    if (!service_content) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto sizing = confirmed_response_sizing(invoke_id, 6, *service_content);
    if (!sizing) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (destination.size() < sizing->required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, sizing->required};
    }

    asn1::BerSpanWriter writer{destination.first(sizing->required)};
    if (!write_confirmed_response_prefix(
            writer, *sizing, invoke_id, 6, *service_content) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, false, 0, 1U) ||
        !writer.write_byte(mms_deletable ? 0xFFU : 0x00U) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 2,
            encoded_type_specification.size()) ||
        !writer.write_bytes(encoded_type_specification) ||
        writer.size() != sizing->required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, sizing->required};
    }
    return {wire::EncodeStatus::ok, sizing->required, sizing->required};
}

bool MmsServiceSpanCodec::try_decode_read_request(
    const MmsConfirmedPduView& confirmed,
    MmsReadRequestView& request) noexcept {
    request = {};
    if (confirmed.kind != MmsWirePduKind::confirmed_request ||
        confirmed.service_tag != 4 || !confirmed.service_constructed) {
        return false;
    }

    bool have_flag = false;
    bool have_specification = false;
    std::size_t offset = 0U;
    while (offset < confirmed.service_value.size()) {
        asn1::BerTlvView field;
        if (!asn1::BerSpanReader::try_read_tlv(confirmed.service_value, offset, field) ||
            field.tag_class != asn1::BerClass::context_specific) {
            request = {};
            return false;
        }
        if (field.tag_number == 0 && !field.constructed) {
            if (have_flag || field.value.size() != 1U) {
                request = {};
                return false;
            }
            request.specification_with_result = field.value[0] != 0U;
            have_flag = true;
        } else if (field.tag_number == 1 && field.constructed) {
            if (have_specification) {
                request = {};
                return false;
            }
            asn1::BerTlvView list;
            if (!asn1::BerSpanReader::try_read_exact(field.value, list) ||
                list.tag_class != asn1::BerClass::context_specific ||
                list.tag_number != 0 || !list.constructed) {
                request = {};
                return false;
            }
            std::size_t variable_offset = 0U;
            std::size_t count = 0U;
            while (variable_offset < list.value.size()) {
                if (count >= maximum_variables) {
                    request = {};
                    return false;
                }
                const auto before = variable_offset;
                asn1::BerTlvView definition;
                if (!asn1::BerSpanReader::try_read_tlv(
                        list.value, variable_offset, definition)) {
                    request = {};
                    return false;
                }
                MmsObjectNameView ignored;
                if (!try_read_variable_definition(
                        list.value.subspan(before, variable_offset - before), 0U, ignored)) {
                    request = {};
                    return false;
                }
                ++count;
            }
            if (count == 0U) {
                request = {};
                return false;
            }
            request.variable_list = list.value;
            request.variable_count = count;
            have_specification = true;
        } else {
            request = {};
            return false;
        }
    }
    if (!have_specification) {
        request = {};
        return false;
    }
    request.invoke_id = confirmed.invoke_id;
    return true;
}

bool MmsServiceSpanCodec::try_decode_read_request(
    const std::span<const std::uint8_t> mms_pdu,
    MmsReadRequestView& request) noexcept {
    MmsConfirmedPduView confirmed;
    return MmsPduSpanCodec::try_decode_confirmed_request_view(mms_pdu, confirmed) &&
        try_decode_read_request(confirmed, request);
}

bool MmsServiceSpanCodec::try_decode_read_response(
    const MmsConfirmedPduView& confirmed,
    MmsReadResponseView& response) noexcept {
    response = {};
    if (confirmed.kind != MmsWirePduKind::confirmed_response ||
        confirmed.service_tag != 4 || !confirmed.service_constructed) {
        return false;
    }
    bool have_list = false;
    bool have_optional_specification = false;
    std::size_t offset = 0U;
    while (offset < confirmed.service_value.size()) {
        asn1::BerTlvView field;
        if (!asn1::BerSpanReader::try_read_tlv(confirmed.service_value, offset, field) ||
            field.tag_class != asn1::BerClass::context_specific) {
            response = {};
            return false;
        }
        if (field.tag_number == 0 && field.constructed) {
            if (have_optional_specification) {
                response = {};
                return false;
            }
            have_optional_specification = true;
        } else if (field.tag_number == 1 && field.constructed) {
            if (have_list) {
                response = {};
                return false;
            }
            std::size_t result_offset = 0U;
            std::size_t count = 0U;
            while (result_offset < field.value.size()) {
                if (count >= maximum_variables) {
                    response = {};
                    return false;
                }
                const auto start = result_offset;
                asn1::BerTlvView result;
                if (!asn1::BerSpanReader::try_read_tlv(
                        field.value, result_offset, result)) {
                    response = {};
                    return false;
                }
                if (result.tag_class == asn1::BerClass::context_specific &&
                    result.tag_number == 0 && !result.constructed) {
                    std::uint32_t ignored{};
                    if (!read_u32(result, ignored)) {
                        response = {};
                        return false;
                    }
                } else if (!valid_mms_data_tlv(result)) {
                    response = {};
                    return false;
                }
                if (result_offset <= start) {
                    response = {};
                    return false;
                }
                ++count;
            }
            if (count == 0U) {
                response = {};
                return false;
            }
            response.access_result_list = field.value;
            response.result_count = count;
            have_list = true;
        } else {
            response = {};
            return false;
        }
    }
    if (!have_list) {
        response = {};
        return false;
    }
    response.invoke_id = confirmed.invoke_id;
    return true;
}

bool MmsServiceSpanCodec::try_decode_read_response(
    const std::span<const std::uint8_t> mms_pdu,
    MmsReadResponseView& response) noexcept {
    MmsConfirmedPduView confirmed;
    return MmsPduSpanCodec::try_decode_confirmed_response_view(mms_pdu, confirmed) &&
        try_decode_read_response(confirmed, response);
}

wire::EncodeResult MmsServiceSpanCodec::encode_read_response_into(
    const std::uint32_t invoke_id,
    const std::span<const MmsReadAccessResultInput> results,
    const std::span<std::uint8_t> destination) noexcept {
    if (results.empty() || results.size() > maximum_variables) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    std::size_t list_content = 0U;
    for (const auto& result : results) {
        std::size_t encoded_size = 0U;
        if (result.success) {
            asn1::BerTlvView data;
            if (!asn1::BerSpanReader::try_read_exact(result.encoded_data, data) ||
                !valid_mms_data_tlv(data)) {
                return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
            }
            encoded_size = result.encoded_data.size();
        } else {
            const auto failure_value = positive_integer_size(result.failure_code);
            const auto failure_tlv = asn1::BerSpanWriter::tlv_size(0, failure_value);
            if (!failure_tlv) {
                return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
            }
            encoded_size = *failure_tlv;
        }
        if (encoded_size > std::numeric_limits<std::size_t>::max() - list_content) {
            return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
        }
        list_content += encoded_size;
    }

    const auto list_tlv = asn1::BerSpanWriter::tlv_size(1, list_content);
    if (!list_tlv) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto sizing = confirmed_response_sizing(invoke_id, 4, *list_tlv);
    if (!sizing) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (destination.size() < sizing->required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, sizing->required};
    }

    asn1::BerSpanWriter writer{destination.first(sizing->required)};
    if (!write_confirmed_response_prefix(
            writer, *sizing, invoke_id, 4, *list_tlv) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 1, list_content)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, sizing->required};
    }
    for (const auto& result : results) {
        if (result.success) {
            if (!writer.write_bytes(result.encoded_data)) {
                return {wire::EncodeStatus::value_out_of_range, 0U, sizing->required};
            }
        } else {
            const auto failure_value = positive_integer_size(result.failure_code);
            if (!writer.write_tlv_header(
                    asn1::BerClass::context_specific, false, 0, failure_value) ||
                !write_positive_integer(writer, result.failure_code)) {
                return {wire::EncodeStatus::value_out_of_range, 0U, sizing->required};
            }
        }
    }
    if (writer.size() != sizing->required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, sizing->required};
    }
    return {wire::EncodeStatus::ok, sizing->required, sizing->required};
}

} // namespace ar::iec61850::mms
