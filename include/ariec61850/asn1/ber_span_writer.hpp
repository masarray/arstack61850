// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/asn1/ber_types.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace ar::iec61850::asn1 {

class BerSpanWriter final {
public:
    explicit BerSpanWriter(std::span<std::uint8_t> destination) noexcept
        : destination_{destination} {}

    [[nodiscard]] static std::optional<std::size_t> identifier_octets(
        const std::int32_t tag_number) noexcept {
        if (tag_number < 0) {
            return std::nullopt;
        }
        if (tag_number <= 30) {
            return 1U;
        }

        auto remaining = static_cast<std::uint32_t>(tag_number);
        std::size_t groups = 0U;
        do {
            ++groups;
            remaining >>= 7U;
        } while (remaining != 0U);
        return 1U + groups;
    }

    [[nodiscard]] static std::optional<std::size_t> length_octets(
        const std::size_t value_length) noexcept {
        if (value_length < 0x80U) {
            return 1U;
        }
        if (value_length > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }

        auto value = static_cast<std::uint32_t>(value_length);
        std::size_t bytes = 0U;
        while (value != 0U) {
            ++bytes;
            value >>= 8U;
        }
        return 1U + bytes;
    }

    [[nodiscard]] static std::optional<std::size_t> tlv_size(
        const std::int32_t tag_number,
        const std::size_t value_length) noexcept {
        const auto identifier = identifier_octets(tag_number);
        const auto length = length_octets(value_length);
        if (!identifier || !length) {
            return std::nullopt;
        }
        if (*length > std::numeric_limits<std::size_t>::max() - *identifier) {
            return std::nullopt;
        }
        const auto header = *identifier + *length;
        if (value_length > std::numeric_limits<std::size_t>::max() - header) {
            return std::nullopt;
        }
        return header + value_length;
    }

    [[nodiscard]] bool write_identifier(
        const BerClass tag_class,
        const bool constructed,
        const std::int32_t tag_number) noexcept {
        if (tag_number < 0) {
            failed_ = true;
            return false;
        }

        const auto class_bits = static_cast<std::uint8_t>(
            static_cast<std::uint8_t>(tag_class) << 6U);
        const auto constructed_bit = constructed ? std::uint8_t{0x20U} : std::uint8_t{0U};
        if (tag_number <= 30) {
            return write_byte(static_cast<std::uint8_t>(
                class_bits | constructed_bit | static_cast<std::uint8_t>(tag_number)));
        }

        if (!write_byte(static_cast<std::uint8_t>(class_bits | constructed_bit | 0x1FU))) {
            return false;
        }

        std::array<std::uint8_t, 5U> groups{};
        std::size_t count = 0U;
        auto remaining = static_cast<std::uint32_t>(tag_number);
        do {
            if (count >= groups.size()) {
                failed_ = true;
                return false;
            }
            groups[count++] = static_cast<std::uint8_t>(remaining & 0x7FU);
            remaining >>= 7U;
        } while (remaining != 0U);

        for (std::size_t index = count; index-- > 0U;) {
            auto byte = groups[index];
            if (index != 0U) {
                byte = static_cast<std::uint8_t>(byte | 0x80U);
            }
            if (!write_byte(byte)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool write_length(const std::size_t value_length) noexcept {
        if (value_length < 0x80U) {
            return write_byte(static_cast<std::uint8_t>(value_length));
        }
        if (value_length > std::numeric_limits<std::uint32_t>::max()) {
            failed_ = true;
            return false;
        }

        std::array<std::uint8_t, 4U> bytes{};
        auto value = static_cast<std::uint32_t>(value_length);
        std::size_t count = 0U;
        while (value != 0U) {
            bytes[bytes.size() - 1U - count] = static_cast<std::uint8_t>(value & 0xFFU);
            value >>= 8U;
            ++count;
        }
        if (!write_byte(static_cast<std::uint8_t>(0x80U | count))) {
            return false;
        }
        return write_bytes(std::span<const std::uint8_t>{bytes}.last(count));
    }

    [[nodiscard]] bool write_tlv_header(
        const BerClass tag_class,
        const bool constructed,
        const std::int32_t tag_number,
        const std::size_t value_length) noexcept {
        return write_identifier(tag_class, constructed, tag_number) &&
            write_length(value_length);
    }

    [[nodiscard]] bool write_tlv(
        const BerClass tag_class,
        const bool constructed,
        const std::int32_t tag_number,
        const std::span<const std::uint8_t> value) noexcept {
        return write_tlv_header(tag_class, constructed, tag_number, value.size()) &&
            write_bytes(value);
    }

    [[nodiscard]] bool write_bytes(const std::span<const std::uint8_t> value) noexcept {
        if (failed_ || value.size() > remaining()) {
            failed_ = true;
            return false;
        }
        std::copy(value.begin(), value.end(), destination_.begin() +
            static_cast<std::ptrdiff_t>(offset_));
        offset_ += value.size();
        return true;
    }

    [[nodiscard]] bool write_byte(const std::uint8_t value) noexcept {
        if (failed_ || offset_ >= destination_.size()) {
            failed_ = true;
            return false;
        }
        destination_[offset_++] = value;
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept { return offset_; }
    [[nodiscard]] std::size_t remaining() const noexcept {
        return offset_ <= destination_.size() ? destination_.size() - offset_ : 0U;
    }
    [[nodiscard]] bool good() const noexcept { return !failed_; }

private:
    std::span<std::uint8_t> destination_;
    std::size_t offset_{};
    bool failed_{};
};

} // namespace ar::iec61850::asn1
