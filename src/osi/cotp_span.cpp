// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/osi/cotp_span.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace ar::iec61850::osi {
namespace {

[[nodiscard]] std::uint16_t read_be_u16(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8U) |
        static_cast<std::uint16_t>(bytes[offset + 1U]));
}

void write_be_u16(
    const std::span<std::uint8_t> bytes,
    const std::size_t offset,
    const std::uint16_t value) noexcept {
    bytes[offset] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value & 0xFFU);
}

[[nodiscard]] bool encoded_parameters_size(
    const std::span<const CotpParameterView> parameters,
    std::size_t& total) noexcept {
    total = 0U;
    for (const auto& parameter : parameters) {
        if (parameter.value.size() > 248U) {
            total = 0U;
            return false;
        }
        const auto encoded = 2U + parameter.value.size();
        if (encoded > 248U - total) {
            total = 0U;
            return false;
        }
        total += encoded;
    }
    return true;
}

[[nodiscard]] bool write_parameter(
    const std::span<std::uint8_t> destination,
    std::size_t& offset,
    const std::uint8_t code,
    const std::span<const std::uint8_t> value) noexcept {
    if (value.size() > 248U || destination.size() - offset < value.size() + 2U) {
        return false;
    }
    destination[offset++] = code;
    destination[offset++] = static_cast<std::uint8_t>(value.size());
    std::copy(
        value.begin(),
        value.end(),
        destination.begin() + static_cast<std::ptrdiff_t>(offset));
    offset += value.size();
    return true;
}

[[nodiscard]] wire::EncodeResult encode_reference_into(
    const std::uint8_t code,
    const std::uint16_t destination_reference,
    const std::uint16_t source_reference,
    const std::uint8_t class_or_reason,
    const std::span<const CotpParameterView> parameters,
    const std::span<std::uint8_t> destination) noexcept {
    std::size_t parameter_bytes{};
    if (!encoded_parameters_size(parameters, parameter_bytes)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    const auto length_indicator = 6U + parameter_bytes;
    const auto required = length_indicator + 1U;
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }

    destination[0] = static_cast<std::uint8_t>(length_indicator);
    destination[1] = code;
    write_be_u16(destination, 2U, destination_reference);
    write_be_u16(destination, 4U, source_reference);
    destination[6] = class_or_reason;

    std::size_t offset = 7U;
    for (const auto& parameter : parameters) {
        if (!write_parameter(destination, offset, parameter.code, parameter.value)) {
            return {wire::EncodeStatus::value_out_of_range, 0U, required};
        }
    }
    if (offset != required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] bool validate_parameter_bytes(
    const std::span<const std::uint8_t> bytes) noexcept {
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        if (bytes.size() - offset < 2U) {
            return false;
        }
        const auto length = static_cast<std::size_t>(bytes[offset + 1U]);
        offset += 2U;
        if (length > bytes.size() - offset) {
            return false;
        }
        offset += length;
    }
    return offset == bytes.size();
}

[[nodiscard]] bool selected_tpdu_size(
    const CotpTpduView& request,
    const std::uint8_t maximum_tpdu_size_code,
    std::uint8_t& selected) noexcept {
    std::size_t ignored{};
    if (!CotpSpanCodec::try_tpdu_size_bytes(maximum_tpdu_size_code, ignored)) {
        return false;
    }

    std::span<const std::uint8_t> offered;
    if (!request.try_parameter(CotpSpanCodec::tpdu_size_parameter, offered) ||
        offered.size() != 1U) {
        selected = maximum_tpdu_size_code;
        return true;
    }

    if (!CotpSpanCodec::try_tpdu_size_bytes(offered[0], ignored)) {
        return false;
    }
    selected = std::min(offered[0], maximum_tpdu_size_code);
    return true;
}

[[nodiscard]] bool mirrored_parameter_size(
    const CotpTpduView& request,
    std::size_t& total) noexcept {
    // The CC always starts with C0 / TPDU size (3 encoded bytes).
    total = 3U;
    std::size_t offset = 0U;
    while (offset < request.parameter_bytes.size()) {
        if (request.parameter_bytes.size() - offset < 2U) {
            total = 0U;
            return false;
        }
        const auto code = request.parameter_bytes[offset];
        const auto length = static_cast<std::size_t>(request.parameter_bytes[offset + 1U]);
        offset += 2U;
        if (length > request.parameter_bytes.size() - offset) {
            total = 0U;
            return false;
        }

        if ((code == CotpSpanCodec::source_tsap_parameter ||
             code == CotpSpanCodec::destination_tsap_parameter) &&
            length != 0U) {
            const auto encoded = length + 2U;
            if (length > 248U || encoded > 248U - total) {
                total = 0U;
                return false;
            }
            total += encoded;
        }
        offset += length;
    }
    return true;
}

} // namespace

