// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/file_service.hpp"

#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/mms/pdu.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <set>
#include <sstream>
#include <utility>

namespace ar::iec61850::mms {
namespace {

using asn1::BerClass;
using asn1::BerReader;
using asn1::BerTlv;
using asn1::BerWriter;

constexpr std::size_t codec_hex_preview_bytes = 512U;

[[nodiscard]] std::vector<std::uint8_t> concat(
    const std::initializer_list<std::span<const std::uint8_t>> parts) {
    std::size_t size = 0U;
    for (const auto part : parts) {
        if (part.size() > MmsPduCodec::maximum_pdu_bytes - size) {
            throw std::length_error("MMS file-service encoding exceeds the PDU bound.");
        }
        size += part.size();
    }
    std::vector<std::uint8_t> result;
    result.reserve(size);
    for (const auto part : parts) {
        result.insert(result.end(), part.begin(), part.end());
    }
    return result;
}

[[nodiscard]] std::string hex_preview(
    const std::span<const std::uint8_t> bytes,
    const std::size_t maximum_bytes = codec_hex_preview_bytes) {
    static constexpr char digits[] = "0123456789ABCDEF";
    const auto retained = std::min(bytes.size(), maximum_bytes);
    std::string result;
    result.reserve((retained * 3U) + 32U);
    for (std::size_t index = 0U; index < retained; ++index) {
        if (index != 0U) {
            result.push_back(' ');
        }
        const auto value = bytes[index];
        result.push_back(digits[(value >> 4U) & 0x0FU]);
        result.push_back(digits[value & 0x0FU]);
    }
    if (retained < bytes.size()) {
        result += " ... (" + std::to_string(bytes.size()) + " bytes total)";
    }
    return result;
}

[[nodiscard]] std::vector<std::uint8_t> positive_u32(const std::uint32_t value) {
    auto result = BerWriter::encode_unsigned_integer(value);
    if (result.empty()) {
        result.push_back(0U);
    }
    if ((result.front() & 0x80U) != 0U) {
        result.insert(result.begin(), 0U);
    }
    return result;
}

[[nodiscard]] std::vector<std::uint8_t> graphic_string(const std::string& value) {
    return BerWriter::encode_tlv(0x19U, BerWriter::encode_ascii(value));
}

[[nodiscard]] std::vector<std::uint8_t> encode_segmented_file_name(
    const std::string& path) {
    BerWriter body;
    std::size_t begin = 0U;
    while (begin < path.size()) {
        const auto end = path.find('/', begin);
        const auto segment = path.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin);
        const auto encoded = graphic_string(segment);
        body.write_raw(encoded);
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1U;
    }
    return body.to_vector();
}

[[nodiscard]] std::string lowercase_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    return value;
}

[[nodiscard]] bool ends_with_separator(const std::string& value) noexcept {
    return !value.empty() && (value.back() == '/' || value.back() == '\\');
}

[[nodiscard]] bool has_extension(const std::string& name) {
    const auto separator = name.find_last_of("/\\");
    const auto dot = name.find_last_of('.');
    return dot != std::string::npos &&
           (separator == std::string::npos || dot > separator) &&
           dot + 1U < name.size();
}

[[noreturn]] void throw_file_error(MmsFileDiagnostic diagnostic) {
    throw MmsFileServiceError(std::move(diagnostic));
}

[[nodiscard]] MmsConfirmedResponse decode_service_response(
    const std::span<const std::uint8_t> payload,
    const std::uint32_t expected_invoke_id,
    const std::int32_t expected_service_tag,
    const std::string& operation) {
    MmsPduEnvelope envelope;
    try {
        envelope = MmsPduCodec::decode_envelope(payload);
    } catch (const std::exception& exception) {
        throw_file_error({
            operation,
            MmsFileFailureKind::malformed_response,
            false,
            operation + " response decode failed: " + exception.what(),
            expected_invoke_id,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            {},
            hex_preview(payload)});
    }

    if (envelope.kind == MmsPduKind::confirmed_error) {
        try {
            const auto error = MmsPduCodec::decode_confirmed_error(envelope.mms_payload);
            std::ostringstream message;
            message << "MMS Confirmed-Error PDU during " << operation
                    << ": errorClass=[" << error.error_class_tag
                    << "], value=" << error.error_value << ".";
            throw_file_error({
                operation,
                MmsFileFailureKind::confirmed_error,
                false,
                message.str(),
                error.invoke_id,
                std::nullopt,
                error.error_class_tag,
                error.error_value,
                {},
                hex_preview(envelope.mms_payload)});
        } catch (const MmsFileServiceError&) {
            throw;
        } catch (const std::exception& exception) {
            throw_file_error({
                operation,
                MmsFileFailureKind::malformed_response,
                false,
                operation + " Confirmed-Error decode failed: " + exception.what(),
                expected_invoke_id,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                {},
                hex_preview(envelope.mms_payload)});
        }
    }

    if (envelope.kind == MmsPduKind::reject ||
        envelope.kind == MmsPduKind::unconfirmed) {
        throw_file_error({
            operation,
            MmsFileFailureKind::reject_or_abort,
            false,
            "MMS Reject/Abort PDU during " + operation + ".",
            envelope.invoke_id,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            {},
            hex_preview(envelope.mms_payload)});
    }
    if (envelope.kind != MmsPduKind::confirmed_response) {
        throw_file_error({
            operation,
            MmsFileFailureKind::unexpected_service,
            false,
            operation + " expected an MMS Confirmed-Response PDU.",
            envelope.invoke_id,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            {},
            hex_preview(envelope.mms_payload)});
    }

    MmsConfirmedResponse response;
    try {
        response = MmsPduCodec::decode_confirmed_response(envelope.mms_payload);
    } catch (const std::exception& exception) {
        throw_file_error({
            operation,
            MmsFileFailureKind::malformed_response,
            false,
            operation + " Confirmed-Response decode failed: " + exception.what(),
            envelope.invoke_id,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            {},
            hex_preview(envelope.mms_payload)});
    }
    if (response.invoke_id != expected_invoke_id) {
        throw_file_error({
            operation,
            MmsFileFailureKind::invoke_id_mismatch,
            false,
            operation + " invoke ID mismatch. Expected " +
                std::to_string(expected_invoke_id) + ", received " +
                std::to_string(response.invoke_id) + ".",
            response.invoke_id,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            {},
            hex_preview(envelope.mms_payload)});
    }
    if (response.service_tag != expected_service_tag) {
        throw_file_error({
            operation,
            MmsFileFailureKind::unexpected_service,
            false,
            operation + " response service mismatch. Expected [" +
                std::to_string(expected_service_tag) + "], received [" +
                std::to_string(response.service_tag) + "].",
            response.invoke_id,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            {},
            hex_preview(envelope.mms_payload)});
    }
    return response;
}

