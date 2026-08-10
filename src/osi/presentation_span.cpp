// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/osi/presentation_span.hpp"

#include "ariec61850/asn1/ber_span_reader.hpp"
#include "ariec61850/asn1/ber_span_writer.hpp"
#include "ariec61850/osi/session_span.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace ar::iec61850::osi {
namespace {

constexpr std::array<std::uint8_t, 4U> kAcseAbstractSyntax{
    0x52U, 0x01U, 0x00U, 0x01U};
constexpr std::array<std::uint8_t, 5U> kMmsAbstractSyntax{
    0x28U, 0xCAU, 0x22U, 0x02U, 0x01U};
constexpr std::array<std::uint8_t, 2U> kBerTransferSyntax{0x51U, 0x01U};

[[nodiscard]] std::size_t unsigned_integer_size(std::uint32_t value) noexcept {
    std::size_t size = 1U;
    while (value > 0xFFU) {
        ++size;
        value >>= 8U;
    }
    return size;
}

[[nodiscard]] bool write_unsigned_integer(
    asn1::BerSpanWriter& writer,
    const std::uint32_t value) noexcept {
    const auto size = unsigned_integer_size(value);
    for (std::size_t index = size; index-- > 0U;) {
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

[[nodiscard]] bool valid_oid(
    const std::span<const std::uint8_t> value) noexcept {
    return !value.empty() && value.size() <= PresentationSpanCodec::maximum_oid_bytes;
}

[[nodiscard]] bool read_positive_integer(
    const asn1::BerTlvView& tlv,
    std::uint32_t& value) noexcept {
    const auto decoded = asn1::BerSpanReader::read_uint32(tlv);
    if (!decoded || *decoded == 0U) {
        value = 0U;
        return false;
    }
    value = *decoded;
    return true;
}

struct ContextDefinitionView final {
    std::uint32_t id{};
    std::span<const std::uint8_t> abstract_syntax{};
    std::span<const std::uint8_t> first_transfer_syntax{};
    std::size_t transfer_syntax_count{};
};

[[nodiscard]] bool parse_context_definition(
    const asn1::BerTlvView& definition,
    ContextDefinitionView& context) noexcept {
    context = {};
    if (definition.tag_class != asn1::BerClass::universal ||
        definition.tag_number != 16 || !definition.constructed) {
        return false;
    }

    bool saw_id = false;
    bool saw_abstract = false;
    bool saw_transfers = false;
    std::size_t offset = 0U;
    while (offset < definition.value.size()) {
        asn1::BerTlvView field;
        if (!asn1::BerSpanReader::try_read_tlv(definition.value, offset, field)) {
            return false;
        }

        if (field.tag_class == asn1::BerClass::universal &&
            field.tag_number == 2 && !field.constructed) {
            if (saw_id || !read_positive_integer(field, context.id)) {
                return false;
            }
            saw_id = true;
        } else if (field.tag_class == asn1::BerClass::universal &&
                   field.tag_number == 6 && !field.constructed) {
            if (saw_abstract || !valid_oid(field.value)) {
                return false;
            }
            context.abstract_syntax = field.value;
            saw_abstract = true;
        } else if (field.tag_class == asn1::BerClass::universal &&
                   field.tag_number == 16 && field.constructed) {
            if (saw_transfers) {
                return false;
            }
            std::size_t transfer_offset = 0U;
            while (transfer_offset < field.value.size()) {
                if (context.transfer_syntax_count >=
                    PresentationSpanCodec::maximum_contexts) {
                    return false;
                }
                asn1::BerTlvView syntax;
                if (!asn1::BerSpanReader::try_read_tlv(
                        field.value, transfer_offset, syntax) ||
                    syntax.tag_class != asn1::BerClass::universal ||
                    syntax.tag_number != 6 || syntax.constructed ||
                    !valid_oid(syntax.value)) {
                    return false;
                }
                if (context.transfer_syntax_count == 0U) {
                    context.first_transfer_syntax = syntax.value;
                }
                ++context.transfer_syntax_count;
            }
            if (context.transfer_syntax_count == 0U) {
                return false;
            }
            saw_transfers = true;
        } else {
            return false;
        }
    }
    return saw_id && saw_abstract && saw_transfers;
}

[[nodiscard]] bool validate_context_list(
    const std::span<const std::uint8_t> list,
    std::size_t& count) noexcept {
    count = 0U;
    std::array<std::uint32_t, PresentationSpanCodec::maximum_contexts> ids{};
    std::size_t offset = 0U;
    while (offset < list.size()) {
        if (count >= PresentationSpanCodec::maximum_contexts) {
            count = 0U;
            return false;
        }
        asn1::BerTlvView definition;
        ContextDefinitionView context;
        if (!asn1::BerSpanReader::try_read_tlv(list, offset, definition) ||
            !parse_context_definition(definition, context)) {
            count = 0U;
            return false;
        }
        for (std::size_t index = 0U; index < count; ++index) {
            if (ids[index] == context.id) {
                count = 0U;
                return false;
            }
        }
        ids[count++] = context.id;
    }
    return count != 0U;
}

[[nodiscard]] bool find_context_by_abstract_syntax(
    const std::span<const std::uint8_t> list,
    const std::span<const std::uint8_t> oid,
    std::uint32_t& id) noexcept {
    id = 0U;
    std::size_t offset = 0U;
    while (offset < list.size()) {
        asn1::BerTlvView definition;
        ContextDefinitionView context;
        if (!asn1::BerSpanReader::try_read_tlv(list, offset, definition) ||
            !parse_context_definition(definition, context)) {
            return false;
        }
        if (context.abstract_syntax.size() == oid.size() &&
            std::equal(
                context.abstract_syntax.begin(),
                context.abstract_syntax.end(),
                oid.begin())) {
            id = context.id;
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool context_id_exists(
    const std::span<const std::uint8_t> list,
    const std::uint32_t id) noexcept {
    std::size_t offset = 0U;
    while (offset < list.size()) {
        asn1::BerTlvView definition;
        ContextDefinitionView context;
        if (!asn1::BerSpanReader::try_read_tlv(list, offset, definition) ||
            !parse_context_definition(definition, context)) {
            return false;
        }
        if (context.id == id) {
            return true;
        }
    }
    return false;
}

struct FullyEncodedSizes final {
    std::size_t id_value{};
    std::size_t id_tlv{};
    std::size_t payload_tlv{};
    std::size_t sequence_content{};
    std::size_t sequence_tlv{};
    std::size_t outer_tlv{};
};

[[nodiscard]] std::optional<FullyEncodedSizes> calculate_fully_encoded_sizes(
    const std::uint32_t context_id,
    const std::size_t payload_bytes) noexcept {
    if (context_id == 0U || payload_bytes > PresentationSpanCodec::maximum_ppdu_bytes) {
        return std::nullopt;
    }

    FullyEncodedSizes sizes;
    sizes.id_value = unsigned_integer_size(context_id);
    const auto id_tlv = asn1::BerSpanWriter::tlv_size(2, sizes.id_value);
    const auto payload_tlv = asn1::BerSpanWriter::tlv_size(0, payload_bytes);
    const auto sequence_content = add_size(id_tlv, payload_tlv);
    if (!id_tlv || !payload_tlv || !sequence_content) {
        return std::nullopt;
    }
    const auto sequence_tlv = asn1::BerSpanWriter::tlv_size(16, *sequence_content);
    if (!sequence_tlv) {
        return std::nullopt;
    }
    const auto outer_tlv = asn1::BerSpanWriter::tlv_size(1, *sequence_tlv);
    if (!outer_tlv || *outer_tlv > PresentationSpanCodec::maximum_ppdu_bytes) {
        return std::nullopt;
    }

    sizes.id_tlv = *id_tlv;
    sizes.payload_tlv = *payload_tlv;
    sizes.sequence_content = *sequence_content;
    sizes.sequence_tlv = *sequence_tlv;
    sizes.outer_tlv = *outer_tlv;
    return sizes;
}

[[nodiscard]] bool decode_fully_encoded_outer(
    const asn1::BerTlvView& outer,
    PresentationPdvView& pdv) noexcept {
    pdv = {};
    if (outer.tag_class != asn1::BerClass::application ||
        outer.tag_number != 1 || !outer.constructed) {
        return false;
    }

    asn1::BerTlvView sequence;
    if (!asn1::BerSpanReader::try_read_exact(outer.value, sequence) ||
        sequence.tag_class != asn1::BerClass::universal ||
        sequence.tag_number != 16 || !sequence.constructed) {
        return false;
    }

    bool saw_context_id = false;
    bool saw_payload = false;
    std::size_t offset = 0U;
    while (offset < sequence.value.size()) {
        asn1::BerTlvView field;
        if (!asn1::BerSpanReader::try_read_tlv(sequence.value, offset, field)) {
            pdv = {};
            return false;
        }

        if (field.tag_class == asn1::BerClass::universal &&
            field.tag_number == 2 && !field.constructed) {
            if (saw_context_id || !read_positive_integer(field, pdv.context_id)) {
                pdv = {};
                return false;
            }
            saw_context_id = true;
        } else if (field.tag_class == asn1::BerClass::context_specific &&
                   field.tag_number == 0 && field.constructed) {
            if (saw_payload) {
                pdv = {};
                return false;
            }
            pdv.single_asn1_type = field.value;
            saw_payload = true;
        } else {
            pdv = {};
            return false;
        }
    }
    return saw_context_id && saw_payload;
}

[[nodiscard]] bool decode_mode_selector(
    const asn1::BerTlvView& item,
    std::uint32_t& mode) noexcept {
    if (item.tag_class != asn1::BerClass::context_specific ||
        item.tag_number != 0 || !item.constructed) {
        return false;
    }
    asn1::BerTlvView value;
    return asn1::BerSpanReader::try_read_exact(item.value, value) &&
        value.tag_class == asn1::BerClass::context_specific &&
        value.tag_number == 0 && !value.constructed &&
        read_positive_integer(value, mode);
}

[[nodiscard]] std::optional<std::size_t> context_results_content_size(
    const PresentationCpView& request) noexcept {
    std::size_t total = 0U;
    std::size_t offset = 0U;
    while (offset < request.context_definition_list.size()) {
        asn1::BerTlvView definition;
        ContextDefinitionView context;
        if (!asn1::BerSpanReader::try_read_tlv(
                request.context_definition_list, offset, definition) ||
            !parse_context_definition(definition, context)) {
            return std::nullopt;
        }
        const auto result_tlv = asn1::BerSpanWriter::tlv_size(0, 1U);
        const auto syntax_tlv = asn1::BerSpanWriter::tlv_size(
            1, context.first_transfer_syntax.size());
        const auto sequence_content = add_size(result_tlv, syntax_tlv);
        if (!sequence_content) {
            return std::nullopt;
        }
        const auto sequence_tlv = asn1::BerSpanWriter::tlv_size(
            16, *sequence_content);
        if (!sequence_tlv || *sequence_tlv >
            std::numeric_limits<std::size_t>::max() - total) {
            return std::nullopt;
        }
        total += *sequence_tlv;
    }
    return total;
}

struct CpaSizes final {
    std::size_t mode_value{};
    std::size_t mode_inner_tlv{};
    std::size_t mode_outer_tlv{};
    std::size_t results_content{};
    std::size_t results_tlv{};
    std::size_t user_tlv{};
    std::size_t normal_content{};
    std::size_t normal_tlv{};
    std::size_t outer_content{};
    std::size_t outer_tlv{};
};

[[nodiscard]] std::optional<CpaSizes> calculate_cpa_sizes(
    const PresentationCpView& request,
    const std::size_t acse_aare_bytes) noexcept {
    if (request.mode_selector == 0U || request.context_count == 0U) {
        return std::nullopt;
    }

    CpaSizes sizes;
    sizes.mode_value = unsigned_integer_size(request.mode_selector);
    const auto mode_inner = asn1::BerSpanWriter::tlv_size(0, sizes.mode_value);
    if (!mode_inner) {
        return std::nullopt;
    }
    const auto mode_outer = asn1::BerSpanWriter::tlv_size(0, *mode_inner);
    const auto results_content = context_results_content_size(request);
    if (!mode_outer || !results_content) {
        return std::nullopt;
    }
    const auto results_tlv = asn1::BerSpanWriter::tlv_size(5, *results_content);
    const auto user_tlv = PresentationSpanCodec::fully_encoded_data_size(
        request.user_data.context_id, acse_aare_bytes);
    const auto normal_content = add_size(results_tlv, user_tlv);
    if (!results_tlv || !user_tlv || !normal_content) {
        return std::nullopt;
    }
    const auto normal_tlv = asn1::BerSpanWriter::tlv_size(2, *normal_content);
    const auto outer_content = add_size(mode_outer, normal_tlv);
    if (!normal_tlv || !outer_content) {
        return std::nullopt;
    }
    const auto outer_tlv = asn1::BerSpanWriter::tlv_size(17, *outer_content);
    if (!outer_tlv || *outer_tlv > PresentationSpanCodec::maximum_ppdu_bytes) {
        return std::nullopt;
    }

    sizes.mode_inner_tlv = *mode_inner;
    sizes.mode_outer_tlv = *mode_outer;
    sizes.results_content = *results_content;
    sizes.results_tlv = *results_tlv;
    sizes.user_tlv = *user_tlv;
    sizes.normal_content = *normal_content;
    sizes.normal_tlv = *normal_tlv;
    sizes.outer_content = *outer_content;
    sizes.outer_tlv = *outer_tlv;
    return sizes;
}

} // namespace

bool PresentationCpView::try_context_id_for_abstract_syntax(
    const std::span<const std::uint8_t> object_identifier_value,
    std::uint32_t& context_id) const noexcept {
    return find_context_by_abstract_syntax(
        context_definition_list, object_identifier_value, context_id);
}

std::span<const std::uint8_t>
PresentationSpanCodec::acse_abstract_syntax_name() noexcept {
    return kAcseAbstractSyntax;
}

std::span<const std::uint8_t>
PresentationSpanCodec::mms_abstract_syntax_name() noexcept {
    return kMmsAbstractSyntax;
}

std::span<const std::uint8_t>
PresentationSpanCodec::ber_transfer_syntax_name() noexcept {
    return kBerTransferSyntax;
}

std::optional<std::size_t> PresentationSpanCodec::fully_encoded_data_size(
    const std::uint32_t context_id,
    const std::size_t single_asn1_type_bytes) noexcept {
    const auto sizes = calculate_fully_encoded_sizes(context_id, single_asn1_type_bytes);
    return sizes ? std::optional<std::size_t>{sizes->outer_tlv} : std::nullopt;
}

wire::EncodeResult PresentationSpanCodec::encode_fully_encoded_data_into(
    const std::uint32_t context_id,
    const std::span<const std::uint8_t> single_asn1_type,
    const std::span<std::uint8_t> destination) noexcept {
    const auto sizes = calculate_fully_encoded_sizes(context_id, single_asn1_type.size());
    if (!sizes) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (destination.size() < sizes->outer_tlv) {
        return {wire::EncodeStatus::buffer_too_small, 0U, sizes->outer_tlv};
    }

    asn1::BerSpanWriter writer{destination.first(sizes->outer_tlv)};
    if (!writer.write_tlv_header(
            asn1::BerClass::application, true, 1, sizes->sequence_tlv) ||
        !writer.write_tlv_header(
            asn1::BerClass::universal, true, 16, sizes->sequence_content) ||
        !writer.write_tlv_header(
            asn1::BerClass::universal, false, 2, sizes->id_value) ||
        !write_unsigned_integer(writer, context_id) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific,
            true,
            0,
            single_asn1_type.size()) ||
        !writer.write_bytes(single_asn1_type) ||
        writer.size() != sizes->outer_tlv) {
        return {wire::EncodeStatus::value_out_of_range, 0U, sizes->outer_tlv};
    }
    return {wire::EncodeStatus::ok, sizes->outer_tlv, sizes->outer_tlv};
}

bool PresentationSpanCodec::try_decode_fully_encoded_data_view(
    const std::span<const std::uint8_t> bytes,
    PresentationPdvView& pdv) noexcept {
    pdv = {};
    if (bytes.empty() || bytes.size() > maximum_ppdu_bytes) {
        return false;
    }
    asn1::BerTlvView outer;
    return asn1::BerSpanReader::try_read_exact(bytes, outer) &&
        decode_fully_encoded_outer(outer, pdv);
}

bool PresentationSpanCodec::try_decode_cp_view(
    const std::span<const std::uint8_t> bytes,
    PresentationCpView& cp) noexcept {
    cp = {};
    if (bytes.empty() || bytes.size() > maximum_ppdu_bytes) {
        return false;
    }

    asn1::BerTlvView outer;
    if (!asn1::BerSpanReader::try_read_exact(bytes, outer) ||
        outer.tag_class != asn1::BerClass::universal ||
        outer.tag_number != 17 || !outer.constructed) {
        return false;
    }

    bool saw_mode = false;
    bool saw_normal = false;
    std::size_t offset = 0U;
    while (offset < outer.value.size()) {
        asn1::BerTlvView item;
        if (!asn1::BerSpanReader::try_read_tlv(outer.value, offset, item)) {
            cp = {};
            return false;
        }

        if (item.tag_class == asn1::BerClass::context_specific &&
            item.tag_number == 0 && item.constructed) {
            if (saw_mode || !decode_mode_selector(item, cp.mode_selector)) {
                cp = {};
                return false;
            }
            saw_mode = true;
        } else if (item.tag_class == asn1::BerClass::context_specific &&
                   item.tag_number == 2 && item.constructed) {
            if (saw_normal) {
                cp = {};
                return false;
            }
            bool saw_calling = false;
            bool saw_called = false;
            bool saw_contexts = false;
            bool saw_user_data = false;
            std::size_t normal_offset = 0U;
            while (normal_offset < item.value.size()) {
                asn1::BerTlvView normal;
                if (!asn1::BerSpanReader::try_read_tlv(
                        item.value, normal_offset, normal)) {
                    cp = {};
                    return false;
                }

                if (normal.tag_class == asn1::BerClass::context_specific &&
                    normal.tag_number == 1 && !normal.constructed) {
                    if (saw_calling || normal.value.size() > maximum_selector_bytes) {
                        cp = {};
                        return false;
                    }
                    cp.calling_selector = normal.value;
                    saw_calling = true;
                } else if (normal.tag_class == asn1::BerClass::context_specific &&
                           normal.tag_number == 2 && !normal.constructed) {
                    if (saw_called || normal.value.size() > maximum_selector_bytes) {
                        cp = {};
                        return false;
                    }
                    cp.called_selector = normal.value;
                    saw_called = true;
                } else if (normal.tag_class == asn1::BerClass::context_specific &&
                           normal.tag_number == 4 && normal.constructed) {
                    if (saw_contexts || !validate_context_list(
                            normal.value, cp.context_count)) {
                        cp = {};
                        return false;
                    }
                    cp.context_definition_list = normal.value;
                    saw_contexts = true;
                } else if (normal.tag_class == asn1::BerClass::application &&
                           normal.tag_number == 1 && normal.constructed) {
                    if (saw_user_data || !decode_fully_encoded_outer(
                            normal, cp.user_data)) {
                        cp = {};
                        return false;
                    }
                    saw_user_data = true;
                } else {
                    cp = {};
                    return false;
                }
            }
            if (!saw_contexts || !saw_user_data ||
                !context_id_exists(
                    cp.context_definition_list, cp.user_data.context_id)) {
                cp = {};
                return false;
            }
            saw_normal = true;
        } else {
            cp = {};
            return false;
        }
    }

    if (!saw_mode || !saw_normal) {
        cp = {};
        return false;
    }
    return true;
}

std::optional<std::size_t> PresentationSpanCodec::cpa_accepting_size(
    const PresentationCpView& request,
    const std::size_t acse_aare_bytes) noexcept {
    const auto sizes = calculate_cpa_sizes(request, acse_aare_bytes);
    return sizes ? std::optional<std::size_t>{sizes->outer_tlv} : std::nullopt;
}

wire::EncodeResult PresentationSpanCodec::encode_cpa_accepting_into(
    const PresentationCpView& request,
    const std::span<const std::uint8_t> acse_aare,
    const std::span<std::uint8_t> destination) noexcept {
    const auto sizes = calculate_cpa_sizes(request, acse_aare.size());
    if (!sizes) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (destination.size() < sizes->outer_tlv) {
        return {wire::EncodeStatus::buffer_too_small, 0U, sizes->outer_tlv};
    }

    asn1::BerSpanWriter writer{destination.first(sizes->outer_tlv)};
    if (!writer.write_tlv_header(
            asn1::BerClass::universal, true, 17, sizes->outer_content) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 0, sizes->mode_inner_tlv) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, false, 0, sizes->mode_value) ||
        !write_unsigned_integer(writer, request.mode_selector) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 2, sizes->normal_content) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 5, sizes->results_content)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, sizes->outer_tlv};
    }

    std::size_t context_offset = 0U;
    while (context_offset < request.context_definition_list.size()) {
        asn1::BerTlvView definition;
        ContextDefinitionView context;
        if (!asn1::BerSpanReader::try_read_tlv(
                request.context_definition_list, context_offset, definition) ||
            !parse_context_definition(definition, context)) {
            return {wire::EncodeStatus::value_out_of_range, 0U, sizes->outer_tlv};
        }
        const auto result_tlv = asn1::BerSpanWriter::tlv_size(0, 1U);
        const auto syntax_tlv = asn1::BerSpanWriter::tlv_size(
            1, context.first_transfer_syntax.size());
        const auto sequence_content = add_size(result_tlv, syntax_tlv);
        if (!sequence_content ||
            !writer.write_tlv_header(
                asn1::BerClass::universal, true, 16, *sequence_content) ||
            !writer.write_tlv_header(
                asn1::BerClass::context_specific, false, 0, 1U) ||
            !writer.write_byte(0x00U) ||
            !writer.write_tlv(
                asn1::BerClass::context_specific,
                false,
                1,
                context.first_transfer_syntax)) {
            return {wire::EncodeStatus::value_out_of_range, 0U, sizes->outer_tlv};
        }
    }

    const auto user_result = encode_fully_encoded_data_into(
        request.user_data.context_id,
        acse_aare,
        destination.first(sizes->outer_tlv).subspan(writer.size(), sizes->user_tlv));
    if (!user_result.success() || user_result.bytes_written != sizes->user_tlv) {
        return {user_result.status, 0U, sizes->outer_tlv};
    }
    const auto written = writer.size() + user_result.bytes_written;
    if (written != sizes->outer_tlv) {
        return {wire::EncodeStatus::value_out_of_range, 0U, sizes->outer_tlv};
    }
    return {wire::EncodeStatus::ok, written, sizes->outer_tlv};
}

