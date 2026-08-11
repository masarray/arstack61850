// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/acse/association_span.hpp"

#include "ariec61850/asn1/ber_span_reader.hpp"
#include "ariec61850/asn1/ber_span_writer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace ar::iec61850::acse {
namespace {

constexpr std::array<std::uint8_t, 5U> kMmsApplicationContext{
    0x28U, 0xCAU, 0x22U, 0x02U, 0x03U};

constexpr std::array<std::uint8_t, 40U> kDefaultMmsInitiateRequest{
    0xA8U, 0x26U,
    0x80U, 0x03U, 0x00U, 0xFDU, 0xE8U,
    0x81U, 0x01U, 0x0AU,
    0x82U, 0x01U, 0x0AU,
    0x83U, 0x01U, 0x05U,
    0xA4U, 0x16U,
    0x80U, 0x01U, 0x01U,
    0x81U, 0x03U, 0x05U, 0xF1U, 0x00U,
    0x82U, 0x0CU, 0x03U, 0xEEU, 0x1CU, 0x00U, 0x00U, 0x04U,
    0x08U, 0x00U, 0x00U, 0x79U, 0xEFU, 0x18U};

// servicesSupportedCalled contains only the confirmed services implemented by
// MmsStaticApplicationDispatcher: GetNameList(1), Read(4), Write(5),
// GetVariableAccessAttributes(6), and GetNamedVariableListAttributes(12).
constexpr std::array<std::uint8_t, 40U> kDefaultMmsInitiateResponse{
    0xA9U, 0x26U,
    0x80U, 0x03U, 0x00U, 0xFDU, 0xE8U,
    0x81U, 0x01U, 0x0AU,
    0x82U, 0x01U, 0x0AU,
    0x83U, 0x01U, 0x05U,
    0xA4U, 0x16U,
    0x80U, 0x01U, 0x01U,
    0x81U, 0x03U, 0x05U, 0xF1U, 0x00U,
    0x82U, 0x0CU, 0x03U, 0x4EU, 0x08U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};

// Byte-identical to the deterministic static AARE profile used by the bounded
// server. The embedded MMS Initiate response carries the same narrow service
// bitmap as kDefaultMmsInitiateResponse above.
constexpr std::array<std::uint8_t, 76U> kDefaultAcceptAare{
    0x61U, 0x4AU,
    0xA1U, 0x07U, 0x06U, 0x05U, 0x28U, 0xCAU, 0x22U, 0x02U, 0x03U,
    0xA2U, 0x03U, 0x02U, 0x01U, 0x00U,
    0xA3U, 0x05U, 0xA1U, 0x03U, 0x02U, 0x01U, 0x00U,
    0xBEU, 0x33U,
    0x28U, 0x31U,
    0x06U, 0x02U, 0x51U, 0x01U,
    0x02U, 0x01U, 0x03U,
    0xA0U, 0x28U,
    0xA9U, 0x26U,
    0x80U, 0x03U, 0x00U, 0xFDU, 0xE8U,
    0x81U, 0x01U, 0x0AU,
    0x82U, 0x01U, 0x0AU,
    0x83U, 0x01U, 0x05U,
    0xA4U, 0x16U,
    0x80U, 0x01U, 0x01U,
    0x81U, 0x03U, 0x05U, 0xF1U, 0x00U,
    0x82U, 0x0CU, 0x03U, 0x4EU, 0x08U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};

[[nodiscard]] std::optional<std::size_t> accept_aare_size(
    const std::size_t mms_initiate_response_bytes) noexcept {
    const auto application_context_oid = asn1::BerSpanWriter::tlv_size(
        6, kMmsApplicationContext.size());
    const auto application_context = application_context_oid
        ? asn1::BerSpanWriter::tlv_size(1, *application_context_oid)
        : std::nullopt;
    const auto result_integer = asn1::BerSpanWriter::tlv_size(2, 1U);
    const auto result = result_integer
        ? asn1::BerSpanWriter::tlv_size(2, *result_integer)
        : std::nullopt;
    const auto diagnostic_integer = asn1::BerSpanWriter::tlv_size(2, 1U);
    const auto diagnostic_choice = diagnostic_integer
        ? asn1::BerSpanWriter::tlv_size(1, *diagnostic_integer)
        : std::nullopt;
    const auto diagnostic = diagnostic_choice
        ? asn1::BerSpanWriter::tlv_size(3, *diagnostic_choice)
        : std::nullopt;
    const auto direct_reference = asn1::BerSpanWriter::tlv_size(6, 2U);
    const auto indirect_reference = asn1::BerSpanWriter::tlv_size(2, 1U);
    const auto single_asn1_type = asn1::BerSpanWriter::tlv_size(
        0, mms_initiate_response_bytes);
    if (!application_context || !result || !diagnostic ||
        !direct_reference || !indirect_reference || !single_asn1_type) {
        return std::nullopt;
    }
    const auto external_content =
        *direct_reference + *indirect_reference + *single_asn1_type;
    const auto external = asn1::BerSpanWriter::tlv_size(8, external_content);
    if (!external) {
        return std::nullopt;
    }
    const auto user_information = asn1::BerSpanWriter::tlv_size(30, *external);
    if (!user_information) {
        return std::nullopt;
    }
    const auto aare_content =
        *application_context + *result + *diagnostic + *user_information;
    return asn1::BerSpanWriter::tlv_size(1, aare_content);
}

[[nodiscard]] wire::EncodeResult encode_accept_aare_into(
    const std::span<const std::uint8_t> mms_initiate_response,
    const std::span<std::uint8_t> destination) noexcept {
    const auto required = accept_aare_size(mms_initiate_response.size());
    if (!required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (destination.size() < *required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, *required};
    }

    const auto application_context_oid = asn1::BerSpanWriter::tlv_size(
        6, kMmsApplicationContext.size()).value();
    const auto result_integer = asn1::BerSpanWriter::tlv_size(2, 1U).value();
    const auto diagnostic_integer = asn1::BerSpanWriter::tlv_size(2, 1U).value();
    const auto diagnostic_choice = asn1::BerSpanWriter::tlv_size(
        1, diagnostic_integer).value();
    const auto direct_reference = asn1::BerSpanWriter::tlv_size(6, 2U).value();
    const auto indirect_reference = asn1::BerSpanWriter::tlv_size(2, 1U).value();
    const auto single_asn1_type = asn1::BerSpanWriter::tlv_size(
        0, mms_initiate_response.size()).value();
    const auto external_content =
        direct_reference + indirect_reference + single_asn1_type;
    const auto external = asn1::BerSpanWriter::tlv_size(8, external_content).value();
    const auto user_information = asn1::BerSpanWriter::tlv_size(30, external).value();
    const auto application_context = asn1::BerSpanWriter::tlv_size(
        1, application_context_oid).value();
    const auto result = asn1::BerSpanWriter::tlv_size(2, result_integer).value();
    const auto diagnostic = asn1::BerSpanWriter::tlv_size(
        3, diagnostic_choice).value();
    const auto aare_content = application_context + result + diagnostic + user_information;

    constexpr std::array<std::uint8_t, 2U> direct_reference_oid{0x51U, 0x01U};
    asn1::BerSpanWriter writer{destination.first(*required)};
    if (!writer.write_tlv_header(asn1::BerClass::application, true, 1, aare_content) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 1, application_context_oid) ||
        !writer.write_tlv(
            asn1::BerClass::universal, false, 6, kMmsApplicationContext) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 2, result_integer) ||
        !writer.write_tlv_header(asn1::BerClass::universal, false, 2, 1U) ||
        !writer.write_byte(0U) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 3, diagnostic_choice) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 1, diagnostic_integer) ||
        !writer.write_tlv_header(asn1::BerClass::universal, false, 2, 1U) ||
        !writer.write_byte(0U) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 30, external) ||
        !writer.write_tlv_header(
            asn1::BerClass::universal, true, 8, external_content) ||
        !writer.write_tlv(
            asn1::BerClass::universal, false, 6, direct_reference_oid) ||
        !writer.write_tlv_header(asn1::BerClass::universal, false, 2, 1U) ||
        !writer.write_byte(3U) ||
        !writer.write_tlv(
            asn1::BerClass::context_specific,
            true,
            0,
            mms_initiate_response) ||
        writer.size() != *required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }
    return {wire::EncodeStatus::ok, *required, *required};
}

