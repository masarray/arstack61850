// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/osi/presentation.hpp"

#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/osi/session.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>

namespace ar::iec61850::osi {
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
        if (part.size() > std::numeric_limits<std::size_t>::max() - length) {
            throw std::length_error("Presentation encoding size overflow.");
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

std::vector<std::uint8_t> encode_integer(const std::uint32_t value) {
    return BerWriter::encode_tlv(
        0x02U, BerWriter::encode_unsigned_integer(value));
}

std::uint32_t read_positive_integer(const BerTlv& tlv, const char* field_name) {
    const auto value = BerReader::read_unsigned_integer(tlv);
    if (!value || *value == 0U || *value > std::numeric_limits<std::uint32_t>::max()) {
        throw PresentationFormatError(std::string{field_name} + " is outside the supported range.");
    }
    return static_cast<std::uint32_t>(*value);
}

std::uint32_t read_nonnegative_integer(const BerTlv& tlv, const char* field_name) {
    const auto value = BerReader::read_unsigned_integer(tlv);
    if (!value || *value > std::numeric_limits<std::uint32_t>::max()) {
        throw PresentationFormatError(std::string{field_name} + " is outside the supported range.");
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
        throw PresentationFormatError(std::string{"Invalid "} + description + ".");
    }
    return tlv;
}

void validate_oid(
    const std::span<const std::uint8_t> value,
    const char* description) {
    if (value.empty() || value.size() > PresentationCodec::maximum_oid_bytes) {
        throw PresentationFormatError(std::string{description} + " has an invalid encoded length.");
    }
}

void validate_optional_normal_field(
    const BerTlv& field,
    const char* description) {
    if (field.value.size() > PresentationCodec::maximum_selector_bytes) {
        throw PresentationFormatError(std::string{description} + " exceeds the bounded limit.");
    }
}

std::vector<std::uint8_t> encode_context_definition_list(
    const std::span<const PresentationContextDefinition> contexts) {
    if (contexts.empty() || contexts.size() > PresentationCodec::maximum_contexts) {
        throw std::invalid_argument("Presentation context count is outside the supported range.");
    }

    std::vector<std::uint8_t> definitions;
    for (const auto& context : contexts) {
        if (context.id == 0U) {
            throw std::invalid_argument("Presentation context identifiers must be positive.");
        }
        validate_oid(context.abstract_syntax_name, "Presentation abstract syntax OID");
        if (context.transfer_syntax_names.empty() ||
            context.transfer_syntax_names.size() > PresentationCodec::maximum_contexts) {
            throw std::invalid_argument("Presentation transfer-syntax list is empty or excessive.");
        }

        std::vector<std::uint8_t> transfer_syntax_list;
        for (const auto& transfer_syntax : context.transfer_syntax_names) {
            validate_oid(transfer_syntax, "Presentation transfer syntax OID");
            const auto encoded = BerWriter::encode_tlv(0x06U, transfer_syntax);
            transfer_syntax_list.insert(
                transfer_syntax_list.end(), encoded.begin(), encoded.end());
        }
        const auto encoded_transfer_list = BerWriter::encode_tlv(0x30U, transfer_syntax_list);
        const auto id = encode_integer(context.id);
        const auto abstract_syntax = BerWriter::encode_tlv(
            0x06U, context.abstract_syntax_name);
        const auto body = concat({id, abstract_syntax, encoded_transfer_list});
        const auto definition = BerWriter::encode_tlv(0x30U, body);
        definitions.insert(definitions.end(), definition.begin(), definition.end());
    }
    return BerWriter::encode_tlv(0xA4U, definitions);
}

std::vector<PresentationContextDefinition> parse_context_definition_list(
    const std::span<const std::uint8_t> bytes) {
    std::vector<PresentationContextDefinition> contexts;
    for (const auto& definition : BerReader::read_children(bytes)) {
        if (definition.encoded_tag != 0x30U) {
            throw PresentationFormatError("Presentation context definition must be a SEQUENCE.");
        }
        if (contexts.size() >= PresentationCodec::maximum_contexts) {
            throw PresentationFormatError("Presentation context count exceeded the limit.");
        }

        PresentationContextDefinition context;
        bool saw_id = false;
        bool saw_abstract_syntax = false;
        bool saw_transfer_syntaxes = false;
        for (const auto& field : BerReader::read_children(definition.value)) {
            if (field.encoded_tag == 0x02U) {
                if (saw_id) {
                    throw PresentationFormatError("Presentation context contains duplicate identifiers.");
                }
                context.id = read_positive_integer(field, "Presentation context identifier");
                saw_id = true;
            } else if (field.encoded_tag == 0x06U) {
                if (saw_abstract_syntax) {
                    throw PresentationFormatError("Presentation context contains duplicate abstract syntaxes.");
                }
                validate_oid(field.value, "Presentation abstract syntax OID");
                context.abstract_syntax_name = field.value;
                saw_abstract_syntax = true;
            } else if (field.encoded_tag == 0x30U) {
                if (saw_transfer_syntaxes) {
                    throw PresentationFormatError("Presentation context contains duplicate transfer-syntax lists.");
                }
                for (const auto& syntax : BerReader::read_children(field.value)) {
                    if (syntax.encoded_tag != 0x06U) {
                        throw PresentationFormatError("Presentation transfer syntax must be an OBJECT IDENTIFIER.");
                    }
                    validate_oid(syntax.value, "Presentation transfer syntax OID");
                    context.transfer_syntax_names.push_back(syntax.value);
                }
                saw_transfer_syntaxes = true;
            } else {
                throw PresentationFormatError("Presentation context contains an unsupported field.");
            }
        }

        if (!saw_id || !saw_abstract_syntax || !saw_transfer_syntaxes ||
            context.transfer_syntax_names.empty()) {
            throw PresentationFormatError("Presentation context definition is incomplete.");
        }
        if (std::any_of(contexts.begin(), contexts.end(), [&context](const auto& existing) {
                return existing.id == context.id;
            })) {
            throw PresentationFormatError("Presentation context identifiers must be unique.");
        }
        contexts.push_back(std::move(context));
    }
    if (contexts.empty()) {
        throw PresentationFormatError("Presentation context definition list is empty.");
    }
    return contexts;
}

std::uint32_t parse_mode_selector(const BerTlv& item) {
    const auto fields = BerReader::read_children(item.value);
    if (fields.size() != 1U || fields.front().encoded_tag != 0x80U) {
        throw PresentationFormatError("Presentation mode selector is malformed.");
    }
    return read_positive_integer(fields.front(), "Presentation mode selector");
}

std::vector<std::uint8_t> encode_mode_selector(const std::uint32_t mode_selector) {
    if (mode_selector == 0U) {
        throw std::invalid_argument("Presentation mode selector must be positive.");
    }
    const auto encoded_mode = BerWriter::encode_tlv(
        0x80U, BerWriter::encode_unsigned_integer(mode_selector));
    return BerWriter::encode_tlv(0xA0U, encoded_mode);
}

std::vector<std::uint8_t> encode_context_result_list(
    const std::span<const PresentationContextResult> results) {
    if (results.empty() || results.size() > PresentationCodec::maximum_contexts) {
        throw std::invalid_argument("Presentation context-result count is outside the supported range.");
    }
    std::vector<std::uint8_t> encoded_results;
    for (const auto& result : results) {
        validate_oid(result.transfer_syntax_name, "Presentation result transfer syntax OID");
        const std::array<std::uint8_t, 1> result_value{
            static_cast<std::uint8_t>(result.result)};
        const auto result_tlv = BerWriter::encode_tlv(0x80U, result_value);
        const auto syntax_tlv = BerWriter::encode_tlv(0x81U, result.transfer_syntax_name);
        const auto sequence = BerWriter::encode_tlv(
            0x30U, concat({result_tlv, syntax_tlv}));
        encoded_results.insert(encoded_results.end(), sequence.begin(), sequence.end());
    }
    return BerWriter::encode_tlv(0xA5U, encoded_results);
}

std::vector<PresentationContextResult> parse_context_result_list(
    const std::span<const std::uint8_t> bytes) {
    std::vector<PresentationContextResult> results;
    for (const auto& sequence : BerReader::read_children(bytes)) {
        if (sequence.encoded_tag != 0x30U) {
            throw PresentationFormatError("Presentation context result must be a SEQUENCE.");
        }
        if (results.size() >= PresentationCodec::maximum_contexts) {
            throw PresentationFormatError("Presentation context-result count exceeded the limit.");
        }

        PresentationContextResult result;
        bool saw_result = false;
        bool saw_transfer_syntax = false;
        for (const auto& field : BerReader::read_children(sequence.value)) {
            if (field.encoded_tag == 0x80U) {
                if (saw_result) {
                    throw PresentationFormatError("Presentation result contains duplicate result codes.");
                }
                const auto value = read_nonnegative_integer(field, "Presentation context result");
                if (value > static_cast<std::uint32_t>(PresentationContextResultCode::provider_rejection)) {
                    throw PresentationFormatError("Presentation context result code is unsupported.");
                }
                result.result = static_cast<PresentationContextResultCode>(value);
                saw_result = true;
            } else if (field.encoded_tag == 0x81U) {
                if (saw_transfer_syntax) {
                    throw PresentationFormatError("Presentation result contains duplicate transfer syntaxes.");
                }
                validate_oid(field.value, "Presentation result transfer syntax OID");
                result.transfer_syntax_name = field.value;
                saw_transfer_syntax = true;
            } else if (field.encoded_tag != 0x82U) {
                throw PresentationFormatError("Presentation context result contains an unsupported field.");
            }
        }
        if (!saw_result || !saw_transfer_syntax) {
            throw PresentationFormatError("Presentation context result is incomplete.");
        }
        results.push_back(std::move(result));
    }
    if (results.empty()) {
        throw PresentationFormatError("Presentation context-result list is empty.");
    }
    return results;
}

} // namespace

std::uint32_t PresentationCp::context_id_for_abstract_syntax(
    const std::span<const std::uint8_t> object_identifier_value) const noexcept {
    const auto found = std::find_if(contexts.begin(), contexts.end(),
        [object_identifier_value](const PresentationContextDefinition& context) {
            return std::span<const std::uint8_t>{context.abstract_syntax_name}
                .size() == object_identifier_value.size() &&
                std::equal(context.abstract_syntax_name.begin(),
                           context.abstract_syntax_name.end(),
                           object_identifier_value.begin());
        });
    return found == contexts.end() ? 0U : found->id;
}

const std::vector<std::uint8_t>& PresentationCodec::acse_abstract_syntax_name() {
    static const std::vector<std::uint8_t> value{0x52U, 0x01U, 0x00U, 0x01U};
    return value;
}

const std::vector<std::uint8_t>& PresentationCodec::mms_abstract_syntax_name() {
    static const std::vector<std::uint8_t> value{0x28U, 0xCAU, 0x22U, 0x02U, 0x01U};
    return value;
}

const std::vector<std::uint8_t>& PresentationCodec::ber_transfer_syntax_name() {
    static const std::vector<std::uint8_t> value{0x51U, 0x01U};
    return value;
}

std::vector<PresentationContextDefinition> PresentationCodec::default_contexts() {
    return {
        PresentationContextDefinition{
            1U, acse_abstract_syntax_name(), {ber_transfer_syntax_name()}},
        PresentationContextDefinition{
            3U, mms_abstract_syntax_name(), {ber_transfer_syntax_name()}},
    };
}

std::vector<std::uint8_t> PresentationCodec::encode_fully_encoded_data(
    const std::uint32_t context_id,
    const std::span<const std::uint8_t> single_asn1_type) {
    if (context_id == 0U) {
        throw std::invalid_argument("Presentation PDV context identifier must be positive.");
    }
    if (single_asn1_type.size() > maximum_ppdu_bytes) {
        throw std::length_error("Presentation PDV payload exceeds the bounded limit.");
    }
    const auto id = encode_integer(context_id);
    const auto payload = BerWriter::encode_tlv(0xA0U, single_asn1_type);
    const auto pdv_list = BerWriter::encode_tlv(0x30U, concat({id, payload}));
    return BerWriter::encode_tlv(0x61U, pdv_list);
}

bool PresentationCodec::try_decode_fully_encoded_data(
    const std::span<const std::uint8_t> bytes,
    PresentationPdv& pdv,
    std::string* error) noexcept {
    try {
        pdv = {};
        if (bytes.empty() || bytes.size() > maximum_ppdu_bytes) {
            set_error(error, "Presentation fully-encoded-data size is outside the supported range.");
            return false;
        }
        const auto outer = read_single_tlv(bytes, 0x61U, "Presentation fully-encoded-data");
        const auto pdv_lists = BerReader::read_children(outer.value);
        if (pdv_lists.size() != 1U || pdv_lists.front().encoded_tag != 0x30U) {
            set_error(error, "Presentation fully-encoded-data must contain one PDV-list.");
            return false;
        }

        bool saw_context_id = false;
        bool saw_payload = false;
        for (const auto& field : BerReader::read_children(pdv_lists.front().value)) {
            if (field.encoded_tag == 0x02U) {
                if (saw_context_id) {
                    throw PresentationFormatError("Presentation PDV contains duplicate context identifiers.");
                }
                pdv.context_id = read_positive_integer(field, "Presentation PDV context identifier");
                saw_context_id = true;
            } else if (field.encoded_tag == 0xA0U) {
                if (saw_payload) {
                    throw PresentationFormatError("Presentation PDV contains duplicate single-ASN.1 values.");
                }
                pdv.single_asn1_type = field.value;
                saw_payload = true;
            } else {
                throw PresentationFormatError("Presentation PDV contains an unsupported field.");
            }
        }
        if (!saw_context_id || !saw_payload) {
            throw PresentationFormatError("Presentation PDV-list is incomplete.");
        }
        if (error != nullptr) {
            error->clear();
        }
        return true;
    } catch (const std::exception& exception) {
        pdv = {};
        set_error(error, exception.what());
        return false;
    } catch (...) {
        pdv = {};
        set_error(error, "Presentation fully-encoded-data decode failed unexpectedly.");
        return false;
    }
}

PresentationPdv PresentationCodec::decode_fully_encoded_data(
    const std::span<const std::uint8_t> bytes) {
    PresentationPdv pdv;
    std::string error;
    if (!try_decode_fully_encoded_data(bytes, pdv, &error)) {
        throw PresentationFormatError(error);
    }
    return pdv;
}

std::vector<std::uint8_t> PresentationCodec::encode_cp(
    const std::span<const PresentationContextDefinition> contexts,
    const std::uint32_t acse_context_id,
    const std::span<const std::uint8_t> acse_aarq,
    const std::span<const std::uint8_t> calling_selector,
    const std::span<const std::uint8_t> called_selector,
    const std::uint32_t mode_selector) {
    if (calling_selector.size() > maximum_selector_bytes ||
        called_selector.size() > maximum_selector_bytes) {
        throw std::length_error("Presentation selector exceeds the bounded limit.");
    }
    if (std::none_of(contexts.begin(), contexts.end(), [acse_context_id](const auto& context) {
            return context.id == acse_context_id;
        })) {
        throw std::invalid_argument("ACSE presentation context identifier is not defined.");
    }

    static const std::array<std::uint8_t, 4> default_selector{0U, 0U, 0U, 1U};
    const auto effective_calling = calling_selector.empty()
        ? std::span<const std::uint8_t>{default_selector}
        : calling_selector;
    const auto effective_called = called_selector.empty()
        ? std::span<const std::uint8_t>{default_selector}
        : called_selector;

    const auto mode = encode_mode_selector(mode_selector);
    const auto calling = BerWriter::encode_tlv(0x81U, effective_calling);
    const auto called = BerWriter::encode_tlv(0x82U, effective_called);
    const auto definitions = encode_context_definition_list(contexts);
    const auto user_data = encode_fully_encoded_data(acse_context_id, acse_aarq);
    const auto normal = BerWriter::encode_tlv(
        0xA2U, concat({calling, called, definitions, user_data}));
    const auto cp = BerWriter::encode_tlv(0x31U, concat({mode, normal}));
    if (cp.size() > maximum_ppdu_bytes) {
        throw std::length_error("Presentation CP PPDU exceeds the bounded limit.");
    }
    return cp;
}

bool PresentationCodec::try_decode_cp(
    const std::span<const std::uint8_t> bytes,
    PresentationCp& cp,
    std::string* error) noexcept {
    try {
        cp = {};
        if (bytes.empty() || bytes.size() > maximum_ppdu_bytes) {
            set_error(error, "Presentation CP PPDU size is outside the supported range.");
            return false;
        }
        const auto outer = read_single_tlv(bytes, 0x31U, "Presentation CP PPDU");
        bool saw_mode = false;
        bool saw_normal = false;
        for (const auto& item : BerReader::read_children(outer.value)) {
            if (item.encoded_tag == 0xA0U) {
                if (saw_mode) {
                    throw PresentationFormatError("Presentation CP contains duplicate mode selectors.");
                }
                cp.mode_selector = parse_mode_selector(item);
                saw_mode = true;
            } else if (item.encoded_tag == 0xA2U) {
                if (saw_normal) {
                    throw PresentationFormatError("Presentation CP contains duplicate normal-mode parameters.");
                }
                bool saw_contexts = false;
                bool saw_user_data = false;
                for (const auto& normal : BerReader::read_children(item.value)) {
                    if (normal.encoded_tag == 0x81U) {
                        if (!cp.calling_selector.empty()) {
                            throw PresentationFormatError("Presentation CP contains duplicate calling selectors.");
                        }
                        if (normal.value.size() > maximum_selector_bytes) {
                            throw PresentationFormatError("Presentation calling selector exceeds the limit.");
                        }
                        cp.calling_selector = normal.value;
                    } else if (normal.encoded_tag == 0x82U) {
                        if (!cp.called_selector.empty()) {
                            throw PresentationFormatError("Presentation CP contains duplicate called selectors.");
                        }
                        if (normal.value.size() > maximum_selector_bytes) {
                            throw PresentationFormatError("Presentation called selector exceeds the limit.");
                        }
                        cp.called_selector = normal.value;
                    } else if (normal.encoded_tag == 0xA4U) {
                        if (saw_contexts) {
                            throw PresentationFormatError("Presentation CP contains duplicate context lists.");
                        }
                        cp.contexts = parse_context_definition_list(normal.value);
                        saw_contexts = true;
                    } else if (normal.encoded_tag == 0x61U) {
                        if (saw_user_data) {
                            throw PresentationFormatError("Presentation CP contains duplicate user data.");
                        }
                        const auto encoded = BerWriter::encode_tlv(0x61U, normal.value);
                        cp.user_data = decode_fully_encoded_data(encoded);
                        saw_user_data = true;
                    } else {
                        throw PresentationFormatError("Presentation CP contains an unsupported normal-mode field.");
                    }
                }
                if (!saw_contexts || !saw_user_data) {
                    throw PresentationFormatError("Presentation CP normal-mode parameters are incomplete.");
                }
                saw_normal = true;
            } else {
                throw PresentationFormatError("Presentation CP contains an unsupported field.");
            }
        }
        if (!saw_mode || !saw_normal) {
            throw PresentationFormatError("Presentation CP PPDU is incomplete.");
        }
        if (std::none_of(cp.contexts.begin(), cp.contexts.end(), [&cp](const auto& context) {
                return context.id == cp.user_data.context_id;
            })) {
            throw PresentationFormatError("Presentation CP user-data context identifier is undefined.");
        }
        if (error != nullptr) {
            error->clear();
        }
        return true;
    } catch (const std::exception& exception) {
        cp = {};
        set_error(error, exception.what());
        return false;
    } catch (...) {
        cp = {};
        set_error(error, "Presentation CP decode failed unexpectedly.");
        return false;
    }
}

PresentationCp PresentationCodec::decode_cp(const std::span<const std::uint8_t> bytes) {
    PresentationCp cp;
    std::string error;
    if (!try_decode_cp(bytes, cp, &error)) {
        throw PresentationFormatError(error);
    }
    return cp;
}

std::vector<std::uint8_t> PresentationCodec::encode_cpa(
    const std::span<const PresentationContextResult> results,
    const std::uint32_t acse_context_id,
    const std::span<const std::uint8_t> acse_aare,
    const std::uint32_t mode_selector) {
    const auto mode = encode_mode_selector(mode_selector);
    const auto context_results = encode_context_result_list(results);
    const auto user_data = encode_fully_encoded_data(acse_context_id, acse_aare);
    const auto normal = BerWriter::encode_tlv(
        0xA2U, concat({context_results, user_data}));
    const auto cpa = BerWriter::encode_tlv(0x31U, concat({mode, normal}));
    if (cpa.size() > maximum_ppdu_bytes) {
        throw std::length_error("Presentation CPA PPDU exceeds the bounded limit.");
    }
    return cpa;
}

std::vector<std::uint8_t> PresentationCodec::encode_cpa_accepting(
    const PresentationCp& request,
    const std::span<const std::uint8_t> acse_aare) {
    if (request.contexts.empty()) {
        throw std::invalid_argument("Presentation CPA requires at least one requested context.");
    }
    std::vector<PresentationContextResult> results;
    results.reserve(request.contexts.size());
    for (const auto& context : request.contexts) {
        if (context.transfer_syntax_names.empty()) {
            throw std::invalid_argument("Requested presentation context has no transfer syntax.");
        }
        results.push_back(PresentationContextResult{
            PresentationContextResultCode::acceptance,
            context.transfer_syntax_names.front()});
    }
    return encode_cpa(results, request.user_data.context_id, acse_aare, request.mode_selector);
}

bool PresentationCodec::try_decode_cpa(
    const std::span<const std::uint8_t> bytes,
    PresentationCpa& cpa,
    std::string* error) noexcept {
    try {
        cpa = {};
        if (bytes.empty() || bytes.size() > maximum_ppdu_bytes) {
            set_error(error, "Presentation CPA PPDU size is outside the supported range.");
            return false;
        }
        const auto outer = read_single_tlv(bytes, 0x31U, "Presentation CPA PPDU");
        bool saw_mode = false;
        bool saw_normal = false;
        for (const auto& item : BerReader::read_children(outer.value)) {
            if (item.encoded_tag == 0xA0U) {
                if (saw_mode) {
                    throw PresentationFormatError("Presentation CPA contains duplicate mode selectors.");
                }
                cpa.mode_selector = parse_mode_selector(item);
                saw_mode = true;
            } else if (item.encoded_tag == 0xA2U) {
                if (saw_normal) {
                    throw PresentationFormatError("Presentation CPA contains duplicate normal-mode parameters.");
                }
                bool saw_protocol_version = false;
                bool saw_results = false;
                bool saw_user_data = false;
                bool saw_responding_selector = false;
                bool saw_presentation_requirements = false;
                bool saw_user_session_requirements = false;
                bool saw_protocol_options = false;
                bool saw_nominated_context = false;
                for (const auto& normal : BerReader::read_children(item.value)) {
                    if (normal.tag_class == BerClass::context_specific &&
                        normal.tag_number == 0) {
                        if (saw_protocol_version) {
                            throw PresentationFormatError(
                                "Presentation CPA contains duplicate protocol versions.");
                        }
                        validate_optional_normal_field(
                            normal, "Presentation protocol-version");
                        saw_protocol_version = true;
                    } else if (normal.encoded_tag == 0x83U) {
                        if (saw_responding_selector) {
                            throw PresentationFormatError(
                                "Presentation CPA contains duplicate responding selectors.");
                        }
                        if (normal.value.size() > maximum_selector_bytes) {
                            throw PresentationFormatError(
                                "Presentation responding selector exceeds the limit.");
                        }
                        saw_responding_selector = true;
                    } else if (normal.encoded_tag == 0xA5U) {
                        if (saw_results) {
                            throw PresentationFormatError("Presentation CPA contains duplicate context results.");
                        }
                        cpa.context_results = parse_context_result_list(normal.value);
                        saw_results = true;
                    } else if (normal.tag_class == BerClass::context_specific &&
                               normal.tag_number == 8) {
                        if (saw_presentation_requirements) {
                            throw PresentationFormatError(
                                "Presentation CPA contains duplicate presentation requirements.");
                        }
                        validate_optional_normal_field(
                            normal, "Presentation requirements");
                        saw_presentation_requirements = true;
                    } else if (normal.tag_class == BerClass::context_specific &&
                               normal.tag_number == 9) {
                        if (saw_user_session_requirements) {
                            throw PresentationFormatError(
                                "Presentation CPA contains duplicate user-session requirements.");
                        }
                        validate_optional_normal_field(
                            normal, "Presentation user-session requirements");
                        saw_user_session_requirements = true;
                    } else if (normal.tag_class == BerClass::context_specific &&
                               normal.tag_number == 11) {
                        if (saw_protocol_options) {
                            throw PresentationFormatError(
                                "Presentation CPA contains duplicate protocol options.");
                        }
                        validate_optional_normal_field(
                            normal, "Presentation protocol options");
                        saw_protocol_options = true;
                    } else if (normal.tag_class == BerClass::context_specific &&
                               normal.tag_number == 13) {
                        if (saw_nominated_context) {
                            throw PresentationFormatError(
                                "Presentation CPA contains duplicate nominated contexts.");
                        }
                        validate_optional_normal_field(
                            normal, "Presentation responders-nominated-context");
                        saw_nominated_context = true;
                    } else if (normal.encoded_tag == 0x61U) {
                        if (saw_user_data) {
                            throw PresentationFormatError("Presentation CPA contains duplicate user data.");
                        }
                        const auto encoded = BerWriter::encode_tlv(0x61U, normal.value);
                        cpa.user_data = decode_fully_encoded_data(encoded);
                        saw_user_data = true;
                    } else {
                        throw PresentationFormatError(
                            "Presentation CPA contains an unsupported normal-mode field tag=" +
                            std::to_string(normal.encoded_tag) +
                            ", tagNumber=" + std::to_string(normal.tag_number) +
                            ", length=" + std::to_string(normal.value.size()) + ".");
                    }
                }
                if (!saw_results || !saw_user_data) {
                    throw PresentationFormatError("Presentation CPA normal-mode parameters are incomplete.");
                }
                saw_normal = true;
            } else {
                throw PresentationFormatError("Presentation CPA contains an unsupported field.");
            }
        }
        if (!saw_mode || !saw_normal) {
            throw PresentationFormatError("Presentation CPA PPDU is incomplete.");
        }
        if (error != nullptr) {
            error->clear();
        }
        return true;
    } catch (const std::exception& exception) {
        cpa = {};
        set_error(error, exception.what());
        return false;
    } catch (...) {
        cpa = {};
        set_error(error, "Presentation CPA decode failed unexpectedly.");
        return false;
    }
}

PresentationCpa PresentationCodec::decode_cpa(const std::span<const std::uint8_t> bytes) {
    PresentationCpa cpa;
    std::string error;
    if (!try_decode_cpa(bytes, cpa, &error)) {
        throw PresentationFormatError(error);
    }
    return cpa;
}

std::vector<std::uint8_t> PresentationCodec::encode_p_data(
    const std::span<const std::uint8_t> abstract_syntax_payload,
    const std::uint32_t presentation_context_id,
    const bool include_give_tokens_prefix) {
    const auto fully_encoded = encode_fully_encoded_data(
        presentation_context_id, abstract_syntax_payload);
    return SessionCodec::encode_data_transfer(fully_encoded, include_give_tokens_prefix);
}

bool PresentationCodec::try_decode_p_data(
    const std::span<const std::uint8_t> bytes,
    PresentationPdv& pdv,
    std::string* error) noexcept {
    try {
        std::span<const std::uint8_t> presentation = bytes;
        SessionDataTransfer transfer;
        if (!bytes.empty() && bytes.front() == SessionCodec::data_transfer_code) {
            std::string session_error;
            if (!SessionCodec::try_decode_data_transfer(bytes, transfer, &session_error)) {
                set_error(error, session_error);
                pdv = {};
                return false;
            }
            presentation = transfer.presentation_payload;
        }
        return try_decode_fully_encoded_data(presentation, pdv, error);
    } catch (const std::exception& exception) {
        pdv = {};
        set_error(error, exception.what());
        return false;
    } catch (...) {
        pdv = {};
        set_error(error, "Presentation P-DATA decode failed unexpectedly.");
        return false;
    }
}

PresentationPdv PresentationCodec::decode_p_data(
    const std::span<const std::uint8_t> bytes) {
    PresentationPdv pdv;
    std::string error;
    if (!try_decode_p_data(bytes, pdv, &error)) {
        throw PresentationFormatError(error);
    }
    return pdv;
}

} // namespace ar::iec61850::osi
