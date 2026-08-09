// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_urcb_objects.hpp"

#include "ariec61850/asn1/ber_span_reader.hpp"
#include "ariec61850/asn1/ber_span_writer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {
namespace {

constexpr std::array<std::uint8_t, 2U> kBooleanType{0x83U, 0x00U};
constexpr std::array<std::uint8_t, 3U> kUnsigned32Type{0x86U, 0x01U, 0x20U};
constexpr std::array<std::uint8_t, 4U> kVisible255Type{0x8AU, 0x02U, 0x00U, 0xFFU};
constexpr std::array<std::uint8_t, 3U> kBitString10Type{0x84U, 0x01U, 0x0AU};
constexpr std::array<std::uint8_t, 3U> kBitString6Type{0x84U, 0x01U, 0x06U};

constexpr std::array<MmsStaticUrcbAttribute,
                     MmsStaticUrcbObjectBank::attributes_per_control_block>
    kAttributes{
        MmsStaticUrcbAttribute::report_id,
        MmsStaticUrcbAttribute::report_enabled,
        MmsStaticUrcbAttribute::reserved,
        MmsStaticUrcbAttribute::data_set,
        MmsStaticUrcbAttribute::conf_revision,
        MmsStaticUrcbAttribute::optional_fields,
        MmsStaticUrcbAttribute::buffer_time,
        MmsStaticUrcbAttribute::trigger_options,
        MmsStaticUrcbAttribute::integrity_period,
        MmsStaticUrcbAttribute::general_interrogation,
        MmsStaticUrcbAttribute::sequence_number};

constexpr std::array<std::string_view,
                     MmsStaticUrcbObjectBank::attributes_per_control_block>
    kSuffixes{
        "RptID",
        "RptEna",
        "Resv",
        "DatSet",
        "ConfRev",
        "OptFlds",
        "BufTm",
        "TrgOps",
        "IntgPd",
        "GI",
        "SqNum"};

constexpr std::uint32_t kHardwareFault = 1U;
constexpr std::uint32_t kTemporarilyUnavailable = 2U;
constexpr std::uint32_t kObjectAccessDenied = 3U;
constexpr std::uint32_t kTypeInconsistent = 7U;
constexpr std::uint32_t kObjectNonExistent = 10U;
constexpr std::uint32_t kObjectValueInvalid = 11U;

[[nodiscard]] std::span<const std::uint8_t> as_bytes(
    const std::string_view text) noexcept {
    return {
        reinterpret_cast<const std::uint8_t*>(text.data()),
        text.size()};
}

[[nodiscard]] bool visible_ascii(const std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.empty() || bytes.size() > MmsServiceSpanCodec::maximum_identifier_bytes) {
        return false;
    }
    for (const auto byte : bytes) {
        if (byte == 0U || byte > 0x7FU) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::span<const std::uint8_t> type_for(
    const MmsStaticUrcbAttribute attribute) noexcept {
    switch (attribute) {
    case MmsStaticUrcbAttribute::report_id:
    case MmsStaticUrcbAttribute::data_set:
        return kVisible255Type;
    case MmsStaticUrcbAttribute::report_enabled:
    case MmsStaticUrcbAttribute::reserved:
    case MmsStaticUrcbAttribute::general_interrogation:
        return kBooleanType;
    case MmsStaticUrcbAttribute::conf_revision:
    case MmsStaticUrcbAttribute::buffer_time:
    case MmsStaticUrcbAttribute::integrity_period:
    case MmsStaticUrcbAttribute::sequence_number:
        return kUnsigned32Type;
    case MmsStaticUrcbAttribute::optional_fields:
        return kBitString10Type;
    case MmsStaticUrcbAttribute::trigger_options:
        return kBitString6Type;
    }
    return {};
}

[[nodiscard]] bool writable_attribute(
    const MmsStaticUrcbAttribute attribute) noexcept {
    return attribute != MmsStaticUrcbAttribute::conf_revision &&
        attribute != MmsStaticUrcbAttribute::sequence_number;
}

[[nodiscard]] wire::EncodeResult capacity_result(
    const std::size_t required,
    const std::span<std::uint8_t> destination) noexcept {
    return destination.size() < required
        ? wire::EncodeResult{wire::EncodeStatus::buffer_too_small, 0U, required}
        : wire::EncodeResult{wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] wire::EncodeResult encode_boolean(
    const bool value,
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 3U;
    const auto capacity = capacity_result(required, destination);
    if (!capacity.success()) {
        return capacity;
    }
    destination[0] = 0x83U;
    destination[1] = 0x01U;
    destination[2] = value ? 0xFFU : 0x00U;
    return capacity;
}

[[nodiscard]] std::size_t unsigned_content_size(std::uint32_t value) noexcept {
    std::size_t size = 1U;
    while (value > 0xFFU) {
        ++size;
        value >>= 8U;
    }
    return size;
}

[[nodiscard]] wire::EncodeResult encode_unsigned(
    const std::uint32_t value,
    const std::span<std::uint8_t> destination) noexcept {
    const auto content_size = unsigned_content_size(value);
    const auto total = asn1::BerSpanWriter::tlv_size(6, content_size);
    if (!total) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto capacity = capacity_result(*total, destination);
    if (!capacity.success()) {
        return capacity;
    }

    asn1::BerSpanWriter writer{destination.first(*total)};
    if (!writer.write_tlv_header(
            asn1::BerClass::context_specific, false, 6, content_size)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *total};
    }
    for (std::size_t index = content_size; index-- > 0U;) {
        const auto shift = static_cast<unsigned>(index * 8U);
        if (!writer.write_byte(static_cast<std::uint8_t>((value >> shift) & 0xFFU))) {
            return {wire::EncodeStatus::value_out_of_range, 0U, *total};
        }
    }
    return writer.size() == *total
        ? capacity
        : wire::EncodeResult{wire::EncodeStatus::value_out_of_range, 0U, *total};
}

[[nodiscard]] wire::EncodeResult encode_visible(
    const std::string_view text,
    const std::span<std::uint8_t> destination) noexcept {
    const auto total = asn1::BerSpanWriter::tlv_size(10, text.size());
    if (!total) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto capacity = capacity_result(*total, destination);
    if (!capacity.success()) {
        return capacity;
    }
    asn1::BerSpanWriter writer{destination.first(*total)};
    if (!writer.write_tlv_header(
            asn1::BerClass::context_specific, false, 10, text.size()) ||
        !writer.write_bytes(as_bytes(text)) || writer.size() != *total) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *total};
    }
    return capacity;
}

