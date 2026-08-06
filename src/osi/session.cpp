// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/osi/session.hpp"

#include <algorithm>
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

std::vector<SessionParameter> effective_parameters(
    const std::span<const SessionParameter> parameters) {
    if (!parameters.empty()) {
        return {parameters.begin(), parameters.end()};
    }
    return SessionCodec::default_parameters();
}

} // namespace

std::optional<std::span<const std::uint8_t>> SessionSpdu::parameter(
    const std::uint8_t parameter_code) const noexcept {
    const auto found = std::find_if(parameters.begin(), parameters.end(),
        [parameter_code](const SessionParameter& parameter_value) {
            return parameter_value.code == parameter_code;
        });
    if (found == parameters.end()) {
        return std::nullopt;
    }
    return std::span<const std::uint8_t>{found->value};
}

SessionSpduKind SessionCodec::kind_from_code(const std::uint8_t code) noexcept {
    switch (code) {
    case connect_code:
        return SessionSpduKind::connect;
    case accept_code:
        return SessionSpduKind::accept;
    case reject_code:
        return SessionSpduKind::reject;
    case refuse_code:
        return SessionSpduKind::refuse;
    case abort_code:
        return SessionSpduKind::abort;
    case data_transfer_code:
        return SessionSpduKind::data_transfer;
    default:
        return SessionSpduKind::unknown;
    }
}

