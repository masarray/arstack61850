// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_setting_group.hpp"

#include "ariec61850/asn1/ber_span_reader.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {
namespace {

constexpr std::array<std::uint8_t, 3U> kUnsigned8Type{0x86U, 0x01U, 0x08U};
constexpr std::array<std::uint8_t, 2U> kBooleanType{0x83U, 0x00U};
constexpr std::array<std::uint8_t, 2U> kUtcTimeType{0x91U, 0x00U};
constexpr std::uint32_t kTemporarilyUnavailable = 2U;
constexpr std::uint32_t kTypeInconsistent = 7U;
constexpr std::uint32_t kObjectValueInvalid = 11U;

constexpr std::array<MmsStaticSettingGroupAttribute,
                     MmsStaticSettingGroupObjectBank::attributes_per_control_block>
    kAttributes{
        MmsStaticSettingGroupAttribute::active_setting_group,
        MmsStaticSettingGroupAttribute::confirm_edit,
        MmsStaticSettingGroupAttribute::edit_setting_group,
        MmsStaticSettingGroupAttribute::last_activation_time,
        MmsStaticSettingGroupAttribute::number_of_setting_groups};

constexpr std::array<std::string_view,
                     MmsStaticSettingGroupObjectBank::attributes_per_control_block>
    kSuffixes{"ActSG", "CnfEdit", "EditSG", "LActTm", "NumOfSG"};

[[nodiscard]] std::span<const std::uint8_t> type_for(
    const MmsStaticSettingGroupAttribute attribute) noexcept {
    switch (attribute) {
    case MmsStaticSettingGroupAttribute::active_setting_group:
    case MmsStaticSettingGroupAttribute::edit_setting_group:
    case MmsStaticSettingGroupAttribute::number_of_setting_groups:
        return kUnsigned8Type;
    case MmsStaticSettingGroupAttribute::confirm_edit:
        return kBooleanType;
    case MmsStaticSettingGroupAttribute::last_activation_time:
        return kUtcTimeType;
    }
    return {};
}

[[nodiscard]] wire::EncodeResult encode_unsigned8(
    const std::uint32_t value,
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 3U;
    if (value > 0xFFU) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    destination[0] = 0x86U;
    destination[1] = 0x01U;
    destination[2] = static_cast<std::uint8_t>(value);
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] wire::EncodeResult encode_boolean(
    const bool value,
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 3U;
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    destination[0] = 0x83U;
    destination[1] = 0x01U;
    destination[2] = value ? 0xFFU : 0x00U;
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] std::uint64_t pack_utc(
    const std::span<const std::uint8_t, 8U> bytes) noexcept {
    std::uint64_t packed{};
    for (const auto byte : bytes) {
        packed = (packed << 8U) | static_cast<std::uint64_t>(byte);
    }
    return packed;
}

void unpack_utc(
    std::uint64_t packed,
    const std::span<std::uint8_t, 8U> destination) noexcept {
    for (std::size_t index = destination.size(); index-- > 0U;) {
        destination[index] = static_cast<std::uint8_t>(packed & 0xFFU);
        packed >>= 8U;
    }
}

[[nodiscard]] wire::EncodeResult encode_utc(
    const std::uint64_t packed,
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 10U;
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    destination[0] = 0x91U;
    destination[1] = 0x08U;
    std::array<std::uint8_t, 8U> bytes{};
    unpack_utc(packed, bytes);
    std::copy(bytes.begin(), bytes.end(), destination.begin() + 2);
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] wire::EncodeResult read_attribute(
    const void* raw_context,
    const std::span<std::uint8_t> destination) noexcept {
    const auto* context = static_cast<const MmsStaticSettingGroupObjectContext*>(raw_context);
    if (context == nullptr || context->state == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    switch (context->attribute) {
    case MmsStaticSettingGroupAttribute::active_setting_group:
        return encode_unsigned8(
            context->state->active_setting_group.load(std::memory_order_acquire),
            destination);
    case MmsStaticSettingGroupAttribute::confirm_edit:
        return encode_boolean(false, destination);
    case MmsStaticSettingGroupAttribute::edit_setting_group:
        return encode_unsigned8(0U, destination);
    case MmsStaticSettingGroupAttribute::last_activation_time:
        return encode_utc(
            context->state->last_activation_time_be.load(std::memory_order_acquire),
            destination);
    case MmsStaticSettingGroupAttribute::number_of_setting_groups:
        return encode_unsigned8(context->state->number_of_setting_groups, destination);
    }
    return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
}

[[nodiscard]] bool decode_unsigned8(
    const std::span<const std::uint8_t> encoded,
    std::uint32_t& value) noexcept {
    asn1::BerTlvView tlv;
    if (!asn1::BerSpanReader::try_read_exact(encoded, tlv) ||
        tlv.tag_class != asn1::BerClass::context_specific ||
        tlv.tag_number != 6 || tlv.constructed || tlv.value.empty() ||
        tlv.value.size() > sizeof(std::uint32_t)) {
        return false;
    }
    const auto parsed = asn1::BerSpanReader::read_unsigned_integer(tlv);
    if (!parsed || *parsed > 0xFFU) return false;
    value = static_cast<std::uint32_t>(*parsed);
    return true;
}

struct ActivationGuard final {
    std::atomic_flag* flag{};
    ~ActivationGuard() {
        if (flag != nullptr) flag->clear(std::memory_order_release);
    }
};

[[nodiscard]] MmsStaticWriteResult write_active_setting_group(
    void* raw_context,
    const std::span<const std::uint8_t> encoded_data) noexcept {
    auto* context = static_cast<MmsStaticSettingGroupObjectContext*>(raw_context);
    if (context == nullptr || context->state == nullptr) {
        return {false, kObjectValueInvalid};
    }
    std::uint32_t requested{};
    if (!decode_unsigned8(encoded_data, requested)) {
        return {false, kTypeInconsistent};
    }
    if (requested == 0U || requested > context->state->number_of_setting_groups) {
        return {false, kObjectValueInvalid};
    }
    if (context->state->activation_in_progress.test_and_set(std::memory_order_acquire)) {
        return {false, kTemporarilyUnavailable};
    }
    ActivationGuard guard{&context->state->activation_in_progress};

    std::array<std::uint8_t, 8U> utc{};
    const bool have_time = context->now_utc != nullptr &&
        context->now_utc(context->now_context, utc);
    context->state->active_setting_group.store(requested, std::memory_order_release);
    if (have_time) {
        context->state->last_activation_time_be.store(
            pack_utc(utc), std::memory_order_release);
    }
    return {true, 0U};
}

} // namespace

std::size_t MmsStaticSettingGroupObjectBank::required_object_capacity() const noexcept {
    if (base_objects_.size() >
        std::numeric_limits<std::size_t>::max() - attributes_per_control_block) {
        return std::numeric_limits<std::size_t>::max();
    }
    return base_objects_.size() + attributes_per_control_block;
}

std::size_t MmsStaticSettingGroupObjectBank::required_name_bytes() const noexcept {
    if (definition_ == nullptr) return std::numeric_limits<std::size_t>::max();
    std::size_t total{};
    for (const auto suffix : kSuffixes) {
        if (definition_->item.size() >
            MmsServiceSpanCodec::maximum_identifier_bytes - 1U - suffix.size()) {
            return std::numeric_limits<std::size_t>::max();
        }
        const auto required = definition_->item.size() + 1U + suffix.size();
        if (required > std::numeric_limits<std::size_t>::max() - total) {
            return std::numeric_limits<std::size_t>::max();
        }
        total += required;
    }
    return total;
}

bool MmsStaticSettingGroupObjectBank::initialize() noexcept {
    initialized_ = false;
    table_ = MmsStaticObjectTable{std::span<const MmsStaticObjectEntry>{}};
    if (definition_ == nullptr || state_ == nullptr || !state_->valid() ||
        state_->number_of_setting_groups > 0xFFU) {
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
    std::size_t name_offset{};
    for (std::size_t index = 0U; index < attributes_per_control_block; ++index) {
        const auto suffix = kSuffixes[index];
        const auto name_size = definition_->item.size() + 1U + suffix.size();
        auto* name = name_storage_.data() + name_offset;
        std::copy(definition_->item.begin(), definition_->item.end(), name);
        name[definition_->item.size()] = '$';
        std::copy(suffix.begin(), suffix.end(), name + definition_->item.size() + 1U);

        auto& context = context_storage_[index];
        context = MmsStaticSettingGroupObjectContext{
            state_, kAttributes[index], now_utc_, now_context_};
        const bool writable =
            kAttributes[index] == MmsStaticSettingGroupAttribute::active_setting_group;
        object_storage_[object_offset++] = MmsStaticObjectEntry{
            definition_->domain,
            std::string_view{name, name_size},
            type_for(kAttributes[index]),
            read_attribute,
            &context,
            false,
            writable ? write_active_setting_group : nullptr,
            writable ? &context : nullptr};
        name_offset += name_size;
    }

    std::stable_sort(
        object_storage_.begin(),
        object_storage_.begin() + static_cast<std::ptrdiff_t>(object_offset),
        [](const MmsStaticObjectEntry& left, const MmsStaticObjectEntry& right) noexcept {
            if (left.domain != right.domain) return left.domain < right.domain;
            return left.item < right.item;
        });
    table_ = MmsStaticObjectTable{
        std::span<const MmsStaticObjectEntry>{object_storage_.data(), object_offset}};
    if (!table_.valid()) {
        table_ = MmsStaticObjectTable{std::span<const MmsStaticObjectEntry>{}};
        return false;
    }
    initialized_ = true;
    return true;
}

} // namespace ar::iec61850::mms
