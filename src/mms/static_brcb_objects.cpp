// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_brcb_objects.hpp"

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
constexpr std::array<std::uint8_t, 3U> kInteger16Type{0x85U, 0x01U, 0x10U};
constexpr std::array<std::uint8_t, 3U> kOctetString8Type{0x89U, 0x01U, 0x08U};
constexpr std::array<std::uint8_t, 3U> kOctetString16Type{0x89U, 0x01U, 0x10U};
constexpr std::array<std::uint8_t, 4U> kVisible255Type{0x8AU, 0x02U, 0x00U, 0xFFU};

constexpr std::array<MmsStaticBrcbAttribute,
                     MmsStaticBrcbObjectBank::attributes_per_control_block>
    kAttributes{
        MmsStaticBrcbAttribute::report_id,
        MmsStaticBrcbAttribute::report_enabled,
        MmsStaticBrcbAttribute::data_set,
        MmsStaticBrcbAttribute::conf_revision,
        MmsStaticBrcbAttribute::purge_buffer,
        MmsStaticBrcbAttribute::entry_id,
        MmsStaticBrcbAttribute::reservation_time,
        MmsStaticBrcbAttribute::owner};

constexpr std::array<std::string_view,
                     MmsStaticBrcbObjectBank::attributes_per_control_block>
    kSuffixes{
        "RptID",
        "RptEna",
        "DatSet",
        "ConfRev",
        "PurgeBuf",
        "EntryID",
        "ResvTms",
        "Owner"};

constexpr std::uint32_t kHardwareFault = 1U;
constexpr std::uint32_t kTemporarilyUnavailable = 2U;
constexpr std::uint32_t kObjectAccessDenied = 3U;
constexpr std::uint32_t kTypeInconsistent = 7U;
constexpr std::uint32_t kObjectValueInvalid = 11U;

[[nodiscard]] std::span<const std::uint8_t> as_bytes(
    const std::string_view text) noexcept {
    return {
        reinterpret_cast<const std::uint8_t*>(text.data()),
        text.size()};
}

[[nodiscard]] std::uint64_t now_ms(const MmsStaticBrcbObjectContext& context) noexcept {
    return context.now_ms == nullptr
        ? std::uint64_t{0U}
        : context.now_ms(context.now_context);
}

[[nodiscard]] std::span<const std::uint8_t> type_for(
    const MmsStaticBrcbAttribute attribute) noexcept {
    switch (attribute) {
    case MmsStaticBrcbAttribute::report_id:
    case MmsStaticBrcbAttribute::data_set:
        return kVisible255Type;
    case MmsStaticBrcbAttribute::report_enabled:
    case MmsStaticBrcbAttribute::purge_buffer:
        return kBooleanType;
    case MmsStaticBrcbAttribute::conf_revision:
        return kUnsigned32Type;
    case MmsStaticBrcbAttribute::entry_id:
        return kOctetString8Type;
    case MmsStaticBrcbAttribute::reservation_time:
        return kInteger16Type;
    case MmsStaticBrcbAttribute::owner:
        return kOctetString16Type;
    }
    return {};
}

[[nodiscard]] bool writable_attribute(const MmsStaticBrcbAttribute attribute) noexcept {
    return attribute == MmsStaticBrcbAttribute::report_enabled ||
        attribute == MmsStaticBrcbAttribute::purge_buffer ||
        attribute == MmsStaticBrcbAttribute::entry_id ||
        attribute == MmsStaticBrcbAttribute::reservation_time;
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

[[nodiscard]] wire::EncodeResult encode_nonnegative_integer16(
    const std::uint32_t value,
    const std::span<std::uint8_t> destination) noexcept {
    if (value > 0x7FFFU) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const std::size_t content_size = value <= 0x7FU ? 1U : 2U;
    const auto total = asn1::BerSpanWriter::tlv_size(5, content_size);
    if (!total) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto capacity = capacity_result(*total, destination);
    if (!capacity.success()) {
        return capacity;
    }
    asn1::BerSpanWriter writer{destination.first(*total)};
    if (!writer.write_tlv_header(
            asn1::BerClass::context_specific, false, 5, content_size)) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *total};
    }
    if (content_size == 2U &&
        !writer.write_byte(static_cast<std::uint8_t>((value >> 8U) & 0x7FU))) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *total};
    }
    if (!writer.write_byte(static_cast<std::uint8_t>(value & 0xFFU)) ||
        writer.size() != *total) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *total};
    }
    return capacity;
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