bool SessionCodec::try_decode_prefix(
    const std::span<const std::uint8_t> bytes,
    SessionSpdu& spdu,
    std::size_t& consumed,
    std::string* error) noexcept {
    try {
        spdu = {};
        consumed = 0U;
        if (bytes.size() < 2U) {
            set_error(error, "ISO Session SPDU is shorter than its two-byte header.");
            return false;
        }

        const auto kind = kind_from_code(bytes[0]);
        if (kind == SessionSpduKind::unknown) {
            set_error(error, "Unsupported ISO Session SPDU code.");
            return false;
        }

        const auto body_length = static_cast<std::size_t>(bytes[1]);
        const auto total_length = body_length + 2U;
        if (total_length > maximum_spdu_bytes || total_length > bytes.size()) {
            set_error(error, "ISO Session SPDU declared length exceeds available bytes.");
            return false;
        }

        spdu.kind = kind;
        spdu.code = bytes[0];
        spdu.length_indicator = bytes[1];
        consumed = total_length;

        std::size_t offset = 2U;
        const auto end = total_length;
        bool saw_user_data = false;
        while (offset < end) {
            if (end - offset < 2U) {
                set_error(error, "ISO Session parameter header is truncated.");
                spdu = {};
                consumed = 0U;
                return false;
            }
            if (spdu.parameters.size() >= maximum_parameter_count) {
                set_error(error, "ISO Session SPDU exceeded the parameter-count limit.");
                spdu = {};
                consumed = 0U;
                return false;
            }

            const auto code = bytes[offset++];
            const auto length = static_cast<std::size_t>(bytes[offset++]);
            if (length > end - offset) {
                set_error(error, "ISO Session parameter value is truncated.");
                spdu = {};
                consumed = 0U;
                return false;
            }

            if (code == user_data_parameter) {
                if (saw_user_data) {
                    set_error(error, "ISO Session SPDU contains duplicate user-data parameters.");
                    spdu = {};
                    consumed = 0U;
                    return false;
                }
                if (offset + length != end) {
                    set_error(error, "ISO Session user-data parameter must be final.");
                    spdu = {};
                    consumed = 0U;
                    return false;
                }
                spdu.user_data.assign(
                    bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                    bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
                saw_user_data = true;
            } else {
                SessionParameter parameter;
                parameter.code = code;
                parameter.value.assign(
                    bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                    bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
                spdu.parameters.push_back(std::move(parameter));
            }
            offset += length;
        }

        if (error != nullptr) {
            error->clear();
        }
        return true;
    } catch (const std::exception& exception) {
        spdu = {};
        consumed = 0U;
        set_error(error, exception.what());
        return false;
    } catch (...) {
        spdu = {};
        consumed = 0U;
        set_error(error, "ISO Session decode failed unexpectedly.");
        return false;
    }
}

bool SessionCodec::try_decode(
    const std::span<const std::uint8_t> bytes,
    SessionSpdu& spdu,
    std::string* error) noexcept {
    std::size_t consumed = 0U;
    if (!try_decode_prefix(bytes, spdu, consumed, error)) {
        return false;
    }
    if (consumed != bytes.size()) {
        spdu = {};
        set_error(error, "ISO Session SPDU declared length does not match the supplied frame length.");
        return false;
    }
    return true;
}

SessionSpdu SessionCodec::decode(const std::span<const std::uint8_t> bytes) {
    SessionSpdu spdu;
    std::string error;
    if (!try_decode(bytes, spdu, &error)) {
        throw SessionFormatError(error);
    }
    return spdu;
}

std::vector<std::uint8_t> SessionCodec::encode(
    const std::uint8_t code,
    const std::span<const SessionParameter> parameters,
    const std::span<const std::uint8_t> user_data) {
    if (kind_from_code(code) == SessionSpduKind::unknown) {
        throw std::invalid_argument("Unsupported ISO Session SPDU code.");
    }
    if (parameters.size() > maximum_parameter_count) {
        throw std::length_error("ISO Session parameter count exceeds the supported limit.");
    }
    if (user_data.size() > std::numeric_limits<std::uint8_t>::max()) {
        throw std::length_error("ISO Session user data exceeds one-byte parameter length encoding.");
    }

    std::size_t body_length = user_data.empty() ? 0U : 2U + user_data.size();
    for (const auto& parameter : parameters) {
        if (parameter.code == user_data_parameter) {
            throw std::invalid_argument("ISO Session user data must be supplied separately.");
        }
        if (parameter.value.size() > std::numeric_limits<std::uint8_t>::max()) {
            throw std::length_error("ISO Session parameter exceeds one-byte length encoding.");
        }
        if (parameter.value.size() > std::numeric_limits<std::size_t>::max() - 2U - body_length) {
            throw std::length_error("ISO Session SPDU size overflow.");
        }
        body_length += 2U + parameter.value.size();
    }
    if (body_length > std::numeric_limits<std::uint8_t>::max()) {
        throw std::length_error("ISO Session SPDU exceeds one-byte length encoding.");
    }

    std::vector<std::uint8_t> encoded;
    encoded.reserve(body_length + 2U);
    encoded.push_back(code);
    encoded.push_back(static_cast<std::uint8_t>(body_length));
    for (const auto& parameter : parameters) {
        encoded.push_back(parameter.code);
        encoded.push_back(static_cast<std::uint8_t>(parameter.value.size()));
        encoded.insert(encoded.end(), parameter.value.begin(), parameter.value.end());
    }
    if (!user_data.empty()) {
        encoded.push_back(user_data_parameter);
        encoded.push_back(static_cast<std::uint8_t>(user_data.size()));
        encoded.insert(encoded.end(), user_data.begin(), user_data.end());
    }
    return encoded;
}

std::vector<SessionParameter> SessionCodec::default_parameters() {
    return {
        SessionParameter{0x05U, {0x13U, 0x01U, 0x00U, 0x16U, 0x01U, 0x02U}},
        SessionParameter{0x14U, {0x00U, 0x02U}},
        SessionParameter{0x33U, {0x00U, 0x01U}},
        SessionParameter{0x34U, {0x00U, 0x01U}},
    };
}

std::vector<std::uint8_t> SessionCodec::encode_connect(
    const std::span<const std::uint8_t> presentation_payload,
    const std::span<const SessionParameter> parameters) {
    const auto effective = effective_parameters(parameters);
    return encode(connect_code, effective, presentation_payload);
}

std::vector<std::uint8_t> SessionCodec::encode_accept(
    const std::span<const std::uint8_t> presentation_payload,
    const std::span<const SessionParameter> parameters) {
    const auto effective = effective_parameters(parameters);
    return encode(accept_code, effective, presentation_payload);
}

std::vector<std::uint8_t> SessionCodec::encode_accept_mirroring(
    const SessionSpdu& connect,
    const std::span<const std::uint8_t> presentation_payload) {
    if (connect.kind != SessionSpduKind::connect) {
        throw std::invalid_argument("ISO Session Accept mirroring requires a Connect SPDU.");
    }
    return encode(accept_code, connect.parameters, presentation_payload);
}

std::vector<std::uint8_t> SessionCodec::encode_data_transfer(
    const std::span<const std::uint8_t> presentation_payload,
    const bool include_give_tokens_prefix) {
    std::vector<std::uint8_t> encoded;
    encoded.reserve(presentation_payload.size() + (include_give_tokens_prefix ? 4U : 2U));
    if (include_give_tokens_prefix) {
        encoded.push_back(data_transfer_code);
        encoded.push_back(0x00U);
    }
    encoded.push_back(data_transfer_code);
    encoded.push_back(0x00U);
    encoded.insert(encoded.end(), presentation_payload.begin(), presentation_payload.end());
    return encoded;
}

bool SessionCodec::try_decode_data_transfer(
    const std::span<const std::uint8_t> bytes,
    SessionDataTransfer& transfer,
    std::string* error) noexcept {
    try {
        transfer = {};
        if (bytes.size() < 2U) {
            set_error(error, "ISO Session data-transfer profile is truncated.");
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
            set_error(error, "ISO Session data-transfer profile has an invalid SPDU prefix.");
            return false;
        }

        transfer.presentation_payload.assign(
            bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());
        if (error != nullptr) {
            error->clear();
        }
        return true;
    } catch (...) {
        transfer = {};
        set_error(error, "ISO Session data-transfer decode failed unexpectedly.");
        return false;
    }
}

SessionDataTransfer SessionCodec::decode_data_transfer(
    const std::span<const std::uint8_t> bytes) {
    SessionDataTransfer transfer;
    std::string error;
    if (!try_decode_data_transfer(bytes, transfer, &error)) {
        throw SessionFormatError(error);
    }
    return transfer;
}

} // namespace ar::iec61850::osi
