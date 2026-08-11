// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/pdu_span.hpp"

#include "ariec61850/asn1/ber_span_reader.hpp"
#include "ariec61850/asn1/ber_span_writer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace ar::iec61850::mms {
namespace {

constexpr std::array<std::uint8_t, 40U> kDefaultInitiateResponse{
    0xA9U, 0x26U,
    0x80U, 0x03U, 0x00U, 0xFDU, 0xE8U,
    0x81U, 0x01U, 0x0AU,
    0x82U, 0x01U, 0x0AU,
    0x83U, 0x01U, 0x05U,
    0xA4U, 0x16U,
    0x80U, 0x01U, 0x01U,
    0x81U, 0x03U, 0x05U, 0xF1U, 0x00U,
    0x82U, 0x0CU, 0x03U, 0xEEU, 0x1CU, 0x00U, 0x00U, 0x04U,
    0x08U, 0x00U, 0x00U, 0x79U, 0xEFU, 0x18U};

constexpr std::array<std::uint8_t, 40U> kIdentifyFields{
    0x80U, 0x0AU,
    'A','R','I','E','C','6','1','8','5','0',
    0x81U, 0x15U,
    'V','i','r','t','u','a','l',' ','I','E','D',' ','S','i','m','u','l','a','t','o','r',
    0x82U, 0x03U, '1','.','0'};

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

[[nodiscard]] bool decode_detail(
    const asn1::BerTlvView& field,
    MmsInitiateDetailView& detail) noexcept {
    detail = {};
    if (field.tag_class != asn1::BerClass::context_specific ||
        field.tag_number != 4 || !field.constructed) {
        return false;
    }

    std::array<bool, 3U> seen{};
    std::size_t count = 0U;
    std::size_t offset = 0U;
    while (offset < field.value.size()) {
        if (count >= 3U) {
            detail = {};
            return false;
        }
        ++count;
        asn1::BerTlvView child;
        if (!asn1::BerSpanReader::try_read_tlv(field.value, offset, child) ||
            child.tag_class != asn1::BerClass::context_specific ||
            child.constructed || child.tag_number < 0 || child.tag_number > 2) {
            detail = {};
            return false;
        }
        const auto index = static_cast<std::size_t>(child.tag_number);
        if (seen[index]) {
            detail = {};
            return false;
        }
        seen[index] = true;

        if (index == 0U) {
            if (!read_u32(child, detail.version_number)) {
                detail = {};
                return false;
            }
        } else if (index == 1U) {
            if (child.value.empty() || child.value.size() >
                MmsPduSpanCodec::maximum_bit_string_bytes) {
                detail = {};
                return false;
            }
            detail.parameter_support_options = child.value;
        } else {
            if (child.value.empty() || child.value.size() >
                MmsPduSpanCodec::maximum_bit_string_bytes) {
                detail = {};
                return false;
            }
            detail.services_supported_calling = child.value;
        }
    }
    return count == 3U && seen[0] && seen[1] && seen[2];
}

[[nodiscard]] bool decode_initiate(
    const std::span<const std::uint8_t> bytes,
    const std::int32_t expected_tag,
    const MmsWirePduKind kind,
    MmsInitiateView& result) noexcept {
    result = {};
    if (bytes.empty() || bytes.size() > MmsPduSpanCodec::maximum_pdu_bytes) {
        return false;
    }

    asn1::BerTlvView outer;
    if (!asn1::BerSpanReader::try_read_exact(bytes, outer) ||
        outer.tag_class != asn1::BerClass::context_specific ||
        outer.tag_number != expected_tag || !outer.constructed) {
        return false;
    }

    std::array<bool, 5U> seen{};
    std::array<std::uint32_t, 4U> values{};
    MmsInitiateDetailView detail;
    std::size_t count = 0U;
    std::size_t offset = 0U;
    while (offset < outer.value.size()) {
        if (count >= 5U) {
            result = {};
            return false;
        }
        ++count;
        asn1::BerTlvView child;
        if (!asn1::BerSpanReader::try_read_tlv(outer.value, offset, child) ||
            child.tag_class != asn1::BerClass::context_specific ||
            child.tag_number < 0 || child.tag_number > 4) {
            result = {};
            return false;
        }
        const auto index = static_cast<std::size_t>(child.tag_number);
        if (seen[index]) {
            result = {};
            return false;
        }
        seen[index] = true;

        if (index < 4U) {
            if (child.constructed || !read_u32(child, values[index])) {
                result = {};
                return false;
            }
        } else if (!decode_detail(child, detail)) {
            result = {};
            return false;
        }
    }

    // ISO 9506 InitiateRequest permits localDetailCalling [0] and
    // proposedDataStructureNestingLevel [3] to be absent. The original
    // ARIEC61850 server accepted such request profiles and several engineering
    // clients rely on that tolerance. Keep InitiateResponse decoding strict,
    // but do not turn standards-valid optional omissions into a protocol fault.
    const bool is_request = kind == MmsWirePduKind::initiate_request;
    const bool mandatory_present = seen[1] && seen[2] && seen[4];
    const bool response_complete = seen[0] && seen[1] && seen[2] && seen[3] && seen[4];
    if (!mandatory_present || (!is_request && !response_complete) ||
        (seen[0] && (values[0] < 64U || values[0] > MmsPduSpanCodec::maximum_pdu_bytes)) ||
        values[1] == 0U || values[2] == 0U ||
        (seen[3] && values[3] == 0U)) {
        result = {};
        return false;
    }

    result.kind = kind;
    result.maximum_mms_pdu_size = seen[0]
        ? values[0]
        : static_cast<std::uint32_t>(MmsPduSpanCodec::maximum_pdu_bytes);
    result.maximum_outstanding_calling = values[1];
    result.maximum_outstanding_called = values[2];
    result.data_structure_nesting_level = seen[3] ? values[3] : 5U;
    result.detail = detail;
    return true;
}

[[nodiscard]] bool decode_invoke(
    const asn1::BerTlvView& tlv,
    std::uint32_t& invoke_id) noexcept {
    const bool universal_integer =
        tlv.tag_class == asn1::BerClass::universal &&
        tlv.tag_number == 2 && !tlv.constructed;
    const bool implicit_invoke =
        tlv.tag_class == asn1::BerClass::context_specific &&
        tlv.tag_number == 0 && !tlv.constructed;
    if ((!universal_integer && !implicit_invoke) || !read_u32(tlv, invoke_id) ||
        invoke_id > MmsPduSpanCodec::maximum_invoke_id) {
        invoke_id = 0U;
        return false;
    }
    return true;
}

[[nodiscard]] bool decode_confirmed(
    const std::span<const std::uint8_t> bytes,
    const std::int32_t expected_outer_tag,
    const MmsWirePduKind kind,
    MmsConfirmedPduView& result) noexcept {
    result = {};
    if (bytes.empty() || bytes.size() > MmsPduSpanCodec::maximum_pdu_bytes) {
        return false;
    }

    asn1::BerTlvView outer;
    if (!asn1::BerSpanReader::try_read_exact(bytes, outer) ||
        outer.tag_class != asn1::BerClass::context_specific ||
        outer.tag_number != expected_outer_tag || !outer.constructed) {
        return false;
    }

    asn1::BerTlvView first;
    asn1::BerTlvView last;
    std::size_t count = 0U;
    std::size_t offset = 0U;
    while (offset < outer.value.size()) {
        if (count >= 4U) {
            result = {};
            return false;
        }
        asn1::BerTlvView child;
        if (!asn1::BerSpanReader::try_read_tlv(outer.value, offset, child)) {
            result = {};
            return false;
        }
        if (count == 0U) {
            first = child;
        }
        last = child;
        ++count;
    }

    if (count < 2U || !decode_invoke(first, result.invoke_id) ||
        last.tag_class != asn1::BerClass::context_specific ||
        last.tag_number < 0) {
        result = {};
        return false;
    }

    result.kind = kind;
    result.service_tag = last.tag_number;
    result.service_constructed = last.constructed;
    result.service_value = last.value;
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
        if (!writer.write_byte(static_cast<std::uint8_t>(
                (value >> shift) & 0xFFU))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] wire::EncodeResult encode_confirmed(
    const std::int32_t outer_tag,
    const std::uint32_t invoke_id,
    const std::int32_t service_tag,
    const bool service_constructed,
    const std::span<const std::uint8_t> service_value,
    const std::span<std::uint8_t> destination) noexcept {
    if (invoke_id > MmsPduSpanCodec::maximum_invoke_id ||
        service_tag < 0 || service_tag > 1'000'000 ||
        service_value.size() > MmsPduSpanCodec::maximum_pdu_bytes) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    const auto invoke_value = positive_integer_size(invoke_id);
    const auto invoke_tlv = asn1::BerSpanWriter::tlv_size(2, invoke_value);
    const auto service_tlv = asn1::BerSpanWriter::tlv_size(
        service_tag, service_value.size());
    if (!invoke_tlv || !service_tlv ||
        *service_tlv > std::numeric_limits<std::size_t>::max() - *invoke_tlv) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto content = *invoke_tlv + *service_tlv;
    const auto required = asn1::BerSpanWriter::tlv_size(outer_tag, content);
    if (!required || *required > MmsPduSpanCodec::maximum_pdu_bytes) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (destination.size() < *required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, *required};
    }

    asn1::BerSpanWriter writer{destination.first(*required)};
    if (!writer.write_tlv_header(
            asn1::BerClass::context_specific, true, outer_tag, content) ||
        !writer.write_tlv_header(
            asn1::BerClass::universal, false, 2, invoke_value) ||
        !write_positive_integer(writer, invoke_id) ||
        !writer.write_tlv(
            asn1::BerClass::context_specific,
            service_constructed,
            service_tag,
            service_value) ||
        writer.size() != *required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }
    return {wire::EncodeStatus::ok, *required, *required};
}

[[nodiscard]] wire::EncodeResult encode_confirmed_error(
    const std::uint32_t invoke_id,
    const std::span<std::uint8_t> destination) noexcept {
    if (invoke_id > MmsPduSpanCodec::maximum_invoke_id) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    // Confirmed-ErrorPDU [2] { invokeID [0], serviceError [2]
    //   { errorClass [0] { service [12] = 0 } } }.
    const auto invoke_value = positive_integer_size(invoke_id);
    const auto invoke_tlv = asn1::BerSpanWriter::tlv_size(0, invoke_value);
    const auto class_choice_tlv = asn1::BerSpanWriter::tlv_size(12, 1U);
    if (!invoke_tlv || !class_choice_tlv) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto error_class_tlv = asn1::BerSpanWriter::tlv_size(0, *class_choice_tlv);
    if (!error_class_tlv) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto service_error_tlv = asn1::BerSpanWriter::tlv_size(2, *error_class_tlv);
    if (!service_error_tlv ||
        *invoke_tlv > std::numeric_limits<std::size_t>::max() - *service_error_tlv) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto outer_content = *invoke_tlv + *service_error_tlv;
    const auto required = asn1::BerSpanWriter::tlv_size(2, outer_content);
    if (!required || *required > MmsPduSpanCodec::maximum_pdu_bytes) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (destination.size() < *required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, *required};
    }