[[nodiscard]] bool valid_oid(
    const std::span<const std::uint8_t> value,
    const bool allow_empty = false) noexcept {
    return (allow_empty || !value.empty()) && value.size() <= AcseSpanCodec::maximum_oid_bytes;
}

[[nodiscard]] bool decode_explicit_oid(
    const asn1::BerTlvView& field,
    std::span<const std::uint8_t>& oid,
    const bool allow_empty = false) noexcept {
    oid = {};
    if (!field.constructed) {
        return false;
    }
    asn1::BerTlvView inner;
    if (!asn1::BerSpanReader::try_read_exact(field.value, inner) ||
        inner.tag_class != asn1::BerClass::universal ||
        inner.tag_number != 6 || inner.constructed ||
        !valid_oid(inner.value, allow_empty)) {
        return false;
    }
    oid = inner.value;
    return true;
}

[[nodiscard]] bool decode_explicit_integer(
    const asn1::BerTlvView& field,
    std::uint32_t& value) noexcept {
    value = 0U;
    if (!field.constructed) {
        return false;
    }
    asn1::BerTlvView inner;
    if (!asn1::BerSpanReader::try_read_exact(field.value, inner) ||
        inner.tag_class != asn1::BerClass::universal ||
        inner.tag_number != 2 || inner.constructed) {
        return false;
    }
    const auto decoded = asn1::BerSpanReader::read_uint32(inner);
    if (!decoded) {
        return false;
    }
    value = *decoded;
    return true;
}

