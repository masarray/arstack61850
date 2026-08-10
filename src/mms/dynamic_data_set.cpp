// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/dynamic_data_set.hpp"

#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/mms/pdu.hpp"
#include "ariec61850/mms/services.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <utility>

namespace ar::iec61850::mms {
namespace {

using asn1::BerClass;
using asn1::BerReader;
using asn1::BerTlv;
using asn1::BerWriter;

[[nodiscard]] std::vector<std::uint8_t> concat(
    const std::initializer_list<std::span<const std::uint8_t>> parts) {
    std::size_t total = 0U;
    for (const auto part : parts) {
        if (part.size() > MmsPduCodec::maximum_pdu_bytes - total) {
            throw MmsDynamicDataSetError(
                "Dynamic DataSet encoding exceeds the configured MMS PDU limit.");
        }
        total += part.size();
    }

    std::vector<std::uint8_t> result;
    result.reserve(total);
    for (const auto part : parts) {
        result.insert(result.end(), part.begin(), part.end());
    }
    return result;
}

[[nodiscard]] std::vector<std::uint8_t> positive_integer_content(
    const std::uint32_t value) {
    auto encoded = BerWriter::encode_unsigned_integer(value);
    if (encoded.empty()) {
        encoded.push_back(0U);
    }
    if ((encoded.front() & 0x80U) != 0U) {
        encoded.insert(encoded.begin(), 0U);
    }
    return encoded;
}

[[nodiscard]] std::uint32_t read_u32(
    const BerTlv& tlv,
    const char* field_name) {
    const auto value = BerReader::read_unsigned_integer(tlv);
    if (!value || *value > std::numeric_limits<std::uint32_t>::max()) {
        throw MmsDynamicDataSetError(
            std::string{"Dynamic DataSet "} + field_name +
            " is not an unsigned 32-bit value.");
    }
    return static_cast<std::uint32_t>(*value);
}

void validate_data_set_name(const MmsObjectName& name) {
    if (name.kind != MmsObjectNameKind::domain_specific ||
        name.domain.empty() || name.item.empty() ||
        name.item.find('$') == std::string::npos) {
        throw MmsDynamicDataSetError(
            "Dynamic DataSet name must be domain-specific IEC 61850 LD/LN.DataSetName.");
    }
    static_cast<void>(MmsServiceCodec::encode_object_name(name));
}

void validate_members(const std::span<const MmsObjectName> members) {
    if (members.empty() || members.size() > MmsNamedVariableListCodec::maximum_members) {
        throw MmsDynamicDataSetError(
            "Dynamic DataSet member count is outside the configured MMS limit.");
    }

    std::set<std::string, std::less<>> unique;
    for (const auto& member : members) {
        if (member.kind != MmsObjectNameKind::domain_specific ||
            member.domain.empty() || member.item.empty()) {
            throw MmsDynamicDataSetError(
                "Dynamic DataSet members must be domain-specific MMS named variables.");
        }
        static_cast<void>(MmsServiceCodec::encode_object_name(member));
        if (!unique.insert(member.reference()).second) {
            throw MmsDynamicDataSetError(
                "Dynamic DataSet contains a duplicate MMS member reference.");
        }
    }
}

[[nodiscard]] std::vector<std::uint8_t> encode_variable_definition(
    const MmsObjectName& name) {
    const auto encoded_name = MmsServiceCodec::encode_object_name(name);
    const auto variable_specification = BerWriter::encode_tlv(0xA0U, encoded_name);
    return BerWriter::encode_tlv(0x30U, variable_specification);
}

[[nodiscard]] MmsObjectName decode_variable_definition(const BerTlv& definition) {
    if (definition.encoded_tag != 0x30U || !definition.constructed) {
        throw MmsDynamicDataSetError(
            "Dynamic DataSet member is not an MMS VariableList SEQUENCE.");
    }
    const auto fields = BerReader::read_children(definition.value);
    if (fields.size() != 1U || fields[0].encoded_tag != 0xA0U ||
        !fields[0].constructed) {
        throw MmsDynamicDataSetError(
            "Dynamic DataSet member has an invalid variableSpecification.");
    }
    return MmsServiceCodec::decode_object_name(fields[0].value);
}

[[nodiscard]] std::vector<std::uint8_t> reencode_tlv(const BerTlv& tlv) {
    return BerWriter::encode_tlv(tlv.encoded_tag, tlv.value);
}

[[nodiscard]] std::span<const std::uint8_t> response_payload(
    const MmsConfirmedExchangeResult& exchange) {
    if (!exchange.presentation_payload.empty()) {
        return exchange.presentation_payload;
    }
    return exchange.envelope.mms_payload;
}

void require_confirmed_response(
    const MmsConfirmedExchangeResult& exchange,
    const char* operation) {
    if (exchange.envelope.kind == MmsPduKind::confirmed_error) {
        throw MmsDynamicDataSetError(
            std::string{"MMS "} + operation + " returned a Confirmed-Error PDU.");
    }
    if (exchange.envelope.kind != MmsPduKind::confirmed_response) {
        throw MmsDynamicDataSetError(
            std::string{"MMS "} + operation +
            " did not return a Confirmed-Response PDU.");
    }
}

} // namespace

std::vector<std::uint8_t> MmsNamedVariableListCodec::encode_define_request_pdu(
    const MmsDefineNamedVariableListRequest& request) {
    validate_data_set_name(request.data_set_name);
    validate_members(request.members);

    const auto list_name = MmsServiceCodec::encode_object_name(request.data_set_name);
    std::vector<std::uint8_t> definitions;
    for (const auto& member : request.members) {
        const auto definition = encode_variable_definition(member);
        if (definition.size() > MmsPduCodec::maximum_pdu_bytes - definitions.size()) {
            throw MmsDynamicDataSetError(
                "Dynamic DataSet member definitions exceed the configured MMS PDU limit.");
        }
        definitions.insert(definitions.end(), definition.begin(), definition.end());
    }
    const auto list_of_variable = BerWriter::encode_tlv(0xA0U, definitions);
    const auto body = concat({list_name, list_of_variable});
    return MmsPduCodec::encode_confirmed_request(
        {request.invoke_id, define_service_tag, true, body});
}

std::vector<std::uint8_t> MmsNamedVariableListCodec::encode_define_request_p_data(
    const MmsDefineNamedVariableListRequest& request,
    const std::uint32_t presentation_context_id) {
    const auto pdu = encode_define_request_pdu(request);
    return MmsPduCodec::wrap_p_data(pdu, presentation_context_id);
}

MmsDefineNamedVariableListRequest MmsNamedVariableListCodec::decode_define_request(
    const std::span<const std::uint8_t> presentation_or_mms_payload) {
    const auto mms = MmsPduCodec::extract_mms_payload(presentation_or_mms_payload);
    const auto request = MmsPduCodec::decode_confirmed_request(mms);
    if (request.service_tag != define_service_tag || !request.service_constructed) {
        throw MmsDynamicDataSetError(
            "MMS request is not DefineNamedVariableList.");
    }

    const auto fields = BerReader::read_children(request.service_value);
    if (fields.size() != 2U || fields[0].encoded_tag != 0xA1U ||
        !fields[0].constructed || fields[1].encoded_tag != 0xA0U ||
        !fields[1].constructed) {
        throw MmsDynamicDataSetError(
            "DefineNamedVariableList request fields are invalid.");
    }

    MmsDefineNamedVariableListRequest result;
    result.invoke_id = request.invoke_id;
    const auto encoded_list_name = reencode_tlv(fields[0]);
    result.data_set_name = MmsServiceCodec::decode_object_name(encoded_list_name);
    validate_data_set_name(result.data_set_name);

    const auto definitions = BerReader::read_children(fields[1].value);
    if (definitions.empty() || definitions.size() > maximum_members) {
        throw MmsDynamicDataSetError(
            "DefineNamedVariableList member count is outside the configured limit.");
    }
    result.members.reserve(definitions.size());
    for (const auto& definition : definitions) {
        result.members.push_back(decode_variable_definition(definition));
    }
    validate_members(result.members);
    return result;
}

std::vector<std::uint8_t> MmsNamedVariableListCodec::encode_define_response_pdu(
    const MmsDefineNamedVariableListResponse& response) {
    // ISO 9506 DefineNamedVariableList success is the service choice [11]
    // with an empty value. The primitive tag (0x8B) matches the C# oracle.
    return MmsPduCodec::encode_confirmed_response(
        {response.invoke_id, define_service_tag, false, {}});
}

std::vector<std::uint8_t> MmsNamedVariableListCodec::encode_define_response_p_data(
    const MmsDefineNamedVariableListResponse& response,
    const std::uint32_t presentation_context_id) {
    const auto pdu = encode_define_response_pdu(response);
    return MmsPduCodec::wrap_p_data(pdu, presentation_context_id);
}

MmsDefineNamedVariableListResponse MmsNamedVariableListCodec::decode_define_response(
    const std::span<const std::uint8_t> presentation_or_mms_payload,
    const std::optional<std::uint32_t> expected_invoke_id) {
    const auto mms = MmsPduCodec::extract_mms_payload(presentation_or_mms_payload);
    const auto response = MmsPduCodec::decode_confirmed_response(mms);
    if (expected_invoke_id && response.invoke_id != *expected_invoke_id) {
        throw MmsDynamicDataSetError(
            "DefineNamedVariableList response invoke ID mismatch.");
    }
    if (response.service_tag != define_service_tag || !response.service_value.empty()) {
        throw MmsDynamicDataSetError(
            "MMS response is not an empty successful DefineNamedVariableList response.");
    }
    return {response.invoke_id};
}

std::vector<std::uint8_t> MmsNamedVariableListCodec::encode_delete_request_pdu(
    const MmsDeleteNamedVariableListRequest& request) {
    validate_data_set_name(request.data_set_name);
    const auto list_name = MmsServiceCodec::encode_object_name(request.data_set_name);
    const auto list_of_variable_list_name = BerWriter::encode_tlv(0xA1U, list_name);
    return MmsPduCodec::encode_confirmed_request(
        {request.invoke_id, delete_service_tag, true, list_of_variable_list_name});
}

std::vector<std::uint8_t> MmsNamedVariableListCodec::encode_delete_request_p_data(
    const MmsDeleteNamedVariableListRequest& request,
    const std::uint32_t presentation_context_id) {
    const auto pdu = encode_delete_request_pdu(request);
    return MmsPduCodec::wrap_p_data(pdu, presentation_context_id);
}

MmsDeleteNamedVariableListRequest MmsNamedVariableListCodec::decode_delete_request(
    const std::span<const std::uint8_t> presentation_or_mms_payload) {
    const auto mms = MmsPduCodec::extract_mms_payload(presentation_or_mms_payload);
    const auto request = MmsPduCodec::decode_confirmed_request(mms);
    if (request.service_tag != delete_service_tag || !request.service_constructed) {
        throw MmsDynamicDataSetError(
            "MMS request is not DeleteNamedVariableList.");
    }

    const auto fields = BerReader::read_children(request.service_value);
    if (fields.size() != 1U || fields[0].encoded_tag != 0xA1U ||
        !fields[0].constructed) {
        throw MmsDynamicDataSetError(
            "DeleteNamedVariableList request listOfVariableListName is invalid.");
    }
    const auto names = BerReader::read_children(fields[0].value);
    if (names.size() != 1U || names[0].encoded_tag != 0xA1U ||
        !names[0].constructed) {
        throw MmsDynamicDataSetError(
            "DeleteNamedVariableList must contain exactly one domain-specific list name.");
    }

    const auto encoded_name = reencode_tlv(names[0]);
    auto data_set_name = MmsServiceCodec::decode_object_name(encoded_name);
    validate_data_set_name(data_set_name);
    return {request.invoke_id, std::move(data_set_name)};
}

std::vector<std::uint8_t> MmsNamedVariableListCodec::encode_delete_response_pdu(
    const MmsDeleteNamedVariableListResponse& response) {
    std::vector<std::uint8_t> body;
    if (response.number_matched) {
        const auto value = positive_integer_content(*response.number_matched);
        const auto field = BerWriter::encode_tlv(
            BerClass::context_specific, false, 0, value);
        body.insert(body.end(), field.begin(), field.end());
    }
    if (response.number_deleted) {
        const auto value = positive_integer_content(*response.number_deleted);
        const auto field = BerWriter::encode_tlv(
            BerClass::context_specific, false, 1, value);
        body.insert(body.end(), field.begin(), field.end());
    }
    if (body.empty()) {
        throw MmsDynamicDataSetError(
            "DeleteNamedVariableList response requires matched or deleted count evidence.");
    }
    return MmsPduCodec::encode_confirmed_response(
        {response.invoke_id, delete_service_tag, true, body});
}

std::vector<std::uint8_t> MmsNamedVariableListCodec::encode_delete_response_p_data(
    const MmsDeleteNamedVariableListResponse& response,
    const std::uint32_t presentation_context_id) {
    const auto pdu = encode_delete_response_pdu(response);
    return MmsPduCodec::wrap_p_data(pdu, presentation_context_id);
}

MmsDeleteNamedVariableListResponse MmsNamedVariableListCodec::decode_delete_response(
    const std::span<const std::uint8_t> presentation_or_mms_payload,
    const std::optional<std::uint32_t> expected_invoke_id) {
    const auto mms = MmsPduCodec::extract_mms_payload(presentation_or_mms_payload);
    const auto response = MmsPduCodec::decode_confirmed_response(mms);
    if (expected_invoke_id && response.invoke_id != *expected_invoke_id) {
        throw MmsDynamicDataSetError(
            "DeleteNamedVariableList response invoke ID mismatch.");
    }
    if (response.service_tag != delete_service_tag || !response.service_constructed) {
        throw MmsDynamicDataSetError(
            "MMS response is not DeleteNamedVariableList.");
    }

    MmsDeleteNamedVariableListResponse result;
    result.invoke_id = response.invoke_id;
    const auto fields = BerReader::read_children(response.service_value);
    for (const auto& field : fields) {
        if (field.tag_class != BerClass::context_specific || field.constructed) {
            throw MmsDynamicDataSetError(
                "DeleteNamedVariableList response contains an invalid count field.");
        }
        if (field.tag_number == 0) {
            if (result.number_matched) {
                throw MmsDynamicDataSetError(
                    "DeleteNamedVariableList response repeats numberMatched.");
            }
            result.number_matched = read_u32(field, "numberMatched");
        } else if (field.tag_number == 1) {
            if (result.number_deleted) {
                throw MmsDynamicDataSetError(
                    "DeleteNamedVariableList response repeats numberDeleted.");
            }
            result.number_deleted = read_u32(field, "numberDeleted");
        } else {
            throw MmsDynamicDataSetError(
                "DeleteNamedVariableList response contains an unknown field.");
        }
    }
    if (!result.number_matched && !result.number_deleted) {
        throw MmsDynamicDataSetError(
            "DeleteNamedVariableList response contains no count evidence.");
    }
    return result;
}

MmsDynamicDataSetRuntime::MmsDynamicDataSetRuntime(
    MmsAssociationRuntime& association,
    MmsDynamicDataSetOptions options)
    : association_{association}, options_{options} {
    if (options_.maximum_members == 0U ||
        options_.maximum_members > MmsNamedVariableListCodec::maximum_members) {
        throw std::invalid_argument(
            "Dynamic DataSet operational member limit is outside the codec bounds.");
    }
}

std::string MmsDynamicDataSetRuntime::normalize_reference(
    const std::string& reference) const {
    auto name = MmsDataSetDirectoryCodec::parse_data_set_reference(reference);
    if (name.kind != MmsObjectNameKind::domain_specific) {
        throw MmsDynamicDataSetError(
            "Dynamic DataSet reference must use IEC 61850 LD/LN.DataSetName form.");
    }
    if (name.item.find('$') == std::string::npos) {
        name.item = "LLN0$" + name.item;
    }
    validate_data_set_name(name);
    return MmsDataSetDirectoryCodec::to_iec_reference(name);
}

MmsDataSetDirectoryResponse MmsDynamicDataSetRuntime::verify(
    const MmsObjectName& data_set_name,
    const std::stop_token stop_token) {
    const auto invoke_id = association_.next_invoke_id();
    const MmsDataSetDirectoryRequest request{invoke_id, data_set_name};
    const auto encoded = MmsDataSetDirectoryCodec::encode_request_p_data(
        request, association_.negotiated().presentation_context_id);
    const auto exchange = association_.exchange_confirmed(
        encoded, invoke_id, stop_token);
    require_confirmed_response(exchange, "GetNamedVariableListAttributes verification");
    return MmsDataSetDirectoryCodec::decode_response(
        response_payload(exchange), invoke_id);
}

MmsDynamicDataSetCreateResult MmsDynamicDataSetRuntime::create(
    const std::string& data_set_reference,
    const std::span<const MmsObjectName> members,
    const std::stop_token stop_token) {
    if (!association_.associated()) {
        throw MmsDynamicDataSetError(
            "Cannot create a dynamic DataSet without an active MMS association.");
    }
    if (members.empty() || members.size() > options_.maximum_members) {
        throw MmsDynamicDataSetError(
            "Dynamic DataSet member count exceeds the configured operational limit.");
    }
    validate_members(members);

    const auto normalized = normalize_reference(data_set_reference);
    if (owned_data_sets_.contains(normalized)) {
        throw MmsDynamicDataSetError(
            "This runtime already owns the requested dynamic DataSet reference.");
    }
    const auto data_set_name = MmsDataSetDirectoryCodec::parse_data_set_reference(normalized);

    const auto invoke_id = association_.next_invoke_id();
    MmsDefineNamedVariableListRequest request;
    request.invoke_id = invoke_id;
    request.data_set_name = data_set_name;
    request.members.assign(members.begin(), members.end());
    const auto encoded = MmsNamedVariableListCodec::encode_define_request_p_data(
        request, association_.negotiated().presentation_context_id);
    const auto exchange = association_.exchange_confirmed(
        encoded, invoke_id, stop_token);
    require_confirmed_response(exchange, "DefineNamedVariableList");
    const auto define_response = MmsNamedVariableListCodec::decode_define_response(
        response_payload(exchange), invoke_id);

    MmsDynamicDataSetCreateResult result;
    result.data_set_reference = normalized;
    result.define_response = define_response;

    if (options_.verify_after_create) {
        try {
            const auto directory = verify(data_set_name, stop_token);
            if (directory.members.size() != members.size()) {
                throw MmsDynamicDataSetError(
                    "Dynamic DataSet verification returned a different member count.");
            }
            for (std::size_t index = 0U; index < members.size(); ++index) {
                if (directory.members[index].object_name != members[index]) {
                    throw MmsDynamicDataSetError(
                        "Dynamic DataSet verification returned different member ordering/content.");
                }
            }
            result.verified_directory = directory;
        } catch (...) {
            try {
                static_cast<void>(remove_impl(normalized, stop_token));
            } catch (...) {
                // The creation error remains primary; callers can inspect the IED
                // if best-effort rollback also failed.
            }
            throw;
        }
    }

    owned_data_sets_.insert(normalized);
    return result;
}

MmsDeleteNamedVariableListResponse MmsDynamicDataSetRuntime::remove_impl(
    const std::string& normalized_reference,
    const std::stop_token stop_token) {
    const auto data_set_name =
        MmsDataSetDirectoryCodec::parse_data_set_reference(normalized_reference);
    const auto invoke_id = association_.next_invoke_id();
    const MmsDeleteNamedVariableListRequest request{invoke_id, data_set_name};
    const auto encoded = MmsNamedVariableListCodec::encode_delete_request_p_data(
        request, association_.negotiated().presentation_context_id);
    const auto exchange = association_.exchange_confirmed(
        encoded, invoke_id, stop_token);
    require_confirmed_response(exchange, "DeleteNamedVariableList");
    return MmsNamedVariableListCodec::decode_delete_response(
        response_payload(exchange), invoke_id);
}

MmsDeleteNamedVariableListResponse MmsDynamicDataSetRuntime::remove(
    const std::string& data_set_reference,
    const MmsDynamicDataSetDeletePolicy policy,
    const std::stop_token stop_token) {
    if (!association_.associated()) {
        throw MmsDynamicDataSetError(
            "Cannot delete a dynamic DataSet without an active MMS association.");
    }
    const auto normalized = normalize_reference(data_set_reference);
    const auto owned = owned_data_sets_.contains(normalized);
    if (!owned && policy != MmsDynamicDataSetDeletePolicy::explicit_override) {
        throw MmsDynamicDataSetError(
            "Refusing to delete a DataSet not created by this runtime; use explicit_override intentionally.");
    }

    const auto response = remove_impl(normalized, stop_token);
    if (response.deleted() || response.number_matched.value_or(1U) == 0U) {
        owned_data_sets_.erase(normalized);
    }
    return response;
}

bool MmsDynamicDataSetRuntime::owns(
    const std::string& data_set_reference) const {
    try {
        return owned_data_sets_.contains(normalize_reference(data_set_reference));
    } catch (...) {
        return false;
    }
}

std::vector<std::string> MmsDynamicDataSetRuntime::owned_data_sets() const {
    return {owned_data_sets_.begin(), owned_data_sets_.end()};
}

} // namespace ar::iec61850::mms