[[nodiscard]] wire::EncodeResult encode_data_set_reference(
    const std::string_view domain,
    const std::string_view item,
    const std::span<std::uint8_t> destination) noexcept {
    if (domain.size() > std::numeric_limits<std::size_t>::max() - 1U - item.size()) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto content_size = domain.size() + 1U + item.size();
    const auto total = asn1::BerSpanWriter::tlv_size(10, content_size);
    if (!total) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto capacity = capacity_result(*total, destination);
    if (!capacity.success()) {
        return capacity;
    }

    asn1::BerSpanWriter writer{destination.first(*total)};
    if (!writer.write_tlv_header(
            asn1::BerClass::context_specific, false, 10, content_size) ||
        !writer.write_bytes(as_bytes(domain)) ||
        !writer.write_byte(static_cast<std::uint8_t>('/')) ||
        !writer.write_bytes(as_bytes(item)) || writer.size() != *total) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *total};
    }
    return capacity;
}

[[nodiscard]] wire::EncodeResult encode_bit_string(
    const std::uint8_t unused_bits,
    const std::span<const std::uint8_t> bytes,
    const std::span<std::uint8_t> destination) noexcept {
    if (unused_bits > 7U || bytes.size() == std::numeric_limits<std::size_t>::max()) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto content_size = bytes.size() + 1U;
    const auto total = asn1::BerSpanWriter::tlv_size(4, content_size);
    if (!total) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto capacity = capacity_result(*total, destination);
    if (!capacity.success()) {
        return capacity;
    }
    asn1::BerSpanWriter writer{destination.first(*total)};
    if (!writer.write_tlv_header(
            asn1::BerClass::context_specific, false, 4, content_size) ||
        !writer.write_byte(unused_bits) || !writer.write_bytes(bytes) ||
        writer.size() != *total) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *total};
    }
    return capacity;
}

