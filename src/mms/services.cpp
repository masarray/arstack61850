// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/services.hpp"

#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/mms/data_codec.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <sstream>
#include <utility>

namespace ar::iec61850::mms {
namespace {

using asn1::BerClass;
using asn1::BerReader;
using asn1::BerTlv;
using asn1::BerWriter;

std::vector<std::uint8_t> concat(
    const std::initializer_list<std::span<const std::uint8_t>> parts) {
    std::size_t size = 0U;
    for (const auto part : parts) {
        if (part.size() > MmsPduCodec::maximum_pdu_bytes - size) {
            throw MmsFormatError("MMS service encoding exceeds the configured limit.");
        }
        size += part.size();
    }
    std::vector<std::uint8_t> result;
    result.reserve(size);
    for (const auto part : parts) {
        result.insert(result.end(), part.begin(), part.end());
    }
    return result;
}

std::vector<std::uint8_t> positive_integer_content(const std::uint64_t value) {
    auto bytes = BerWriter::encode_unsigned_integer(value);
    if (bytes.empty()) {
        bytes.push_back(0U);
    }
    if ((bytes.front() & 0x80U) != 0U) {
        bytes.insert(bytes.begin(), 0U);
    }
    return bytes;
}

void validate_identifier(const std::string& value, const char* name, const bool allow_empty = false) {
    if ((!allow_empty && value.empty()) || value.size() > MmsServiceCodec::maximum_identifier_bytes) {
        throw MmsFormatError(std::string{name} + " length is outside the configured limit.");
    }
    if (std::any_of(value.begin(), value.end(), [](const char ch) {
            const auto byte = static_cast<unsigned char>(ch);
            return byte == 0U || byte > 0x7FU;
        })) {
        throw MmsFormatError(std::string{name} + " must contain visible ASCII bytes.");
    }
}

std::vector<std::uint8_t> visible_string(const std::string& value) {
    validate_identifier(value, "MMS identifier");
    return BerWriter::encode_tlv(0x1AU, BerWriter::encode_ascii(value));
}

BerTlv read_one_exact(const std::span<const std::uint8_t> bytes, const char* name) {
    if (bytes.empty() || bytes.size() > MmsPduCodec::maximum_pdu_bytes) {
        throw MmsFormatError(std::string{name} + " is empty or too large.");
    }
    std::size_t offset = 0U;
    BerTlv tlv;
    if (!BerReader::try_read_tlv(bytes, offset, tlv) || offset != bytes.size()) {
        throw MmsFormatError(std::string{name} + " is not one exact BER TLV.");
    }
    return tlv;
}

std::uint32_t read_u32(const BerTlv& tlv, const char* field) {
    const auto value = BerReader::read_unsigned_integer(tlv);
    if (!value || *value > std::numeric_limits<std::uint32_t>::max()) {
        throw MmsFormatError(std::string{field} + " is not an unsigned 32-bit value.");
    }
    return static_cast<std::uint32_t>(*value);
}

std::string read_identifier(const BerTlv& tlv, const char* field) {
    if (tlv.constructed ||
        !((tlv.tag_class == BerClass::universal && (tlv.tag_number == 22 || tlv.tag_number == 26)) ||
          tlv.tag_class == BerClass::context_specific)) {
        throw MmsFormatError(std::string{field} + " is not a primitive identifier.");
    }
    const auto value = BerReader::read_ascii_string(tlv);
    validate_identifier(value, field);
    return value;
}

MmsConfirmedRequest confirmed_request_for(
    const std::span<const std::uint8_t> payload,
    const std::int32_t expected_service) {
    const auto mms = MmsPduCodec::extract_mms_payload(payload);
    const auto request = MmsPduCodec::decode_confirmed_request(mms);
    if (request.service_tag != expected_service || !request.service_constructed) {
        throw MmsFormatError("MMS Confirmed-Request service tag does not match the expected service.");
    }
    return request;
}

MmsConfirmedResponse confirmed_response_for(
    const std::span<const std::uint8_t> payload,
    const std::int32_t expected_service,
    const std::optional<std::uint32_t> expected_invoke) {
    const auto mms = MmsPduCodec::extract_mms_payload(payload);
    const auto response = MmsPduCodec::decode_confirmed_response(mms);
    if (expected_invoke && response.invoke_id != *expected_invoke) {
        throw MmsFormatError("MMS Confirmed-Response invoke ID does not match the expected value.");
    }
    if (response.service_tag != expected_service) {
        throw MmsFormatError("MMS Confirmed-Response service tag does not match the expected service.");
    }
    return response;
}

std::vector<std::uint8_t> encode_variable_definition(const MmsObjectName& name) {
    const auto object_name = MmsServiceCodec::encode_object_name(name);
    const auto specification_name = BerWriter::encode_tlv(0xA0U, object_name);
    return BerWriter::encode_tlv(0x30U, specification_name);
}

std::vector<std::uint8_t> encode_list_of_variable(
    const std::span<const MmsObjectName> variables) {
    if (variables.empty() || variables.size() > MmsServiceCodec::maximum_variables) {
        throw MmsFormatError("MMS variable list size is outside the configured limit.");
    }
    std::vector<std::uint8_t> definitions;
    for (const auto& variable : variables) {
        const auto encoded = encode_variable_definition(variable);
        if (encoded.size() > MmsPduCodec::maximum_pdu_bytes - definitions.size()) {
            throw MmsFormatError("MMS variable list encoding is too large.");
        }
        definitions.insert(definitions.end(), encoded.begin(), encoded.end());
    }
    return BerWriter::encode_tlv(0xA0U, definitions);
}

std::vector<MmsObjectName> decode_list_of_variable(const BerTlv& list) {
    if (list.tag_class != BerClass::context_specific || list.tag_number != 0 || !list.constructed) {
        throw MmsFormatError("MMS listOfVariable wrapper is invalid.");
    }
    const auto definitions = BerReader::read_children(list.value);
    if (definitions.empty() || definitions.size() > MmsServiceCodec::maximum_variables) {
        throw MmsFormatError("MMS listOfVariable count is outside the configured limit.");
    }

    std::vector<MmsObjectName> result;
    result.reserve(definitions.size());
    for (const auto& definition : definitions) {
        if (definition.encoded_tag != 0x30U || !definition.constructed) {
            throw MmsFormatError("MMS variable definition is not a SEQUENCE.");
        }
        const auto fields = BerReader::read_children(definition.value);
        if (fields.size() != 1U || fields[0].encoded_tag != 0xA0U || !fields[0].constructed) {
            throw MmsFormatError("MMS variable definition name wrapper is invalid.");
        }
        result.push_back(MmsServiceCodec::decode_object_name(fields[0].value));
    }
    return result;
}

bool is_mms_data_tlv(const BerTlv& tlv) noexcept {
    if (tlv.tag_class != BerClass::context_specific) {
        return false;
    }
    if (tlv.tag_number == 1 || tlv.tag_number == 2) {
        return tlv.constructed;
    }
    return tlv.tag_number >= 3 && tlv.tag_number <= 17 && !tlv.constructed;
}

std::vector<std::uint8_t> encode_type_impl(
    const MmsTypeSpecification& type,
    const std::size_t depth,
    std::size_t& component_count) {
    if (depth > MmsServiceCodec::maximum_type_depth) {
        throw MmsFormatError("MMS TypeSpecification nesting exceeds the configured limit.");
    }
    if (type.name.size() > MmsServiceCodec::maximum_identifier_bytes) {
        throw MmsFormatError("MMS component name is too long.");
    }

    const auto size_content = [&type]() {
        return type.size ? positive_integer_content(*type.size) : std::vector<std::uint8_t>{};
    };

    switch (type.kind) {
    case MmsTypeKind::array: {
        if (type.children.size() != 1U || !type.size) {
            throw MmsFormatError("MMS array TypeSpecification requires a size and one element type.");
        }
        const auto count = BerWriter::encode_tlv(
            BerClass::context_specific, false, 1, positive_integer_content(*type.size));
        const auto element = encode_type_impl(type.children[0], depth + 1U, component_count);
        const auto element_wrapper = BerWriter::encode_tlv(0xA2U, element);
        const auto body = concat({count, element_wrapper});
        return BerWriter::encode_tlv(0xA1U, body);
    }
    case MmsTypeKind::structure: {
        if (type.children.size() > MmsServiceCodec::maximum_type_components - component_count) {
            throw MmsFormatError("MMS structure component count exceeds the configured limit.");
        }
        component_count += type.children.size();
        std::vector<std::uint8_t> components;
        for (const auto& child : type.children) {
            validate_identifier(child.name, "MMS structure component name");
            const auto name = BerWriter::encode_tlv(
                BerClass::context_specific, false, 0, BerWriter::encode_ascii(child.name));
            const auto child_type = encode_type_impl(child, depth + 1U, component_count);
            const auto wrapper = BerWriter::encode_tlv(0xA1U, child_type);
            const auto component_body = concat({name, wrapper});
            const auto component = BerWriter::encode_tlv(0x30U, component_body);
            components.insert(components.end(), component.begin(), component.end());
        }
        const auto component_list = BerWriter::encode_tlv(0xA1U, components);
        return BerWriter::encode_tlv(0xA2U, component_list);
    }
    case MmsTypeKind::floating_point: {
        const auto width = type.size.value_or(32U);
        const auto exponent = type.exponent_width.value_or(width == 64U ? 11U : 8U);
        const auto width_tlv = BerWriter::encode_tlv(0x02U, positive_integer_content(width));
        const auto exponent_tlv = BerWriter::encode_tlv(0x02U, positive_integer_content(exponent));
        const auto body = concat({width_tlv, exponent_tlv});
        return BerWriter::encode_tlv(0xA7U, body);
    }
    case MmsTypeKind::boolean:
        return BerWriter::encode_tlv(0x83U, {});
    case MmsTypeKind::bit_string:
        return BerWriter::encode_tlv(0x84U, size_content());
    case MmsTypeKind::integer:
        return BerWriter::encode_tlv(0x85U, size_content());
    case MmsTypeKind::unsigned_integer:
        return BerWriter::encode_tlv(0x86U, size_content());
    case MmsTypeKind::octet_string:
        return BerWriter::encode_tlv(0x89U, size_content());
    case MmsTypeKind::visible_string:
        return BerWriter::encode_tlv(0x8AU, size_content());
    case MmsTypeKind::binary_time:
        return BerWriter::encode_tlv(0x8CU, size_content());
    case MmsTypeKind::bcd:
        return BerWriter::encode_tlv(0x8DU, size_content());
    case MmsTypeKind::boolean_array:
        return BerWriter::encode_tlv(0x8EU, size_content());
    case MmsTypeKind::object_id:
        return BerWriter::encode_tlv(0x8FU, size_content());
    case MmsTypeKind::mms_string:
        return BerWriter::encode_tlv(0x90U, size_content());
    case MmsTypeKind::utc_time:
        return BerWriter::encode_tlv(0x91U, {});
    case MmsTypeKind::unknown:
        break;
    }
    throw MmsFormatError("Cannot encode an unknown MMS TypeSpecification.");
}

std::optional<std::uint32_t> read_optional_size(const BerTlv& tlv) {
    if (tlv.value.empty()) {
        return std::nullopt;
    }
    return read_u32(tlv, "MMS type size");
}

MmsTypeSpecification decode_type_impl(
    const BerTlv& tlv,
    std::string component_name,
    const std::size_t depth,
    std::size_t& component_count) {
    if (depth > MmsServiceCodec::maximum_type_depth) {
        throw MmsFormatError("MMS TypeSpecification nesting exceeds the configured limit.");
    }
    if (tlv.tag_class != BerClass::context_specific) {
        throw MmsFormatError("MMS TypeSpecification must be context-specific.");
    }

    MmsTypeSpecification result;
    result.name = std::move(component_name);
    switch (tlv.tag_number) {
    case 1: {
        if (!tlv.constructed) {
            throw MmsFormatError("MMS array TypeSpecification must be constructed.");
        }
        result.kind = MmsTypeKind::array;
        const auto children = BerReader::read_children(tlv.value);
        std::optional<MmsTypeSpecification> element;
        for (const auto& child : children) {
            if (child.tag_class == BerClass::context_specific && child.tag_number == 1 && !child.constructed) {
                result.size = read_u32(child, "MMS array element count");
            } else if (child.tag_class == BerClass::context_specific && child.tag_number == 2 && child.constructed) {
                const auto wrapped = BerReader::read_children(child.value);
                if (wrapped.size() != 1U) {
                    throw MmsFormatError("MMS array element type wrapper is invalid.");
                }
                element = decode_type_impl(wrapped[0], "element", depth + 1U, component_count);
            }
        }
        if (!result.size || !element) {
            throw MmsFormatError("MMS array TypeSpecification is incomplete.");
        }
        result.children.push_back(std::move(*element));
        return result;
    }
    case 2: {
        if (!tlv.constructed) {
            throw MmsFormatError("MMS structure TypeSpecification must be constructed.");
        }
        result.kind = MmsTypeKind::structure;
        auto fields = BerReader::read_children(tlv.value);
        if (fields.size() == 1U && fields[0].tag_class == BerClass::context_specific &&
            fields[0].tag_number == 1 && fields[0].constructed) {
            fields = BerReader::read_children(fields[0].value);
        }
        if (fields.size() > MmsServiceCodec::maximum_type_components - component_count) {
            throw MmsFormatError("MMS structure component count exceeds the configured limit.");
        }
        component_count += fields.size();
        for (const auto& component : fields) {
            if (component.encoded_tag != 0x30U || !component.constructed) {
                throw MmsFormatError("MMS structure component is not a SEQUENCE.");
            }
            const auto component_fields = BerReader::read_children(component.value);
            std::string name;
            std::optional<MmsTypeSpecification> type;
            for (const auto& field : component_fields) {
                if (field.tag_class == BerClass::context_specific && field.tag_number == 0) {
                    name = read_identifier(field, "MMS component name");
                } else if (field.tag_class == BerClass::context_specific && field.tag_number == 1 && field.constructed) {
                    const auto wrapped = BerReader::read_children(field.value);
                    if (wrapped.size() != 1U) {
                        throw MmsFormatError("MMS component type wrapper is invalid.");
                    }
                    type = decode_type_impl(wrapped[0], name, depth + 1U, component_count);
                }
            }
            if (name.empty() || !type) {
                throw MmsFormatError("MMS structure component is incomplete.");
            }
            result.children.push_back(std::move(*type));
        }
        return result;
    }
    case 3: result.kind = MmsTypeKind::boolean; break;
    case 4: result.kind = MmsTypeKind::bit_string; break;
    case 5: result.kind = MmsTypeKind::integer; break;
    case 6: result.kind = MmsTypeKind::unsigned_integer; break;
    case 7: {
        if (!tlv.constructed) {
            throw MmsFormatError("MMS floating-point TypeSpecification must be constructed.");
        }
        result.kind = MmsTypeKind::floating_point;
        const auto fields = BerReader::read_children(tlv.value);
        if (fields.size() != 2U || fields[0].encoded_tag != 0x02U || fields[1].encoded_tag != 0x02U) {
            throw MmsFormatError("MMS floating-point TypeSpecification is invalid.");
        }
        result.size = read_u32(fields[0], "MMS floating-point width");
        result.exponent_width = read_u32(fields[1], "MMS floating-point exponent width");
        return result;
    }
    case 9: result.kind = MmsTypeKind::octet_string; break;
    case 10: result.kind = MmsTypeKind::visible_string; break;
    case 12: result.kind = MmsTypeKind::binary_time; break;
    case 13: result.kind = MmsTypeKind::bcd; break;
    case 14: result.kind = MmsTypeKind::boolean_array; break;
    case 15: result.kind = MmsTypeKind::object_id; break;
    case 16: result.kind = MmsTypeKind::mms_string; break;
    case 17: result.kind = MmsTypeKind::utc_time; break;
    default:
        throw MmsFormatError("Unsupported MMS TypeSpecification tag.");
    }
    result.size = read_optional_size(tlv);
    return result;
}

} // namespace

MmsObjectName MmsObjectName::vmd(std::string name) {
    return {MmsObjectNameKind::vmd_specific, {}, std::move(name)};
}

MmsObjectName MmsObjectName::domain_specific(std::string domain, std::string item) {
    return {MmsObjectNameKind::domain_specific, std::move(domain), std::move(item)};
}

MmsObjectName MmsObjectName::aa(std::string name) {
    return {MmsObjectNameKind::aa_specific, {}, std::move(name)};
}

std::string MmsObjectName::reference() const {
    return kind == MmsObjectNameKind::domain_specific ? domain + "/" + item : item;
}

std::string MmsTypeSpecification::mms_type_name() const {
    switch (kind) {
    case MmsTypeKind::array: return "array";
    case MmsTypeKind::structure: return "structure";
    case MmsTypeKind::boolean: return "boolean";
    case MmsTypeKind::bit_string: return "bit-string";
    case MmsTypeKind::integer: return "integer";
    case MmsTypeKind::unsigned_integer: return "unsigned";
    case MmsTypeKind::floating_point: return "floating-point";
    case MmsTypeKind::octet_string: return "octet-string";
    case MmsTypeKind::visible_string: return "visible-string";
    case MmsTypeKind::binary_time: return "binary-time";
    case MmsTypeKind::bcd: return "bcd";
    case MmsTypeKind::boolean_array: return "boolean-array";
    case MmsTypeKind::object_id: return "object-id";
    case MmsTypeKind::mms_string: return "mms-string";
    case MmsTypeKind::utc_time: return "utc-time";
    case MmsTypeKind::unknown: return "unknown";
    }
    return "unknown";
}

std::string MmsTypeSpecification::scl_basic_type() const {
    switch (kind) {
    case MmsTypeKind::array:
    case MmsTypeKind::structure: return "Struct";
    case MmsTypeKind::boolean: return "BOOLEAN";
    case MmsTypeKind::bit_string:
    case MmsTypeKind::boolean_array: return "Check";
    case MmsTypeKind::integer:
    case MmsTypeKind::bcd: return "INT32";
    case MmsTypeKind::unsigned_integer: return "INT32U";
    case MmsTypeKind::floating_point: return size == 64U ? "FLOAT64" : "FLOAT32";
    case MmsTypeKind::octet_string: return "Octet64";
    case MmsTypeKind::visible_string: return "VisString255";
    case MmsTypeKind::binary_time:
    case MmsTypeKind::utc_time: return "Timestamp";
    case MmsTypeKind::object_id: return "ObjRef";
    case MmsTypeKind::mms_string: return "Unicode255";
    case MmsTypeKind::unknown: return {};
    }
    return {};
}

std::string MmsTypeSpecification::signature() const {
    std::ostringstream stream;
    if (!name.empty()) {
        stream << name << ':';
    }
    stream << mms_type_name();
    if (!children.empty()) {
        stream << '(';
        for (std::size_t index = 0; index < children.size(); ++index) {
            if (index != 0U) {
                stream << ',';
            }
            stream << children[index].signature();
        }
        stream << ')';
    }
    return stream.str();
}

bool MmsWriteResponse::all_success() const noexcept {
    return !results.empty() && std::all_of(results.begin(), results.end(), [](const auto& result) {
        return result.success;
    });
}

std::vector<std::uint8_t> MmsServiceCodec::encode_object_name(const MmsObjectName& name) {
    switch (name.kind) {
    case MmsObjectNameKind::vmd_specific:
        validate_identifier(name.item, "MMS VMD-specific name");
        return BerWriter::encode_tlv(
            BerClass::context_specific, false, 0, BerWriter::encode_ascii(name.item));
    case MmsObjectNameKind::domain_specific: {
        validate_identifier(name.domain, "MMS domain ID");
        validate_identifier(name.item, "MMS item ID");
        const auto domain = visible_string(name.domain);
        const auto item = visible_string(name.item);
        const auto body = concat({domain, item});
        return BerWriter::encode_tlv(0xA1U, body);
    }
    case MmsObjectNameKind::aa_specific:
        validate_identifier(name.item, "MMS association-specific name");
        return BerWriter::encode_tlv(
            BerClass::context_specific, false, 2, BerWriter::encode_ascii(name.item));
    }
    throw MmsFormatError("Unknown MMS ObjectName kind.");
}

MmsObjectName MmsServiceCodec::decode_object_name(
    const std::span<const std::uint8_t> encoded_object_name) {
    const auto tlv = read_one_exact(encoded_object_name, "MMS ObjectName");
    if (tlv.tag_class != BerClass::context_specific) {
        throw MmsFormatError("MMS ObjectName is not context-specific.");
    }
    if (tlv.tag_number == 0 && !tlv.constructed) {
        return MmsObjectName::vmd(read_identifier(tlv, "MMS VMD-specific name"));
    }
    if (tlv.tag_number == 2 && !tlv.constructed) {
        return MmsObjectName::aa(read_identifier(tlv, "MMS association-specific name"));
    }
    if (tlv.tag_number == 1 && tlv.constructed) {
        const auto children = BerReader::read_children(tlv.value);
        if (children.size() != 2U) {
            throw MmsFormatError("MMS domain-specific ObjectName must contain two identifiers.");
        }
        return MmsObjectName::domain_specific(
            read_identifier(children[0], "MMS domain ID"),
            read_identifier(children[1], "MMS item ID"));
    }
    throw MmsFormatError("Unsupported MMS ObjectName choice.");
}

std::vector<std::uint8_t> MmsServiceCodec::encode_get_name_list_request_pdu(
    const MmsGetNameListRequest& request) {
    const auto object_class_value = BerWriter::encode_tlv(
        BerClass::context_specific, false, 0,
        positive_integer_content(static_cast<std::uint32_t>(request.object_class)));
    const auto object_class = BerWriter::encode_tlv(0xA0U, object_class_value);

    std::vector<std::uint8_t> scope_choice;
    switch (request.scope) {
    case MmsObjectScopeKind::vmd_specific:
        scope_choice = BerWriter::encode_tlv(
            BerClass::context_specific, false, 0, {});
        break;
    case MmsObjectScopeKind::domain_specific:
        validate_identifier(request.domain_id, "MMS GetNameList domain ID");
        scope_choice = BerWriter::encode_tlv(
            BerClass::context_specific, false, 1,
            BerWriter::encode_ascii(request.domain_id));
        break;
    case MmsObjectScopeKind::aa_specific:
        scope_choice = BerWriter::encode_tlv(
            BerClass::context_specific, false, 2, {});
        break;
    }
    const auto object_scope = BerWriter::encode_tlv(0xA1U, scope_choice);
    auto body = concat({object_class, object_scope});
    if (!request.continue_after.empty()) {
        validate_identifier(request.continue_after, "MMS GetNameList continuation");
        const auto continuation = BerWriter::encode_tlv(
            BerClass::context_specific, false, 2,
            BerWriter::encode_ascii(request.continue_after));
        body = concat({body, continuation});
    }
    return MmsPduCodec::encode_confirmed_request(
        {request.invoke_id, 1, true, std::move(body)});
}

std::vector<std::uint8_t> MmsServiceCodec::encode_get_name_list_request_p_data(
    const MmsGetNameListRequest& request,
    const std::uint32_t presentation_context_id) {
    const auto pdu = encode_get_name_list_request_pdu(request);
    return MmsPduCodec::wrap_p_data(pdu, presentation_context_id);
}

MmsGetNameListRequest MmsServiceCodec::decode_get_name_list_request(
    const std::span<const std::uint8_t> payload) {
    const auto confirmed = confirmed_request_for(payload, 1);
    const auto fields = BerReader::read_children(confirmed.service_value);
    if (fields.size() < 2U || fields.size() > 3U) {
        throw MmsFormatError("MMS GetNameList request field count is invalid.");
    }

    MmsGetNameListRequest result;
    result.invoke_id = confirmed.invoke_id;
    bool have_class = false;
    bool have_scope = false;
    for (const auto& field : fields) {
        if (field.encoded_tag == 0xA0U && field.constructed) {
            if (have_class) {
                throw MmsFormatError("Duplicate MMS GetNameList object class.");
            }
            const auto children = BerReader::read_children(field.value);
            if (children.size() != 1U || children[0].tag_class != BerClass::context_specific ||
                children[0].tag_number != 0 || children[0].constructed) {
                throw MmsFormatError("MMS GetNameList object class field is invalid.");
            }
            const auto value = read_u32(children[0], "MMS object class");
            if (value != 0U && value != 2U && value != 9U) {
                throw MmsFormatError("Unsupported MMS GetNameList object class.");
            }
            result.object_class = static_cast<MmsGetNameListObjectClass>(value);
            have_class = true;
        } else if (field.encoded_tag == 0xA1U && field.constructed) {
            if (have_scope) {
                throw MmsFormatError("Duplicate MMS GetNameList object scope.");
            }
            const auto children = BerReader::read_children(field.value);
            if (children.size() != 1U || children[0].tag_class != BerClass::context_specific ||
                children[0].constructed) {
                throw MmsFormatError("MMS GetNameList object scope is invalid.");
            }
            switch (children[0].tag_number) {
            case 0:
                if (!children[0].value.empty()) {
                    throw MmsFormatError("MMS VMD object scope must be empty.");
                }
                result.scope = MmsObjectScopeKind::vmd_specific;
                break;
            case 1:
                result.scope = MmsObjectScopeKind::domain_specific;
                result.domain_id = read_identifier(children[0], "MMS GetNameList domain ID");
                break;
            case 2:
                if (!children[0].value.empty()) {
                    throw MmsFormatError("MMS association object scope must be empty.");
                }
                result.scope = MmsObjectScopeKind::aa_specific;
                break;
            default:
                throw MmsFormatError("Unsupported MMS GetNameList object scope.");
            }
            have_scope = true;
        } else if (field.tag_class == BerClass::context_specific && field.tag_number == 2 && !field.constructed) {
            result.continue_after = read_identifier(field, "MMS GetNameList continuation");
        } else {
            throw MmsFormatError("Unexpected MMS GetNameList request field.");
        }
    }
    if (!have_class || !have_scope) {
        throw MmsFormatError("MMS GetNameList request is incomplete.");
    }
    return result;
}

std::vector<std::uint8_t> MmsServiceCodec::encode_get_name_list_response_pdu(
    const MmsGetNameListResponse& response) {
    if (response.names.size() > maximum_identifiers) {
        throw MmsFormatError("MMS GetNameList response contains too many names.");
    }
    std::vector<std::uint8_t> identifiers;
    for (const auto& name : response.names) {
        const auto encoded = visible_string(name);
        identifiers.insert(identifiers.end(), encoded.begin(), encoded.end());
    }
    const auto list = BerWriter::encode_tlv(0xA0U, identifiers);
    const std::array<std::uint8_t, 1> boolean{
        response.more_follows ? std::uint8_t{0xFFU} : std::uint8_t{0x00U}};
    const auto more = BerWriter::encode_tlv(
        BerClass::context_specific, false, 1, boolean);
    const auto body = concat({list, more});
    return MmsPduCodec::encode_confirmed_response(
        {response.invoke_id, 1, true, body});
}

std::vector<std::uint8_t> MmsServiceCodec::encode_get_name_list_response_p_data(
    const MmsGetNameListResponse& response,
    const std::uint32_t presentation_context_id) {
    const auto pdu = encode_get_name_list_response_pdu(response);
    return MmsPduCodec::wrap_p_data(pdu, presentation_context_id);
}

MmsGetNameListResponse MmsServiceCodec::decode_get_name_list_response(
    const std::span<const std::uint8_t> payload,
    const std::optional<std::uint32_t> expected_invoke_id) {
    const auto confirmed = confirmed_response_for(payload, 1, expected_invoke_id);
    const auto fields = BerReader::read_children(confirmed.service_value);
    MmsGetNameListResponse result;
    result.invoke_id = confirmed.invoke_id;
    bool have_list = false;
    bool have_more = false;
    for (const auto& field : fields) {
        if (field.tag_class == BerClass::context_specific && field.tag_number == 0 && field.constructed) {
            if (have_list) {
                throw MmsFormatError("Duplicate MMS GetNameList identifier list.");
            }
            const auto names = BerReader::read_children(field.value);
            if (names.size() > maximum_identifiers) {
                throw MmsFormatError("MMS GetNameList identifier count exceeds the configured limit.");
            }
            for (const auto& name : names) {
                result.names.push_back(read_identifier(name, "MMS GetNameList identifier"));
            }
            have_list = true;
        } else if (field.tag_class == BerClass::context_specific && field.tag_number == 1 && !field.constructed) {
            if (have_more || field.value.size() != 1U) {
                throw MmsFormatError("Invalid MMS GetNameList moreFollows field.");
            }
            result.more_follows = field.value[0] != 0U;
            have_more = true;
        } else {
            throw MmsFormatError("Unexpected MMS GetNameList response field.");
        }
    }
    if (!have_list) {
        throw MmsFormatError("MMS GetNameList response has no identifier list.");
    }
    return result;
}

std::vector<std::uint8_t> MmsServiceCodec::encode_type_specification(
    const MmsTypeSpecification& type) {
    std::size_t component_count = 0U;
    return encode_type_impl(type, 0U, component_count);
}

MmsTypeSpecification MmsServiceCodec::decode_type_specification(
    const std::span<const std::uint8_t> encoded_type) {
    const auto tlv = read_one_exact(encoded_type, "MMS TypeSpecification");
    std::size_t component_count = 0U;
    return decode_type_impl(tlv, {}, 0U, component_count);
}

std::vector<std::uint8_t>
MmsServiceCodec::encode_variable_access_attributes_request_pdu(
    const MmsVariableAccessAttributesRequest& request) {
    const auto object_name = encode_object_name(request.name);
    const auto named_variable = BerWriter::encode_tlv(0xA0U, object_name);
    return MmsPduCodec::encode_confirmed_request(
        {request.invoke_id, 6, true, named_variable});
}

std::vector<std::uint8_t>
MmsServiceCodec::encode_variable_access_attributes_request_p_data(
    const MmsVariableAccessAttributesRequest& request,
    const std::uint32_t presentation_context_id) {
    const auto pdu = encode_variable_access_attributes_request_pdu(request);
    return MmsPduCodec::wrap_p_data(pdu, presentation_context_id);
}

MmsVariableAccessAttributesRequest
MmsServiceCodec::decode_variable_access_attributes_request(
    const std::span<const std::uint8_t> payload) {
    const auto confirmed = confirmed_request_for(payload, 6);
    const auto fields = BerReader::read_children(confirmed.service_value);
    if (fields.size() != 1U || fields[0].encoded_tag != 0xA0U || !fields[0].constructed) {
        throw MmsFormatError("MMS GetVariableAccessAttributes request is invalid.");
    }
    return {confirmed.invoke_id, decode_object_name(fields[0].value)};
}

std::vector<std::uint8_t>
MmsServiceCodec::encode_variable_access_attributes_response_pdu(
    const MmsVariableAccessAttributesResponse& response) {
    const std::array<std::uint8_t, 1> deletable{
        response.mms_deletable ? std::uint8_t{0xFFU} : std::uint8_t{0x00U}};
    const auto deletable_field = BerWriter::encode_tlv(
        BerClass::context_specific, false, 0, deletable);
    const auto type = encode_type_specification(response.type);
    const auto type_field = BerWriter::encode_tlv(0xA2U, type);
    const auto body = concat({deletable_field, type_field});
    return MmsPduCodec::encode_confirmed_response(
        {response.invoke_id, 6, true, body});
}

std::vector<std::uint8_t>
MmsServiceCodec::encode_variable_access_attributes_response_p_data(
    const MmsVariableAccessAttributesResponse& response,
    const std::uint32_t presentation_context_id) {
    const auto pdu = encode_variable_access_attributes_response_pdu(response);
    return MmsPduCodec::wrap_p_data(pdu, presentation_context_id);
}

MmsVariableAccessAttributesResponse
MmsServiceCodec::decode_variable_access_attributes_response(
    const std::span<const std::uint8_t> payload,
    const std::optional<std::uint32_t> expected_invoke_id) {
    const auto confirmed = confirmed_response_for(payload, 6, expected_invoke_id);
    const auto fields = BerReader::read_children(confirmed.service_value);
    bool have_deletable = false;
    bool have_type = false;
    MmsVariableAccessAttributesResponse result;
    result.invoke_id = confirmed.invoke_id;
    for (const auto& field : fields) {
        if (field.tag_class == BerClass::context_specific && field.tag_number == 0 && !field.constructed) {
            if (have_deletable || field.value.size() != 1U) {
                throw MmsFormatError("Invalid MMS mmsDeletable field.");
            }
            result.mms_deletable = field.value[0] != 0U;
            have_deletable = true;
        } else if (field.tag_class == BerClass::context_specific && field.tag_number == 2 && field.constructed) {
            if (have_type) {
                throw MmsFormatError("Duplicate MMS TypeDescription field.");
            }
            result.type = decode_type_specification(field.value);
            have_type = true;
        } else {
            throw MmsFormatError("Unexpected MMS GetVariableAccessAttributes response field.");
        }
    }
    if (!have_type) {
        throw MmsFormatError("MMS GetVariableAccessAttributes response has no TypeDescription.");
    }
    return result;
}

std::vector<std::uint8_t> MmsServiceCodec::encode_read_request_pdu(
    const MmsReadRequest& request) {
    const auto list = encode_list_of_variable(request.variables);
    const auto specification = BerWriter::encode_tlv(0xA1U, list);
    std::vector<std::uint8_t> body;
    if (request.specification_with_result) {
        const std::array<std::uint8_t, 1> true_value{0xFFU};
        const auto flag = BerWriter::encode_tlv(
            BerClass::context_specific, false, 0, true_value);
        body = concat({flag, specification});
    } else {
        body = specification;
    }
    return MmsPduCodec::encode_confirmed_request(
        {request.invoke_id, 4, true, body});
}

std::vector<std::uint8_t> MmsServiceCodec::encode_read_request_p_data(
    const MmsReadRequest& request,
    const std::uint32_t presentation_context_id) {
    const auto pdu = encode_read_request_pdu(request);
    return MmsPduCodec::wrap_p_data(pdu, presentation_context_id);
}

MmsReadRequest MmsServiceCodec::decode_read_request(
    const std::span<const std::uint8_t> payload) {
    const auto confirmed = confirmed_request_for(payload, 4);
    const auto fields = BerReader::read_children(confirmed.service_value);
    MmsReadRequest result;
    result.invoke_id = confirmed.invoke_id;
    bool have_specification = false;
    for (const auto& field : fields) {
        if (field.tag_class == BerClass::context_specific && field.tag_number == 0 && !field.constructed) {
            if (field.value.size() != 1U) {
                throw MmsFormatError("MMS Read specificationWithResult field is invalid.");
            }
            result.specification_with_result = field.value[0] != 0U;
        } else if (field.tag_class == BerClass::context_specific && field.tag_number == 1 && field.constructed) {
            if (have_specification) {
                throw MmsFormatError("Duplicate MMS Read VariableAccessSpecification.");
            }
            const auto inner = BerReader::read_children(field.value);
            if (inner.size() != 1U) {
                throw MmsFormatError("MMS Read VariableAccessSpecification wrapper is invalid.");
            }
            result.variables = decode_list_of_variable(inner[0]);
            have_specification = true;
        } else {
            throw MmsFormatError("Unexpected MMS Read request field.");
        }
    }
    if (!have_specification) {
        throw MmsFormatError("MMS Read request has no VariableAccessSpecification.");
    }
    return result;
}

std::vector<std::uint8_t> MmsServiceCodec::encode_read_response_pdu(
    const MmsReadResponse& response) {
    if (response.results.empty() || response.results.size() > maximum_variables) {
        throw MmsFormatError("MMS Read response result count is outside the configured limit.");
    }
    std::vector<std::uint8_t> results;
    for (const auto& result : response.results) {
        std::vector<std::uint8_t> encoded;
        if (result.value) {
            encoded = MmsDataCodec::encode(*result.value);
        } else if (result.failure_code) {
            encoded = BerWriter::encode_tlv(
                BerClass::context_specific, false, 0,
                positive_integer_content(*result.failure_code));
        } else {
            throw MmsFormatError("MMS Read AccessResult is neither success nor failure.");
        }
        results.insert(results.end(), encoded.begin(), encoded.end());
    }
    const auto list = BerWriter::encode_tlv(0xA1U, results);
    return MmsPduCodec::encode_confirmed_response(
        {response.invoke_id, 4, true, list});
}

std::vector<std::uint8_t> MmsServiceCodec::encode_read_response_p_data(
    const MmsReadResponse& response,
    const std::uint32_t presentation_context_id) {
    const auto pdu = encode_read_response_pdu(response);
    return MmsPduCodec::wrap_p_data(pdu, presentation_context_id);
}

MmsReadResponse MmsServiceCodec::decode_read_response(
    const std::span<const std::uint8_t> payload,
    const std::optional<std::uint32_t> expected_invoke_id) {
    const auto confirmed = confirmed_response_for(payload, 4, expected_invoke_id);
    const auto fields = BerReader::read_children(confirmed.service_value);
    const auto list_it = std::find_if(fields.begin(), fields.end(), [](const BerTlv& field) {
        return field.tag_class == BerClass::context_specific &&
               field.tag_number == 1 && field.constructed;
    });
    if (list_it == fields.end()) {
        throw MmsFormatError("MMS Read response has no listOfAccessResult.");
    }
    const auto access_results = BerReader::read_children(list_it->value);
    if (access_results.empty() || access_results.size() > maximum_variables) {
        throw MmsFormatError("MMS Read AccessResult count is outside the configured limit.");
    }

    MmsReadResponse result;
    result.invoke_id = confirmed.invoke_id;
    for (const auto& access : access_results) {
        if (access.tag_class == BerClass::context_specific &&
            access.tag_number == 0 && !access.constructed) {
            result.results.push_back({std::nullopt, read_u32(access, "MMS DataAccessError")});
        } else if (is_mms_data_tlv(access)) {
            result.results.push_back({MmsDataCodec::decode(access), std::nullopt});
        } else if (access.tag_class == BerClass::context_specific &&
                   access.tag_number == 0 && access.constructed) {
            const auto wrapped = BerReader::read_children(access.value);
            if (wrapped.size() != 1U || !is_mms_data_tlv(wrapped[0])) {
                throw MmsFormatError("MMS legacy Read success wrapper is invalid.");
            }
            result.results.push_back({MmsDataCodec::decode(wrapped[0]), std::nullopt});
        } else {
            throw MmsFormatError("Unsupported MMS Read AccessResult.");
        }
    }
    return result;
}

std::vector<std::uint8_t> MmsServiceCodec::encode_write_request_pdu(
    const MmsWriteRequest& request) {
    if (request.variables.empty() || request.variables.size() != request.values.size() ||
        request.variables.size() > maximum_variables) {
        throw MmsFormatError("MMS Write variable/value counts are invalid.");
    }
    const auto variables = encode_list_of_variable(request.variables);
    std::vector<std::uint8_t> values;
    for (const auto& value : request.values) {
        const auto encoded = MmsDataCodec::encode(value);
        values.insert(values.end(), encoded.begin(), encoded.end());
    }
    const auto list_of_data = BerWriter::encode_tlv(0xA0U, values);
    const auto body = concat({variables, list_of_data});
    return MmsPduCodec::encode_confirmed_request(
        {request.invoke_id, 5, true, body});
}

std::vector<std::uint8_t> MmsServiceCodec::encode_write_request_p_data(
    const MmsWriteRequest& request,
    const std::uint32_t presentation_context_id) {
    const auto pdu = encode_write_request_pdu(request);
    return MmsPduCodec::wrap_p_data(pdu, presentation_context_id);
}

MmsWriteRequest MmsServiceCodec::decode_write_request(
    const std::span<const std::uint8_t> payload) {
    const auto confirmed = confirmed_request_for(payload, 5);
    const auto fields = BerReader::read_children(confirmed.service_value);
    if (fields.size() != 2U ||
        fields[0].tag_class != BerClass::context_specific || fields[0].tag_number != 0 || !fields[0].constructed ||
        fields[1].tag_class != BerClass::context_specific || fields[1].tag_number != 0 || !fields[1].constructed) {
        throw MmsFormatError("MMS Write request must contain listOfVariable and listOfData.");
    }

    MmsWriteRequest result;
    result.invoke_id = confirmed.invoke_id;
    result.variables = decode_list_of_variable(fields[0]);
    const auto data = BerReader::read_children(fields[1].value);
    if (data.empty() || data.size() > maximum_variables || data.size() != result.variables.size()) {
        throw MmsFormatError("MMS Write listOfData count does not match listOfVariable.");
    }
    for (const auto& value : data) {
        if (!is_mms_data_tlv(value)) {
            throw MmsFormatError("MMS Write listOfData contains an invalid Data value.");
        }
        result.values.push_back(MmsDataCodec::decode(value));
    }
    return result;
}

std::vector<std::uint8_t> MmsServiceCodec::encode_write_response_pdu(
    const MmsWriteResponse& response) {
    if (response.results.empty() || response.results.size() > maximum_variables) {
        throw MmsFormatError("MMS Write response result count is outside the configured limit.");
    }
    std::vector<std::uint8_t> results;
    for (const auto& result : response.results) {
        std::vector<std::uint8_t> encoded;
        if (result.success) {
            encoded = BerWriter::encode_tlv(
                BerClass::context_specific, false, 1, {});
        } else if (result.failure_code) {
            encoded = BerWriter::encode_tlv(
                BerClass::context_specific, false, 0,
                positive_integer_content(*result.failure_code));
        } else {
            throw MmsFormatError("MMS Write failure result has no failure code.");
        }
        results.insert(results.end(), encoded.begin(), encoded.end());
    }
    return MmsPduCodec::encode_confirmed_response(
        {response.invoke_id, 5, true, results});
}

std::vector<std::uint8_t> MmsServiceCodec::encode_write_response_p_data(
    const MmsWriteResponse& response,
    const std::uint32_t presentation_context_id) {
    const auto pdu = encode_write_response_pdu(response);
    return MmsPduCodec::wrap_p_data(pdu, presentation_context_id);
}

MmsWriteResponse MmsServiceCodec::decode_write_response(
    const std::span<const std::uint8_t> payload,
    const std::optional<std::uint32_t> expected_invoke_id) {
    const auto confirmed = confirmed_response_for(payload, 5, expected_invoke_id);
    MmsWriteResponse result;
    result.invoke_id = confirmed.invoke_id;
    if (!confirmed.service_constructed && confirmed.service_value.empty()) {
        result.results.push_back({true, std::nullopt});
        return result;
    }
    const auto fields = BerReader::read_children(confirmed.service_value);
    if (fields.empty() || fields.size() > maximum_variables) {
        throw MmsFormatError("MMS Write response result count is outside the configured limit.");
    }
    for (const auto& field : fields) {
        if (field.tag_class != BerClass::context_specific || field.constructed) {
            throw MmsFormatError("MMS Write response contains an invalid result.");
        }
        if (field.tag_number == 1) {
            if (!field.value.empty()) {
                throw MmsFormatError("MMS Write success result must be empty.");
            }
            result.results.push_back({true, std::nullopt});
        } else if (field.tag_number == 0) {
            result.results.push_back({false, read_u32(field, "MMS Write failure code")});
        } else {
            throw MmsFormatError("Unsupported MMS Write response result choice.");
        }
    }
    return result;
}

} // namespace ar::iec61850::mms
