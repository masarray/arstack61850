// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/mms/data_value.hpp"
#include "ariec61850/mms/services.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ar::iec61850::mms {

// Host-side bridge from the SCL bType text carried by the simulator manifest
// to exact MMS TypeSpecification/Data values. This deliberately lives beside
// the protocol types instead of in the Qt shell so every desktop simulator
// adapter uses the same wire mapping.
class MmsSimulatorManifestCodec final {
public:
    [[nodiscard]] static MmsTypeSpecification type(
        const std::string_view raw_type,
        const std::string_view normalized_type,
        std::string name = {}) {
        const auto raw = normalized_token(raw_type);
        const auto normalized = normalized_token(normalized_type);

        MmsTypeSpecification result;
        result.name = std::move(name);

        if (normalized == "QUALITY" || raw == "QUALITY") {
            result.kind = MmsTypeKind::bit_string;
            result.size = 13U;
            return result;
        }
        if (raw == "DBPOS" || raw == "TCMD" || raw == "CHECK") {
            result.kind = MmsTypeKind::bit_string;
            result.size = 2U;
            return result;
        }
        if (raw == "TRGOPS") {
            result.kind = MmsTypeKind::bit_string;
            result.size = 6U;
            return result;
        }
        if (raw == "OPTFLDS") {
            result.kind = MmsTypeKind::bit_string;
            result.size = 10U;
            return result;
        }
        if (normalized == "TIMESTAMP" || raw == "TIMESTAMP") {
            result.kind = MmsTypeKind::utc_time;
            return result;
        }
        if (raw == "ENTRYTIME") {
            result.kind = MmsTypeKind::binary_time;
            result.size = 6U;
            return result;
        }
        if (normalized == "ENUMERATION" || raw == "ENUM") {
            result.kind = MmsTypeKind::integer;
            result.size = 32U;
            return result;
        }
        if (normalized == "BOOLEAN" || raw == "BOOLEAN" || raw == "BOOL") {
            result.kind = MmsTypeKind::boolean;
            return result;
        }
        if (const auto width = signed_integer_width(raw); width != 0U) {
            result.kind = MmsTypeKind::integer;
            result.size = width;
            return result;
        }
        if (const auto width = unsigned_integer_width(raw); width != 0U) {
            result.kind = MmsTypeKind::unsigned_integer;
            result.size = width;
            return result;
        }
        if (raw == "FLOAT64") {
            result.kind = MmsTypeKind::floating_point;
            result.size = 64U;
            result.exponent_width = 11U;
            return result;
        }
        if (raw == "FLOAT32" || raw == "FLOAT") {
            result.kind = MmsTypeKind::floating_point;
            result.size = 32U;
            result.exponent_width = 8U;
            return result;
        }
        if (starts_with(raw, "VISSTRING")) {
            result.kind = MmsTypeKind::visible_string;
            result.size = suffix_width(raw, "VISSTRING", 255U);
            return result;
        }
        if (starts_with(raw, "UNICODE") || starts_with(raw, "MMSSTRING")) {
            result.kind = MmsTypeKind::mms_string;
            result.size = starts_with(raw, "UNICODE")
                ? suffix_width(raw, "UNICODE", 255U)
                : suffix_width(raw, "MMSSTRING", 255U);
            return result;
        }
        if (starts_with(raw, "OCTET")) {
            result.kind = MmsTypeKind::octet_string;
            result.size = suffix_width(raw, "OCTET", 64U);
            return result;
        }
        if (raw == "OBJREF") {
            result.kind = MmsTypeKind::visible_string;
            result.size = 129U;
            return result;
        }
        if (raw == "CURRENCY") {
            result.kind = MmsTypeKind::visible_string;
            result.size = 3U;
            return result;
        }

        if (normalized == "NUMBER") {
            result.kind = MmsTypeKind::integer;
            result.size = 32U;
            return result;
        }

        result.kind = MmsTypeKind::visible_string;
        result.size = 255U;
        return result;
    }

