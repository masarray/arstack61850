// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/osi/cotp.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace ar::iec61850::osi {
namespace {

void set_error(std::string* error, std::string message) noexcept {
    if (error == nullptr) {
        return;
    }
    try {
        *error = std::move(message);
    } catch (...) {
    }
}

std::uint16_t read_be_u16(const std::span<const std::uint8_t> bytes, const std::size_t offset) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8U) |
        static_cast<std::uint16_t>(bytes[offset + 1U]));
}

void append_be_u16(std::vector<std::uint8_t>& bytes, const std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

std::vector<CotpParameter> parse_parameters(
    const std::span<const std::uint8_t> bytes,
    const std::size_t start,
    const std::size_t end) {
    std::vector<CotpParameter> parameters;
    auto offset = start;
    while (offset < end) {
        if (end - offset < 2U) {
            throw CotpFormatError("COTP parameter header is truncated.");
        }
        const auto code = bytes[offset];
        const auto length = static_cast<std::size_t>(bytes[offset + 1U]);
        offset += 2U;
        if (length > end - offset) {
            throw CotpFormatError("COTP parameter value exceeds the variable header.");
        }
        CotpParameter parameter;
        parameter.code = code;
        parameter.value.assign(
            bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
        parameters.push_back(std::move(parameter));
        offset += length;
    }
    return parameters;
}

std::size_t encoded_parameters_size(const std::span<const CotpParameter> parameters) {
    std::size_t total = 0U;
    for (const auto& parameter : parameters) {
        if (parameter.value.size() > 248U) {
            throw std::length_error("COTP parameter value exceeds the variable-header capacity.");
        }
        const auto encoded_size = 2U + parameter.value.size();
        if (encoded_size > 248U - total) {
            throw std::length_error("COTP variable header exceeds the length-indicator capacity.");
        }
        total += encoded_size;
    }
    return total;
}

void append_parameters(
    std::vector<std::uint8_t>& encoded,
    const std::span<const CotpParameter> parameters) {
    for (const auto& parameter : parameters) {
        encoded.push_back(parameter.code);
        encoded.push_back(static_cast<std::uint8_t>(parameter.value.size()));
        encoded.insert(encoded.end(), parameter.value.begin(), parameter.value.end());
    }
}

std::uint8_t selected_tpdu_size(
    const CotpTpdu& request,
    const std::uint8_t maximum_tpdu_size_code) {
    static_cast<void>(CotpFrameCodec::tpdu_size_bytes(maximum_tpdu_size_code));
    const auto offered = request.parameter(CotpFrameCodec::tpdu_size_parameter);
    if (!offered.has_value() || offered->size() != 1U) {
        return maximum_tpdu_size_code;
    }
    const auto value = (*offered)[0];
    static_cast<void>(CotpFrameCodec::tpdu_size_bytes(value));
    return std::min(value, maximum_tpdu_size_code);
}

std::vector<CotpParameter> connection_confirm_parameters(
    const CotpTpdu& request,
    const std::uint8_t maximum_tpdu_size_code) {
    std::vector<CotpParameter> result;
    result.push_back(CotpParameter{
        CotpFrameCodec::tpdu_size_parameter,
        {selected_tpdu_size(request, maximum_tpdu_size_code)}});

    for (const auto& parameter : request.parameters) {
        if (parameter.code == CotpFrameCodec::source_tsap_parameter &&
            !parameter.value.empty()) {
            result.push_back(CotpParameter{
                CotpFrameCodec::destination_tsap_parameter,
                parameter.value});
        } else if (parameter.code == CotpFrameCodec::destination_tsap_parameter &&
                   !parameter.value.empty()) {
            result.push_back(CotpParameter{
                CotpFrameCodec::source_tsap_parameter,
                parameter.value});
        }
    }
    return result;
}

std::vector<std::uint8_t> encode_reference_tpdu(
    const std::uint8_t code,
    const std::uint16_t destination_reference,
    const std::uint16_t source_reference,
    const std::uint8_t class_or_reason,
    const std::span<const CotpParameter> parameters) {
    const auto parameter_bytes = encoded_parameters_size(parameters);
    const auto length_indicator_size = 6U + parameter_bytes;
    if (length_indicator_size > 255U) {
        throw std::length_error("COTP header exceeds the one-byte length indicator.");
    }

    std::vector<std::uint8_t> encoded;
    encoded.reserve(length_indicator_size + 1U);
    encoded.push_back(static_cast<std::uint8_t>(length_indicator_size));
    encoded.push_back(code);
    append_be_u16(encoded, destination_reference);
    append_be_u16(encoded, source_reference);
    encoded.push_back(class_or_reason);
    append_parameters(encoded, parameters);
    return encoded;
}

} // namespace

std::optional<std::span<const std::uint8_t>> CotpTpdu::parameter(
    const std::uint8_t parameter_code) const noexcept {
    for (const auto& candidate : parameters) {
        if (candidate.code == parameter_code) {
            return std::span<const std::uint8_t>{candidate.value};
        }
    }
    return std::nullopt;
}

bool CotpFrameCodec::try_decode(
    const std::span<const std::uint8_t> bytes,
    CotpTpdu& tpdu,
    std::string* error) noexcept {
    try {
        tpdu = {};
        if (bytes.size() < 2U) {
            set_error(error, "COTP TPDU is shorter than LI and code octets.");
            return false;
        }

        const auto length_indicator = static_cast<std::size_t>(bytes[0]);
        const auto header_end = length_indicator + 1U;
        if (length_indicator < 2U || header_end > bytes.size()) {
            set_error(error, "COTP length indicator is invalid for the supplied TPDU.");
            return false;
        }

        tpdu.length_indicator = bytes[0];
        tpdu.code = bytes[1];
        tpdu.kind = kind_from_code(tpdu.code);
        if (tpdu.kind == CotpTpduKind::unknown) {
            set_error(error, "Unknown COTP TPDU code.");
            tpdu = {};
            return false;
        }

        if (tpdu.kind == CotpTpduKind::data) {
            if (length_indicator != 2U || header_end != 3U) {
                set_error(error, "COTP Data TPDU must use a two-octet length indicator.");
                tpdu = {};
                return false;
            }
            tpdu.end_of_transmission = (bytes[2] & 0x80U) != 0U;
            tpdu.tpdu_number = static_cast<std::uint8_t>(bytes[2] & 0x7FU);
            tpdu.user_data.assign(bytes.begin() + 3, bytes.end());
            if (error != nullptr) {
                error->clear();
            }
            return true;
        }

        if (length_indicator < 6U || bytes.size() < 7U) {
            set_error(error, "COTP reference TPDU is shorter than the fixed header.");
            tpdu = {};
            return false;
        }

        tpdu.destination_reference = read_be_u16(bytes, 2U);
        tpdu.source_reference = read_be_u16(bytes, 4U);
        tpdu.class_or_reason = bytes[6];
        tpdu.end_of_transmission = true;
        tpdu.parameters = parse_parameters(bytes, 7U, header_end);
        tpdu.user_data.assign(
            bytes.begin() + static_cast<std::ptrdiff_t>(header_end), bytes.end());
        if ((tpdu.kind == CotpTpduKind::connection_request ||
             tpdu.kind == CotpTpduKind::connection_confirm) &&
            !tpdu.user_data.empty()) {
            set_error(error, "COTP CR/CC contains unsupported trailing user data.");
            tpdu = {};
            return false;
        }
        if (error != nullptr) {
            error->clear();
        }
        return true;
    } catch (const std::exception& exception) {
        tpdu = {};
        set_error(error, exception.what());
        return false;
    } catch (...) {
        tpdu = {};
        set_error(error, "COTP decode failed unexpectedly.");
        return false;
    }
}

CotpTpdu CotpFrameCodec::decode(const std::span<const std::uint8_t> bytes) {
    CotpTpdu tpdu;
    std::string error;
    if (!try_decode(bytes, tpdu, &error)) {
        throw CotpFormatError(error);
    }
    return tpdu;
}

std::vector<std::uint8_t> CotpFrameCodec::encode_default_connection_request() {
    const std::array<CotpParameter, 3> parameters{
        CotpParameter{tpdu_size_parameter, {0x0AU}},
        CotpParameter{source_tsap_parameter, {0x00U, 0x01U}},
        CotpParameter{destination_tsap_parameter, {0x00U, 0x01U}},
    };
    return encode_connection_request(0x0001U, parameters);
}

std::vector<std::uint8_t> CotpFrameCodec::encode_connection_request(
    const std::uint16_t source_reference,
    const std::span<const CotpParameter> parameters,
    const std::uint16_t destination_reference,
    const std::uint8_t transport_class) {
    return encode_reference_tpdu(
        connection_request_code,
        destination_reference,
        source_reference,
        transport_class,
        parameters);
}

std::vector<std::uint8_t> CotpFrameCodec::encode_connection_confirm(
    const std::uint16_t destination_reference,
    const std::uint16_t source_reference,
    const std::uint8_t maximum_tpdu_size_code) {
    static_cast<void>(tpdu_size_bytes(maximum_tpdu_size_code));
    const std::array<CotpParameter, 1> parameters{
        CotpParameter{tpdu_size_parameter, {maximum_tpdu_size_code}},
    };
    return encode_reference_tpdu(
        connection_confirm_code,
        destination_reference,
        source_reference,
        0U,
        parameters);
}

std::vector<std::uint8_t> CotpFrameCodec::encode_connection_confirm(
    const CotpTpdu& connection_request,
    const std::uint16_t source_reference,
    const std::uint8_t maximum_tpdu_size_code) {
    if (connection_request.kind != CotpTpduKind::connection_request) {
        throw std::invalid_argument("COTP connection confirm requires a decoded connection request.");
    }
    const auto parameters = connection_confirm_parameters(
        connection_request,
        maximum_tpdu_size_code);
    return encode_reference_tpdu(
        connection_confirm_code,
        connection_request.source_reference,
        source_reference,
        0U,
        parameters);
}

std::vector<std::uint8_t> CotpFrameCodec::encode_data(
    const std::span<const std::uint8_t> user_data,
    const bool end_of_transmission,
    const std::uint8_t tpdu_number) {
    if (tpdu_number > 0x7FU) {
        throw std::invalid_argument("COTP TPDU number must fit in seven bits.");
    }
    if (user_data.size() > std::numeric_limits<std::size_t>::max() - 3U) {
        throw std::length_error("COTP Data payload size overflows the encoded frame length.");
    }
    std::vector<std::uint8_t> encoded(user_data.size() + 3U);
    encoded[0] = 0x02U;
    encoded[1] = data_code;
    encoded[2] = static_cast<std::uint8_t>(
        (end_of_transmission ? 0x80U : 0x00U) | tpdu_number);
    std::copy(user_data.begin(), user_data.end(), encoded.begin() + 3);
    return encoded;
}

std::vector<std::vector<std::uint8_t>> CotpFrameCodec::encode_data_segments(
    const std::span<const std::uint8_t> user_data,
    const std::uint8_t tpdu_size_code) {
    const auto capacity = tpdu_size_bytes(tpdu_size_code);
    if (capacity <= 3U) {
        throw std::invalid_argument("COTP TPDU capacity cannot hold a Data header.");
    }
    const auto maximum_user_data = capacity - 3U;
    if (user_data.empty()) {
        return {encode_data({})};
    }

    std::vector<std::vector<std::uint8_t>> segments;
    segments.reserve((user_data.size() + maximum_user_data - 1U) / maximum_user_data);
    std::size_t offset = 0U;
    std::uint8_t tpdu_number = 0U;
    while (offset < user_data.size()) {
        const auto length = std::min(maximum_user_data, user_data.size() - offset);
        const auto final = offset + length == user_data.size();
        segments.push_back(encode_data(user_data.subspan(offset, length), final, tpdu_number));
        offset += length;
        tpdu_number = static_cast<std::uint8_t>((tpdu_number + 1U) & 0x7FU);
    }
    return segments;
}

std::vector<std::uint8_t> CotpFrameCodec::encode_disconnect_request(
    const std::uint16_t destination_reference,
    const std::uint16_t source_reference,
    const std::uint8_t reason,
    const std::span<const CotpParameter> parameters) {
    return encode_reference_tpdu(
        disconnect_request_code,
        destination_reference,
        source_reference,
        reason,
        parameters);
}

std::size_t CotpFrameCodec::tpdu_size_bytes(const std::uint8_t tpdu_size_code) {
    if (tpdu_size_code < 0x07U || tpdu_size_code > 0x0FU) {
        throw std::invalid_argument("COTP TPDU size code must be in the range 0x07..0x0F.");
    }
    return static_cast<std::size_t>(1U) << tpdu_size_code;
}

CotpTpduKind CotpFrameCodec::kind_from_code(const std::uint8_t code) noexcept {
    switch (code) {
    case connection_request_code:
        return CotpTpduKind::connection_request;
    case connection_confirm_code:
        return CotpTpduKind::connection_confirm;
    case data_code:
        return CotpTpduKind::data;
    case disconnect_request_code:
        return CotpTpduKind::disconnect_request;
    case error_code:
        return CotpTpduKind::error;
    default:
        return CotpTpduKind::unknown;
    }
}

CotpDataReassembler::CotpDataReassembler(
    const std::size_t maximum_bytes,
    const std::size_t maximum_fragments,
    const std::size_t maximum_empty_nonfinal_fragments)
    : maximum_bytes_(maximum_bytes),
      maximum_fragments_(maximum_fragments),
      maximum_empty_nonfinal_fragments_(maximum_empty_nonfinal_fragments) {
    if (maximum_bytes_ == 0U || maximum_bytes_ > 512U * 1024U * 1024U) {
        throw std::invalid_argument("COTP reassembly byte limit is outside the supported range.");
    }
    if (maximum_fragments_ == 0U || maximum_fragments_ > 16U * 1024U * 1024U) {
        throw std::invalid_argument("COTP reassembly fragment limit is outside the supported range.");
    }
    if (maximum_empty_nonfinal_fragments_ == 0U ||
        maximum_empty_nonfinal_fragments_ > maximum_fragments_) {
        throw std::invalid_argument("COTP empty-fragment limit is invalid.");
    }
}

void CotpDataReassembler::append(const CotpTpdu& tpdu) {
    if (complete_) {
        throw CotpFormatError("COTP Data sequence is already complete.");
    }
    if (tpdu.kind != CotpTpduKind::data) {
        throw CotpFormatError("COTP reassembler accepts only Data TPDUs.");
    }
    if (fragment_count_ >= maximum_fragments_) {
        throw CotpFormatError("COTP Data sequence exceeded the fragment-count limit.");
    }
    ++fragment_count_;

    if (tpdu.user_data.empty() && !tpdu.end_of_transmission) {
        if (empty_nonfinal_fragments_ >= maximum_empty_nonfinal_fragments_) {
            throw CotpFormatError("COTP Data sequence exceeded the empty non-final fragment limit.");
        }
        ++empty_nonfinal_fragments_;
    }
    if (tpdu.user_data.size() > maximum_bytes_ - buffer_.size()) {
        throw CotpFormatError("COTP Data sequence exceeded the bounded reassembly size.");
    }
    buffer_.insert(buffer_.end(), tpdu.user_data.begin(), tpdu.user_data.end());
    complete_ = tpdu.end_of_transmission;
}

void CotpDataReassembler::append_encoded(
    const std::span<const std::uint8_t> encoded_tpdu) {
    append(CotpFrameCodec::decode(encoded_tpdu));
}

std::vector<std::uint8_t> CotpDataReassembler::complete() const {
    if (!complete_) {
        throw CotpFormatError("COTP Data sequence ended before EOT.");
    }
    return buffer_;
}

void CotpDataReassembler::reset() noexcept {
    fragment_count_ = 0U;
    empty_nonfinal_fragments_ = 0U;
    complete_ = false;
    buffer_.clear();
}

} // namespace ar::iec61850::osi