[[nodiscard]] wire::EncodeResult read_urcb_attribute(
    const void* raw_context,
    const std::span<std::uint8_t> destination) noexcept {
    const auto* context = static_cast<const MmsStaticUrcbObjectContext*>(raw_context);
    if (context == nullptr || context->runtime == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto* state = context->runtime->state(context->index);
    if (state == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    switch (context->attribute) {
    case MmsStaticUrcbAttribute::report_id:
        return encode_visible(state->report_id(), destination);
    case MmsStaticUrcbAttribute::report_enabled:
        return encode_boolean(state->enabled, destination);
    case MmsStaticUrcbAttribute::reserved:
        return encode_boolean(state->reserved, destination);
    case MmsStaticUrcbAttribute::data_set:
        return encode_data_set_reference(
            state->data_set_domain(), state->data_set_item(), destination);
    case MmsStaticUrcbAttribute::conf_revision:
        return encode_unsigned(state->conf_revision, destination);
    case MmsStaticUrcbAttribute::optional_fields:
        return encode_bit_string(6U, state->optional_fields, destination);
    case MmsStaticUrcbAttribute::buffer_time:
        return encode_unsigned(state->buffer_time_ms, destination);
    case MmsStaticUrcbAttribute::trigger_options: {
        const std::array<std::uint8_t, 1U> trigger{state->trigger_options};
        return encode_bit_string(2U, trigger, destination);
    }
    case MmsStaticUrcbAttribute::integrity_period:
        return encode_unsigned(state->integrity_period_ms, destination);
    case MmsStaticUrcbAttribute::general_interrogation:
        return encode_boolean(state->general_interrogation_pending, destination);
    case MmsStaticUrcbAttribute::sequence_number:
        return encode_unsigned(state->sequence_number, destination);
    }
    return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
}

[[nodiscard]] bool decode_boolean(
    const std::span<const std::uint8_t> encoded,
    bool& value) noexcept {
    asn1::BerTlvView tlv;
    if (!asn1::BerSpanReader::try_read_exact(encoded, tlv) ||
        tlv.tag_class != asn1::BerClass::context_specific ||
        tlv.tag_number != 3 || tlv.constructed || tlv.value.size() != 1U) {
        return false;
    }
    value = tlv.value[0] != 0U;
    return true;
}

[[nodiscard]] bool decode_unsigned(
    const std::span<const std::uint8_t> encoded,
    std::uint32_t& value) noexcept {
    asn1::BerTlvView tlv;
    if (!asn1::BerSpanReader::try_read_exact(encoded, tlv) ||
        tlv.tag_class != asn1::BerClass::context_specific ||
        tlv.tag_number != 6 || tlv.constructed) {
        return false;
    }
    const auto decoded = asn1::BerSpanReader::read_uint32(tlv);
    if (!decoded) {
        return false;
    }
    value = *decoded;
    return true;
}

[[nodiscard]] bool decode_visible(
    const std::span<const std::uint8_t> encoded,
    std::string_view& value) noexcept {
    value = {};
    asn1::BerTlvView tlv;
    if (!asn1::BerSpanReader::try_read_exact(encoded, tlv) ||
        tlv.tag_class != asn1::BerClass::context_specific ||
        tlv.tag_number != 10 || tlv.constructed || !visible_ascii(tlv.value)) {
        return false;
    }
    value = {
        reinterpret_cast<const char*>(tlv.value.data()),
        tlv.value.size()};
    return true;
}

[[nodiscard]] bool decode_bit_string(
    const std::span<const std::uint8_t> encoded,
    const std::uint8_t expected_unused_bits,
    const std::size_t expected_bytes,
    std::span<const std::uint8_t>& bytes) noexcept {
    bytes = {};
    asn1::BerTlvView tlv;
    if (!asn1::BerSpanReader::try_read_exact(encoded, tlv) ||
        tlv.tag_class != asn1::BerClass::context_specific ||
        tlv.tag_number != 4 || tlv.constructed ||
        tlv.value.size() != expected_bytes + 1U ||
        tlv.value[0] != expected_unused_bits) {
        return false;
    }
    bytes = tlv.value.subspan(1U);
    if (!bytes.empty() && expected_unused_bits != 0U) {
        const auto mask = static_cast<std::uint8_t>((1U << expected_unused_bits) - 1U);
        if ((bytes.back() & mask) != 0U) {
            bytes = {};
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool split_reference(
    const std::string_view reference,
    std::string_view& domain,
    std::string_view& item) noexcept {
    domain = {};
    item = {};
    std::size_t slash = reference.size();
    for (std::size_t index = 0U; index < reference.size(); ++index) {
        if (reference[index] != '/') {
            continue;
        }
        if (slash != reference.size()) {
            return false;
        }
        slash = index;
    }
    if (slash == 0U || slash >= reference.size() - 1U) {
        return false;
    }
    domain = reference.substr(0U, slash);
    item = reference.substr(slash + 1U);
    return true;
}

[[nodiscard]] MmsStaticWriteResult map_write_status(
    const MmsStaticUrcbStatus status) noexcept {
    switch (status) {
    case MmsStaticUrcbStatus::ok:
        return {true, 0U};
    case MmsStaticUrcbStatus::object_access_denied:
        return {false, kObjectAccessDenied};
    case MmsStaticUrcbStatus::temporarily_unavailable:
        return {false, kTemporarilyUnavailable};
    case MmsStaticUrcbStatus::data_set_not_found:
        return {false, kObjectNonExistent};
    case MmsStaticUrcbStatus::invalid_value:
        return {false, kObjectValueInvalid};
    case MmsStaticUrcbStatus::invalid_runtime:
    case MmsStaticUrcbStatus::index_out_of_range:
    case MmsStaticUrcbStatus::no_report_due:
    case MmsStaticUrcbStatus::stale_plan:
    case MmsStaticUrcbStatus::workspace_too_small:
    case MmsStaticUrcbStatus::backend_failure:
    case MmsStaticUrcbStatus::response_buffer_too_small:
    case MmsStaticUrcbStatus::report_encode_failed:
        return {false, kHardwareFault};
    }
    return {false, kHardwareFault};
}

[[nodiscard]] MmsStaticWriteResult write_urcb_attribute(
    void* raw_context,
    const std::span<const std::uint8_t> encoded_data) noexcept {
    auto* context = static_cast<MmsStaticUrcbObjectContext*>(raw_context);
    if (context == nullptr || context->runtime == nullptr) {
        return {false, kHardwareFault};
    }

    switch (context->attribute) {
    case MmsStaticUrcbAttribute::report_id: {
        std::string_view value;
        if (!decode_visible(encoded_data, value)) {
            return {false, kTypeInconsistent};
        }
        return map_write_status(context->runtime->set_report_id(context->index, value));
    }
    case MmsStaticUrcbAttribute::report_enabled: {
        bool value = false;
        if (!decode_boolean(encoded_data, value)) {
            return {false, kTypeInconsistent};
        }
        const auto now = context->now_ms == nullptr
            ? std::uint64_t{0U}
            : context->now_ms(context->now_context);
        return map_write_status(
            context->runtime->set_enabled(context->index, value, now));
    }
    case MmsStaticUrcbAttribute::reserved: {
        bool value = false;
        if (!decode_boolean(encoded_data, value)) {
            return {false, kTypeInconsistent};
        }
        return map_write_status(
            context->runtime->set_reserved(context->index, value));
    }
    case MmsStaticUrcbAttribute::data_set: {
        std::string_view reference;
        std::string_view domain;
        std::string_view item;
        if (!decode_visible(encoded_data, reference) ||
            !split_reference(reference, domain, item)) {
            return {false, kTypeInconsistent};
        }
        return map_write_status(
            context->runtime->set_data_set(context->index, domain, item));
    }
    case MmsStaticUrcbAttribute::optional_fields: {
        std::span<const std::uint8_t> bytes;
        if (!decode_bit_string(encoded_data, 6U, 2U, bytes)) {
            return {false, kTypeInconsistent};
        }
        return map_write_status(
            context->runtime->set_optional_fields(context->index, bytes));
    }
    case MmsStaticUrcbAttribute::buffer_time: {
        std::uint32_t value = 0U;
        if (!decode_unsigned(encoded_data, value)) {
            return {false, kTypeInconsistent};
        }
        return map_write_status(
            context->runtime->set_buffer_time_ms(context->index, value));
    }
    case MmsStaticUrcbAttribute::trigger_options: {
        std::span<const std::uint8_t> bytes;
        if (!decode_bit_string(encoded_data, 2U, 1U, bytes)) {
            return {false, kTypeInconsistent};
        }
        return map_write_status(
            context->runtime->set_trigger_options(context->index, bytes[0]));
    }
    case MmsStaticUrcbAttribute::integrity_period: {
        std::uint32_t value = 0U;
        if (!decode_unsigned(encoded_data, value)) {
            return {false, kTypeInconsistent};
        }
        return map_write_status(
            context->runtime->set_integrity_period_ms(context->index, value));
    }
    case MmsStaticUrcbAttribute::general_interrogation: {
        bool value = false;
        if (!decode_boolean(encoded_data, value)) {
            return {false, kTypeInconsistent};
        }
        return value
            ? map_write_status(
                context->runtime->request_general_interrogation(context->index))
            : MmsStaticWriteResult{true, 0U};
    }
    case MmsStaticUrcbAttribute::conf_revision:
    case MmsStaticUrcbAttribute::sequence_number:
        return {false, kObjectAccessDenied};
    }
    return {false, kHardwareFault};
}

} // namespace

std::size_t MmsStaticUrcbObjectBank::required_context_capacity() const noexcept {
    if (runtime_ == nullptr || !runtime_->valid() ||
        runtime_->size() > std::numeric_limits<std::size_t>::max() /
            attributes_per_control_block) {
        return std::numeric_limits<std::size_t>::max();
    }
    return runtime_->size() * attributes_per_control_block;
}

std::size_t MmsStaticUrcbObjectBank::required_object_capacity() const noexcept {
    const auto contexts = required_context_capacity();
    if (contexts == std::numeric_limits<std::size_t>::max() ||
        contexts > std::numeric_limits<std::size_t>::max() - base_objects_.size()) {
        return std::numeric_limits<std::size_t>::max();
    }
    return base_objects_.size() + contexts;
}

std::size_t MmsStaticUrcbObjectBank::required_name_bytes() const noexcept {
    if (runtime_ == nullptr || !runtime_->valid()) {
        return std::numeric_limits<std::size_t>::max();
    }
    std::size_t total = 0U;
    for (std::size_t index = 0U; index < runtime_->size(); ++index) {
        const auto* definition = runtime_->definition(index);
        if (definition == nullptr) {
            return std::numeric_limits<std::size_t>::max();
        }
        for (const auto suffix : kSuffixes) {
            if (definition->item.size() >
                    MmsServiceSpanCodec::maximum_identifier_bytes - 1U - suffix.size()) {
                return std::numeric_limits<std::size_t>::max();
            }
            const auto name_size = definition->item.size() + 1U + suffix.size();
            if (name_size > std::numeric_limits<std::size_t>::max() - total) {
                return std::numeric_limits<std::size_t>::max();
            }
            total += name_size;
        }
    }
    return total;
}

bool MmsStaticUrcbObjectBank::initialize() noexcept {
    initialized_ = false;
    object_count_ = 0U;
    name_bytes_used_ = 0U;
    table_ = MmsStaticObjectTable{std::span<const MmsStaticObjectEntry>{}};

    if (runtime_ == nullptr || !runtime_->valid()) {
        return false;
    }
    const auto required_objects = required_object_capacity();
    const auto required_contexts = required_context_capacity();
    const auto required_names = required_name_bytes();
    if (required_objects == std::numeric_limits<std::size_t>::max() ||
        required_contexts == std::numeric_limits<std::size_t>::max() ||
        required_names == std::numeric_limits<std::size_t>::max() ||
        required_objects > object_storage_.size() ||
        required_objects > MmsStaticObjectTable::maximum_objects ||
        required_contexts > context_storage_.size() ||
        required_names > name_storage_.size()) {
        return false;
    }

    std::copy(base_objects_.begin(), base_objects_.end(), object_storage_.begin());
    std::size_t object_offset = base_objects_.size();
    std::size_t context_offset = 0U;
    std::size_t name_offset = 0U;

    for (std::size_t urcb_index = 0U; urcb_index < runtime_->size(); ++urcb_index) {
        const auto* definition = runtime_->definition(urcb_index);
        if (definition == nullptr) {
            return false;
        }
        for (std::size_t attribute_index = 0U;
             attribute_index < attributes_per_control_block;
             ++attribute_index) {
            const auto suffix = kSuffixes[attribute_index];
            const auto name_size = definition->item.size() + 1U + suffix.size();
            auto* name = name_storage_.data() + name_offset;
            std::copy(definition->item.begin(), definition->item.end(), name);
            name[definition->item.size()] = '$';
            std::copy(
                suffix.begin(),
                suffix.end(),
                name + definition->item.size() + 1U);

            auto& context = context_storage_[context_offset];
            context = MmsStaticUrcbObjectContext{
                runtime_,
                urcb_index,
                kAttributes[attribute_index],
                now_ms_,
                now_context_};

            const auto attribute = kAttributes[attribute_index];
            object_storage_[object_offset] = MmsStaticObjectEntry{
                definition->domain,
                std::string_view{name, name_size},
                type_for(attribute),
                read_urcb_attribute,
                &context,
                false,
                writable_attribute(attribute) ? write_urcb_attribute : nullptr,
                writable_attribute(attribute) ? &context : nullptr};

            name_offset += name_size;
            ++context_offset;
            ++object_offset;
        }
    }

    table_ = MmsStaticObjectTable{
        std::span<const MmsStaticObjectEntry>{object_storage_.data(), object_offset}};
    if (!table_.valid()) {
        table_ = MmsStaticObjectTable{std::span<const MmsStaticObjectEntry>{}};
        return false;
    }

    object_count_ = object_offset;
    name_bytes_used_ = name_offset;
    initialized_ = true;
    return true;
}

} // namespace ar::iec61850::mms
