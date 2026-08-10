// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/services_span.hpp"

#include "ariec61850/asn1/ber_span_reader.hpp"
#include "ariec61850/asn1/ber_span_writer.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace ar::iec61850::mms {
namespace {

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

[[nodiscard]] bool valid_mms_data_tlv(
    const asn1::BerTlvView& tlv) noexcept {
    if (tlv.tag_class != asn1::BerClass::context_specific) {
        return false;
    }
    if (tlv.tag_number == 1 || tlv.tag_number == 2) {
        return tlv.constructed;
    }
    return tlv.tag_number >= 3 && tlv.tag_number <= 17 && !tlv.constructed;
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

struct ConfirmedResponseSizing final {
    std::size_t invoke_value{};
    std::size_t outer_content{};
    std::size_t required{};
};

[[nodiscard]] std::optional<ConfirmedResponseSizing> confirmed_response_sizing(
    const std::uint32_t invoke_id,
    const std::size_t service_content) noexcept {
    if (invoke_id > MmsPduSpanCodec::maximum_invoke_id) {
        return std::nullopt;
    }
    const auto invoke_value = positive_integer_size(invoke_id);
    const auto invoke_tlv = asn1::BerSpanWriter::tlv_size(2, invoke_value);
    const auto service_tlv = asn1::BerSpanWriter::tlv_size(5, service_content);
    const auto outer_content = add_size(invoke_tlv, service_tlv);
    if (!outer_content) {
        return std::nullopt;
    }
    const auto required = asn1::BerSpanWriter::tlv_size(1, *outer_content);
    if (!required || *required > MmsPduSpanCodec::maximum_pdu_bytes) {
        return std::nullopt;
    }
    return ConfirmedResponseSizing{invoke_value, *outer_content, *required};
}

[[nodiscard]] bool write_confirmed_response_prefix(
    asn1::BerSpanWriter& writer,
    const ConfirmedResponseSizing& sizing,
    const std::uint32_t invoke_id,
    const std::size_t service_content) noexcept {
    return writer.write_tlv_header(
               asn1::BerClass::context_specific, true, 1, sizing.outer_content) &&
        writer.write_tlv_header(
               asn1::BerClass::universal, false, 2, sizing.invoke_value) &&
        write_positive_integer(writer, invoke_id) &&
        writer.write_tlv_header(
               asn1::BerClass::context_specific, true, 5, service_content);
}

[[nodiscard]] bool try_read_variable_definition(
    const std::span<const std::uint8_t> variable_list,
    const std::size_t wanted,
    MmsObjectNameView& name) noexcept {
    name = {};
    std::size_t offset = 0U;
    std::size_t current = 0U;
    while (offset < variable_list.size()) {
        asn1::BerTlvView definition;
        if (!asn1::BerSpanReader::try_read_tlv(variable_list, offset, definition) ||
            definition.tag_class != asn1::BerClass::universal ||
            definition.tag_number != 16 || !definition.constructed) {
            return false;
        }
        asn1::BerTlvView wrapper;
        if (!asn1::BerSpanReader::try_read_exact(definition.value, wrapper) ||
            wrapper.tag_class != asn1::BerClass::context_specific ||
            wrapper.tag_number != 0 || !wrapper.constructed) {
            return false;
        }
        MmsObjectNameView decoded;
        if (!MmsServiceSpanCodec::try_decode_object_name_view(wrapper.value, decoded)) {
            return false;
        }
        if (current == wanted) {
            name = decoded;
            return true;
        }
        ++current;
    }
    return false;
}

[[nodiscard]] bool try_read_data_value(
    const std::span<const std::uint8_t> data_list,
    const std::size_t wanted,
    std::span<const std::uint8_t>& encoded_data) noexcept {
    encoded_data = {};
    std::size_t offset = 0U;
    std::size_t current = 0U;
    while (offset < data_list.size()) {
        const auto start = offset;
        asn1::BerTlvView value;
        if (!asn1::BerSpanReader::try_read_tlv(data_list, offset, value) ||
            !valid_mms_data_tlv(value)) {
            return false;
        }
        if (current == wanted) {
            encoded_data = data_list.subspan(start, offset - start);
            return true;
        }
        ++current;
    }
    return false;
}

[[nodiscard]] bool try_read_write_result(
    const std::span<const std::uint8_t> result_list,
    const std::size_t wanted,
    MmsWriteAccessResultView& result) noexcept {
    result = {};
    std::size_t offset = 0U;
    std::size_t current = 0U;
    while (offset < result_list.size()) {
        asn1::BerTlvView field;
        if (!asn1::BerSpanReader::try_read_tlv(result_list, offset, field) ||
            field.tag_class != asn1::BerClass::context_specific || field.constructed) {
            return false;
        }
        MmsWriteAccessResultView decoded;
        if (field.tag_number == 1) {
            if (!field.value.empty()) {
                return false;
            }
            decoded.success = true;
        } else if (field.tag_number == 0) {
            if (!read_u32(field, decoded.failure_code)) {
                return false;
            }
            decoded.success = false;
        } else {
            return false;
        }
        if (current == wanted) {
            result = decoded;
            return true;
        }
        ++current;
    }
    return false;
}

} // namespace

bool MmsWriteRequestView::try_variable(
    const std::size_t index,
    MmsObjectNameView& name) const noexcept {
    if (index >= variable_count) {
        name = {};
        return false;
    }
    return try_read_variable_definition(variable_list, index, name);
}

bool MmsWriteRequestView::try_value(
    const std::size_t index,
    std::span<const std::uint8_t>& encoded_data) const noexcept {
    if (index >= data_count) {
        encoded_data = {};
        return false;
    }
    return try_read_data_value(data_list, index, encoded_data);
}

bool MmsWriteResponseView::try_result(
    const std::size_t index,
    MmsWriteAccessResultView& result) const noexcept {
    if (implicit_single_success) {
        if (index != 0U) {
            result = {};
            return false;
        }
        result = MmsWriteAccessResultView{true, 0U};
        return true;
    }
    if (index >= result_count) {
        result = {};
        return false;
    }
    return try_read_write_result(result_list, index, result);
}

bool MmsServiceSpanCodec::try_decode_write_request(
    const MmsConfirmedPduView& confirmed,
    MmsWriteRequestView& request) noexcept {
    request = {};
    if (confirmed.kind != MmsWirePduKind::confirmed_request ||
        confirmed.service_tag != 5 || !confirmed.service_constructed) {
        return false;
    }

    std::size_t offset = 0U;
    asn1::BerTlvView variables;
    asn1::BerTlvView data;
    if (!asn1::BerSpanReader::try_read_tlv(confirmed.service_value, offset, variables) ||
        !asn1::BerSpanReader::try_read_tlv(confirmed.service_value, offset, data) ||
        offset != confirmed.service_value.size() ||
        variables.tag_class != asn1::BerClass::context_specific ||
        variables.tag_number != 0 || !variables.constructed ||
        data.tag_class != asn1::BerClass::context_specific ||
        data.tag_number != 0 || !data.constructed) {
        return false;
    }

    std::size_t variable_offset = 0U;
    std::size_t variable_count = 0U;
    while (variable_offset < variables.value.size()) {
        if (variable_count >= maximum_variables) {
            return false;
        }
        const auto before = variable_offset;
        asn1::BerTlvView definition;
        if (!asn1::BerSpanReader::try_read_tlv(
                variables.value, variable_offset, definition)) {
            return false;
        }
        MmsObjectNameView ignored;
        if (!try_read_variable_definition(
                variables.value.subspan(before, variable_offset - before), 0U, ignored)) {
            return false;
        }
        ++variable_count;
    }

    std::size_t data_offset = 0U;
    std::size_t data_count = 0U;
    while (data_offset < data.value.size()) {
        if (data_count >= maximum_variables) {
            return false;
        }
        asn1::BerTlvView value;
        if (!asn1::BerSpanReader::try_read_tlv(data.value, data_offset, value) ||
            !valid_mms_data_tlv(value)) {
            return false;
        }
        ++data_count;
    }

    if (variable_count == 0U || variable_count != data_count) {
        return false;
    }
    request.invoke_id = confirmed.invoke_id;
    request.variable_list = variables.value;
    request.variable_count = variable_count;
    request.data_list = data.value;
    request.data_count = data_count;
    return true;
}

bool MmsServiceSpanCodec::try_decode_write_request(
    const std::span<const std::uint8_t> mms_pdu,
    MmsWriteRequestView& request) noexcept {
    MmsConfirmedPduView confirmed;
    return MmsPduSpanCodec::try_decode_confirmed_request_view(mms_pdu, confirmed) &&
        try_decode_write_request(confirmed, request);
}

bool MmsServiceSpanCodec::try_decode_write_response(
    const MmsConfirmedPduView& confirmed,
    MmsWriteResponseView& response) noexcept {
    response = {};
    if (confirmed.kind != MmsWirePduKind::confirmed_response ||
        confirmed.service_tag != 5) {
        return false;
    }
    response.invoke_id = confirmed.invoke_id;
    if (!confirmed.service_constructed) {
        if (!confirmed.service_value.empty()) {
            response = {};
            return false;
        }
        response.implicit_single_success = true;
        response.result_count = 1U;
        return true;
    }

    std::size_t offset = 0U;
    std::size_t count = 0U;
    while (offset < confirmed.service_value.size()) {
        if (count >= maximum_variables) {
            response = {};
            return false;
        }
        asn1::BerTlvView field;
        if (!asn1::BerSpanReader::try_read_tlv(
                confirmed.service_value, offset, field) ||
            field.tag_class != asn1::BerClass::context_specific || field.constructed) {
            response = {};
            return false;
        }
        if (field.tag_number == 1) {
            if (!field.value.empty()) {
                response = {};
                return false;
            }
        } else if (field.tag_number == 0) {
            std::uint32_t ignored{};
            if (!read_u32(field, ignored)) {
                response = {};
                return false;
            }
        } else {
            response = {};
            return false;
        }
        ++count;
    }
    if (count == 0U) {
        response = {};
        return false;
    }
    response.result_list = confirmed.service_value;
    response.result_count = count;
    return true;
}

bool MmsServiceSpanCodec::try_decode_write_response(
    const std::span<const std::uint8_t> mms_pdu,
    MmsWriteResponseView& response) noexcept {
    MmsConfirmedPduView confirmed;
    return MmsPduSpanCodec::try_decode_confirmed_response_view(mms_pdu, confirmed) &&
        try_decode_write_response(confirmed, response);
}

wire::EncodeResult MmsServiceSpanCodec::encode_write_response_into(
    const std::uint32_t invoke_id,
    const std::span<const MmsWriteAccessResultInput> results,
    const std::span<std::uint8_t> destination) noexcept {
    if (results.empty() || results.size() > maximum_variables) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    std::size_t service_content = 0U;
    for (const auto& result : results) {
        std::optional<std::size_t> encoded;
        if (result.success) {
            encoded = asn1::BerSpanWriter::tlv_size(1, 0U);
        } else {
            encoded = asn1::BerSpanWriter::tlv_size(
                0, positive_integer_size(result.failure_code));
        }
        if (!encoded || *encoded > std::numeric_limits<std::size_t>::max() - service_content) {
            return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
        }
        service_content += *encoded;
    }

    const auto sizing = confirmed_response_sizing(invoke_id, service_content);
    if (!sizing) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (destination.size() < sizing->required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, sizing->required};
    }

    asn1::BerSpanWriter writer{destination.first(sizing->required)};
    if (!write_confirmed_response_prefix(
            writer, *sizing, invoke_id, service_content)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, sizing->required};
    }
    for (const auto& result : results) {
        if (result.success) {
            if (!writer.write_tlv_header(
                    asn1::BerClass::context_specific, false, 1, 0U)) {
                return {wire::EncodeStatus::value_out_of_range, 0U, sizing->required};
            }
        } else {
            const auto failure_value = positive_integer_size(result.failure_code);
            if (!writer.write_tlv_header(
                    asn1::BerClass::context_specific, false, 0, failure_value) ||
                !write_positive_integer(writer, result.failure_code)) {
                return {wire::EncodeStatus::value_out_of_range, 0U, sizing->required};
            }
        }
    }
    if (writer.size() != sizing->required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, sizing->required};
    }
    return {wire::EncodeStatus::ok, sizing->required, sizing->required};
}

} // namespace ar::iec61850::mms