[[nodiscard]] wire::EncodeResult encode_octets(
    const std::span<const std::uint8_t> bytes,
    const std::span<std::uint8_t> destination) noexcept {
    const auto total = asn1::BerSpanWriter::tlv_size(9, bytes.size());
    if (!total) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto capacity = capacity_result(*total, destination);
    if (!capacity.success()) {
        return capacity;
    }
    asn1::BerSpanWriter writer{destination.first(*total)};
    if (!writer.write_tlv_header(
            asn1::BerClass::context_specific, false, 9, bytes.size()) ||
        !writer.write_bytes(bytes) || writer.size() != *total) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *total};
    }
    return capacity;
}

[[nodiscard]] wire::EncodeResult read_brcb_attribute(
    const void* raw_context,
    const std::span<std::uint8_t> destination) noexcept {
    const auto* context = static_cast<const MmsStaticBrcbObjectContext*>(raw_context);
    if (context == nullptr || context->definition == nullptr ||
        context->reports == nullptr || context->control == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    switch (context->attribute) {
    case MmsStaticBrcbAttribute::report_id:
        return encode_visible(context->definition->report_id, destination);
    case MmsStaticBrcbAttribute::report_enabled:
        return encode_boolean(context->reports->enabled(), destination);
    case MmsStaticBrcbAttribute::data_set:
        return encode_data_set_reference(
            context->definition->data_set_domain,
            context->definition->data_set_item,
            destination);
    case MmsStaticBrcbAttribute::conf_revision:
        return encode_unsigned(context->definition->conf_revision, destination);
    case MmsStaticBrcbAttribute::purge_buffer:
        return encode_boolean(false, destination);
    case MmsStaticBrcbAttribute::entry_id: {
        const auto entry_id = context->reports->latest_entry_id();
        return encode_octets(entry_id, destination);
    }
    case MmsStaticBrcbAttribute::reservation_time: {
        const auto state = context->control->state(now_ms(*context));
        return encode_nonnegative_integer16(state.resv_tms_seconds, destination);
    }
    case MmsStaticBrcbAttribute::owner: {
        const auto state = context->control->state(now_ms(*context));
        return encode_octets(state.owner, destination);
    }
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

[[nodiscard]] bool decode_nonnegative_integer16(
    const std::span<const std::uint8_t> encoded,
    std::uint32_t& value) noexcept {
    value = 0U;
    asn1::BerTlvView tlv;
    if (!asn1::BerSpanReader::try_read_exact(encoded, tlv) ||
        tlv.tag_class != asn1::BerClass::context_specific ||
        tlv.tag_number != 5 || tlv.constructed) {
        return false;
    }
    const auto decoded = asn1::BerSpanReader::read_signed_integer(tlv);
    if (!decoded || *decoded < 0 || *decoded > 0x7FFF) {
        return false;
    }
    value = static_cast<std::uint32_t>(*decoded);
    return true;
}

[[nodiscard]] bool decode_entry_id(
    const std::span<const std::uint8_t> encoded,
    std::span<const std::uint8_t>& entry_id) noexcept {
    entry_id = {};
    asn1::BerTlvView tlv;
    if (!asn1::BerSpanReader::try_read_exact(encoded, tlv) ||
        tlv.tag_class != asn1::BerClass::context_specific ||
        tlv.tag_number != 9 || tlv.constructed ||
        tlv.value.size() != MmsInformationReportSpanCodec::entry_id_bytes) {
        return false;
    }
    entry_id = tlv.value;
    return true;
}

[[nodiscard]] bool all_zero(const std::span<const std::uint8_t> bytes) noexcept {
    return std::all_of(bytes.begin(), bytes.end(), [](const std::uint8_t byte) {
        return byte == 0U;
    });
}

[[nodiscard]] bool make_client(
    const MmsStaticRequestAccessContext& access,
    MmsStaticBrcbClientIdentity& client) noexcept {
    client = {};
    if (!access.valid() ||
        access.owner.size() > MmsStaticBrcbClientIdentity::maximum_owner_bytes) {
        return false;
    }
    client.association_id = access.association_id;
    client.owner_size = access.owner.size();
    std::copy(access.owner.begin(), access.owner.end(), client.owner.begin());
    return client.valid();
}

[[nodiscard]] MmsStaticWriteResult map_control_status(
    const MmsStaticBrcbControlStatus status) noexcept {
    switch (status) {
    case MmsStaticBrcbControlStatus::ok:
        return {true, 0U};
    case MmsStaticBrcbControlStatus::object_access_denied:
    case MmsStaticBrcbControlStatus::invalid_identity:
        return {false, kObjectAccessDenied};
    case MmsStaticBrcbControlStatus::temporarily_unavailable:
        return {false, kTemporarilyUnavailable};
    case MmsStaticBrcbControlStatus::entry_not_found:
        return {false, kObjectValueInvalid};
    case MmsStaticBrcbControlStatus::invalid_runtime:
    case MmsStaticBrcbControlStatus::backend_failure:
        return {false, kHardwareFault};
    }
    return {false, kHardwareFault};
}

[[nodiscard]] MmsStaticBrcbControlStatus ensure_claimed(
    MmsStaticBrcbObjectContext& context,
    const MmsStaticBrcbClientIdentity& client,
    const std::uint64_t now) noexcept {
    const auto state = context.control->state(now);
    return state.reserved
        ? MmsStaticBrcbControlStatus::ok
        : context.control->reserve(client, 0U, now);
}

[[nodiscard]] MmsStaticWriteResult write_brcb_attribute(
    void* raw_context,
    const std::span<const std::uint8_t> encoded_data,
    const MmsStaticRequestAccessContext& access) noexcept {
    auto* context = static_cast<MmsStaticBrcbObjectContext*>(raw_context);
    if (context == nullptr || context->definition == nullptr ||
        context->reports == nullptr || context->control == nullptr) {
        return {false, kHardwareFault};
    }

    MmsStaticBrcbClientIdentity client;
    if (!make_client(access, client)) {
        return {false, kObjectAccessDenied};
    }
    const auto now = now_ms(*context);

    switch (context->attribute) {
    case MmsStaticBrcbAttribute::report_enabled: {
        bool value = false;
        if (!decode_boolean(encoded_data, value)) {
            return {false, kTypeInconsistent};
        }
        const auto state = context->control->state(now);
        if (!value && !state.reserved) {
            return {true, 0U};
        }
        const auto claim = ensure_claimed(*context, client, now);
        if (claim != MmsStaticBrcbControlStatus::ok) {
            return map_control_status(claim);
        }
        return map_control_status(
            context->control->set_report_enabled(client, value, now));
    }
    case MmsStaticBrcbAttribute::purge_buffer: {
        bool value = false;
        if (!decode_boolean(encoded_data, value)) {
            return {false, kTypeInconsistent};
        }
        if (!value) {
            return {true, 0U};
        }
        const auto claim = ensure_claimed(*context, client, now);
        if (claim != MmsStaticBrcbControlStatus::ok) {
            return map_control_status(claim);
        }
        return map_control_status(context->control->purge_buffer(client, now));
    }
    case MmsStaticBrcbAttribute::entry_id: {
        std::span<const std::uint8_t> entry_id;
        if (!decode_entry_id(encoded_data, entry_id)) {
            return {false, kTypeInconsistent};
        }
        const auto claim = ensure_claimed(*context, client, now);
        if (claim != MmsStaticBrcbControlStatus::ok) {
            return map_control_status(claim);
        }
        return all_zero(entry_id)
            ? map_control_status(context->control->rewind_to_oldest(client, now))
            : map_control_status(context->control->resume_after(client, entry_id, now));
    }
    case MmsStaticBrcbAttribute::reservation_time: {
        std::uint32_t seconds = 0U;
        if (!decode_nonnegative_integer16(encoded_data, seconds)) {
            return {false, kTypeInconsistent};
        }
        const auto state = context->control->state(now);
        if (seconds == 0U) {
            return !state.reserved
                ? MmsStaticWriteResult{true, 0U}
                : map_control_status(context->control->release(client, now));
        }
        return map_control_status(context->control->reserve(client, seconds, now));
    }
    case MmsStaticBrcbAttribute::report_id:
    case MmsStaticBrcbAttribute::data_set:
    case MmsStaticBrcbAttribute::conf_revision:
    case MmsStaticBrcbAttribute::owner:
        return {false, kObjectAccessDenied};
    }
    return {false, kHardwareFault};
}

} // namespace

std::size_t MmsStaticBrcbObjectBank::required_object_capacity() const noexcept {
    if (base_objects_.size() >
        std::numeric_limits<std::size_t>::max() - attributes_per_control_block) {
        return std::numeric_limits<std::size_t>::max();
    }
    return base_objects_.size() + attributes_per_control_block;
}

std::size_t MmsStaticBrcbObjectBank::required_name_bytes() const noexcept {
    if (definition_ == nullptr) {
        return std::numeric_limits<std::size_t>::max();
    }
    std::size_t total = 0U;
    for (const auto suffix : kSuffixes) {
        if (definition_->item.size() >
                MmsServiceSpanCodec::maximum_identifier_bytes - 1U - suffix.size()) {
            return std::numeric_limits<std::size_t>::max();
        }
        const auto bytes = definition_->item.size() + 1U + suffix.size();
        if (bytes > std::numeric_limits<std::size_t>::max() - total) {
            return std::numeric_limits<std::size_t>::max();
        }
        total += bytes;
    }
    return total;
}

bool MmsStaticBrcbObjectBank::initialize() noexcept {
    initialized_ = false;
    object_count_ = 0U;
    name_bytes_used_ = 0U;
    table_ = MmsStaticObjectTable{std::span<const MmsStaticObjectEntry>{}};

    if (definition_ == nullptr || reports_ == nullptr || control_ == nullptr ||
        !reports_->valid() || !control_->valid()) {
        return false;
    }
    const auto required_objects = required_object_capacity();
    const auto required_names = required_name_bytes();
    if (required_objects == std::numeric_limits<std::size_t>::max() ||
        required_names == std::numeric_limits<std::size_t>::max() ||
        required_objects > object_storage_.size() ||
        required_objects > MmsStaticObjectTable::maximum_objects ||
        attributes_per_control_block > context_storage_.size() ||
        required_names > name_storage_.size()) {
        return false;
    }

    std::copy(base_objects_.begin(), base_objects_.end(), object_storage_.begin());
    std::size_t object_offset = base_objects_.size();
    std::size_t name_offset = 0U;

    for (std::size_t attribute_index = 0U;
         attribute_index < attributes_per_control_block;
         ++attribute_index) {
        const auto suffix = kSuffixes[attribute_index];
        const auto name_size = definition_->item.size() + 1U + suffix.size();
        auto* name = name_storage_.data() + name_offset;
        std::copy(definition_->item.begin(), definition_->item.end(), name);
        name[definition_->item.size()] = '$';
        std::copy(
            suffix.begin(), suffix.end(),
            name + definition_->item.size() + 1U);

        auto& context = context_storage_[attribute_index];
        context = MmsStaticBrcbObjectContext{
            definition_,
            reports_,
            control_,
            kAttributes[attribute_index],
            now_ms_,
            now_context_};

        const auto attribute = kAttributes[attribute_index];
        const auto writable = writable_attribute(attribute);
        object_storage_[object_offset] = MmsStaticObjectEntry{
            definition_->domain,
            std::string_view{name, name_size},
            type_for(attribute),
            read_brcb_attribute,
            &context,
            false,
            nullptr,
            writable ? &context : nullptr,
            writable ? write_brcb_attribute : nullptr};

        name_offset += name_size;
        ++object_offset;
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
