// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/osi/session_span.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace ar::iec61850::osi {
namespace {

constexpr std::array<std::uint8_t, 6U> kParameter05{
    0x13U, 0x01U, 0x00U, 0x16U, 0x01U, 0x02U};
constexpr std::array<std::uint8_t, 2U> kParameter14{0x00U, 0x02U};
constexpr std::array<std::uint8_t, 2U> kParameter33{0x00U, 0x01U};
constexpr std::array<std::uint8_t, 2U> kParameter34{0x00U, 0x01U};

const std::array<SessionParameterView, 4U> kDefaultParameters{
    SessionParameterView{0x05U, kParameter05},
    SessionParameterView{0x14U, kParameter14},
    SessionParameterView{0x33U, kParameter33},
    SessionParameterView{0x34U, kParameter34},
};

[[nodiscard]] bool encoded_body_size(
    const std::span<const SessionParameterView> parameters,
    const std::span<const std::uint8_t> user_data,
    std::size_t& body_length) noexcept {
    body_length = 0U;
    if (parameters.size() > SessionSpanCodec::maximum_parameter_count ||
        user_data.size() > std::numeric_limits<std::uint8_t>::max()) {
        return false;
    }

    if (!user_data.empty()) {
        const auto encoded_user = user_data.size() + 2U;
        if (encoded_user > std::numeric_limits<std::uint8_t>::max()) {
            return false;
        }
        body_length = encoded_user;
    }
    for (const auto& parameter : parameters) {
        if (parameter.code == SessionSpanCodec::user_data_parameter ||
            parameter.value.size() > std::numeric_limits<std::uint8_t>::max()) {
            body_length = 0U;
            return false;
        }
        const auto encoded = parameter.value.size() + 2U;
        if (body_length > std::numeric_limits<std::uint8_t>::max() ||
            encoded > std::numeric_limits<std::uint8_t>::max() - body_length) {
            body_length = 0U;
            return false;
        }
        body_length += encoded;
    }
    return body_length <= std::numeric_limits<std::uint8_t>::max();
}

[[nodiscard]] bool write_parameter(
    const std::span<std::uint8_t> destination,
    std::size_t& offset,
    const std::uint8_t code,
    const std::span<const std::uint8_t> value) noexcept {
    if (offset > destination.size() ||
        value.size() > std::numeric_limits<std::uint8_t>::max() ||
        value.size() + 2U > destination.size() - offset) {
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

} // namespace

bool SessionSpduView::try_parameter(
    const std::uint8_t parameter_code,
    std::span<const std::uint8_t>& value) const noexcept {
    value = {};
    std::size_t offset = 0U;
    while (offset < parameter_bytes.size()) {
        if (parameter_bytes.size() - offset < 2U) {
            return false;
        }
        const auto code = parameter_bytes[offset];
        const auto length = static_cast<std::size_t>(parameter_bytes[offset + 1U]);
        offset += 2U;
        if (length > parameter_bytes.size() - offset) {
            return false;
        }
        if (code == parameter_code) {
            value = parameter_bytes.subspan(offset, length);
            return true;
        }
        offset += length;
    }
    return false;
}

SessionWireKind SessionSpanCodec::kind_from_code(const std::uint8_t code) noexcept {
    switch (code) {
    case connect_code:
        return SessionWireKind::connect;
    case accept_code:
        return SessionWireKind::accept;
    case reject_code:
        return SessionWireKind::reject;
    case refuse_code:
        return SessionWireKind::refuse;
    case abort_code:
        return SessionWireKind::abort;
    case data_transfer_code:
        return SessionWireKind::data_transfer;
    default:
        return SessionWireKind::unknown;
    }
}

std::span<const SessionParameterView> SessionSpanCodec::default_parameters() noexcept {
    return kDefaultParameters;
}

bool SessionSpanCodec::try_decode_prefix_view(
    const std::span<const std::uint8_t> bytes,
    SessionSpduView& spdu,
    std::size_t& consumed) noexcept {
    spdu = {};
    consumed = 0U;
    if (bytes.size() < 2U) {
        return false;
    }

    const auto kind = kind_from_code(bytes[0]);
    if (kind == SessionWireKind::unknown) {
        return false;
    }

    const auto body_length = static_cast<std::size_t>(bytes[1]);
    const auto total_length = body_length + 2U;
    if (total_length > maximum_spdu_bytes || total_length > bytes.size()) {
        return false;
    }

    std::size_t offset = 2U;
    std::size_t parameter_end = total_length;
    std::size_t parameter_count = 0U;
    bool saw_user_data = false;
    std::span<const std::uint8_t> user_data;

    while (offset < total_length) {
        if (parameter_count >= maximum_parameter_count ||
            total_length - offset < 2U) {
            return false;
        }
        ++parameter_count;
        const auto parameter_start = offset;
        const auto code = bytes[offset++];
        const auto length = static_cast<std::size_t>(bytes[offset++]);
        if (length > total_length - offset) {
            return false;
        }

        if (code == user_data_parameter) {
            if (saw_user_data || offset + length != total_length) {
                return false;
            }
            saw_user_data = true;
            parameter_end = parameter_start;
            user_data = bytes.subspan(offset, length);
        }
        offset += length;
    }

    spdu.kind = kind;
    spdu.code = bytes[0];
    spdu.length_indicator = bytes[1];
    spdu.parameter_bytes = bytes.subspan(2U, parameter_end - 2U);
    spdu.user_data = user_data;
    consumed = total_length;
    return true;
}

bool SessionSpanCodec::try_decode_view(
    const std::span<const std::uint8_t> bytes,
    SessionSpduView& spdu) noexcept {
    std::size_t consumed = 0U;
    return try_decode_prefix_view(bytes, spdu, consumed) && consumed == bytes.size();
}

wire::EncodeResult SessionSpanCodec::encode_into(
    const std::uint8_t code,
    const std::span<const SessionParameterView> parameters,
    const std::span<const std::uint8_t> user_data,
    const std::span<std::uint8_t> destination) noexcept {
    if (kind_from_code(code) == SessionWireKind::unknown) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    std::size_t body_length{};
    if (!encoded_body_size(parameters, user_data, body_length)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto required = body_length + 2U;
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }

    destination[0] = code;
    destination[1] = static_cast<std::uint8_t>(body_length);
    std::size_t offset = 2U;
    for (const auto& parameter : parameters) {
        if (!write_parameter(destination, offset, parameter.code, parameter.value)) {
            return {wire::EncodeStatus::value_out_of_range, 0U, required};
        }
    }
    if (!user_data.empty() &&
        !write_parameter(destination, offset, user_data_parameter, user_data)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    if (offset != required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    return {wire::EncodeStatus::ok, required, required};
}

wire::EncodeResult SessionSpanCodec::encode_connect_into(
    const std::span<const std::uint8_t> presentation_payload,
    const std::span<std::uint8_t> destination,
    const std::span<const SessionParameterView> parameters) noexcept {
    const auto effective = parameters.empty() ? default_parameters() : parameters;
    return encode_into(connect_code, effective, presentation_payload, destination);
}

wire::EncodeResult SessionSpanCodec::encode_accept_into(
    const std::span<const std::uint8_t> presentation_payload,
    const std::span<std::uint8_t> destination,
    const std::span<const SessionParameterView> parameters) noexcept {
    const auto effective = parameters.empty() ? default_parameters() : parameters;
    return encode_into(accept_code, effective, presentation_payload, destination);
}

wire::EncodeResult SessionSpanCodec::encode_accept_mirroring_into(
    const SessionSpduView& connect,
    const std::span<const std::uint8_t> presentation_payload,
    const std::span<std::uint8_t> destination) noexcept {
    if (connect.kind != SessionWireKind::connect ||
        presentation_payload.size() > std::numeric_limits<std::uint8_t>::max()) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    const auto user_bytes = presentation_payload.empty()
        ? 0U
        : presentation_payload.size() + 2U;
    if (user_bytes > std::numeric_limits<std::uint8_t>::max() ||
        connect.parameter_bytes.size() >
            std::numeric_limits<std::uint8_t>::max() - user_bytes) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto body_length = connect.parameter_bytes.size() + user_bytes;
    const auto required = body_length + 2U;
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }

    destination[0] = accept_code;
    destination[1] = static_cast<std::uint8_t>(body_length);
    std::copy(
        connect.parameter_bytes.begin(),
        connect.parameter_bytes.end(),
        destination.begin() + 2);
    std::size_t offset = 2U + connect.parameter_bytes.size();
    if (!presentation_payload.empty() &&
        !write_parameter(
            destination,
            offset,
            user_data_parameter,
            presentation_payload)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    return offset == required
        ? wire::EncodeResult{wire::EncodeStatus::ok, required, required}
        : wire::EncodeResult{wire::EncodeStatus::value_out_of_range, 0U, required};
}

wire::EncodeResult SessionSpanCodec::encode_data_transfer_into(
    const std::span<const std::uint8_t> presentation_payload,
    const std::span<std::uint8_t> destination,
    const bool include_give_tokens_prefix) noexcept {
    const auto prefix = include_give_tokens_prefix ? 4U : 2U;
    if (presentation_payload.size() >
        std::numeric_limits<std::size_t>::max() - prefix) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto required = presentation_payload.size() + prefix;
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }

    std::size_t offset = 0U;
    if (include_give_tokens_prefix) {
        destination[offset++] = data_transfer_code;
        destination[offset++] = 0x00U;
    }
    destination[offset++] = data_transfer_code;
    destination[offset++] = 0x00U;
    std::copy(
        presentation_payload.begin(),
        presentation_payload.end(),
        destination.begin() + static_cast<std::ptrdiff_t>(offset));
    return {wire::EncodeStatus::ok, required, required};
}

bool SessionSpanCodec::try_decode_data_transfer_view(
    const std::span<const std::uint8_t> bytes,
    SessionDataTransferView& transfer) noexcept {
    transfer = {};
    if (bytes.size() < 2U) {
        return false;
    }

    std::size_t offset = 0U;
    if (bytes.size() >= 4U &&
        bytes[0] == data_transfer_code && bytes[1] == 0x00U &&
        bytes[2] == data_transfer_code && bytes[3] == 0x00U) {
        transfer.has_give_tokens_prefix = true;
        offset = 4U;
    } else if (bytes[0] == data_transfer_code && bytes[1] == 0x00U) {
        transfer.has_give_tokens_prefix = false;
        offset = 2U;
    } else {
        return false;
    }

    transfer.presentation_payload = bytes.subspan(offset);
    return true;
}

} // namespace ar::iec61850::osi