[[nodiscard]] std::optional<std::uint32_t> read_u32(const BerTlv& field) noexcept {
    const auto value = BerReader::read_unsigned_integer(field);
    if (!value || *value > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*value);
}

void collect_graphic_strings(
    const BerTlv& field,
    std::vector<std::string>& parts,
    const std::size_t depth) {
    if (!field.constructed || depth > MmsFileServiceCodec::maximum_directory_depth) {
        return;
    }
    for (const auto& child : BerReader::read_children(field.value)) {
        if (child.encoded_tag == 0x19U || child.encoded_tag == 0x1AU ||
            child.encoded_tag == 0x16U) {
            parts.push_back(BerReader::read_ascii_string(child));
        } else if (child.constructed) {
            collect_graphic_strings(child, parts, depth + 1U);
        }
    }
}

[[nodiscard]] bool looks_like_directory_entry(const BerTlv& candidate) {
    if (!candidate.constructed) {
        return false;
    }
    for (const auto& field : BerReader::read_children(candidate.value)) {
        if (field.tag_class == BerClass::context_specific &&
            field.tag_number == 0 && field.constructed) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] MmsFileDirectoryEntry decode_directory_entry(
    const BerTlv& encoded,
    const std::string& directory_name) {
    std::vector<std::string> parts;
    std::optional<std::uint32_t> size;
    std::vector<std::uint8_t> modified;
    for (const auto& field : BerReader::read_children(encoded.value)) {
        if (field.tag_class != BerClass::context_specific) {
            continue;
        }
        if (field.tag_number == 0 && field.constructed) {
            collect_graphic_strings(field, parts, 0U);
        } else if (field.tag_number == 1 && field.constructed) {
            for (const auto& attribute : BerReader::read_children(field.value)) {
                if (attribute.tag_class != BerClass::context_specific) {
                    continue;
                }
                if (attribute.tag_number == 0) {
                    size = read_u32(attribute);
                    if (!size) {
                        throw MmsFormatError("FileDirectory size is not an unsigned Integer32.");
                    }
                } else if (attribute.tag_number == 1) {
                    modified = attribute.value;
                }
            }
        }
    }
    if (parts.empty()) {
        throw MmsFormatError("FileDirectory entry has no FileName GraphicString.");
    }
    std::string joined;
    for (const auto& part : parts) {
        const auto normalized = MmsFileServiceCodec::normalize_remote_path(part);
        if (!joined.empty()) {
            joined.push_back('/');
        }
        joined += normalized;
    }
    const auto directory = MmsFileServiceCodec::normalize_remote_path(directory_name, true);
    std::string path = joined;
    if (!directory.empty() && joined != directory &&
        joined.rfind(directory + '/', 0U) != 0U) {
        path = directory + '/' + joined;
    }
    return {joined, path, size, std::move(modified)};
}

void collect_directory_entries(
    const BerTlv& container,
    const std::string& directory_name,
    std::vector<MmsFileDirectoryEntry>& entries,
    const std::size_t depth) {
    if (!container.constructed || depth > MmsFileServiceCodec::maximum_directory_depth) {
        return;
    }
    for (const auto& child : BerReader::read_children(container.value)) {
        if (looks_like_directory_entry(child)) {
            if (entries.size() >= MmsFileServiceCodec::maximum_directory_entries) {
                throw MmsFormatError("FileDirectory response exceeds the entry bound.");
            }
            entries.push_back(decode_directory_entry(child, directory_name));
        } else if (child.constructed) {
            collect_directory_entries(child, directory_name, entries, depth + 1U);
        }
    }
}

void validate_invoke_id(const std::uint32_t invoke_id) {
    if (invoke_id == 0U || invoke_id > MmsPduCodec::maximum_invoke_id) {
        throw std::invalid_argument("MMS file-service invoke ID is outside the supported range.");
    }
}

[[nodiscard]] std::vector<std::uint8_t> encode_frsm_request(
    const std::uint32_t invoke_id,
    const std::int32_t frsm_id,
    const std::int32_t service_tag) {
    validate_invoke_id(invoke_id);
    return MmsPduCodec::encode_confirmed_request(
        {invoke_id, service_tag, false, BerWriter::encode_signed_integer(frsm_id)});
}

void bound_text(std::string& value, const std::size_t maximum_bytes) {
    if (value.size() <= maximum_bytes) {
        return;
    }
    const auto original = value.size();
    value.resize(maximum_bytes);
    value += "...(" + std::to_string(original) + " bytes)";
}

void append_diagnostic(
    std::vector<MmsFileDiagnostic>& diagnostics,
    MmsFileDiagnostic diagnostic,
    const std::size_t maximum_entries,
    const std::size_t maximum_bytes) {
    bound_text(diagnostic.message, maximum_bytes);
    bound_text(diagnostic.request_hex, maximum_bytes);
    bound_text(diagnostic.response_hex, maximum_bytes);
    if (maximum_entries == 0U) {
        return;
    }
    if (diagnostics.size() < maximum_entries) {
        diagnostics.push_back(std::move(diagnostic));
    } else if (!diagnostic.success) {
        diagnostics.back() = std::move(diagnostic);
    }
}

[[nodiscard]] MmsFileDiagnostic exception_diagnostic(
    const std::string& stage,
    const std::uint32_t invoke_id,
    const std::string& request_hex,
    const std::exception& exception,
    const MmsFileFailureKind kind = MmsFileFailureKind::transport) {
    return {
        stage,
        kind,
        false,
        stage + " exchange failed: " + exception.what(),
        invoke_id,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        request_hex,
        {}};
}

[[nodiscard]] bool has_file_nonexistent_open(
    const MmsFileTransferResult& result) noexcept {
    if (result.success || result.bytes_transferred != 0U || result.read_operations != 0U) {
        return false;
    }
    return std::any_of(
        result.diagnostics.begin(), result.diagnostics.end(),
        [](const MmsFileDiagnostic& diagnostic) {
            return diagnostic.stage == "FileOpen" && diagnostic.file_non_existent();
        });
}

} // namespace

