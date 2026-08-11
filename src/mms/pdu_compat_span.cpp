// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/pdu_span.hpp"

#include "ariec61850/asn1/ber_span_reader.hpp"
#include "ariec61850/asn1/ber_span_writer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace ar::iec61850::mms {
namespace {

constexpr std::array<std::uint8_t, 40U> kIdentifyFields{
    0x80U, 0x0AU,
    'A','R','I','E','C','6','1','8','5','0',
    0x81U, 0x15U,
    'V','i','r','t','u','a','l',' ','I','E','D',' ','S','i','m','u','l','a','t','o','r',
    0x82U, 0x03U, '1','.','0'};

[[nodiscard]] std::size_t positive_integer_size(const std::uint32_t value) noexcept {
    std::size_t bytes = 1U;
    auto remaining = value;
    while (remaining > 0xFFU) {
        ++bytes;
        remaining >>= 8U;
    }
    const auto leading = static_cast<std::uint8_t>(
        value >> static_cast<unsigned>((bytes - 1U) * 8U));
    return bytes + ((leading & 0x80U) != 0U ? 1U : 0U);
}

[[nodiscard]] bool write_positive_integer(
    asn1::BerSpanWriter& writer,
    const std::uint32_t value,
    const std::size_t bytes) noexcept {
    const auto encoded_bytes = bytes -
        ((bytes > 1U && (value >> static_cast<unsigned>((bytes - 2U) * 8U)) <= 0xFFU)
            ? 1U
            : 0U);
    if (encoded_bytes < bytes && !writer.write_byte(0U)) {
        return false;
    }
    for (std::size_t index = encoded_bytes; index > 0U; --index) {
        const auto shift = static_cast<unsigned>((index - 1U) * 8U);
        if (!writer.write_byte(static_cast<std::uint8_t>((value >> shift) & 0xFFU))) {
            return false;
        }
    }
    return true;
}

} // namespace

wire::EncodeResult MmsPduSpanCodec::encode_identify_response_into(
    const std::uint32_t invoke_id,
    const std::span<std::uint8_t> destination) noexcept {
    return encode_confirmed_response_into(
        invoke_id,
        static_cast<std::int32_t>(MmsWireConfirmedService::identify),
        true,
        kIdentifyFields,
        destination);
}

wire::EncodeResult MmsPduSpanCodec::encode_confirmed_error_into(
    const std::uint32_t invoke_id,
    const std::span<std::uint8_t> destination) noexcept {
    if (invoke_id > maximum_invoke_id) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    // ConfirmedErrorPDU ::= [2] { invokeID [0], serviceError [2] }
    // serviceError.errorClass uses access[7] object-non-existent(2).  This is
    // the interoperable outcome for optional IEC 61850 objects such as Cancel:
    // the request was syntactically valid, but the named service object is not
    // present.  A RejectPDU would incorrectly classify the request itself as
    // malformed and abort otherwise valid control discovery clients.
    const auto invoke_bytes = positive_integer_size(invoke_id);
    const auto invoke_tlv = asn1::BerSpanWriter::tlv_size(0, invoke_bytes);
    const auto access_tlv = asn1::BerSpanWriter::tlv_size(7, 1U);
    if (!invoke_tlv || !access_tlv) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto error_class_tlv = asn1::BerSpanWriter::tlv_size(0, *access_tlv);
    if (!error_class_tlv) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto service_error_tlv = asn1::BerSpanWriter::tlv_size(2, *error_class_tlv);
    if (!service_error_tlv ||
        *invoke_tlv > std::numeric_limits<std::size_t>::max() - *service_error_tlv) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto content = *invoke_tlv + *service_error_tlv;
    const auto required = asn1::BerSpanWriter::tlv_size(2, content);
    if (!required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (destination.size() < *required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, *required};
    }

    asn1::BerSpanWriter writer{destination.first(*required)};
    if (!writer.write_tlv_header(asn1::BerClass::context_specific, true, 2, content) ||
        !writer.write_tlv_header(asn1::BerClass::context_specific, false, 0, invoke_bytes) ||
        !write_positive_integer(writer, invoke_id, invoke_bytes) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 2, *error_class_tlv) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 0, *access_tlv) ||
        !writer.write_tlv_header(asn1::BerClass::context_specific, false, 7, 1U) ||
        !writer.write_byte(2U) || writer.size() != *required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }
    return {wire::EncodeStatus::ok, *required, *required};
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
