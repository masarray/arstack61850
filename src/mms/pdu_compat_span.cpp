// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/pdu_span.hpp"

#include "ariec61850/asn1/ber_span_reader.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::mms {
namespace {

constexpr std::array<std::uint8_t, 40U> kIdentifyFields{
    0x80U, 0x0AU,
    'A','R','I','E','C','6','1','8','5','0',
    0x81U, 0x15U,
    'V','i','r','t','u','a','l',' ','I','E','D',' ','S','i','m','u','l','a','t','o','r',
    0x82U, 0x03U, '1','.','0'};

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
    // Preserve the current server's truthful failure semantics. The legacy
    // simulator used ConfirmedError here; current-lineage core has a bounded
    // RejectPDU primitive that is a better fit for unsupported/invalid probes.
    return encode_confirmed_request_reject_into(
        invoke_id,
        MmsConfirmedRequestRejectReason::unrecognized_service,
        destination);
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