MmsFileServiceError::MmsFileServiceError(MmsFileDiagnostic diagnostic)
    : std::runtime_error(diagnostic.message), diagnostic_(std::move(diagnostic)) {}

bool MmsFileDirectoryEntry::likely_directory() const {
    if (ends_with_separator(name) || ends_with_separator(path)) {
        return true;
    }
    return !has_extension(name) && (!size_bytes || *size_bytes == 0U);
}

std::string MmsFileServiceCodec::normalize_remote_path(
    std::string path,
    const bool allow_root) {
    if (path.find('\0') != std::string::npos) {
        throw std::invalid_argument("MMS remote path contains a null character.");
    }
    const auto first = path.find_first_not_of(" \t\r\n");
    const auto last = path.find_last_not_of(" \t\r\n");
    if (first == std::string::npos) {
        if (allow_root) {
            return {};
        }
        throw std::invalid_argument("MMS remote path is empty.");
    }
    path = path.substr(first, last - first + 1U);
    std::replace(path.begin(), path.end(), '\\', '/');
    if (allow_root && (path == "/" || path == "*")) {
        return {};
    }

    std::string normalized;
    std::size_t begin = 0U;
    while (begin <= path.size()) {
        const auto end = path.find('/', begin);
        auto segment = path.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin);
        const auto segment_first = segment.find_first_not_of(" \t\r\n");
        const auto segment_last = segment.find_last_not_of(" \t\r\n");
        if (segment_first != std::string::npos) {
            segment = segment.substr(segment_first, segment_last - segment_first + 1U);
            if (segment == "." || segment == "..") {
                throw std::invalid_argument("MMS remote path contains a traversal segment.");
            }
            if (segment.find(':') != std::string::npos) {
                throw std::invalid_argument("MMS remote path contains an ambiguous drive/scheme segment.");
            }
            for (const char character : segment) {
                if (static_cast<unsigned char>(character) < 0x20U) {
                    throw std::invalid_argument("MMS remote path contains a control character.");
                }
            }
            if (!normalized.empty()) {
                normalized.push_back('/');
            }
            normalized += segment;
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1U;
    }
    if (normalized.empty()) {
        if (allow_root) {
            return {};
        }
        throw std::invalid_argument("MMS remote path has no usable segment.");
    }
    if (normalized.size() > maximum_path_bytes) {
        throw std::length_error("MMS remote path exceeds the configured byte bound.");
    }
    return normalized;
}

std::string MmsFileServiceCodec::rooted_backslash_path(const std::string& path) {
    auto normalized = normalize_remote_path(path);
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    return "\\" + normalized;
}

std::vector<std::uint8_t> MmsFileServiceCodec::encode_file_directory_request_pdu(
    const MmsFileDirectoryRequest& request) {
    validate_invoke_id(request.invoke_id);
    BerWriter body;
    const auto directory = normalize_remote_path(request.directory_name, true);
    const auto continuation = normalize_remote_path(request.continue_after, true);
    if (!directory.empty()) {
        const auto name = graphic_string(directory);
        body.write_tlv(BerClass::context_specific, true, 0, name);
    }
    if (!continuation.empty()) {
        const auto name = graphic_string(continuation);
        body.write_tlv(BerClass::context_specific, true, 1, name);
    }
    return MmsPduCodec::encode_confirmed_request(
        {request.invoke_id, file_directory_service_tag, true, body.to_vector()});
}

std::vector<std::uint8_t> MmsFileServiceCodec::encode_file_directory_request_p_data(
    const MmsFileDirectoryRequest& request,
    const std::uint32_t presentation_context_id) {
    return MmsPduCodec::wrap_p_data(
        encode_file_directory_request_pdu(request), presentation_context_id);
}

MmsFileDirectoryResponse MmsFileServiceCodec::decode_file_directory_response(
    const std::span<const std::uint8_t> payload,
    const std::uint32_t expected_invoke_id,
    const std::string& directory_name) {
    const auto service = decode_service_response(
        payload, expected_invoke_id, file_directory_service_tag, "FileDirectory");
    MmsFileDirectoryResponse result;
    result.invoke_id = service.invoke_id;
    try {
        for (const auto& field : BerReader::read_children(service.service_value)) {
            if (field.tag_class == BerClass::context_specific &&
                field.tag_number == 0 && field.constructed) {
                collect_directory_entries(field, directory_name, result.entries, 0U);
            } else if (field.tag_class == BerClass::context_specific &&
                       field.tag_number == 1 && !field.value.empty()) {
                result.more_follows = BerReader::read_boolean(field)
                    .value_or(field.value.front() != 0U);
            }
        }
        std::set<std::string, std::less<>> seen;
        result.entries.erase(
            std::remove_if(result.entries.begin(), result.entries.end(),
                [&seen](const MmsFileDirectoryEntry& entry) {
                    return !seen.insert(lowercase_ascii(entry.path)).second;
                }),
            result.entries.end());
        return result;
    } catch (const std::exception& exception) {
        throw_file_error({
            "FileDirectory",
            MmsFileFailureKind::malformed_response,
            false,
            std::string{"FileDirectory service decode failed: "} + exception.what(),
            service.invoke_id,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            {},
            hex_preview(payload)});
    }
}

