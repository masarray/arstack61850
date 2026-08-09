// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/services_span.hpp"

#include "ariec61850/asn1/ber_span_reader.hpp"
#include "ariec61850/asn1/ber_span_writer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {
namespace {

[[nodiscard]] bool visible_ascii(
    const std::span<const std::uint8_t> value,
    const bool allow_empty = false) noexcept {
    if ((!allow_empty && value.empty()) ||
        value.size() > MmsServiceSpanCodec::maximum_identifier_bytes) {
        return false;
    }
    for (const auto byte : value) {
        if (byte == 0U || byte > 0x7FU) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool visible_ascii(
    const std::string_view value,
    const bool allow_empty = false) noexcept {
    if ((!allow_empty && value.empty()) ||
        value.size() > MmsServiceSpanCodec::maximum_identifier_bytes) {
        return false;
    }
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte == 0U || byte > 0x7FU) {
            return false;
        }
    }
    return true;
}

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

[[nodiscard]] bool valid_identifier_tlv(
    const asn1::BerTlvView& tlv) noexcept {
    return !tlv.constructed &&
        tlv.tag_class == asn1::BerClass::universal &&
        (tlv.tag_number == 22 || tlv.tag_number == 26) &&
        visible_ascii(tlv.value);
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

[[nodiscard]] std::optional<std::size_t> add_size(
    const std::optional<std::size_t> left,
    const std::optional<std::size_t> right) noexcept {
    if (!left || !right || *right > std::numeric_limits<std::size_t>::max() - *left) {
        return std::nullopt;
    }
    return *left + *right;
}

} // namespace

bool MmsGetNameListResponseView::try_identifier(
    const std::size_t index,
    std::span<const std::uint8_t>& identifier) const noexcept {
    identifier = {};
    if (index >= identifier_count) {
        return false;
    }

    std::size_t offset = 0U;
    std::size_t current = 0U;
    while (offset < identifier_list.size()) {
        asn1::BerTlvView item;
        if (!asn1::BerSpanReader::try_read_tlv(identifier_list, offset, item) ||
            !valid_identifier_tlv(item)) {
            return false;
        }
        if (current == index) {
            identifier = item.value;
            return true;
        }
        ++current;
    }
    return false;
}

bool MmsServiceSpanCodec::try_decode_get_name_list_request(
    const MmsConfirmedPduView& confirmed,
    MmsGetNameListRequestView& request) noexcept {
    request = {};
    if (confirmed.kind != MmsWirePduKind::confirmed_request ||
        confirmed.service_tag != 1 || !confirmed.service_constructed) {
        return false;
    }

    bool have_class = false;
    bool have_scope = false;
    bool have_continuation = false;
    std::size_t count = 0U;
    std::size_t offset = 0U;
    while (offset < confirmed.service_value.size()) {
        if (count >= 3U) {
            request = {};
            return false;
        }
        ++count;
        asn1::BerTlvView field;
        if (!asn1::BerSpanReader::try_read_tlv(
                confirmed.service_value, offset, field) ||
            field.tag_class != asn1::BerClass::context_specific) {
            request = {};
            return false;
        }

        if (field.tag_number == 0 && field.constructed) {
            if (have_class) {
                request = {};
                return false;
            }
            asn1::BerTlvView inner;
            std::uint32_t value{};
            if (!asn1::BerSpanReader::try_read_exact(field.value, inner) ||
                inner.tag_class != asn1::BerClass::context_specific ||
                inner.tag_number != 0 || inner.constructed ||
                !read_u32(inner, value) ||
                (value != 0U && value != 2U && value != 9U)) {
                request = {};
                return false;
            }
            request.object_class = static_cast<MmsNameListObjectClass>(value);
            have_class = true;
        } else if (field.tag_number == 1 && field.constructed) {
            if (have_scope) {
                request = {};
                return false;
            }
            asn1::BerTlvView inner;
            if (!asn1::BerSpanReader::try_read_exact(field.value, inner) ||
                inner.tag_class != asn1::BerClass::context_specific ||
                inner.constructed) {
                request = {};
                return false;
            }
            switch (inner.tag_number) {
            case 0:
                if (!inner.value.empty()) {
                    request = {};
                    return false;
                }
                request.scope = MmsNameScopeKind::vmd_specific;
                break;
            case 1:
                if (!visible_ascii(inner.value)) {
                    request = {};
                    return false;
                }
                request.scope = MmsNameScopeKind::domain_specific;
                request.domain_id = inner.value;
                break;
            case 2:
                if (!inner.value.empty()) {
                    request = {};
                    return false;
                }
                request.scope = MmsNameScopeKind::aa_specific;
                break;
            default:
                request = {};
                return false;
            }
            have_scope = true;
        } else if (field.tag_number == 2 && !field.constructed) {
            if (have_continuation || !visible_ascii(field.value)) {
                request = {};
                return false;
            }
            request.continue_after = field.value;
            have_continuation = true;
        } else {
            request = {};
            return false;
        }
    }

    if (!have_class || !have_scope) {
        request = {};
        return false;
    }
    request.invoke_id = confirmed.invoke_id;
    return true;
}

bool MmsServiceSpanCodec::try_decode_get_name_list_request(
    const std::span<const std::uint8_t> mms_pdu,
    MmsGetNameListRequestView& request) noexcept {
    MmsConfirmedPduView confirmed;
    return MmsPduSpanCodec::try_decode_confirmed_request_view(mms_pdu, confirmed) &&
        try_decode_get_name_list_request(confirmed, request);
}

bool MmsServiceSpanCodec::try_decode_get_name_list_response(
    const MmsConfirmedPduView& confirmed,
    MmsGetNameListResponseView& response) noexcept {
    response = {};
    if (confirmed.kind != MmsWirePduKind::confirmed_response ||
        confirmed.service_tag != 1 || !confirmed.service_constructed) {
        return false;
    }

    bool have_list = false;
    bool have_more = false;
    std::size_t offset = 0U;
    while (offset < confirmed.service_value.size()) {
        asn1::BerTlvView field;
        if (!asn1::BerSpanReader::try_read_tlv(
                confirmed.service_value, offset, field) ||
            field.tag_class != asn1::BerClass::context_specific) {
            response = {};
            return false;
        }

        if (field.tag_number == 0 && field.constructed) {
            if (have_list) {
                response = {};
                return false;
            }
            std::size_t name_offset = 0U;
            std::size_t name_count = 0U;
            while (name_offset < field.value.size()) {
                if (name_count >= maximum_identifiers) {
                    response = {};
                    return false;
                }
                asn1::BerTlvView name;
                if (!asn1::BerSpanReader::try_read_tlv(
                        field.value, name_offset, name) ||
                    !valid_identifier_tlv(name)) {
                    response = {};
                    return false;
                }
                ++name_count;
            }
            response.identifier_list = field.value;
            response.identifier_count = name_count;
            have_list = true;
        } else if (field.tag_number == 1 && !field.constructed) {
            if (have_more || field.value.size() != 1U) {
                response = {};
                return false;
            }
            response.more_follows = field.value[0] != 0U;
            have_more = true;
        } else {
            response = {};
            return false;
        }
    }

    if (!have_list) {
        response = {};
        return false;
    }
    response.invoke_id = confirmed.invoke_id;
    return true;
}

bool MmsServiceSpanCodec::try_decode_get_name_list_response(
    const std::span<const std::uint8_t> mms_pdu,
    MmsGetNameListResponseView& response) noexcept {
    MmsConfirmedPduView confirmed;
    return MmsPduSpanCodec::try_decode_confirmed_response_view(mms_pdu, confirmed) &&
        try_decode_get_name_list_response(confirmed, response);
}

wire::EncodeResult MmsServiceSpanCodec::encode_get_name_list_response_into(
    const std::uint32_t invoke_id,
    const std::span<const std::string_view> names,
    const bool more_follows,
    const std::span<std::uint8_t> destination) noexcept {
    if (invoke_id > MmsPduSpanCodec::maximum_invoke_id ||
        names.size() > maximum_identifiers) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    std::size_t names_content = 0U;
    for (const auto name : names) {
        if (!visible_ascii(name)) {
            return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
        }
        const auto encoded = asn1::BerSpanWriter::tlv_size(26, name.size());
        if (!encoded || *encoded > std::numeric_limits<std::size_t>::max() - names_content) {
            return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
        }
        names_content += *encoded;
    }

    const auto list_tlv = asn1::BerSpanWriter::tlv_size(0, names_content);
    const auto more_tlv = asn1::BerSpanWriter::tlv_size(1, 1U);
    const auto service_content = add_size(list_tlv, more_tlv);
    if (!service_content) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto service_tlv = asn1::BerSpanWriter::tlv_size(1, *service_content);

    const auto invoke_value = positive_integer_size(invoke_id);
    const auto invoke_tlv = asn1::BerSpanWriter::tlv_size(2, invoke_value);
    const auto outer_content = add_size(invoke_tlv, service_tlv);
    if (!outer_content) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto required = asn1::BerSpanWriter::tlv_size(1, *outer_content);
    if (!required || *required > MmsPduSpanCodec::maximum_pdu_bytes) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (destination.size() < *required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, *required};
    }

    asn1::BerSpanWriter writer{destination.first(*required)};
    if (!writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 1, *outer_content) ||
        !writer.write_tlv_header(
            asn1::BerClass::universal, false, 2, invoke_value) ||
        !write_positive_integer(writer, invoke_id) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 1, *service_content) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 0, names_content)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }

    for (const auto name : names) {
        if (!writer.write_tlv_header(
                asn1::BerClass::universal, false, 26, name.size())) {
            return {wire::EncodeStatus::value_out_of_range, 0U, *required};
        }
        for (const auto character : name) {
            if (!writer.write_byte(static_cast<std::uint8_t>(
                    static_cast<unsigned char>(character)))) {
                return {wire::EncodeStatus::value_out_of_range, 0U, *required};
            }
        }
    }

    if (!writer.write_tlv_header(
            asn1::BerClass::context_specific, false, 1, 1U) ||
        !writer.write_byte(more_follows ? 0xFFU : 0x00U) ||
        writer.size() != *required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }
    return {wire::EncodeStatus::ok, *required, *required};
}

} // namespace ar::iec61850::mms
