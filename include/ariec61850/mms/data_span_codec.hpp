// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/asn1/ber_span_writer.hpp"
#include "ariec61850/mms/data_value.hpp"
#include "ariec61850/wire/encode_result.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>

namespace ar::iec61850::mms {

class MmsDataSpanCodec final {
public:
    static constexpr std::size_t maximum_nesting_depth = 16U;

    [[nodiscard]] static std::optional<std::size_t> encoded_size(
        const MmsDataValue& value) noexcept {
        return encoded_size_impl(value, 0U);
    }

    [[nodiscard]] static std::optional<std::size_t> encoded_all_size(
        const std::span<const MmsDataValue> values) noexcept {
        return encoded_all_size_impl(values, 0U);
    }

    [[nodiscard]] static wire::EncodeResult encode_into(
        const MmsDataValue& value,
        const std::span<std::uint8_t> destination) noexcept {
        const auto required = encoded_size(value);
        if (!required) {
            return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
        }
        if (destination.size() < *required) {
            return {wire::EncodeStatus::buffer_too_small, 0U, *required};
        }

        asn1::BerSpanWriter writer{destination.first(*required)};
        if (!write_value(writer, value, 0U) || writer.size() != *required) {
            return {wire::EncodeStatus::value_out_of_range, 0U, *required};
        }
        return {wire::EncodeStatus::ok, writer.size(), *required};
    }

    [[nodiscard]] static wire::EncodeResult encode_all_into(
        const std::span<const MmsDataValue> values,
        const std::span<std::uint8_t> destination) noexcept {
        const auto required = encoded_all_size(values);
        if (!required) {
            return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
        }
        if (destination.size() < *required) {
            return {wire::EncodeStatus::buffer_too_small, 0U, *required};
        }

        asn1::BerSpanWriter writer{destination.first(*required)};
        for (const auto& value : values) {
            if (!write_value(writer, value, 0U)) {
                return {wire::EncodeStatus::value_out_of_range, 0U, *required};
            }
        }
        if (writer.size() != *required) {
            return {wire::EncodeStatus::value_out_of_range, 0U, *required};
        }
        return {wire::EncodeStatus::ok, writer.size(), *required};
    }

private:
    [[nodiscard]] static std::optional<std::int32_t> tag_for(
        const MmsDataValue& value) noexcept {
        switch (value.kind()) {
        case MmsDataKind::array: return 1;
        case MmsDataKind::structure: return 2;
        case MmsDataKind::boolean: return 3;
        case MmsDataKind::bit_string: return 4;
        case MmsDataKind::integer: return 5;
        case MmsDataKind::unsigned_integer: return 6;
        case MmsDataKind::floating_point: return 7;
        case MmsDataKind::octet_string: return 9;
        case MmsDataKind::visible_string: return 10;
        case MmsDataKind::binary_time: return 12;
        case MmsDataKind::mms_string: return 16;
        case MmsDataKind::utc_time: return 17;
        case MmsDataKind::unknown:
            return value.unknown_tag_number();
        default:
            return std::nullopt;
        }
    }

    [[nodiscard]] static std::size_t unsigned_integer_size(
        std::uint64_t value) noexcept {
        std::size_t size = 1U;
        while (value > 0xFFU) {
            ++size;
            value >>= 8U;
        }
        return size;
    }

    [[nodiscard]] static std::size_t signed_integer_size(
        const std::int64_t value) noexcept {
        const auto raw = std::bit_cast<std::uint64_t>(value);
        std::array<std::uint8_t, 8U> bytes{};
        for (std::size_t index = 0U; index < bytes.size(); ++index) {
            const auto shift = static_cast<unsigned>((bytes.size() - 1U - index) * 8U);
            bytes[index] = static_cast<std::uint8_t>((raw >> shift) & 0xFFU);
        }

        std::size_t first = 0U;
        while (first < bytes.size() - 1U) {
            const auto current = bytes[first];
            const auto next = bytes[first + 1U];
            const bool redundant_positive = current == 0x00U && (next & 0x80U) == 0U;
            const bool redundant_negative = current == 0xFFU && (next & 0x80U) != 0U;
            if (!redundant_positive && !redundant_negative) {
                break;
            }
            ++first;
        }
        return bytes.size() - first;
    }