std::vector<std::uint8_t> MmsFileServiceCodec::encode_file_open_request_pdu(
    const MmsFileOpenRequest& request) {
    validate_invoke_id(request.invoke_id);
    const auto normalized = normalize_remote_path(request.remote_path);
    const auto name_content = request.rooted_backslash
        ? graphic_string(rooted_backslash_path(normalized))
        : encode_segmented_file_name(normalized);
    const auto file_name = BerWriter::encode_tlv(
        BerClass::context_specific, true, 0, name_content);
    const auto position = BerWriter::encode_tlv(
        BerClass::context_specific, false, 1, positive_u32(request.initial_position));
    return MmsPduCodec::encode_confirmed_request(
        {request.invoke_id, file_open_service_tag, true, concat({file_name, position})});
}

std::vector<std::uint8_t> MmsFileServiceCodec::encode_file_open_request_p_data(
    const MmsFileOpenRequest& request,
    const std::uint32_t presentation_context_id) {
    return MmsPduCodec::wrap_p_data(
        encode_file_open_request_pdu(request), presentation_context_id);
}

MmsFileOpenResponse MmsFileServiceCodec::decode_file_open_response(
    const std::span<const std::uint8_t> payload,
    const std::uint32_t expected_invoke_id) {
    const auto service = decode_service_response(
        payload, expected_invoke_id, file_open_service_tag, "FileOpen");
    MmsFileOpenResponse result;
    result.invoke_id = service.invoke_id;
    bool have_frsm = false;
    try {
        for (const auto& field : BerReader::read_children(service.service_value)) {
            if (field.tag_class != BerClass::context_specific) {
                continue;
            }
            if (field.tag_number == 0 && !field.constructed) {
                const auto value = BerReader::read_signed_integer(field);
                if (!value || *value < std::numeric_limits<std::int32_t>::min() ||
                    *value > std::numeric_limits<std::int32_t>::max()) {
                    throw MmsFormatError("FileOpen FRSM is not a signed Integer32.");
                }
                result.frsm_id = static_cast<std::int32_t>(*value);
                have_frsm = true;
            } else if (field.tag_number == 1 && field.constructed) {
                for (const auto& attribute : BerReader::read_children(field.value)) {
                    if (attribute.tag_class != BerClass::context_specific) {
                        continue;
                    }
                    if (attribute.tag_number == 0) {
                        result.file_size_bytes = read_u32(attribute);
                        if (!result.file_size_bytes) {
                            throw MmsFormatError("FileOpen size is not an unsigned Integer32.");
                        }
                    } else if (attribute.tag_number == 1) {
                        result.last_modified = attribute.value;
                    }
                }
            }
        }
        if (!have_frsm) {
            throw MmsFormatError("FileOpen response has no signed Integer32 FRSM identifier.");
        }
        return result;
    } catch (const std::exception& exception) {
        throw_file_error({
            "FileOpen",
            MmsFileFailureKind::malformed_response,
            false,
            std::string{"FileOpen service decode failed: "} + exception.what(),
            service.invoke_id,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            {},
            hex_preview(payload)});
    }
}

std::vector<std::uint8_t> MmsFileServiceCodec::encode_file_read_request_pdu(
    const MmsFileReadRequest& request) {
    return encode_frsm_request(request.invoke_id, request.frsm_id, file_read_service_tag);
}

std::vector<std::uint8_t> MmsFileServiceCodec::encode_file_read_request_p_data(
    const MmsFileReadRequest& request,
    const std::uint32_t presentation_context_id) {
    return MmsPduCodec::wrap_p_data(
        encode_file_read_request_pdu(request), presentation_context_id);
}

MmsFileReadResponse MmsFileServiceCodec::decode_file_read_response(
    const std::span<const std::uint8_t> payload,
    const std::uint32_t expected_invoke_id) {
    const auto service = decode_service_response(
        payload, expected_invoke_id, file_read_service_tag, "FileRead");
    MmsFileReadResponse result;
    result.invoke_id = service.invoke_id;
    bool have_data = false;
    try {
        for (const auto& field : BerReader::read_children(service.service_value)) {
            if (field.tag_class != BerClass::context_specific) {
                continue;
            }
            if (field.tag_number == 0 && !field.constructed) {
                if (have_data) {
                    throw MmsFormatError("FileRead response contains duplicate data blocks.");
                }
                result.data = field.value;
                have_data = true;
            } else if (field.tag_number == 1 && !field.value.empty()) {
                result.more_follows = BerReader::read_boolean(field)
                    .value_or(field.value.front() != 0U);
            }
        }
        if (!have_data) {
            throw MmsFormatError("FileRead response has no data block.");
        }
        return result;
    } catch (const std::exception& exception) {
        throw_file_error({
            "FileRead",
            MmsFileFailureKind::malformed_response,
            false,
            std::string{"FileRead service decode failed: "} + exception.what(),
            service.invoke_id,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            {},
            hex_preview(payload)});
    }
}

std::vector<std::uint8_t> MmsFileServiceCodec::encode_file_close_request_pdu(
    const MmsFileCloseRequest& request) {
    return encode_frsm_request(request.invoke_id, request.frsm_id, file_close_service_tag);
}