[[nodiscard]] bool decode_external(
    const asn1::BerTlvView& user_information,
    AcseExternalView& external) noexcept {
    external = {};
    if (!user_information.constructed) {
        return false;
    }

    asn1::BerTlvView outer;
    if (!asn1::BerSpanReader::try_read_exact(user_information.value, outer) ||
        outer.tag_class != asn1::BerClass::universal ||
        outer.tag_number != 8 || !outer.constructed) {
        return false;
    }

    bool saw_direct = false;
    bool saw_indirect = false;
    bool saw_encoding = false;
    std::size_t offset = 0U;
    while (offset < outer.value.size()) {
        asn1::BerTlvView field;
        if (!asn1::BerSpanReader::try_read_tlv(outer.value, offset, field)) {
            external = {};
            return false;
        }

        if (field.tag_class == asn1::BerClass::universal &&
            field.tag_number == 6 && !field.constructed) {
            if (saw_direct || !valid_oid(field.value)) {
                external = {};
                return false;
            }
            external.direct_reference = field.value;
            saw_direct = true;
        } else if (field.tag_class == asn1::BerClass::universal &&
                   field.tag_number == 2 && !field.constructed) {
            if (saw_indirect) {
                external = {};
                return false;
            }
            const auto value = asn1::BerSpanReader::read_uint32(field);
            if (!value) {
                external = {};
                return false;
            }
            external.indirect_reference = *value;
            saw_indirect = true;
        } else if (field.tag_class == asn1::BerClass::context_specific &&
                   field.tag_number == 0 && field.constructed) {
            if (saw_encoding || field.value.size() > AcseSpanCodec::maximum_acse_bytes) {
                external = {};
                return false;
            }
            external.single_asn1_type = field.value;
            saw_encoding = true;
        } else {
            external = {};
            return false;
        }
    }
    return saw_direct && saw_indirect && saw_encoding;
}

[[nodiscard]] bool request_is_valid_for_response(
    const AssociationRequestView& request) noexcept {
    return request.session.kind == osi::SessionWireKind::connect &&
        request.presentation.mode_selector != 0U &&
        request.presentation.context_count != 0U &&
        request.acse_presentation_context_id != 0U &&
        request.mms_presentation_context_id != 0U &&
        request.acse_presentation_context_id == request.presentation.user_data.context_id;
}

} // namespace