    [[nodiscard]] static std::optional<std::size_t> content_size(
        const MmsDataValue& value,
        const std::size_t depth) noexcept {
        if (depth > maximum_nesting_depth) {
            return std::nullopt;
        }

        switch (value.kind()) {
        case MmsDataKind::array:
        case MmsDataKind::structure:
            return encoded_all_size_impl(value.children(), depth + 1U);
        case MmsDataKind::boolean:
            return std::get_if<bool>(&value.value()) != nullptr
                ? std::optional<std::size_t>{1U}
                : std::nullopt;
        case MmsDataKind::bit_string:
        case MmsDataKind::octet_string:
        case MmsDataKind::binary_time:
            return value.raw_value().size();
        case MmsDataKind::integer: {
            const auto* scalar = std::get_if<std::int64_t>(&value.value());
            return scalar != nullptr
                ? std::optional<std::size_t>{signed_integer_size(*scalar)}
                : std::nullopt;
        }
        case MmsDataKind::unsigned_integer: {
            const auto* scalar = std::get_if<std::uint64_t>(&value.value());
            return scalar != nullptr
                ? std::optional<std::size_t>{unsigned_integer_size(*scalar)}
                : std::nullopt;
        }
        case MmsDataKind::floating_point:
            return (std::get_if<float>(&value.value()) != nullptr ||
                    std::get_if<double>(&value.value()) != nullptr)
                ? std::optional<std::size_t>{5U}
                : std::nullopt;
        case MmsDataKind::visible_string:
        case MmsDataKind::mms_string: {
            const auto* text = std::get_if<std::string>(&value.value());
            return text != nullptr
                ? std::optional<std::size_t>{text->size()}
                : std::nullopt;
        }
        case MmsDataKind::utc_time: {
            const auto* utc = std::get_if<Iec61850UtcTime>(&value.value());
            if (utc == nullptr) {
                return std::nullopt;
            }
            std::array<std::uint8_t, 8U> bytes{};
            return utc->try_write_bytes(bytes)
                ? std::optional<std::size_t>{bytes.size()}
                : std::nullopt;
        }
        case MmsDataKind::unknown:
            return value.unknown_tag_number().has_value()
                ? std::optional<std::size_t>{value.raw_value().size()}
                : std::nullopt;
        default:
            return std::nullopt;
        }
    }

    [[nodiscard]] static std::optional<std::size_t> encoded_size_impl(
        const MmsDataValue& value,
        const std::size_t depth) noexcept {
        if (depth > maximum_nesting_depth) {
            return std::nullopt;
        }
        const auto tag = tag_for(value);
        const auto content = content_size(value, depth);
        if (!tag || !content) {
            return std::nullopt;
        }
        return asn1::BerSpanWriter::tlv_size(*tag, *content);
    }

    [[nodiscard]] static std::optional<std::size_t> encoded_all_size_impl(
        const std::span<const MmsDataValue> values,
        const std::size_t depth) noexcept {
        if (depth > maximum_nesting_depth) {
            return std::nullopt;
        }
        std::size_t total = 0U;
        for (const auto& value : values) {
            const auto size = encoded_size_impl(value, depth);
            if (!size || *size > std::numeric_limits<std::size_t>::max() - total) {
                return std::nullopt;
            }
            total += *size;
        }
        return total;
    }