std::vector<std::uint8_t> MmsFileServiceCodec::encode_file_close_request_p_data(
    const MmsFileCloseRequest& request,
    const std::uint32_t presentation_context_id) {
    return MmsPduCodec::wrap_p_data(
        encode_file_close_request_pdu(request), presentation_context_id);
}

MmsFileCloseResponse MmsFileServiceCodec::decode_file_close_response(
    const std::span<const std::uint8_t> payload,
    const std::uint32_t expected_invoke_id) {
    const auto service = decode_service_response(
        payload, expected_invoke_id, file_close_service_tag, "FileClose");
    if (!service.service_value.empty()) {
        throw_file_error({
            "FileClose",
            MmsFileFailureKind::malformed_response,
            false,
            "FileClose successful response must be empty.",
            service.invoke_id,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            {},
            hex_preview(payload)});
    }
    return {service.invoke_id};
}

std::uint32_t MmsAssociationFileServiceChannel::next_invoke_id() {
    return association_.next_invoke_id();
}

std::vector<std::uint8_t> MmsAssociationFileServiceChannel::exchange_confirmed(
    const std::span<const std::uint8_t> request,
    const std::uint32_t expected_invoke_id,
    const std::stop_token stop_token) {
    auto exchange = association_.exchange_confirmed(
        request, expected_invoke_id, stop_token);
    if (!exchange.presentation_payload.empty()) {
        return exchange.presentation_payload;
    }
    return exchange.envelope.mms_payload;
}

bool MmsAssociationFileServiceChannel::associated() const noexcept {
    return association_.associated();
}

std::uint32_t MmsAssociationFileServiceChannel::presentation_context_id() const noexcept {
    return association_.negotiated().presentation_context_id;
}

MmsFileDirectoryResult MmsFileTransferRuntime::list_directory(
    const std::string& directory_name,
    MmsFileDirectoryOptions options,
    const std::stop_token stop_token) {
    MmsFileDirectoryResult result;
    try {
        result.directory_name = MmsFileServiceCodec::normalize_remote_path(
            directory_name, true);
    } catch (const std::exception& exception) {
        result.failure_kind = MmsFileFailureKind::invalid_argument;
        result.message = exception.what();
        return result;
    }
    if (options.maximum_pages == 0U || options.maximum_entries == 0U ||
        options.maximum_diagnostic_bytes == 0U) {
        result.failure_kind = MmsFileFailureKind::invalid_argument;
        result.message = "FileDirectory bounds must be greater than zero.";
        return result;
    }

    std::set<std::string, std::less<>> seen_paths;
    std::set<std::string, std::less<>> seen_continuations;
    std::string continuation;
    for (std::size_t page = 0U; page < options.maximum_pages; ++page) {
        if (stop_token.stop_requested()) {
            result.failure_kind = MmsFileFailureKind::cancelled;
            result.message = "FileDirectory cancelled before the next page.";
            break;
        }
        const auto invoke_id = channel_.next_invoke_id();
        const MmsFileDirectoryRequest request{
            invoke_id, result.directory_name, continuation};
        std::vector<std::uint8_t> encoded;
        try {
            encoded = MmsFileServiceCodec::encode_file_directory_request_p_data(
                request, channel_.presentation_context_id());
            const auto response_payload = channel_.exchange_confirmed(
                encoded, invoke_id, stop_token);
            const auto response = MmsFileServiceCodec::decode_file_directory_response(
                response_payload, invoke_id, result.directory_name);

            append_diagnostic(result.diagnostics, {
                "FileDirectory page " + std::to_string(page + 1U),
                MmsFileFailureKind::none,
                true,
                "Decoded " + std::to_string(response.entries.size()) +
                    " directory entries; moreFollows=" +
                    (response.more_follows ? "true." : "false."),
                invoke_id,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                hex_preview(encoded, options.maximum_diagnostic_bytes),
                hex_preview(response_payload, options.maximum_diagnostic_bytes)},
                options.maximum_diagnostics, options.maximum_diagnostic_bytes);

            result.pages.push_back({
                page + 1U, continuation, response.entries.size(), response.more_follows});
            const auto before = result.entries.size();
            for (const auto& entry : response.entries) {
                if (!seen_paths.insert(lowercase_ascii(entry.path)).second) {
                    continue;
                }
                if (result.entries.size() >= options.maximum_entries) {
                    result.failure_kind = MmsFileFailureKind::limit_exceeded;
                    result.message = "FileDirectory exceeded the configured entry limit.";
                    break;
                }
                result.entries.push_back(entry);
            }
            if (result.failure_kind != MmsFileFailureKind::none) {
                break;
            }
            if (!response.more_follows) {
                result.success = true;
                result.message = "FileDirectory completed in " +
                    std::to_string(result.pages.size()) + " page(s) with " +
                    std::to_string(result.entries.size()) + " unique entry/entries.";
                return result;
            }
            if (response.entries.empty() || result.entries.size() == before) {
                result.failure_kind = MmsFileFailureKind::protocol;
                result.message = "FileDirectory reported moreFollows without forward progress.";
                break;
            }
            const auto next = response.entries.back().name;
            if (next.empty() || next == continuation ||
                !seen_continuations.insert(lowercase_ascii(next)).second) {
                result.failure_kind = MmsFileFailureKind::protocol;
                result.message = "FileDirectory continuation repeated without progress.";
                break;
            }
            continuation = next;
            if (page + 1U == options.maximum_pages) {
                result.failure_kind = MmsFileFailureKind::limit_exceeded;
                result.message = "FileDirectory exceeded the configured page limit.";
            }
        } catch (const MmsFileServiceError& exception) {
            auto diagnostic = exception.diagnostic();
            diagnostic.request_hex = hex_preview(encoded, options.maximum_diagnostic_bytes);
            append_diagnostic(result.diagnostics, std::move(diagnostic),
                options.maximum_diagnostics, options.maximum_diagnostic_bytes);
            result.failure_kind = exception.diagnostic().failure_kind;
            result.message = exception.what();
            break;
        } catch (const MmsTransportCancelledError& exception) {
            auto diagnostic = exception_diagnostic(
                "FileDirectory", invoke_id,
                hex_preview(encoded, options.maximum_diagnostic_bytes), exception,
                MmsFileFailureKind::cancelled);
            result.failure_kind = diagnostic.failure_kind;
            result.message = diagnostic.message;
            append_diagnostic(result.diagnostics, std::move(diagnostic),
                options.maximum_diagnostics, options.maximum_diagnostic_bytes);
            break;
        } catch (const MmsTransportTimeoutError& exception) {
            auto diagnostic = exception_diagnostic(
                "FileDirectory", invoke_id,
                hex_preview(encoded, options.maximum_diagnostic_bytes), exception,
                MmsFileFailureKind::timed_out);
            result.failure_kind = diagnostic.failure_kind;
            result.message = diagnostic.message;
            append_diagnostic(result.diagnostics, std::move(diagnostic),
                options.maximum_diagnostics, options.maximum_diagnostic_bytes);
            break;
        } catch (const std::exception& exception) {
            auto diagnostic = exception_diagnostic(
                "FileDirectory", invoke_id,
                hex_preview(encoded, options.maximum_diagnostic_bytes), exception);
            result.failure_kind = diagnostic.failure_kind;
            result.message = diagnostic.message;
            append_diagnostic(result.diagnostics, std::move(diagnostic),
                options.maximum_diagnostics, options.maximum_diagnostic_bytes);
            break;
        }
    }
    return result;
}