    [[nodiscard]] static MmsDataValue data(
        const MmsTypeSpecification& type,
        const std::string_view raw_type,
        const std::string_view normalized_type,
        const std::string_view text) {
        const auto raw = normalized_token(raw_type);
        const auto normalized = normalized_token(normalized_type);

        switch (type.kind) {
        case MmsTypeKind::boolean:
            return MmsDataValue::boolean(text_boolean(text));
        case MmsTypeKind::bit_string:
            if (normalized == "QUALITY" || raw == "QUALITY") {
                return quality(text);
            }
            if (raw == "DBPOS" || raw == "TCMD") {
                return coded_bit_string(2U, dbpos_value(text));
            }
            return coded_bit_string(type.size.value_or(0U), unsigned_text(text));
        case MmsTypeKind::integer:
            return MmsDataValue::integer(signed_text(text));
        case MmsTypeKind::unsigned_integer:
            return MmsDataValue::unsigned_integer(unsigned_text(text));
        case MmsTypeKind::floating_point:
            try {
                return type.size.value_or(32U) == 64U
                    ? MmsDataValue::floating_point(std::stod(std::string{text}))
                    : MmsDataValue::floating_point(std::stof(std::string{text}));
            } catch (...) {
                return type.size.value_or(32U) == 64U
                    ? MmsDataValue::floating_point(0.0)
                    : MmsDataValue::floating_point(0.0F);
            }
        case MmsTypeKind::utc_time:
            return MmsDataValue::utc_time(Iec61850UtcTime{
                timestamp(text), 0U});
        case MmsTypeKind::binary_time: {
            const auto bytes = hex_bytes(text);
            if (!bytes.empty()) return MmsDataValue::binary_time(bytes);
            constexpr std::array<std::uint8_t, 6U> zero{};
            return MmsDataValue::binary_time(zero);
        }
        case MmsTypeKind::octet_string: {
            const auto bytes = hex_bytes(text);
            if (!bytes.empty()) return MmsDataValue::octet_string(bytes);
            return MmsDataValue::octet_string(std::span<const std::uint8_t>{});
        }
        case MmsTypeKind::mms_string:
            return MmsDataValue::mms_string(display_text(text));
        default:
            return MmsDataValue::visible_string(display_text(text));
        }
    }

private:
    [[nodiscard]] static std::string normalized_token(const std::string_view value) {
        std::string result;
        result.reserve(value.size());
        for (const auto ch : value) {
            const auto byte = static_cast<unsigned char>(ch);
            if (std::isalnum(byte) != 0) {
                result.push_back(static_cast<char>(std::toupper(byte)));
            }
        }
        return result;
    }

    [[nodiscard]] static bool starts_with(
        const std::string_view value,
        const std::string_view prefix) noexcept {
        return value.size() >= prefix.size() && value.substr(0U, prefix.size()) == prefix;
    }

    [[nodiscard]] static std::uint32_t suffix_width(
        const std::string_view value,
        const std::string_view prefix,
        const std::uint32_t fallback) noexcept {
        if (!starts_with(value, prefix) || value.size() == prefix.size()) return fallback;
        std::uint32_t width{};
        const auto suffix = value.substr(prefix.size());
        const auto result = std::from_chars(suffix.data(), suffix.data() + suffix.size(), width, 10);
        return result.ec == std::errc{} && result.ptr == suffix.data() + suffix.size() && width != 0U
            ? width
            : fallback;
    }

    [[nodiscard]] static std::uint32_t signed_integer_width(
        const std::string_view raw) noexcept {
        if (raw == "INT8") return 8U;
        if (raw == "INT16") return 16U;
        if (raw == "INT24") return 24U;
        if (raw == "INT32") return 32U;
        if (raw == "INT64") return 64U;
        return 0U;
    }

    [[nodiscard]] static std::uint32_t unsigned_integer_width(
        const std::string_view raw) noexcept {
        if (raw == "INT8U" || raw == "UINT8") return 8U;
        if (raw == "INT16U" || raw == "UINT16") return 16U;
        if (raw == "INT24U" || raw == "UINT24") return 24U;
        if (raw == "INT32U" || raw == "UINT32") return 32U;
        if (raw == "INT64U" || raw == "UINT64") return 64U;
        return 0U;
    }

