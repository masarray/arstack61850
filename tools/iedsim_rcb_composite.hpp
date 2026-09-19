// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/static_brcb_control.hpp"
#include "ariec61850/mms/static_brcb_runtime.hpp"
#include "ariec61850/mms/static_urcb_runtime.hpp"
#include "ariec61850/wire/encode_result.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ar::iec61850::iedsim_rcb {
namespace detail {

using wire::EncodeResult;
using wire::EncodeStatus;

class BufferWriter final {
public:
    explicit BufferWriter(const std::span<std::uint8_t> destination) noexcept
        : destination_{destination} {}

    [[nodiscard]] bool byte(const std::uint8_t value) noexcept {
        if (offset_ >= destination_.size()) return false;
        destination_[offset_++] = value;
        return true;
    }

    [[nodiscard]] bool bytes(const std::span<const std::uint8_t> value) noexcept {
        if (value.size() > destination_.size() - offset_) return false;
        for (const auto byte_value : value) destination_[offset_++] = byte_value;
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept { return offset_; }

private:
    std::span<std::uint8_t> destination_{};
    std::size_t offset_{};
};

[[nodiscard]] inline std::size_t encoded_length_size(const std::size_t value) noexcept {
    if (value < 128U) return 1U;
    if (value <= 0xFFU) return 2U;
    if (value <= 0xFFFFU) return 3U;
    return 0U;
}

[[nodiscard]] inline bool append_length(BufferWriter& writer, const std::size_t value) noexcept {
    if (value < 128U) return writer.byte(static_cast<std::uint8_t>(value));
    if (value <= 0xFFU) {
        return writer.byte(0x81U) && writer.byte(static_cast<std::uint8_t>(value));
    }
    if (value <= 0xFFFFU) {
        return writer.byte(0x82U) &&
            writer.byte(static_cast<std::uint8_t>((value >> 8U) & 0xFFU)) &&
            writer.byte(static_cast<std::uint8_t>(value & 0xFFU));
    }
    return false;
}

[[nodiscard]] inline bool append_header(
    BufferWriter& writer,
    const std::uint8_t tag,
    const bool constructed,
    const std::size_t content_size) noexcept {
    if (tag >= 31U) return false;
    const auto identifier = static_cast<std::uint8_t>(
        0x80U | (constructed ? 0x20U : 0x00U) | tag);
    return writer.byte(identifier) && append_length(writer, content_size);
}

[[nodiscard]] inline bool append_boolean(BufferWriter& writer, const bool value) noexcept {
    return append_header(writer, 3U, false, 1U) &&
        writer.byte(value ? 0xFFU : 0x00U);
}

[[nodiscard]] inline std::size_t unsigned_size(std::uint32_t value) noexcept {
    std::size_t size = 1U;
    while (value > 0xFFU) {
        ++size;
        value >>= 8U;
    }
    return size;
}

[[nodiscard]] inline bool append_unsigned(
    BufferWriter& writer,
    const std::uint32_t value) noexcept {
    const auto content_size = unsigned_size(value);
    if (!append_header(writer, 6U, false, content_size)) return false;
    for (std::size_t index = content_size; index-- > 0U;) {
        if (!writer.byte(static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool append_nonnegative_integer16(
    BufferWriter& writer,
    const std::uint32_t value) noexcept {
    if (value > 0x7FFFU) return false;
    const auto content_size = value <= 0x7FU ? std::size_t{1U} : std::size_t{2U};
    if (!append_header(writer, 5U, false, content_size)) return false;
    if (content_size == 2U &&
        !writer.byte(static_cast<std::uint8_t>((value >> 8U) & 0x7FU))) {
        return false;
    }
    return writer.byte(static_cast<std::uint8_t>(value & 0xFFU));
}

[[nodiscard]] inline std::span<const std::uint8_t> as_bytes(
    const std::string_view value) noexcept {
    return {
        reinterpret_cast<const std::uint8_t*>(value.data()),
        value.size()};
}

[[nodiscard]] inline bool append_visible(
    BufferWriter& writer,
    const std::string_view value) noexcept {
    return append_header(writer, 10U, false, value.size()) &&
        writer.bytes(as_bytes(value));
}

[[nodiscard]] inline bool append_octets(
    BufferWriter& writer,
    const std::span<const std::uint8_t> value) noexcept {
    return append_header(writer, 9U, false, value.size()) && writer.bytes(value);
}

[[nodiscard]] inline bool append_bit_string(
    BufferWriter& writer,
    const std::uint8_t unused_bits,
    const std::span<const std::uint8_t> value) noexcept {
    if (unused_bits > 7U) return false;
    return append_header(writer, 4U, false, value.size() + 1U) &&
        writer.byte(unused_bits) && writer.bytes(value);
}

[[nodiscard]] inline bool append_data_set_reference(
    BufferWriter& writer,
    const std::string_view domain,
    const std::string_view item) noexcept {
    if (domain.size() > 2'000U || item.size() > 2'000U ||
        domain.size() + item.size() + 1U > 0xFFFFU) {
        return false;
    }
    const auto size = domain.size() + 1U + item.size();
    return append_header(writer, 10U, false, size) &&
        writer.bytes(as_bytes(domain)) && writer.byte(static_cast<std::uint8_t>('/')) &&
        writer.bytes(as_bytes(item));
}

[[nodiscard]] inline bool append_binary_time_zero(BufferWriter& writer) noexcept {
    constexpr std::array<std::uint8_t, 6U> zero{};
    return append_header(writer, 12U, false, zero.size()) && writer.bytes(zero);
}

template <typename EncodeBody>
[[nodiscard]] inline EncodeResult encode_structure(
    const std::span<std::uint8_t> destination,
    EncodeBody&& body) noexcept {
    std::array<std::uint8_t, 2'048U> body_storage{};
    BufferWriter body_writer{body_storage};
    if (!body(body_writer)) {
        return {EncodeStatus::value_out_of_range, 0U, 0U};
    }

    const auto length_size = encoded_length_size(body_writer.size());
    if (length_size == 0U) {
        return {EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto required = 1U + length_size + body_writer.size();
    if (destination.size() < required) {
        return {EncodeStatus::buffer_too_small, 0U, required};
    }

    BufferWriter writer{destination.first(required)};
    if (!append_header(writer, 2U, true, body_writer.size()) ||
        !writer.bytes(std::span<const std::uint8_t>{
            body_storage.data(), body_writer.size()}) ||
        writer.size() != required) {
        return {EncodeStatus::value_out_of_range, 0U, required};
    }
    return {EncodeStatus::ok, required, required};
}

} // namespace detail

inline std::span<const std::uint8_t> parent_structure_type() noexcept {
    static constexpr std::array<std::uint8_t, 2U> value{0xA2U, 0x00U};
    return value;
}

struct HostUrcbCompositeContext final {
    const mms::MmsStaticUrcbState* state{};
};

inline wire::EncodeResult read_urcb_parent(
    const void* raw_context,
    const std::span<std::uint8_t> destination) noexcept {
    const auto* context = static_cast<const HostUrcbCompositeContext*>(raw_context);
    if (context == nullptr || context->state == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto& state = *context->state;
    return detail::encode_structure(destination, [&](detail::BufferWriter& writer) noexcept {
        const std::array<std::uint8_t, 1U> trg_ops{state.trigger_options};
        return detail::append_visible(writer, state.report_id()) &&
            detail::append_boolean(writer, state.enabled) &&
            detail::append_boolean(writer, state.reserved) &&
            detail::append_data_set_reference(writer, state.data_set_domain(), state.data_set_item()) &&
            detail::append_unsigned(writer, state.conf_revision) &&
            detail::append_bit_string(writer, 6U, state.optional_fields) &&
            detail::append_unsigned(writer, state.buffer_time_ms) &&
            detail::append_unsigned(writer, state.sequence_number) &&
            detail::append_bit_string(writer, 2U, trg_ops) &&
            detail::append_unsigned(writer, state.integrity_period_ms) &&
            detail::append_boolean(writer, state.general_interrogation_pending);
    });
}

using HostNowMs = std::uint64_t (*)(const void*) noexcept;

struct HostBrcbCompositeContext final {
    const mms::MmsStaticBrcbDefinition* definition{};
    mms::MmsStaticBrcbRuntime* runtime{};
    mms::MmsStaticBrcbControl* control{};
    std::uint32_t integrity_period_ms{};
    HostNowMs now_ms{};
    const void* now_context{};
};

inline wire::EncodeResult read_brcb_parent(
    const void* raw_context,
    const std::span<std::uint8_t> destination) noexcept {
    auto* context = const_cast<HostBrcbCompositeContext*>(
        static_cast<const HostBrcbCompositeContext*>(raw_context));
    if (context == nullptr || context->definition == nullptr ||
        context->runtime == nullptr || context->control == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    const auto& definition = *context->definition;
    const auto now = context->now_ms == nullptr ? std::uint64_t{0U}
                                                 : context->now_ms(context->now_context);
    const auto control_state = context->control->state(now);
    const auto entry_id = context->runtime->latest_entry_id();
    mms::MmsStaticBrcbEntryView front{};
    const auto sequence_number = context->runtime->front(front)
        ? static_cast<std::uint32_t>(front.sequence_number)
        : std::uint32_t{0U};
    const std::array<std::uint8_t, 1U> trg_ops{definition.trigger_options};

    return detail::encode_structure(destination, [&](detail::BufferWriter& writer) noexcept {
        return detail::append_visible(writer, definition.report_id) &&
            detail::append_boolean(writer, context->runtime->enabled()) &&
            detail::append_data_set_reference(writer, definition.data_set_domain, definition.data_set_item) &&
            detail::append_unsigned(writer, definition.conf_revision) &&
            detail::append_bit_string(writer, 6U, definition.optional_fields) &&
            detail::append_unsigned(writer, definition.buffer_time_ms) &&
            detail::append_unsigned(writer, sequence_number) &&
            detail::append_bit_string(writer, 2U, trg_ops) &&
            detail::append_unsigned(writer, context->integrity_period_ms) &&
            detail::append_boolean(writer, false) &&
            detail::append_boolean(writer, false) &&
            detail::append_octets(writer, entry_id) &&
            detail::append_binary_time_zero(writer) &&
            detail::append_nonnegative_integer16(writer, control_state.resv_tms_seconds) &&
            detail::append_octets(writer, control_state.owner);
    });
}

} // namespace ar::iec61850::iedsim_rcb
