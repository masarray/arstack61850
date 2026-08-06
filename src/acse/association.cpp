// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/acse/association.hpp"

#include "ariec61850/asn1/ber.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace ar::iec61850::acse {
namespace {

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
        if (part.size() > std::numeric_limits<std::size_t>::max() - length) {
            throw std::length_error("ACSE encoding size overflow.");
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

void validate_oid(
    const std::span<const std::uint8_t> value,
    const char* description,
    const bool allow_empty = false) {
    if ((!allow_empty && value.empty()) ||
        value.size() > AcseAssociationCodec::maximum_oid_bytes) {
        throw AcseFormatError(std::string{description} + " has an invalid encoded length.");
    }
}

std::vector<std::uint8_t> encode_integer(const std::uint32_t value) {
    return BerWriter::encode_tlv(
        0x02U, BerWriter::encode_unsigned_integer(value));
}

std::uint32_t read_integer(const BerTlv& tlv, const char* description) {
    const auto value = BerReader::read_unsigned_integer(tlv);
    if (!value || *value > std::numeric_limits<std::uint32_t>::max()) {
        throw AcseFormatError(std::string{description} + " is outside the supported range.");
    }
    return static_cast<std::uint32_t>(*value);
}

BerTlv read_single_tlv(
    const std::span<const std::uint8_t> bytes,
    const std::uint8_t expected_tag,
    const char* description) {
    std::size_t offset = 0U;
    BerTlv tlv;
    if (!BerReader::try_read_tlv(bytes, offset, tlv) ||
        offset != bytes.size() || tlv.encoded_tag != expected_tag) {
        throw AcseFormatError(std::string{"Invalid "} + description + ".");
    }
    return tlv;
}

std::vector<std::uint8_t> encode_explicit_oid(
    const std::uint8_t tag,
    const std::span<const std::uint8_t> oid,
    const bool allow_empty = false) {
    validate_oid(oid, "ACSE OBJECT IDENTIFIER", allow_empty);
    const auto object_identifier = BerWriter::encode_tlv(0x06U, oid);
    return BerWriter::encode_tlv(tag, object_identifier);
}

std::vector<std::uint8_t> decode_explicit_oid(
    const BerTlv& field,
    const char* description,
    const bool allow_empty = false) {
    const auto children = BerReader::read_children(field.value);
    if (children.size() != 1U || children.front().encoded_tag != 0x06U) {
        throw AcseFormatError(std::string{description} + " is malformed.");
    }
    validate_oid(children.front().value, description, allow_empty);
    return children.front().value;
}

std::uint32_t decode_explicit_integer(
    const BerTlv& field,
    const char* description) {
    const auto children = BerReader::read_children(field.value);
    if (children.size() != 1U || children.front().encoded_tag != 0x02U) {
        throw AcseFormatError(std::string{description} + " is malformed.");
    }
    return read_integer(children.front(), description);
}

std::vector<std::uint8_t> encode_external(const AcseExternal& external) {
    validate_oid(external.direct_reference, "ACSE EXTERNAL direct-reference");
    if (external.single_asn1_type.size() > AcseAssociationCodec::maximum_acse_bytes) {
        throw std::length_error("ACSE EXTERNAL single-ASN.1 payload exceeds the bounded limit.");
    }
    const auto direct_reference = BerWriter::encode_tlv(
        0x06U, external.direct_reference);
    const auto indirect_reference = encode_integer(external.indirect_reference);
    const auto encoding = BerWriter::encode_tlv(
        0xA0U, external.single_asn1_type);
    return BerWriter::encode_tlv(
        0x28U, concat({direct_reference, indirect_reference, encoding}));
}

AcseExternal decode_external(const BerTlv& user_information) {
    const auto user_info_children = BerReader::read_children(user_information.value);
    if (user_info_children.size() != 1U || user_info_children.front().encoded_tag != 0x28U) {
        throw AcseFormatError("ACSE user-information must contain one EXTERNAL value.");
    }

    AcseExternal external;
    bool saw_direct = false;
    bool saw_indirect = false;
    bool saw_encoding = false;
    for (const auto& field : BerReader::read_children(user_info_children.front().value)) {
        if (field.encoded_tag == 0x06U) {
            if (saw_direct) {
                throw AcseFormatError("ACSE EXTERNAL contains duplicate direct-references.");
            }
            validate_oid(field.value, "ACSE EXTERNAL direct-reference");
            external.direct_reference = field.value;
            saw_direct = true;
        } else if (field.encoded_tag == 0x02U) {
            if (saw_indirect) {
                throw AcseFormatError("ACSE EXTERNAL contains duplicate indirect-references.");
            }
            external.indirect_reference = read_integer(
                field, "ACSE EXTERNAL indirect-reference");
            saw_indirect = true;
        } else if (field.encoded_tag == 0xA0U) {
            if (saw_encoding) {
                throw AcseFormatError("ACSE EXTERNAL contains duplicate encodings.");
            }
            external.single_asn1_type = field.value;
            saw_encoding = true;
        } else {
            throw AcseFormatError("ACSE EXTERNAL contains an unsupported field.");
        }
    }
    if (!saw_direct || !saw_indirect || !saw_encoding) {
        throw AcseFormatError("ACSE EXTERNAL is incomplete.");
    }
    return external;
}

std::vector<std::uint8_t> encode_user_information(const AcseExternal& external) {
    const auto encoded = encode_external(external);
    return BerWriter::encode_tlv(0xBEU, encoded);
}

} // namespace

const std::vector<std::uint8_t>& AcseAssociationCodec::mms_application_context_name() {
    static const std::vector<std::uint8_t> value{0x28U, 0xCAU, 0x22U, 0x02U, 0x03U};
    return value;
}

const std::vector<std::uint8_t>& AcseAssociationCodec::balanced_called_ap_title() {
    static const std::vector<std::uint8_t> value{0x29U, 0x01U, 0x87U, 0x67U, 0x01U};
    return value;
}

const std::vector<std::uint8_t>& AcseAssociationCodec::balanced_calling_ap_title() {
    static const std::vector<std::uint8_t> value{0x29U, 0x01U, 0x87U, 0x67U};
    return value;
}

std::vector<std::uint8_t> AcseAssociationCodec::default_mms_initiate_request() {
    const std::array<std::uint8_t, 3> local_detail{0x00U, 0xFDU, 0xE8U};
    const std::array<std::uint8_t, 1> ten{0x0AU};
    const std::array<std::uint8_t, 1> five{0x05U};
    const std::array<std::uint8_t, 1> version{0x01U};
    const std::array<std::uint8_t, 3> parameter_support{0x05U, 0xF1U, 0x00U};
    const std::array<std::uint8_t, 12> services{
        0x03U, 0xEEU, 0x1CU, 0x00U, 0x00U, 0x04U,
        0x08U, 0x00U, 0x00U, 0x79U, 0xEFU, 0x18U};

    const auto detail = concat({
        asn1::BerWriter::encode_tlv(0x80U, version),
        asn1::BerWriter::encode_tlv(0x81U, parameter_support),
        asn1::BerWriter::encode_tlv(0x82U, services)});
    return asn1::BerWriter::encode_tlv(0xA8U, concat({
        asn1::BerWriter::encode_tlv(0x80U, local_detail),
        asn1::BerWriter::encode_tlv(0x81U, ten),
        asn1::BerWriter::encode_tlv(0x82U, ten),
        asn1::BerWriter::encode_tlv(0x83U, five),
        asn1::BerWriter::encode_tlv(0xA4U, detail)}));
}

std::vector<std::uint8_t> AcseAssociationCodec::default_mms_initiate_response() {
    auto response = default_mms_initiate_request();
    if (response.empty() || response.front() != 0xA8U) {
        throw AcseFormatError("Internal MMS Initiate profile is invalid.");
    }
    response.front() = 0xA9U;
    return response;
}

AcseAarq AcseAssociationCodec::default_balanced_aarq() {
    AcseAarq aarq;
    aarq.application_context_name = mms_application_context_name();
    aarq.called_ap_title = balanced_called_ap_title();
    aarq.called_ae_qualifier = 12U;
    aarq.calling_ap_title = balanced_calling_ap_title();
    aarq.calling_ae_qualifier = 12U;
    aarq.user_information = AcseExternal{
        osi::PresentationCodec::ber_transfer_syntax_name(),
        3U,
        default_mms_initiate_request()};
    return aarq;
}

AcseAare AcseAssociationCodec::default_accept_aare() {
    AcseAare aare;
    aare.application_context_name = mms_application_context_name();
    aare.result = 0U;
    aare.result_source_diagnostic = 0U;
    aare.user_information = AcseExternal{
        osi::PresentationCodec::ber_transfer_syntax_name(),
        3U,
        default_mms_initiate_response()};
    return aare;
}

std::vector<std::uint8_t> AcseAssociationCodec::encode_aarq(const AcseAarq& aarq) {
    validate_oid(aarq.application_context_name, "ACSE application-context-name");
    std::vector<std::uint8_t> body;
    const auto application = encode_explicit_oid(0xA1U, aarq.application_context_name);
    body.insert(body.end(), application.begin(), application.end());

    if (!aarq.called_ap_title.empty()) {
        const auto value = encode_explicit_oid(0xA2U, aarq.called_ap_title, true);
        body.insert(body.end(), value.begin(), value.end());
    }
    if (aarq.called_ae_qualifier) {
        const auto integer = encode_integer(*aarq.called_ae_qualifier);
        const auto value = BerWriter::encode_tlv(0xA3U, integer);
        body.insert(body.end(), value.begin(), value.end());
    }
    if (!aarq.calling_ap_title.empty()) {
        const auto value = encode_explicit_oid(0xA6U, aarq.calling_ap_title, true);
        body.insert(body.end(), value.begin(), value.end());
    }
    if (aarq.calling_ae_qualifier) {
        const auto integer = encode_integer(*aarq.calling_ae_qualifier);
        const auto value = BerWriter::encode_tlv(0xA7U, integer);
        body.insert(body.end(), value.begin(), value.end());
    }
    if (aarq.user_information) {
        const auto value = encode_user_information(*aarq.user_information);
        body.insert(body.end(), value.begin(), value.end());
    }
    if (body.size() > maximum_acse_bytes) {
        throw std::length_error("ACSE AARQ exceeds the bounded limit.");
    }
    return BerWriter::encode_tlv(0x60U, body);
}

bool AcseAssociationCodec::try_decode_aarq(
    const std::span<const std::uint8_t> bytes,
    AcseAarq& aarq,
    std::string* error) noexcept {
    try {
        aarq = {};
        if (bytes.empty() || bytes.size() > maximum_acse_bytes) {
            set_error(error, "ACSE AARQ size is outside the supported range.");
            return false;
        }
        const auto outer = read_single_tlv(bytes, 0x60U, "ACSE AARQ");
        bool saw_application = false;
        bool saw_called_ap = false;
        bool saw_called_ae = false;
        bool saw_calling_ap = false;
        bool saw_calling_ae = false;
        bool saw_user_information = false;
        for (const auto& field : BerReader::read_children(outer.value)) {
            switch (field.encoded_tag) {
            case 0xA1U:
                if (saw_application) {
                    throw AcseFormatError("ACSE AARQ contains duplicate application contexts.");
                }
                aarq.application_context_name = decode_explicit_oid(
                    field, "ACSE application-context-name");
                saw_application = true;
                break;
            case 0xA2U:
                if (saw_called_ap) {
                    throw AcseFormatError("ACSE AARQ contains duplicate called AP-titles.");
                }
                aarq.called_ap_title = decode_explicit_oid(
                    field, "ACSE called AP-title", true);
                saw_called_ap = true;
                break;
            case 0xA3U:
                if (saw_called_ae) {
                    throw AcseFormatError("ACSE AARQ contains duplicate called AE-qualifiers.");
                }
                aarq.called_ae_qualifier = decode_explicit_integer(
                    field, "ACSE called AE-qualifier");
                saw_called_ae = true;
                break;
            case 0xA6U:
                if (saw_calling_ap) {
                    throw AcseFormatError("ACSE AARQ contains duplicate calling AP-titles.");
                }
                aarq.calling_ap_title = decode_explicit_oid(
                    field, "ACSE calling AP-title", true);
                saw_calling_ap = true;
                break;
            case 0xA7U:
                if (saw_calling_ae) {
                    throw AcseFormatError("ACSE AARQ contains duplicate calling AE-qualifiers.");
                }
                aarq.calling_ae_qualifier = decode_explicit_integer(
                    field, "ACSE calling AE-qualifier");
                saw_calling_ae = true;
                break;
            case 0xBEU:
                if (saw_user_information) {
                    throw AcseFormatError("ACSE AARQ contains duplicate user-information fields.");
                }
                aarq.user_information = decode_external(field);
                saw_user_information = true;
                break;
            default:
                throw AcseFormatError("ACSE AARQ contains an unsupported field.");
            }
        }
        if (!saw_application || !saw_user_information) {
            throw AcseFormatError("ACSE AARQ is missing required fields.");
        }
        if (error != nullptr) {
            error->clear();
        }
        return true;
    } catch (const std::exception& exception) {
        aarq = {};
        set_error(error, exception.what());
        return false;
    } catch (...) {
        aarq = {};
        set_error(error, "ACSE AARQ decode failed unexpectedly.");
        return false;
    }
}

AcseAarq AcseAssociationCodec::decode_aarq(const std::span<const std::uint8_t> bytes) {
    AcseAarq aarq;
    std::string error;
    if (!try_decode_aarq(bytes, aarq, &error)) {
        throw AcseFormatError(error);
    }
    return aarq;
}

std::vector<std::uint8_t> AcseAssociationCodec::encode_aare(const AcseAare& aare) {
    validate_oid(aare.application_context_name, "ACSE application-context-name");
    const auto application = encode_explicit_oid(0xA1U, aare.application_context_name);
    const auto result = BerWriter::encode_tlv(0xA2U, encode_integer(aare.result));
    const auto diagnostic_value = BerWriter::encode_tlv(
        0xA1U, encode_integer(aare.result_source_diagnostic));
    const auto diagnostic = BerWriter::encode_tlv(0xA3U, diagnostic_value);

    std::vector<std::uint8_t> body = concat({application, result, diagnostic});
    if (aare.user_information) {
        const auto user_info = encode_user_information(*aare.user_information);
        body.insert(body.end(), user_info.begin(), user_info.end());
    }
    if (body.size() > maximum_acse_bytes) {
        throw std::length_error("ACSE AARE exceeds the bounded limit.");
    }
    return BerWriter::encode_tlv(0x61U, body);
}

bool AcseAssociationCodec::try_decode_aare(
    const std::span<const std::uint8_t> bytes,
    AcseAare& aare,
    std::string* error) noexcept {
    try {
        aare = {};
        if (bytes.empty() || bytes.size() > maximum_acse_bytes) {
            set_error(error, "ACSE AARE size is outside the supported range.");
            return false;
        }
        const auto outer = read_single_tlv(bytes, 0x61U, "ACSE AARE");
        bool saw_application = false;
        bool saw_result = false;
        bool saw_diagnostic = false;
        bool saw_user_information = false;
        for (const auto& field : BerReader::read_children(outer.value)) {
            if (field.encoded_tag == 0xA1U) {
                if (saw_application) {
                    throw AcseFormatError("ACSE AARE contains duplicate application contexts.");
                }
                aare.application_context_name = decode_explicit_oid(
                    field, "ACSE application-context-name");
                saw_application = true;
            } else if (field.encoded_tag == 0xA2U) {
                if (saw_result) {
                    throw AcseFormatError("ACSE AARE contains duplicate result fields.");
                }
                aare.result = decode_explicit_integer(field, "ACSE association result");
                saw_result = true;
            } else if (field.encoded_tag == 0xA3U) {
                if (saw_diagnostic) {
                    throw AcseFormatError("ACSE AARE contains duplicate diagnostics.");
                }
                const auto sources = BerReader::read_children(field.value);
                if (sources.size() != 1U || sources.front().encoded_tag != 0xA1U) {
                    throw AcseFormatError("ACSE AARE result-source-diagnostic is malformed.");
                }
                aare.result_source_diagnostic = decode_explicit_integer(
                    sources.front(), "ACSE result-source-diagnostic");
                saw_diagnostic = true;
            } else if (field.encoded_tag == 0xBEU) {
                if (saw_user_information) {
                    throw AcseFormatError("ACSE AARE contains duplicate user-information fields.");
                }
                aare.user_information = decode_external(field);
                saw_user_information = true;
            } else {
                throw AcseFormatError("ACSE AARE contains an unsupported field.");
            }
        }
        if (!saw_application || !saw_result || !saw_diagnostic) {
            throw AcseFormatError("ACSE AARE is missing required fields.");
        }
        if (error != nullptr) {
            error->clear();
        }
        return true;
    } catch (const std::exception& exception) {
        aare = {};
        set_error(error, exception.what());
        return false;
    } catch (...) {
        aare = {};
        set_error(error, "ACSE AARE decode failed unexpectedly.");
        return false;
    }
}

AcseAare AcseAssociationCodec::decode_aare(const std::span<const std::uint8_t> bytes) {
    AcseAare aare;
    std::string error;
    if (!try_decode_aare(bytes, aare, &error)) {
        throw AcseFormatError(error);
    }
    return aare;
}

std::vector<std::uint8_t> AcseAssociationCodec::build_default_association_request() {
    const auto aarq = encode_aarq(default_balanced_aarq());
    const auto contexts = osi::PresentationCodec::default_contexts();
    const auto cp = osi::PresentationCodec::encode_cp(contexts, 1U, aarq);
    return osi::SessionCodec::encode_connect(cp);
}

bool AcseAssociationCodec::try_decode_association_request(
    const std::span<const std::uint8_t> bytes,
    AssociationRequestEnvelope& request,
    std::string* error) noexcept {
    try {
        request = {};
        if (!osi::SessionCodec::try_decode(bytes, request.session, error)) {
            return false;
        }
        if (request.session.kind != osi::SessionSpduKind::connect) {
            set_error(error, "Association request must use an ISO Session Connect SPDU.");
            request = {};
            return false;
        }
        if (!osi::PresentationCodec::try_decode_cp(
                request.session.user_data, request.presentation, error)) {
            request = {};
            return false;
        }
        if (!try_decode_aarq(
                request.presentation.user_data.single_asn1_type, request.aarq, error)) {
            request = {};
            return false;
        }

        request.acse_presentation_context_id =
            request.presentation.context_id_for_abstract_syntax(
                osi::PresentationCodec::acse_abstract_syntax_name());
        request.mms_presentation_context_id =
            request.presentation.context_id_for_abstract_syntax(
                osi::PresentationCodec::mms_abstract_syntax_name());
        if (request.acse_presentation_context_id == 0U ||
            request.acse_presentation_context_id != request.presentation.user_data.context_id) {
            set_error(error, "Association request uses an undefined or mismatched ACSE context.");
            request = {};
            return false;
        }
        if (request.mms_presentation_context_id == 0U) {
            set_error(error, "Association request does not define an MMS presentation context.");
            request = {};
            return false;
        }
        if (error != nullptr) {
            error->clear();
        }
        return true;
    } catch (const std::exception& exception) {
        request = {};
        set_error(error, exception.what());
        return false;
    } catch (...) {
        request = {};
        set_error(error, "Association request decode failed unexpectedly.");
        return false;
    }
}

AssociationRequestEnvelope AcseAssociationCodec::decode_association_request(
    const std::span<const std::uint8_t> bytes) {
    AssociationRequestEnvelope request;
    std::string error;
    if (!try_decode_association_request(bytes, request, &error)) {
        throw AcseFormatError(error);
    }
    return request;
}

AssociationResponseProfile AcseAssociationCodec::build_accept_response(
    const AssociationRequestEnvelope& request,
    const AcseAare& aare) {
    if (request.session.kind != osi::SessionSpduKind::connect ||
        request.acse_presentation_context_id == 0U ||
        request.mms_presentation_context_id == 0U) {
        throw std::invalid_argument("Association response requires a valid decoded request.");
    }
    const auto encoded_aare = encode_aare(aare);
    const auto cpa = osi::PresentationCodec::encode_cpa_accepting(
        request.presentation, encoded_aare);
    AssociationResponseProfile profile;
    profile.payload = osi::SessionCodec::encode_accept_mirroring(
        request.session, cpa);
    profile.mms_presentation_context_id = request.mms_presentation_context_id;
    return profile;
}

AssociationResponseProfile AcseAssociationCodec::build_accept_response(
    const std::span<const std::uint8_t> encoded_request,
    const AcseAare& aare) {
    return build_accept_response(decode_association_request(encoded_request), aare);
}

bool AcseAssociationCodec::try_decode_association_response(
    const std::span<const std::uint8_t> bytes,
    AssociationResponseEnvelope& response,
    std::string* error) noexcept {
    try {
        response = {};
        if (!osi::SessionCodec::try_decode(bytes, response.session, error)) {
            return false;
        }
        if (response.session.kind != osi::SessionSpduKind::accept) {
            set_error(error, "Association response must use an ISO Session Accept SPDU.");
            response = {};
            return false;
        }
        if (!osi::PresentationCodec::try_decode_cpa(
                response.session.user_data, response.presentation, error)) {
            response = {};
            return false;
        }
        if (!try_decode_aare(
                response.presentation.user_data.single_asn1_type, response.aare, error)) {
            response = {};
            return false;
        }
        if (error != nullptr) {
            error->clear();
        }
        return true;
    } catch (const std::exception& exception) {
        response = {};
        set_error(error, exception.what());
        return false;
    } catch (...) {
        response = {};
        set_error(error, "Association response decode failed unexpectedly.");
        return false;
    }
}

AssociationResponseEnvelope AcseAssociationCodec::decode_association_response(
    const std::span<const std::uint8_t> bytes) {
    AssociationResponseEnvelope response;
    std::string error;
    if (!try_decode_association_response(bytes, response, &error)) {
        throw AcseFormatError(error);
    }
    return response;
}

} // namespace ar::iec61850::acse