MmsFileTransferResult MmsFileTransferRuntime::download(
    const std::string& remote_path,
    MmsFileSink& sink,
    MmsFileTransferOptions options,
    MmsFileProgressSink* progress,
    const std::stop_token stop_token) {
    std::string normalized;
    try {
        normalized = MmsFileServiceCodec::normalize_remote_path(remote_path);
    } catch (const std::exception& exception) {
        MmsFileTransferResult result;
        result.failure_kind = MmsFileFailureKind::invalid_argument;
        result.message = exception.what();
        return result;
    }
    return download_attempt(normalized, false, sink, options, progress, stop_token);
}

MmsFileTransferResult MmsFileTransferRuntime::download_adaptive(
    const std::string& remote_path,
    MmsFileSink& sink,
    MmsFileTransferOptions options,
    MmsFileProgressSink* progress,
    const std::stop_token stop_token) {
    auto primary = download(remote_path, sink, options, progress, stop_token);
    primary.primary_attempt_diagnostics = primary.diagnostics;
    if (!has_file_nonexistent_open(primary)) {
        return primary;
    }

    std::string reset_error;
    if (!sink.reset(reset_error)) {
        primary.failure_kind = MmsFileFailureKind::sink;
        primary.message += " Rooted-backslash retry was skipped because the sink could not reset: " +
            reset_error;
        append_diagnostic(primary.diagnostics, {
            "Adaptive sink reset",
            MmsFileFailureKind::sink,
            false,
            reset_error,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            {},
            {}}, options.maximum_diagnostics, options.maximum_diagnostic_bytes);
        return primary;
    }

    auto fallback = download_attempt(
        MmsFileServiceCodec::normalize_remote_path(remote_path), true,
        sink, options, progress, stop_token);
    fallback.adaptive_fallback_attempted = true;
    fallback.adaptive_fallback_succeeded = fallback.success;
    fallback.primary_attempt_diagnostics = primary.diagnostics;
    fallback.fallback_attempt_diagnostics = fallback.diagnostics;
    auto combined = primary.diagnostics;
    for (const auto& diagnostic : fallback.diagnostics) {
        append_diagnostic(combined, diagnostic,
            options.maximum_diagnostics, options.maximum_diagnostic_bytes);
    }
    fallback.diagnostics = std::move(combined);
    fallback.message += fallback.success
        ? " Adaptive rooted-backslash FileOpen fallback succeeded."
        : " Adaptive rooted-backslash FileOpen fallback also failed.";
    return fallback;
}