    asn1::BerSpanWriter writer{destination.first(*required)};
    if (!writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 2, outer_content) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, false, 0, invoke_value) ||
        !write_positive_integer(writer, invoke_id) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 2, *error_class_tlv) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 0, *class_choice_tlv) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, false, 12, 1U) ||
        !writer.write_byte(0x00U) || writer.size() != *required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }
    return {wire::EncodeStatus::ok, *required, *required};
}

} // namespace

MmsWireConfirmedService MmsConfirmedPduView::service() const noexcept {
    switch (service_tag) {
    case 1: return MmsWireConfirmedService::get_name_list;
    case 2: return MmsWireConfirmedService::identify;
    case 4: return MmsWireConfirmedService::read;
    case 5: return MmsWireConfirmedService::write;
    case 6: return MmsWireConfirmedService::get_variable_access_attributes;
    case 12: return MmsWireConfirmedService::get_named_variable_list_attributes;
    case 77: return MmsWireConfirmedService::file_directory;
    default: return MmsWireConfirmedService::unknown;
    }
}

bool MmsPduSpanCodec::try_decode_initiate_request_view(
    const std::span<const std::uint8_t> bytes,
    MmsInitiateView& request) noexcept {
    return decode_initiate(bytes, 8, MmsWirePduKind::initiate_request, request);
}