bool CotpTpduView::try_parameter(
    const std::uint8_t parameter_code,
    std::span<const std::uint8_t>& value) const noexcept {
    value = {};
    std::size_t offset = 0U;
    while (offset < parameter_bytes.size()) {
        if (parameter_bytes.size() - offset < 2U) {
            return false;
        }
        const auto current_code = parameter_bytes[offset];
        const auto length = static_cast<std::size_t>(parameter_bytes[offset + 1U]);
        offset += 2U;
        if (length > parameter_bytes.size() - offset) {
            return false;
        }
        if (current_code == parameter_code) {
            value = parameter_bytes.subspan(offset, length);
            return true;
        }
        offset += length;
    }
    return false;
}

CotpWireKind CotpSpanCodec::kind_from_code(const std::uint8_t code) noexcept {
    switch (code) {
    case connection_request_code:
        return CotpWireKind::connection_request;
    case connection_confirm_code:
        return CotpWireKind::connection_confirm;
    case data_code:
        return CotpWireKind::data;
    case disconnect_request_code:
        return CotpWireKind::disconnect_request;
    case error_code:
        return CotpWireKind::error;
    default:
        return CotpWireKind::unknown;
    }
}

bool CotpSpanCodec::try_decode_view(
    const std::span<const std::uint8_t> bytes,
    CotpTpduView& tpdu) noexcept {
    tpdu = {};
    if (bytes.size() < 2U) {
        return false;
    }

    const auto length_indicator = static_cast<std::size_t>(bytes[0]);
    const auto header_end = length_indicator + 1U;
    if (length_indicator < 2U || header_end > bytes.size()) {
        return false;
    }

    const auto kind = kind_from_code(bytes[1]);
    if (kind == CotpWireKind::unknown) {
        return false;
    }

    tpdu.code = bytes[1];
    tpdu.kind = kind;
    tpdu.length_indicator = bytes[0];

    if (kind == CotpWireKind::data) {
        if (length_indicator != 2U || header_end != 3U) {
            tpdu = {};
            return false;
        }
        tpdu.end_of_transmission = (bytes[2] & 0x80U) != 0U;
        tpdu.tpdu_number = static_cast<std::uint8_t>(bytes[2] & 0x7FU);
        tpdu.user_data = bytes.subspan(3U);
        return true;
    }

    if (length_indicator < 6U || bytes.size() < 7U) {
        tpdu = {};
        return false;
    }

    tpdu.destination_reference = read_be_u16(bytes, 2U);
    tpdu.source_reference = read_be_u16(bytes, 4U);
    tpdu.class_or_reason = bytes[6];
    tpdu.end_of_transmission = true;
    tpdu.parameter_bytes = bytes.subspan(7U, header_end - 7U);
    tpdu.user_data = bytes.subspan(header_end);

    if (!validate_parameter_bytes(tpdu.parameter_bytes) ||
        ((kind == CotpWireKind::connection_request ||
          kind == CotpWireKind::connection_confirm) &&
         !tpdu.user_data.empty())) {
        tpdu = {};
        return false;
    }
    return true;
}

bool CotpSpanCodec::try_tpdu_size_bytes(
    const std::uint8_t tpdu_size_code,
    std::size_t& bytes) noexcept {
    bytes = 0U;
    if (tpdu_size_code < 0x07U || tpdu_size_code > 0x0FU) {
        return false;
    }
    bytes = static_cast<std::size_t>(1U) << tpdu_size_code;
    return true;
}