    [[nodiscard]] static std::string trim_copy(const std::string_view value) {
        std::size_t first{};
        while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
            ++first;
        }
        std::size_t last = value.size();
        while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1U])) != 0) {
            --last;
        }
        return std::string{value.substr(first, last - first)};
    }

    [[nodiscard]] static bool text_boolean(const std::string_view value) {
        const auto token = normalized_token(value);
        return token == "1" || token == "TRUE" || token == "ON" || token == "CLOSED";
    }

    [[nodiscard]] static std::int64_t signed_text(const std::string_view value) noexcept {
        const auto token = normalized_token(value);
        if (token == "INTERMEDIATESTATE") return 0;
        if (token == "OFF" || token == "OPEN") return 1;
        if (token == "ON" || token == "CLOSED") return 2;
        if (token == "BADSTATE") return 3;

        const auto text = trim_copy(value);
        if (text.empty()) return 0;
        std::int64_t result{};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result, 10);
        return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() ? result : 0;
    }

    [[nodiscard]] static std::uint64_t unsigned_text(const std::string_view value) noexcept {
        const auto text = trim_copy(value);
        if (!text.empty() && text.front() != '-') {
            std::uint64_t result{};
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result, 10);
            if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size()) return result;
        }
        const auto signed_value = signed_text(value);
        return signed_value < 0 ? 0U : static_cast<std::uint64_t>(signed_value);
    }

    [[nodiscard]] static std::uint64_t dbpos_value(const std::string_view value) noexcept {
        const auto token = normalized_token(value);
        if (token == "OFF" || token == "OPEN") return 1U;
        if (token == "ON" || token == "CLOSED") return 2U;
        if (token == "BADSTATE") return 3U;
        if (token == "INTERMEDIATESTATE") return 0U;
        return unsigned_text(value) & 0x03U;
    }

    [[nodiscard]] static MmsDataValue coded_bit_string(
        const std::uint32_t bit_count,
        const std::uint64_t value) {
        if (bit_count == 0U || bit_count > 64U) {
            return MmsDataValue::bit_string(0U, {});
        }
        const auto byte_count = static_cast<std::size_t>((bit_count + 7U) / 8U);
        const auto unused = static_cast<std::uint8_t>((8U - (bit_count % 8U)) % 8U);
        std::vector<std::uint8_t> bytes(byte_count, 0U);
        for (std::uint32_t bit = 0U; bit < bit_count; ++bit) {
            const auto source_shift = bit_count - 1U - bit;
            if ((value & (std::uint64_t{1U} << source_shift)) == 0U) continue;
            bytes[bit / 8U] |= static_cast<std::uint8_t>(0x80U >> (bit % 8U));
        }
        return MmsDataValue::bit_string(unused, bytes);
    }

    static void set_named_bit(
        std::array<std::uint8_t, 2U>& bytes,
        const std::uint32_t index) noexcept {
        if (index >= 13U) return;
        bytes[index / 8U] |= static_cast<std::uint8_t>(0x80U >> (index % 8U));
    }

    [[nodiscard]] static MmsDataValue quality(const std::string_view value) {
        const auto lower = [&] {
            std::string result{value};
            std::transform(result.begin(), result.end(), result.begin(), [](const unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return result;
        }();

        std::array<std::uint8_t, 2U> bytes{};
        std::uint8_t validity{};
        if (lower.find("questionable") != std::string::npos) validity = 3U;
        else if (lower.find("reserved") != std::string::npos) validity = 2U;
        else if (lower.find("invalid") != std::string::npos) validity = 1U;

        if ((validity & 0x02U) != 0U) set_named_bit(bytes, 0U);
        if ((validity & 0x01U) != 0U) set_named_bit(bytes, 1U);
        const auto flag = [&](const std::string_view hyphenated,
                              const std::string_view compact,
                              const std::uint32_t bit) {
            if (lower.find(hyphenated) != std::string::npos ||
                lower.find(compact) != std::string::npos) {
                set_named_bit(bytes, bit);
            }
        };
        flag("overflow", "overflow", 2U);
        flag("out-of-range", "outofrange", 3U);
        flag("bad-reference", "badreference", 4U);
        flag("oscillatory", "oscillatory", 5U);
        flag("failure", "failure", 6U);
        flag("old-data", "olddata", 7U);
        flag("inconsistent", "inconsistent", 8U);
        flag("inaccurate", "inaccurate", 9U);
        flag("substituted", "substituted", 10U);
        flag("test", "test", 11U);
        flag("operator-blocked", "operatorblocked", 12U);
        return MmsDataValue::bit_string(3U, bytes);
    }

    [[nodiscard]] static std::chrono::system_clock::time_point timestamp(
        const std::string_view value) noexcept {
        auto text = trim_copy(value);
        if (starts_with(text, "unix-ms:") || starts_with(text, "unix-ms=")) {
            text.erase(0U, 8U);
        }
        std::int64_t milliseconds{};
        const auto parsed = std::from_chars(
            text.data(), text.data() + text.size(), milliseconds, 10);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || milliseconds < 0) {
            milliseconds = 0;
        }
        return std::chrono::system_clock::time_point{std::chrono::milliseconds{milliseconds}};
    }

    [[nodiscard]] static std::vector<std::uint8_t> hex_bytes(const std::string_view value) {
        auto text = trim_copy(value);
        if (text.size() >= 4U &&
            (text.substr(0U, 4U) == "hex:" || text.substr(0U, 4U) == "HEX:")) {
            text.erase(0U, 4U);
        }
        text.erase(std::remove_if(text.begin(), text.end(), [](const char ch) {
            return ch == ':' || ch == '-' || std::isspace(static_cast<unsigned char>(ch)) != 0;
        }), text.end());
        if (text.empty() || (text.size() % 2U) != 0U) return {};

        std::vector<std::uint8_t> bytes;
        bytes.reserve(text.size() / 2U);
        for (std::size_t offset = 0U; offset < text.size(); offset += 2U) {
            unsigned value_byte{};
            const auto pair = std::string_view{text}.substr(offset, 2U);
            const auto parsed = std::from_chars(
                pair.data(), pair.data() + pair.size(), value_byte, 16);
            if (parsed.ec != std::errc{} || parsed.ptr != pair.data() + pair.size() || value_byte > 0xFFU) {
                return {};
            }
            bytes.push_back(static_cast<std::uint8_t>(value_byte));
        }
        return bytes;
    }

    [[nodiscard]] static std::string display_text(const std::string_view value) {
        const auto text = trim_copy(value);
        return text == "---" ? std::string{} : text;
    }
};

} // namespace ar::iec61850::mms