bool MmsPduSpanCodec::try_decode_initiate_response_view(
    const std::span<const std::uint8_t> bytes,
    MmsInitiateView& response) noexcept {
    return decode_initiate(bytes, 9, MmsWirePduKind::initiate_response, response);
}

wire::EncodeResult MmsPduSpanCodec::encode_default_initiate_response_into(
    const std::span<std::uint8_t> destination) noexcept {
    if (destination.size() < kDefaultInitiateResponse.size()) {
        return {
            wire::EncodeStatus::buffer_too_small,
            0U,
            kDefaultInitiateResponse.size()};
    }
    std::copy(
        kDefaultInitiateResponse.begin(),
        kDefaultInitiateResponse.end(),
        destination.begin());
    return {
        wire::EncodeStatus::ok,
        kDefaultInitiateResponse.size(),
        kDefaultInitiateResponse.size()};
}

bool MmsPduSpanCodec::try_decode_confirmed_request_view(
    const std::span<const std::uint8_t> bytes,
    MmsConfirmedPduView& request) noexcept {
    return decode_confirmed(bytes, 0, MmsWirePduKind::confirmed_request, request);
}

bool MmsPduSpanCodec::try_decode_confirmed_response_view(
    const std::span<const std::uint8_t> bytes,
    MmsConfirmedPduView& response) noexcept {
    return decode_confirmed(bytes, 1, MmsWirePduKind::confirmed_response, response);
}