std::span<const std::uint8_t> AcseSpanCodec::mms_application_context_name() noexcept {
    return kMmsApplicationContext;
}

std::span<const std::uint8_t> AcseSpanCodec::default_mms_initiate_request() noexcept {
    return kDefaultMmsInitiateRequest;
}

std::span<const std::uint8_t> AcseSpanCodec::default_mms_initiate_response() noexcept {
    return kDefaultMmsInitiateResponse;
}

bool AcseSpanCodec::try_decode_aarq_view(
    const std::span<const std::uint8_t> bytes,
    AcseAarqView& aarq) noexcept {
    aarq = {};
    if (bytes.empty() || bytes.size() > maximum_acse_bytes) {
        return false;
    }

    asn1::BerTlvView outer;
    if (!asn1::BerSpanReader::try_read_exact(bytes, outer) ||
        outer.tag_class != asn1::BerClass::application ||
        outer.tag_number != 0 || !outer.constructed) {
        return false;
    }

    bool saw_application = false;
    bool saw_called_ap = false;
    bool saw_called_ae = false;
    bool saw_calling_ap = false;
    bool saw_calling_ae = false;
    bool saw_user_information = false;
    std::size_t offset = 0U;
    while (offset < outer.value.size()) {
        asn1::BerTlvView field;
        if (!asn1::BerSpanReader::try_read_tlv(outer.value, offset, field) ||
            field.tag_class != asn1::BerClass::context_specific) {
            aarq = {};
            return false;
        }

        switch (field.tag_number) {
        case 1:
            if (saw_application ||
                !decode_explicit_oid(field, aarq.application_context_name)) {
                aarq = {};
                return false;
            }
            saw_application = true;
            break;
        case 2:
            if (saw_called_ap ||
                !decode_explicit_oid(field, aarq.called_ap_title, true)) {
                aarq = {};
                return false;
            }
            saw_called_ap = true;
            break;
        case 3: {
            if (saw_called_ae) {
                aarq = {};
                return false;
            }
            std::uint32_t value{};
            if (!decode_explicit_integer(field, value)) {
                aarq = {};
                return false;
            }
            aarq.called_ae_qualifier = value;
            saw_called_ae = true;
            break;
        }
        case 6:
            if (saw_calling_ap ||
                !decode_explicit_oid(field, aarq.calling_ap_title, true)) {
                aarq = {};
                return false;
            }
            saw_calling_ap = true;
            break;
        case 7: {
            if (saw_calling_ae) {
                aarq = {};
                return false;
            }
            std::uint32_t value{};
            if (!decode_explicit_integer(field, value)) {
                aarq = {};
                return false;
            }
            aarq.calling_ae_qualifier = value;
            saw_calling_ae = true;
            break;
        }
        case 30:
            if (saw_user_information || !decode_external(field, aarq.user_information)) {
                aarq = {};
                return false;
            }
            saw_user_information = true;
            break;
        default:
            aarq = {};
            return false;
        }
    }

    if (!saw_application || !saw_user_information) {
        aarq = {};
        return false;
    }
    return true;
}

bool AcseSpanCodec::try_decode_association_request_view(
    const std::span<const std::uint8_t> bytes,
    AssociationRequestView& request) noexcept {
    request = {};
    if (!osi::SessionSpanCodec::try_decode_view(bytes, request.session) ||
        request.session.kind != osi::SessionWireKind::connect ||
        !osi::PresentationSpanCodec::try_decode_cp_view(
            request.session.user_data, request.presentation) ||
        !try_decode_aarq_view(
            request.presentation.user_data.single_asn1_type, request.aarq)) {
        request = {};
        return false;
    }

    if (!request.presentation.try_context_id_for_abstract_syntax(
            osi::PresentationSpanCodec::acse_abstract_syntax_name(),
            request.acse_presentation_context_id) ||
        !request.presentation.try_context_id_for_abstract_syntax(
            osi::PresentationSpanCodec::mms_abstract_syntax_name(),
            request.mms_presentation_context_id) ||
        !request_is_valid_for_response(request)) {
        request = {};
        return false;
    }
    return true;
}