wire::EncodeResult CotpSpanCodec::encode_data_into(
    const std::span<const std::uint8_t> user_data,
    const std::span<std::uint8_t> destination,
    const bool end_of_transmission,
    const std::uint8_t tpdu_number) noexcept {
    if (tpdu_number > 0x7FU ||
        user_data.size() > std::numeric_limits<std::size_t>::max() - 3U) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    const auto required = user_data.size() + 3U;
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }

    destination[0] = 0x02U;
    destination[1] = data_code;
    destination[2] = static_cast<std::uint8_t>(
        (end_of_transmission ? 0x80U : 0x00U) | tpdu_number);
    std::copy(
        user_data.begin(),
        user_data.end(),
        destination.begin() + 3);
    return {wire::EncodeStatus::ok, required, required};
}

wire::EncodeResult CotpSpanCodec::encode_connection_request_into(
    const std::uint16_t source_reference,
    const std::span<const CotpParameterView> parameters,
    const std::span<std::uint8_t> destination,
    const std::uint16_t destination_reference,
    const std::uint8_t transport_class) noexcept {
    return encode_reference_into(
        connection_request_code,
        destination_reference,
        source_reference,
        transport_class,
        parameters,
        destination);
}

wire::EncodeResult CotpSpanCodec::encode_connection_confirm_into(
    const std::uint16_t destination_reference,
    const std::uint16_t source_reference,
    const std::span<std::uint8_t> destination,
    const std::uint8_t maximum_tpdu_size_code) noexcept {
    std::size_t ignored{};
    if (!try_tpdu_size_bytes(maximum_tpdu_size_code, ignored)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    const std::uint8_t value = maximum_tpdu_size_code;
    const CotpParameterView parameter{
        tpdu_size_parameter,
        std::span<const std::uint8_t>{&value, 1U}};
    return encode_reference_into(
        connection_confirm_code,
        destination_reference,
        source_reference,
        0U,
        std::span<const CotpParameterView>{&parameter, 1U},
        destination);
}

wire::EncodeResult CotpSpanCodec::encode_connection_confirm_from_request_into(
    const CotpTpduView& connection_request,
    const std::uint16_t source_reference,
    const std::span<std::uint8_t> destination,
    const std::uint8_t maximum_tpdu_size_code) noexcept {
    if (connection_request.kind != CotpWireKind::connection_request ||
        !connection_request.user_data.empty()) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    std::uint8_t selected{};
    std::size_t parameter_bytes{};
    if (!selected_tpdu_size(
            connection_request, maximum_tpdu_size_code, selected) ||
        !mirrored_parameter_size(connection_request, parameter_bytes)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    const auto length_indicator = 6U + parameter_bytes;
    const auto required = length_indicator + 1U;
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }

    destination[0] = static_cast<std::uint8_t>(length_indicator);
    destination[1] = connection_confirm_code;
    write_be_u16(destination, 2U, connection_request.source_reference);
    write_be_u16(destination, 4U, source_reference);
    destination[6] = 0U;

    std::size_t output_offset = 7U;
    const std::span<const std::uint8_t> selected_span{&selected, 1U};
    if (!write_parameter(
            destination,
            output_offset,
            tpdu_size_parameter,
            selected_span)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }

    std::size_t input_offset = 0U;
    while (input_offset < connection_request.parameter_bytes.size()) {
        if (connection_request.parameter_bytes.size() - input_offset < 2U) {
            return {wire::EncodeStatus::value_out_of_range, 0U, required};
        }
        const auto code = connection_request.parameter_bytes[input_offset];
        const auto length = static_cast<std::size_t>(
            connection_request.parameter_bytes[input_offset + 1U]);
        input_offset += 2U;
        if (length > connection_request.parameter_bytes.size() - input_offset) {
            return {wire::EncodeStatus::value_out_of_range, 0U, required};
        }
        const auto value = connection_request.parameter_bytes.subspan(
            input_offset, length);

        if (code == source_tsap_parameter && !value.empty()) {
            if (!write_parameter(
                    destination,
                    output_offset,
                    destination_tsap_parameter,
                    value)) {
                return {wire::EncodeStatus::value_out_of_range, 0U, required};
            }
        } else if (code == destination_tsap_parameter && !value.empty()) {
            if (!write_parameter(
                    destination,
                    output_offset,
                    source_tsap_parameter,
                    value)) {
                return {wire::EncodeStatus::value_out_of_range, 0U, required};
            }
        }
        input_offset += length;
    }

    if (output_offset != required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    return {wire::EncodeStatus::ok, required, required};
}

} // namespace ar::iec61850::osi