wire::EncodeResult MmsPduSpanCodec::encode_confirmed_request_into(
    const std::uint32_t invoke_id,
    const std::int32_t service_tag,
    const bool service_constructed,
    const std::span<const std::uint8_t> service_value,
    const std::span<std::uint8_t> destination) noexcept {
    return encode_confirmed(
        0, invoke_id, service_tag, service_constructed, service_value, destination);
}

wire::EncodeResult MmsPduSpanCodec::encode_confirmed_response_into(
    const std::uint32_t invoke_id,
    const std::int32_t service_tag,
    const bool service_constructed,
    const std::span<const std::uint8_t> service_value,
    const std::span<std::uint8_t> destination) noexcept {
    return encode_confirmed(
        1, invoke_id, service_tag, service_constructed, service_value, destination);
}

wire::EncodeResult MmsPduSpanCodec::encode_identify_response_into(
    const std::uint32_t invoke_id,
    const std::span<std::uint8_t> destination) noexcept {
    return encode_confirmed(1, invoke_id, 2, true, kIdentifyFields, destination);
}

wire::EncodeResult MmsPduSpanCodec::encode_confirmed_error_into(
    const std::uint32_t invoke_id,
    const std::span<std::uint8_t> destination) noexcept {
    return encode_confirmed_error(invoke_id, destination);
}

bool MmsPduSpanCodec::is_conclude_request(
    const std::span<const std::uint8_t> bytes) noexcept {
    asn1::BerTlvView pdu;
    return asn1::BerSpanReader::try_read_exact(bytes, pdu) &&
        pdu.tag_class == asn1::BerClass::context_specific &&
        pdu.tag_number == 11 && !pdu.constructed && pdu.value.empty();
}

wire::EncodeResult MmsPduSpanCodec::encode_conclude_response_into(
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 2U;
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    destination[0] = 0x8CU;
    destination[1] = 0x00U;
    return {wire::EncodeStatus::ok, required, required};
}

} // namespace ar::iec61850::mms