wire::EncodeResult AcseSpanCodec::encode_default_accept_aare_into(
    const std::span<std::uint8_t> destination) noexcept {
    if (destination.size() < kDefaultAcceptAare.size()) {
        return {
            wire::EncodeStatus::buffer_too_small,
            0U,
            kDefaultAcceptAare.size()};
    }
    std::copy(
        kDefaultAcceptAare.begin(),
        kDefaultAcceptAare.end(),
        destination.begin());
    return {
        wire::EncodeStatus::ok,
        kDefaultAcceptAare.size(),
        kDefaultAcceptAare.size()};
}

std::optional<std::size_t> AcseSpanCodec::accept_response_size(
    const AssociationRequestView& request) noexcept {
    if (!request_is_valid_for_response(request)) {
        return std::nullopt;
    }

    const auto cpa_size = osi::PresentationSpanCodec::cpa_accepting_size(
        request.presentation, default_accept_aare_size());
    if (!cpa_size || *cpa_size > std::numeric_limits<std::uint8_t>::max()) {
        return std::nullopt;
    }

    const auto user_parameter_bytes = 2U + *cpa_size;
    if (user_parameter_bytes > std::numeric_limits<std::uint8_t>::max() ||
        request.session.parameter_bytes.size() >
            std::numeric_limits<std::uint8_t>::max() - user_parameter_bytes) {
        return std::nullopt;
    }
    const auto body_bytes = request.session.parameter_bytes.size() + user_parameter_bytes;
    return body_bytes + 2U;
}

wire::EncodeResult AcseSpanCodec::build_accept_response_into(
    const AssociationRequestView& request,
    const std::span<std::uint8_t> destination) noexcept {
    return build_accept_response_into(request, kDefaultMmsInitiateResponse, destination);
}

wire::EncodeResult AcseSpanCodec::build_accept_response_into(
    const AssociationRequestView& request,
    const std::span<const std::uint8_t> mms_initiate_response,
    const std::span<std::uint8_t> destination) noexcept {
    const auto aare_size = accept_aare_size(mms_initiate_response.size());
    if (!aare_size || !request_is_valid_for_response(request)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto cpa_size = osi::PresentationSpanCodec::cpa_accepting_size(
        request.presentation, *aare_size);
    if (!cpa_size || *cpa_size > std::numeric_limits<std::uint8_t>::max()) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto user_parameter_bytes = 2U + *cpa_size;
    if (user_parameter_bytes > std::numeric_limits<std::uint8_t>::max() ||
        request.session.parameter_bytes.size() >
            std::numeric_limits<std::uint8_t>::max() - user_parameter_bytes) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto required =
        request.session.parameter_bytes.size() + user_parameter_bytes + 2U;
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }

    std::array<std::uint8_t, 128U> aare{};
    const auto aare_result = encode_accept_aare_into(mms_initiate_response, aare);
    if (!aare_result.success() || aare_result.bytes_written != *aare_size) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }

    const auto body_bytes = required - 2U;
    destination[0] = osi::SessionSpanCodec::accept_code;
    destination[1] = static_cast<std::uint8_t>(body_bytes);
    std::copy(
        request.session.parameter_bytes.begin(),
        request.session.parameter_bytes.end(),
        destination.begin() + 2);

    std::size_t offset = 2U + request.session.parameter_bytes.size();
    destination[offset++] = osi::SessionSpanCodec::user_data_parameter;
    destination[offset++] = static_cast<std::uint8_t>(*cpa_size);

    const auto cpa_result = osi::PresentationSpanCodec::encode_cpa_accepting_into(
        request.presentation,
        std::span<const std::uint8_t>{aare}.first(aare_result.bytes_written),
        destination.subspan(offset, *cpa_size));
    if (!cpa_result.success() || cpa_result.bytes_written != *cpa_size) {
        return {cpa_result.status, 0U, required};
    }
    offset += cpa_result.bytes_written;

    if (offset != required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    return {wire::EncodeStatus::ok, required, required};
}

} // namespace ar::iec61850::acse