MmsFileTransferResult MmsFileTransferRuntime::download_attempt(
    const std::string& normalized_path,
    const bool rooted_backslash,
    MmsFileSink& sink,
    const MmsFileTransferOptions& options,
    MmsFileProgressSink* progress,
    const std::stop_token stop_token) {
    MmsFileTransferResult result;
    result.remote_path = rooted_backslash
        ? MmsFileServiceCodec::rooted_backslash_path(normalized_path)
        : normalized_path;
    if (options.maximum_bytes == 0U || options.maximum_read_operations == 0U ||
        options.maximum_block_bytes == 0U || options.maximum_diagnostic_bytes == 0U) {
        result.failure_kind = MmsFileFailureKind::invalid_argument;
        result.message = "File transfer bounds must be greater than zero.";
        return result;
    }

    std::optional<std::int32_t> frsm;
    auto fail = [&result](const MmsFileFailureKind kind, std::string message) {
        if (result.failure_kind == MmsFileFailureKind::none) {
            result.failure_kind = kind;
            result.message = std::move(message);
        }
    };

    const auto exchange = [this, &result, &options, stop_token](
        const std::string& stage,
        const std::uint32_t invoke_id,
        const std::vector<std::uint8_t>& request) -> std::vector<std::uint8_t> {
        try {
            return channel_.exchange_confirmed(request, invoke_id, stop_token);
        } catch (const MmsTransportCancelledError& exception) {
            auto diagnostic = exception_diagnostic(
                stage, invoke_id,
                hex_preview(request, options.maximum_diagnostic_bytes), exception,
                MmsFileFailureKind::cancelled);
            append_diagnostic(result.diagnostics, diagnostic,
                options.maximum_diagnostics, options.maximum_diagnostic_bytes);
            throw MmsFileServiceError(std::move(diagnostic));
        } catch (const MmsTransportTimeoutError& exception) {
            auto diagnostic = exception_diagnostic(
                stage, invoke_id,
                hex_preview(request, options.maximum_diagnostic_bytes), exception,
                MmsFileFailureKind::timed_out);
            append_diagnostic(result.diagnostics, diagnostic,
                options.maximum_diagnostics, options.maximum_diagnostic_bytes);
            throw MmsFileServiceError(std::move(diagnostic));
        } catch (const std::exception& exception) {
            auto diagnostic = exception_diagnostic(
                stage, invoke_id,
                hex_preview(request, options.maximum_diagnostic_bytes), exception);
            append_diagnostic(result.diagnostics, diagnostic,
                options.maximum_diagnostics, options.maximum_diagnostic_bytes);
            throw MmsFileServiceError(std::move(diagnostic));
        }
    };

    try {
        if (stop_token.stop_requested()) {
            fail(MmsFileFailureKind::cancelled, "File transfer cancelled before FileOpen.");
        } else {
            const auto invoke_id = channel_.next_invoke_id();
            const auto request = MmsFileServiceCodec::encode_file_open_request_p_data(
                {invoke_id, normalized_path, 0U, rooted_backslash},
                channel_.presentation_context_id());
            try {
                const auto response_payload = exchange("FileOpen", invoke_id, request);
                const auto opened = MmsFileServiceCodec::decode_file_open_response(
                    response_payload, invoke_id);
                frsm = opened.frsm_id;
                result.expected_bytes = opened.file_size_bytes;
                append_diagnostic(result.diagnostics, {
                    "FileOpen",
                    MmsFileFailureKind::none,
                    true,
                    "FileOpen succeeded; FRSM=" + std::to_string(opened.frsm_id) +
                        (opened.file_size_bytes
                            ? ", declaredSize=" + std::to_string(*opened.file_size_bytes) + "."
                            : ", declared size unavailable."),
                    invoke_id,
                    opened.frsm_id,
                    std::nullopt,
                    std::nullopt,
                    hex_preview(request, options.maximum_diagnostic_bytes),
                    hex_preview(response_payload, options.maximum_diagnostic_bytes)},
                    options.maximum_diagnostics, options.maximum_diagnostic_bytes);
                if (result.expected_bytes && *result.expected_bytes > options.maximum_bytes) {
                    fail(MmsFileFailureKind::limit_exceeded,
                        "Remote file declared size exceeds the configured maximum byte limit.");
                }
            } catch (const MmsFileServiceError& exception) {
                if (result.diagnostics.empty() ||
                    result.diagnostics.back().message != exception.diagnostic().message) {
                    auto diagnostic = exception.diagnostic();
                    diagnostic.stage = "FileOpen";
                    diagnostic.request_hex = hex_preview(
                        request, options.maximum_diagnostic_bytes);
                    append_diagnostic(result.diagnostics, std::move(diagnostic),
                        options.maximum_diagnostics, options.maximum_diagnostic_bytes);
                }
                fail(exception.diagnostic().failure_kind, exception.what());
            }
        }

        bool more_follows = result.failure_kind == MmsFileFailureKind::none;
        while (more_follows) {
            if (stop_token.stop_requested()) {
                fail(MmsFileFailureKind::cancelled, "File transfer cancelled before FileRead.");
                break;
            }
            if (result.read_operations >= options.maximum_read_operations) {
                fail(MmsFileFailureKind::limit_exceeded,
                    "FileRead exceeded the configured operation limit.");
                break;
            }
            ++result.read_operations;
            const auto invoke_id = channel_.next_invoke_id();
            const auto request = MmsFileServiceCodec::encode_file_read_request_p_data(
                {invoke_id, *frsm}, channel_.presentation_context_id());
            try {
                const auto response_payload = exchange("FileRead", invoke_id, request);
                const auto block = MmsFileServiceCodec::decode_file_read_response(
                    response_payload, invoke_id);
                if (block.data.empty() && block.more_follows) {
                    fail(MmsFileFailureKind::protocol,
                        "FileRead returned an empty block while moreFollows remained true.");
                } else if (block.data.size() > options.maximum_block_bytes) {
                    fail(MmsFileFailureKind::limit_exceeded,
                        "FileRead block exceeds the configured maximum block size.");
                } else if (static_cast<std::uint64_t>(block.data.size()) >
                           options.maximum_bytes - result.bytes_transferred) {
                    fail(MmsFileFailureKind::limit_exceeded,
                        "File transfer exceeds the configured maximum byte limit.");
                } else if (result.expected_bytes &&
                           static_cast<std::uint64_t>(block.data.size()) >
                               *result.expected_bytes -
                               std::min(*result.expected_bytes, result.bytes_transferred)) {
                    fail(MmsFileFailureKind::protocol,
                        "FileRead bytes exceed the advertised remote file size.");
                }

                append_diagnostic(result.diagnostics, {
                    "FileRead #" + std::to_string(result.read_operations),
                    result.failure_kind,
                    result.failure_kind == MmsFileFailureKind::none,
                    "FileRead decoded " + std::to_string(block.data.size()) +
                        " byte(s); moreFollows=" +
                        (block.more_follows ? "true." : "false."),
                    invoke_id,
                    *frsm,
                    std::nullopt,
                    std::nullopt,
                    hex_preview(request, options.maximum_diagnostic_bytes),
                    hex_preview(response_payload, options.maximum_diagnostic_bytes)},
                    options.maximum_diagnostics, options.maximum_diagnostic_bytes);

                if (result.failure_kind != MmsFileFailureKind::none) {
                    break;
                }
                if (!block.data.empty()) {
                    std::string sink_error;
                    if (!sink.write(block.data, sink_error)) {
                        append_diagnostic(result.diagnostics, {
                            "Sink write after FileRead #" +
                                std::to_string(result.read_operations),
                            MmsFileFailureKind::sink,
                            false,
                            sink_error,
                            invoke_id,
                            *frsm,
                            std::nullopt,
                            std::nullopt,
                            {},
                            {}}, options.maximum_diagnostics,
                            options.maximum_diagnostic_bytes);
                        fail(MmsFileFailureKind::sink,
                            "Local sink write failed: " + sink_error);
                        break;
                    }
                    result.bytes_transferred +=
                        static_cast<std::uint64_t>(block.data.size());
                }
                more_follows = block.more_follows;
                if (progress != nullptr) {
                    progress->report({
                        result.remote_path,
                        result.bytes_transferred,
                        result.expected_bytes,
                        result.read_operations,
                        !more_follows});
                }
            } catch (const MmsFileServiceError& exception) {
                if (result.diagnostics.empty() ||
                    result.diagnostics.back().message != exception.diagnostic().message) {
                    auto diagnostic = exception.diagnostic();
                    diagnostic.stage = "FileRead #" +
                        std::to_string(result.read_operations);
                    diagnostic.frsm_id = frsm;
                    diagnostic.request_hex = hex_preview(
                        request, options.maximum_diagnostic_bytes);
                    append_diagnostic(result.diagnostics, std::move(diagnostic),
                        options.maximum_diagnostics, options.maximum_diagnostic_bytes);
                }
                fail(exception.diagnostic().failure_kind, exception.what());
                break;
            }
        }

        if (result.failure_kind == MmsFileFailureKind::none &&
            options.require_declared_size_match && result.expected_bytes &&
            result.bytes_transferred != *result.expected_bytes) {
            fail(MmsFileFailureKind::protocol,
                "Transferred byte count does not match the advertised file size.");
        }
        if (result.failure_kind == MmsFileFailureKind::none) {
            std::string flush_error;
            if (!sink.flush(flush_error)) {
                fail(MmsFileFailureKind::sink,
                    "Local sink flush failed: " + flush_error);
            }
        }
    } catch (const std::exception& exception) {
        fail(MmsFileFailureKind::transport,
            std::string{"File transfer pipeline failed: "} + exception.what());
    }

    if (frsm) {
        if (channel_.associated()) {
            const auto invoke_id = channel_.next_invoke_id();
            std::vector<std::uint8_t> request;
            try {
                request = MmsFileServiceCodec::encode_file_close_request_p_data(
                    {invoke_id, *frsm}, channel_.presentation_context_id());
                const auto response_payload = channel_.exchange_confirmed(
                    request, invoke_id, {});
                static_cast<void>(MmsFileServiceCodec::decode_file_close_response(
                    response_payload, invoke_id));
                result.remote_file_closed = true;
                append_diagnostic(result.diagnostics, {
                    "FileClose",
                    MmsFileFailureKind::none,
                    true,
                    "FileClose succeeded for FRSM=" + std::to_string(*frsm) + ".",
                    invoke_id,
                    *frsm,
                    std::nullopt,
                    std::nullopt,
                    hex_preview(request, options.maximum_diagnostic_bytes),
                    hex_preview(response_payload, options.maximum_diagnostic_bytes)},
                    options.maximum_diagnostics, options.maximum_diagnostic_bytes);
            } catch (const MmsFileServiceError& exception) {
                auto diagnostic = exception.diagnostic();
                diagnostic.stage = "FileClose";
                diagnostic.frsm_id = frsm;
                diagnostic.request_hex = hex_preview(
                    request, options.maximum_diagnostic_bytes);
                append_diagnostic(result.diagnostics, std::move(diagnostic),
                    options.maximum_diagnostics, options.maximum_diagnostic_bytes);
                if (result.failure_kind == MmsFileFailureKind::none) {
                    result.failure_kind = MmsFileFailureKind::cleanup;
                    result.message = exception.what();
                }
            } catch (const std::exception& exception) {
                append_diagnostic(result.diagnostics, {
                    "FileClose",
                    MmsFileFailureKind::cleanup,
                    false,
                    std::string{"FileClose exchange failed: "} + exception.what(),
                    invoke_id,
                    *frsm,
                    std::nullopt,
                    std::nullopt,
                    hex_preview(request, options.maximum_diagnostic_bytes),
                    {}}, options.maximum_diagnostics, options.maximum_diagnostic_bytes);
                if (result.failure_kind == MmsFileFailureKind::none) {
                    result.failure_kind = MmsFileFailureKind::cleanup;
                    result.message = std::string{"FileClose failed: "} + exception.what();
                }
            }
        } else {
            append_diagnostic(result.diagnostics, {
                "FileClose",
                MmsFileFailureKind::cleanup,
                false,
                "FileClose skipped because the MMS association is no longer active.",
                std::nullopt,
                *frsm,
                std::nullopt,
                std::nullopt,
                {},
                {}}, options.maximum_diagnostics, options.maximum_diagnostic_bytes);
            if (result.failure_kind == MmsFileFailureKind::none) {
                result.failure_kind = MmsFileFailureKind::cleanup;
                result.message = "FileClose skipped because the association is inactive.";
            }
        }
    }

    result.success = result.failure_kind == MmsFileFailureKind::none &&
                     (!frsm || result.remote_file_closed);
    if (result.success) {
        result.message = "Downloaded " + std::to_string(result.bytes_transferred) +
            " byte(s) from '" + result.remote_path + "' in " +
            std::to_string(result.read_operations) + " FileRead operation(s).";
        if (progress != nullptr) {
            progress->report({
                result.remote_path,
                result.bytes_transferred,
                result.expected_bytes,
                result.read_operations,
                true});
        }
    } else if (result.message.empty()) {
        result.message = "MMS file transfer failed.";
    }
    return result;
}

} // namespace ar::iec61850::mms
