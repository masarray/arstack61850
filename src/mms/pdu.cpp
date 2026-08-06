// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/pdu.hpp"

#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/osi/presentation.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <type_traits>
#include <utility>

namespace ar::iec61850::mms {
namespace {

using asn1::BerClass;
using asn1::BerFormatError;
using asn1::BerReader;
using asn1::BerTlv;
using asn1::BerWriter;

void set_error(std::string* error, std::string message) noexcept {
    if (error == nullptr) {
        return;
    }
    try {
        *error = std::move(message);
    } catch (...) {
    }
}

std::vector<std::uint8_t> concat(
    const std::initializer_list<std::span<const std::uint8_t>> parts) {
    std::size_t length = 0U;
    for (const auto part : parts) {
        if (part.size() > MmsPduCodec::maximum_pdu_bytes - length) {
            throw MmsFormatError("MMS encoded length exceeds the configured limit.");
        }
        length += part.size();
    }

    std::vector<std::uint8_t> result;
    result.reserve(length);
    for (const auto part : parts) {
        result.insert(result.end(), part.begin(), part.end());
    }
    return result;
}

std::vector<std::uint8_t> encode_positive_integer_content(const std::uint32_t value) {
    auto bytes = BerWriter::encode_unsigned_integer(value);
    if (bytes.empty()) {
        bytes.push_back(0U);
    }
    if ((bytes.front() & 0x80U) != 0U) {
        bytes.insert(bytes.begin(), 0U);
    }
    return bytes;
}

std::uint32_t read_u32(const BerTlv& tlv, const char* field_name) {
    const auto value = BerReader::read_unsigned_integer(tlv);
    if (!value || *value > std::numeric_limits<std::uint32_t>::max()) {
        throw MmsFormatError(std::string{field_name} + " is not a valid unsigned 32-bit value.");
    }
    return static_cast<std::uint32_t>(*value);
}

BerTlv read_outer_exact(
    const std::span<const std::uint8_t> bytes,
    const std::uint8_t expected_tag,
    const char* name) {
    if (bytes.empty() || bytes.size() > MmsPduCodec::maximum_pdu_bytes) {
        throw MmsFormatError(std::string{name} + " length is outside the configured limit.");
    }

    std::size_t offset = 0U;
    BerTlv outer;
    if (!BerReader::try_read_tlv(bytes, offset, outer) || offset != bytes.size()) {
        throw MmsFormatError(std::string{name} + " is not one exact BER TLV.");
    }
    if (outer.encoded_tag != expected_tag || !outer.constructed) {
        throw MmsFormatError(std::string{name} + " has an unexpected top-level tag.");
    }
    return outer;
}

MmsInitiateDetail decode_detail(const BerTlv& detail_field) {
    if (detail_field.encoded_tag != 0xA4U || !detail_field.constructed) {
        throw MmsFormatError("MMS initiate detail must be context [4] constructed.");
    }

    MmsInitiateDetail detail;
    bool have_version = false;
    bool have_parameters = false;
    bool have_services = false;

    const auto children = BerReader::read_children(detail_field.value);
    if (children.size() != 3U) {
        throw MmsFormatError("MMS initiate detail must contain exactly three fields.");
    }

    for (const auto& child : children) {
        if (child.tag_class != BerClass::context_specific || child.constructed) {
            throw MmsFormatError("MMS initiate detail contains an invalid field.");
        }
        switch (child.tag_number) {
        case 0:
            if (have_version) {
                throw MmsFormatError("Duplicate MMS initiate version field.");
            }
            detail.version_number = read_u32(child, "MMS version");
            have_version = true;
            break;
        case 1:
            if (have_parameters || child.value.empty() ||
                child.value.size() > MmsPduCodec::maximum_bit_string_bytes) {
                throw MmsFormatError("Invalid MMS parameter-support bit string.");
            }
            detail.parameter_support_options = child.value;
            have_parameters = true;
            break;
        case 2:
            if (have_services || child.value.empty() ||
                child.value.size() > MmsPduCodec::maximum_bit_string_bytes) {
                throw MmsFormatError("Invalid MMS services-supported bit string.");
            }
            detail.services_supported_calling = child.value;
            have_services = true;
            break;
        default:
            throw MmsFormatError("Unexpected MMS initiate-detail field.");
        }
    }

    if (!have_version || !have_parameters || !have_services) {
        throw MmsFormatError("MMS initiate detail is incomplete.");
    }
    return detail;
}

std::vector<std::uint8_t> encode_detail(const MmsInitiateDetail& detail) {
    if (detail.parameter_support_options.empty() ||
        detail.parameter_support_options.size() > MmsPduCodec::maximum_bit_string_bytes ||
        detail.services_supported_calling.empty() ||
        detail.services_supported_calling.size() > MmsPduCodec::maximum_bit_string_bytes) {
        throw MmsFormatError("MMS initiate bit strings are outside the configured limit.");
    }

    const auto version = BerWriter::encode_tlv(
        BerClass::context_specific, false, 0,
        encode_positive_integer_content(detail.version_number));
    const auto parameters = BerWriter::encode_tlv(
        BerClass::context_specific, false, 1, detail.parameter_support_options);
    const auto services = BerWriter::encode_tlv(
        BerClass::context_specific, false, 2, detail.services_supported_calling);
    const auto body = concat({version, parameters, services});
    return BerWriter::encode_tlv(0xA4U, body);
}

template <typename Initiate>
Initiate decode_initiate_common(
    const std::span<const std::uint8_t> bytes,
    const std::uint8_t expected_tag,
    const char* name) {
    const auto outer = read_outer_exact(bytes, expected_tag, name);
    const auto children = BerReader::read_children(outer.value);
    if (children.size() != 5U) {
        throw MmsFormatError(std::string{name} + " must contain exactly five fields.");
    }

    std::array<bool, 5> seen{};
    std::array<std::uint32_t, 4> values{};
    std::optional<MmsInitiateDetail> detail;
    for (const auto& child : children) {
        if (child.tag_class != BerClass::context_specific) {
            throw MmsFormatError(std::string{name} + " contains a non-context field.");
        }
        if (child.tag_number >= 0 && child.tag_number <= 3 && !child.constructed) {
            const auto index = static_cast<std::size_t>(child.tag_number);
            if (seen[index]) {
                throw MmsFormatError(std::string{name} + " contains a duplicate numeric field.");
            }
            values[index] = read_u32(child, "MMS initiate numeric field");
            seen[index] = true;
            continue;
        }
        if (child.tag_number == 4 && child.constructed) {
            if (seen[4]) {
                throw MmsFormatError(std::string{name} + " contains duplicate detail.");
            }
            detail = decode_detail(child);
            seen[4] = true;
            continue;
        }
        throw MmsFormatError(std::string{name} + " contains an unexpected field.");
    }

    if (!std::all_of(seen.begin(), seen.end(), [](const bool value) { return value; })) {
        throw MmsFormatError(std::string{name} + " is missing a mandatory field.");
    }
    if (values[0] < 64U || values[0] > MmsPduCodec::maximum_pdu_bytes ||
        values[1] == 0U || values[2] == 0U || values[3] == 0U) {
        throw MmsFormatError(std::string{name} + " negotiated limits are invalid.");
    }

    Initiate result;
    if constexpr (std::is_same_v<Initiate, MmsInitiateRequest>) {
        result.proposed_maximum_mms_pdu_size = values[0];
        result.proposed_maximum_outstanding_calling = values[1];
        result.proposed_maximum_outstanding_called = values[2];
        result.proposed_data_structure_nesting_level = values[3];
    } else {
        result.negotiated_maximum_mms_pdu_size = values[0];
        result.negotiated_maximum_outstanding_calling = values[1];
        result.negotiated_maximum_outstanding_called = values[2];
        result.negotiated_data_structure_nesting_level = values[3];
    }
    result.detail = *detail;
    return result;
}

template <typename Initiate>
std::vector<std::uint8_t> encode_initiate_common(
    const Initiate& initiate,
    const std::uint8_t tag) {
    std::array<std::uint32_t, 4> values{};
    if constexpr (std::is_same_v<Initiate, MmsInitiateRequest>) {
        values = {initiate.proposed_maximum_mms_pdu_size,
                  initiate.proposed_maximum_outstanding_calling,
                  initiate.proposed_maximum_outstanding_called,
                  initiate.proposed_data_structure_nesting_level};
    } else {
        values = {initiate.negotiated_maximum_mms_pdu_size,
                  initiate.negotiated_maximum_outstanding_calling,
                  initiate.negotiated_maximum_outstanding_called,
                  initiate.negotiated_data_structure_nesting_level};
    }

    if (values[0] < 64U || values[0] > MmsPduCodec::maximum_pdu_bytes ||
        values[1] == 0U || values[2] == 0U || values[3] == 0U) {
        throw MmsFormatError("MMS initiate negotiated limits are invalid.");
    }

    std::array<std::vector<std::uint8_t>, 4> fields;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        fields[index] = BerWriter::encode_tlv(
            BerClass::context_specific, false, static_cast<std::int32_t>(index),
            encode_positive_integer_content(values[index]));
    }
    const auto detail = encode_detail(initiate.detail);
    const auto body = concat({fields[0], fields[1], fields[2], fields[3], detail});
    return BerWriter::encode_tlv(tag, body);
}

std::uint32_t decode_invoke_tlv(const BerTlv& tlv) {
    const bool universal_integer =
        tlv.tag_class == BerClass::universal && tlv.tag_number == 2 && !tlv.constructed;
    const bool implicit_invoke =
        tlv.tag_class == BerClass::context_specific && tlv.tag_number == 0 && !tlv.constructed;
    if (!universal_integer && !implicit_invoke) {
        throw MmsFormatError("MMS PDU does not begin with a valid invoke ID.");
    }
    const auto value = read_u32(tlv, "MMS invoke ID");
    if (value > MmsPduCodec::maximum_invoke_id) {
        throw MmsFormatError("MMS invoke ID exceeds the supported range.");
    }
    return value;
}

std::vector<std::uint8_t> encode_confirmed_common(
    const std::uint8_t outer_tag,
    const std::uint32_t invoke_id,
    const std::int32_t service_tag,
    const bool service_constructed,
    const std::span<const std::uint8_t> service_value) {
    if (invoke_id > MmsPduCodec::maximum_invoke_id) {
        throw MmsFormatError("MMS invoke ID exceeds the supported range.");
    }
    if (service_tag < 0 || service_tag > 1'000'000) {
        throw MmsFormatError("MMS confirmed service tag is invalid.");
    }
    if (service_value.size() > MmsPduCodec::maximum_pdu_bytes) {
        throw MmsFormatError("MMS confirmed service payload is too large.");
    }

    const auto invoke = BerWriter::encode_tlv(
        0x02U, encode_positive_integer_content(invoke_id));
    const auto service = BerWriter::encode_tlv(
        BerClass::context_specific, service_constructed, service_tag, service_value);
    const auto body = concat({invoke, service});
    return BerWriter::encode_tlv(outer_tag, body);
}

template <typename Confirmed>
Confirmed decode_confirmed_common(
    const std::span<const std::uint8_t> bytes,
    const std::uint8_t expected_tag,
    const char* name) {
    const auto outer = read_outer_exact(bytes, expected_tag, name);
    const auto children = BerReader::read_children(outer.value);
    if (children.size() < 2U || children.size() > 4U) {
        throw MmsFormatError(std::string{name} + " must contain invoke ID and one service.");
    }

    Confirmed result;
    result.invoke_id = decode_invoke_tlv(children.front());
    const auto& service = children.back();
    if (service.tag_class != BerClass::context_specific || service.tag_number < 0) {
        throw MmsFormatError(std::string{name} + " service is not context-specific.");
    }
    result.service_tag = service.tag_number;
    result.service_constructed = service.constructed;
    result.service_value = service.value;
    return result;
}

MmsPduKind kind_for_tag(const std::uint8_t tag) noexcept {
    switch (tag) {
    case 0xA0U: return MmsPduKind::confirmed_request;
    case 0xA1U: return MmsPduKind::confirmed_response;
    case 0xA2U: return MmsPduKind::confirmed_error;
    case 0xA3U: return MmsPduKind::unconfirmed;
    case 0xA4U: return MmsPduKind::reject;
    case 0xA5U: return MmsPduKind::cancel_request;
    case 0xA6U: return MmsPduKind::cancel_response;
    case 0xA7U: return MmsPduKind::cancel_error;
    case 0xA8U: return MmsPduKind::initiate_request;
    case 0xA9U: return MmsPduKind::initiate_response;
    case 0xAAU: return MmsPduKind::initiate_error;
    case 0x8BU: return MmsPduKind::conclude_request;
    case 0x8CU: return MmsPduKind::conclude_response;
    case 0xADU: return MmsPduKind::conclude_error;
    default: return MmsPduKind::unknown;
    }
}

std::optional<std::uint32_t> try_read_invoke_from_children(
    const std::vector<BerTlv>& children) noexcept {
    const auto count = std::min<std::size_t>(children.size(), 2U);
    for (std::size_t index = 0; index < count; ++index) {
        try {
            const auto& child = children[index];
            const bool candidate =
                (child.tag_class == BerClass::universal && child.tag_number == 2 && !child.constructed) ||
                (child.tag_class == BerClass::context_specific && child.tag_number == 0 && !child.constructed);
            if (candidate) {
                return decode_invoke_tlv(child);
            }
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

} // namespace

MmsConfirmedService MmsConfirmedRequest::service() const noexcept {
    switch (service_tag) {
    case 1: return MmsConfirmedService::get_name_list;
    case 2: return MmsConfirmedService::identify;
    case 4: return MmsConfirmedService::read;
    case 5: return MmsConfirmedService::write;
    case 6: return MmsConfirmedService::get_variable_access_attributes;
    case 12: return MmsConfirmedService::get_named_variable_list_attributes;
    default: return MmsConfirmedService::unknown;
    }
}

MmsConfirmedService MmsConfirmedResponse::service() const noexcept {
    switch (service_tag) {
    case 1: return MmsConfirmedService::get_name_list;
    case 2: return MmsConfirmedService::identify;
    case 4: return MmsConfirmedService::read;
    case 5: return MmsConfirmedService::write;
    case 6: return MmsConfirmedService::get_variable_access_attributes;
    case 12: return MmsConfirmedService::get_named_variable_list_attributes;
    default: return MmsConfirmedService::unknown;
    }
}

bool MmsPduEnvelope::confirmed_result() const noexcept {
    return kind == MmsPduKind::confirmed_response ||
           kind == MmsPduKind::confirmed_error ||
           kind == MmsPduKind::reject;
}

bool MmsPduEnvelope::matches_invoke(const std::uint32_t expected) const noexcept {
    return invoke_id.has_value() && *invoke_id == expected;
}

MmsInitiateRequest MmsPduCodec::default_initiate_request() {
    return {};
}

MmsInitiateResponse MmsPduCodec::default_initiate_response() {
    return {};
}

std::vector<std::uint8_t> MmsPduCodec::encode_initiate_request(
    const MmsInitiateRequest& request) {
    return encode_initiate_common(request, 0xA8U);
}

MmsInitiateRequest MmsPduCodec::decode_initiate_request(
    const std::span<const std::uint8_t> bytes) {
    return decode_initiate_common<MmsInitiateRequest>(bytes, 0xA8U, "MMS Initiate-Request");
}

bool MmsPduCodec::try_decode_initiate_request(
    const std::span<const std::uint8_t> bytes,
    MmsInitiateRequest& request,
    std::string* error) noexcept {
    try {
        request = decode_initiate_request(bytes);
        set_error(error, {});
        return true;
    } catch (const std::exception& exception) {
        set_error(error, exception.what());
        return false;
    }
}

std::vector<std::uint8_t> MmsPduCodec::encode_initiate_response(
    const MmsInitiateResponse& response) {
    return encode_initiate_common(response, 0xA9U);
}

MmsInitiateResponse MmsPduCodec::decode_initiate_response(
    const std::span<const std::uint8_t> bytes) {
    return decode_initiate_common<MmsInitiateResponse>(bytes, 0xA9U, "MMS Initiate-Response");
}

bool MmsPduCodec::try_decode_initiate_response(
    const std::span<const std::uint8_t> bytes,
    MmsInitiateResponse& response,
    std::string* error) noexcept {
    try {
        response = decode_initiate_response(bytes);
        set_error(error, {});
        return true;
    } catch (const std::exception& exception) {
        set_error(error, exception.what());
        return false;
    }
}

std::vector<std::uint8_t> MmsPduCodec::encode_confirmed_request(
    const MmsConfirmedRequest& request) {
    return encode_confirmed_common(
        0xA0U, request.invoke_id, request.service_tag,
        request.service_constructed, request.service_value);
}

MmsConfirmedRequest MmsPduCodec::decode_confirmed_request(
    const std::span<const std::uint8_t> bytes) {
    return decode_confirmed_common<MmsConfirmedRequest>(
        bytes, 0xA0U, "MMS Confirmed-Request");
}

std::vector<std::uint8_t> MmsPduCodec::encode_confirmed_response(
    const MmsConfirmedResponse& response) {
    return encode_confirmed_common(
        0xA1U, response.invoke_id, response.service_tag,
        response.service_constructed, response.service_value);
}

MmsConfirmedResponse MmsPduCodec::decode_confirmed_response(
    const std::span<const std::uint8_t> bytes) {
    return decode_confirmed_common<MmsConfirmedResponse>(
        bytes, 0xA1U, "MMS Confirmed-Response");
}

std::vector<std::uint8_t> MmsPduCodec::encode_confirmed_error(
    const MmsConfirmedError& error) {
    if (error.invoke_id > maximum_invoke_id || error.error_class_tag < 0 ||
        error.error_class_tag > 1'000'000) {
        throw MmsFormatError("MMS Confirmed-Error fields are invalid.");
    }

    const auto invoke = BerWriter::encode_tlv(
        BerClass::context_specific, false, 0,
        encode_positive_integer_content(error.invoke_id));
    const auto class_choice = BerWriter::encode_tlv(
        BerClass::context_specific, false, error.error_class_tag,
        encode_positive_integer_content(error.error_value));
    const auto error_class = BerWriter::encode_tlv(0xA0U, class_choice);
    const auto service_error = BerWriter::encode_tlv(0xA2U, error_class);
    const auto body = concat({invoke, service_error});
    return BerWriter::encode_tlv(0xA2U, body);
}

MmsConfirmedError MmsPduCodec::decode_confirmed_error(
    const std::span<const std::uint8_t> bytes) {
    const auto outer = read_outer_exact(bytes, 0xA2U, "MMS Confirmed-Error");
    const auto children = BerReader::read_children(outer.value);
    if (children.size() != 2U) {
        throw MmsFormatError("MMS Confirmed-Error must contain invoke ID and serviceError.");
    }

    MmsConfirmedError result;
    result.invoke_id = decode_invoke_tlv(children[0]);
    const auto& service_error = children[1];
    if (service_error.encoded_tag != 0xA2U || !service_error.constructed) {
        throw MmsFormatError("MMS Confirmed-Error serviceError field is invalid.");
    }
    const auto service_children = BerReader::read_children(service_error.value);
    if (service_children.size() != 1U || service_children[0].encoded_tag != 0xA0U ||
        !service_children[0].constructed) {
        throw MmsFormatError("MMS Confirmed-Error errorClass wrapper is invalid.");
    }
    const auto class_children = BerReader::read_children(service_children[0].value);
    if (class_children.size() != 1U ||
        class_children[0].tag_class != BerClass::context_specific ||
        class_children[0].constructed) {
        throw MmsFormatError("MMS Confirmed-Error class choice is invalid.");
    }
    result.error_class_tag = class_children[0].tag_number;
    result.error_value = read_u32(class_children[0], "MMS service error value");
    return result;
}

std::vector<std::uint8_t> MmsPduCodec::extract_mms_payload(
    const std::span<const std::uint8_t> presentation_or_mms_payload) {
    if (presentation_or_mms_payload.empty() ||
        presentation_or_mms_payload.size() > maximum_pdu_bytes) {
        throw MmsFormatError("MMS/presentation payload length is outside the configured limit.");
    }

    const auto kind = kind_for_tag(presentation_or_mms_payload.front());
    if (kind != MmsPduKind::unknown) {
        return {presentation_or_mms_payload.begin(), presentation_or_mms_payload.end()};
    }

    osi::PresentationPdv pdv;
    std::string error;
    if (!osi::PresentationCodec::try_decode_p_data(
            presentation_or_mms_payload, pdv, &error)) {
        throw MmsFormatError("MMS presentation P-DATA decode failed: " + error);
    }
    if (pdv.single_asn1_type.empty() || pdv.single_asn1_type.size() > maximum_pdu_bytes) {
        throw MmsFormatError("MMS presentation PDV payload is empty or too large.");
    }
    return pdv.single_asn1_type;
}

std::vector<std::uint8_t> MmsPduCodec::wrap_p_data(
    const std::span<const std::uint8_t> mms_payload,
    const std::uint32_t presentation_context_id) {
    if (mms_payload.empty() || mms_payload.size() > maximum_pdu_bytes) {
        throw MmsFormatError("MMS payload is empty or too large.");
    }
    return osi::PresentationCodec::encode_p_data(
        mms_payload, presentation_context_id, true);
}

MmsPduEnvelope MmsPduCodec::decode_envelope(
    const std::span<const std::uint8_t> presentation_or_mms_payload) {
    const auto mms = extract_mms_payload(presentation_or_mms_payload);
    std::size_t offset = 0U;
    BerTlv outer;
    if (!BerReader::try_read_tlv(mms, offset, outer) || offset != mms.size()) {
        throw MmsFormatError("MMS envelope is not one exact BER TLV.");
    }

    MmsPduEnvelope envelope;
    envelope.kind = kind_for_tag(outer.encoded_tag);
    envelope.mms_payload = mms;
    if (envelope.kind == MmsPduKind::unknown) {
        throw MmsFormatError("Unknown MMS top-level PDU tag.");
    }

    if (!outer.constructed) {
        return envelope;
    }
    const auto children = BerReader::read_children(outer.value);
    switch (envelope.kind) {
    case MmsPduKind::confirmed_request: {
        const auto decoded = decode_confirmed_request(mms);
        envelope.invoke_id = decoded.invoke_id;
        envelope.service_tag = decoded.service_tag;
        break;
    }
    case MmsPduKind::confirmed_response: {
        const auto decoded = decode_confirmed_response(mms);
        envelope.invoke_id = decoded.invoke_id;
        envelope.service_tag = decoded.service_tag;
        break;
    }
    case MmsPduKind::confirmed_error: {
        const auto decoded = decode_confirmed_error(mms);
        envelope.invoke_id = decoded.invoke_id;
        break;
    }
    case MmsPduKind::reject:
    case MmsPduKind::cancel_request:
    case MmsPduKind::cancel_response:
    case MmsPduKind::cancel_error:
        envelope.invoke_id = try_read_invoke_from_children(children);
        break;
    case MmsPduKind::unconfirmed:
        envelope.information_report = std::any_of(
            children.begin(), children.end(), [](const BerTlv& child) {
                return child.tag_class == BerClass::context_specific &&
                       child.tag_number == 0 && child.constructed;
            });
        break;
    default:
        break;
    }
    return envelope;
}

bool MmsPduCodec::try_decode_envelope(
    const std::span<const std::uint8_t> presentation_or_mms_payload,
    MmsPduEnvelope& envelope,
    std::string* error) noexcept {
    try {
        envelope = decode_envelope(presentation_or_mms_payload);
        set_error(error, {});
        return true;
    } catch (const std::exception& exception) {
        set_error(error, exception.what());
        return false;
    }
}

} // namespace ar::iec61850::mms