    [[nodiscard]] static bool write_unsigned(
        asn1::BerSpanWriter& writer,
        const std::uint64_t value) noexcept {
        const auto size = unsigned_integer_size(value);
        for (std::size_t index = size; index-- > 0U;) {
            const auto shift = static_cast<unsigned>(index * 8U);
            if (!writer.write_byte(static_cast<std::uint8_t>((value >> shift) & 0xFFU))) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static bool write_signed(
        asn1::BerSpanWriter& writer,
        const std::int64_t value) noexcept {
        const auto raw = std::bit_cast<std::uint64_t>(value);
        std::array<std::uint8_t, 8U> bytes{};
        for (std::size_t index = 0U; index < bytes.size(); ++index) {
            const auto shift = static_cast<unsigned>((bytes.size() - 1U - index) * 8U);
            bytes[index] = static_cast<std::uint8_t>((raw >> shift) & 0xFFU);
        }
        const auto size = signed_integer_size(value);
        return writer.write_bytes(std::span<const std::uint8_t>{bytes}.last(size));
    }

    [[nodiscard]] static bool write_value(
        asn1::BerSpanWriter& writer,
        const MmsDataValue& value,
        const std::size_t depth) noexcept {
        if (depth > maximum_nesting_depth) {
            return false;
        }
        const auto tag = tag_for(value);
        const auto content = content_size(value, depth);
        if (!tag || !content) {
            return false;
        }
        const bool constructed =
            value.kind() == MmsDataKind::array || value.kind() == MmsDataKind::structure;
        if (!writer.write_tlv_header(
                asn1::BerClass::context_specific,
                constructed,
                *tag,
                *content)) {
            return false;
        }

        switch (value.kind()) {
        case MmsDataKind::array:
        case MmsDataKind::structure:
            for (const auto& child : value.children()) {
                if (!write_value(writer, child, depth + 1U)) {
                    return false;
                }
            }
            return true;
        case MmsDataKind::boolean: {
            const auto* scalar = std::get_if<bool>(&value.value());
            return scalar != nullptr && writer.write_byte(*scalar ? 0x01U : 0x00U);
        }
        case MmsDataKind::bit_string:
        case MmsDataKind::octet_string:
        case MmsDataKind::binary_time:
        case MmsDataKind::unknown:
            return writer.write_bytes(value.raw_value());
        case MmsDataKind::integer: {
            const auto* scalar = std::get_if<std::int64_t>(&value.value());
            return scalar != nullptr && write_signed(writer, *scalar);
        }
        case MmsDataKind::unsigned_integer: {
            const auto* scalar = std::get_if<std::uint64_t>(&value.value());
            return scalar != nullptr && write_unsigned(writer, *scalar);
        }
        case MmsDataKind::floating_point: {
            float scalar = 0.0F;
            if (const auto* single = std::get_if<float>(&value.value())) {
                scalar = *single;
            } else if (const auto* double_value = std::get_if<double>(&value.value())) {
                scalar = static_cast<float>(*double_value);
            } else {
                return false;
            }
            const auto raw = std::bit_cast<std::uint32_t>(scalar);
            return writer.write_byte(0x08U) &&
                writer.write_byte(static_cast<std::uint8_t>((raw >> 24U) & 0xFFU)) &&
                writer.write_byte(static_cast<std::uint8_t>((raw >> 16U) & 0xFFU)) &&
                writer.write_byte(static_cast<std::uint8_t>((raw >> 8U) & 0xFFU)) &&
                writer.write_byte(static_cast<std::uint8_t>(raw & 0xFFU));
        }
        case MmsDataKind::visible_string:
        case MmsDataKind::mms_string: {
            const auto* text = std::get_if<std::string>(&value.value());
            if (text == nullptr) {
                return false;
            }
            for (const auto character : *text) {
                if (!writer.write_byte(static_cast<std::uint8_t>(character))) {
                    return false;
                }
            }
            return true;
        }
        case MmsDataKind::utc_time: {
            const auto* utc = std::get_if<Iec61850UtcTime>(&value.value());
            if (utc == nullptr) {
                return false;
            }
            std::array<std::uint8_t, 8U> bytes{};
            return utc->try_write_bytes(bytes) && writer.write_bytes(bytes);
        }
        default:
            return false;
        }
    }
};

} // namespace ar::iec61850::mms