wire::EncodeResult PresentationSpanCodec::encode_p_data_into(
    const std::span<const std::uint8_t> abstract_syntax_payload,
    const std::span<std::uint8_t> destination,
    const std::uint32_t presentation_context_id,
    const bool include_give_tokens_prefix) noexcept {
    const auto fully_encoded = fully_encoded_data_size(
        presentation_context_id, abstract_syntax_payload.size());
    if (!fully_encoded) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    const auto prefix = include_give_tokens_prefix ? 4U : 2U;
    if (*fully_encoded > std::numeric_limits<std::size_t>::max() - prefix) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto required = prefix + *fully_encoded;
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }

    std::size_t output_offset = 0U;
    if (include_give_tokens_prefix) {
        destination[output_offset++] = SessionSpanCodec::data_transfer_code;
        destination[output_offset++] = 0x00U;
    }
    destination[output_offset++] = SessionSpanCodec::data_transfer_code;
    destination[output_offset++] = 0x00U;

    const auto result = encode_fully_encoded_data_into(
        presentation_context_id,
        abstract_syntax_payload,
        destination.subspan(output_offset, *fully_encoded));
    if (!result.success() || result.bytes_written != *fully_encoded) {
        return {result.status, 0U, required};
    }
    return {wire::EncodeStatus::ok, required, required};
}

bool PresentationSpanCodec::try_decode_p_data_view(
    const std::span<const std::uint8_t> bytes,
    PresentationPdvView& pdv) noexcept {
    pdv = {};
    std::span<const std::uint8_t> presentation = bytes;
    SessionDataTransferView transfer;
    if (!bytes.empty() && bytes.front() == SessionSpanCodec::data_transfer_code) {
        if (!SessionSpanCodec::try_decode_data_transfer_view(bytes, transfer)) {
            return false;
        }
        presentation = transfer.presentation_payload;
    }
    return try_decode_fully_encoded_data_view(presentation, pdv);
}

} // namespace ar::iec61850::osi
