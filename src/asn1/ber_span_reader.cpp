// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/asn1/ber_span_reader.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace ar::iec61850::asn1 {

bool BerSpanReader::try_read_tlv(
    const std::span<const std::uint8_t> source,
    std::size_t& offset,
    BerTlvView& tlv) noexcept {
    tlv = {};
    if (offset >= source.size()) {
        return false;
    }

    auto cursor = offset;
    const auto start = cursor;
    const auto encoded_tag = source[cursor++];
    std::int32_t tag_number = static_cast<std::int32_t>(encoded_tag & 0x1FU);

    if (tag_number == 0x1F) {
        tag_number = 0;
        bool read_any = false;
        bool terminated = false;
        while (cursor < source.size()) {
            const auto byte = source[cursor++];
            read_any = true;
            if (tag_number > (1'000'000 >> 7)) {
                return false;
            }
            const auto next_tag =
                (static_cast<std::uint32_t>(tag_number) << 7U) |
                static_cast<std::uint32_t>(byte & 0x7FU);
            tag_number = static_cast<std::int32_t>(next_tag);
            if (tag_number > 1'000'000) {
                return false;
            }
            if ((byte & 0x80U) == 0U) {
                terminated = true;
                break;
            }
        }
        if (!read_any || !terminated) {
            return false;
        }
    }

    if (cursor >= source.size()) {
        return false;
    }

    const auto length_byte = source[cursor++];
    std::size_t length = 0U;
    if ((length_byte & 0x80U) == 0U) {
        length = static_cast<std::size_t>(length_byte);
    } else {
        const auto length_bytes = static_cast<std::size_t>(length_byte & 0x7FU);
        if (length_bytes == 0U || length_bytes > 4U ||
            length_bytes > source.size() - cursor) {
            return false;
        }
        for (std::size_t index = 0U; index < length_bytes; ++index) {
            length = (length << 8U) |
                static_cast<std::size_t>(source[cursor++]);
        }
    }

    if (length > source.size() - cursor) {
        return false;
    }

    const auto header_bytes = cursor - start;
    const auto encoded_bytes = header_bytes + length;
    tlv.encoded_tag = encoded_tag;
    tlv.tag_class = static_cast<BerClass>((encoded_tag >> 6U) & 0x03U);
    tlv.constructed = (encoded_tag & 0x20U) != 0U;
    tlv.tag_number = tag_number;
    tlv.header_bytes = header_bytes;
    tlv.encoded_bytes = encoded_bytes;
    tlv.value = source.subspan(cursor, length);
    offset = cursor + length;
    return true;
}

bool BerSpanReader::try_read_exact(
    const std::span<const std::uint8_t> source,
    BerTlvView& tlv) noexcept {
    std::size_t offset = 0U;
    return try_read_tlv(source, offset, tlv) && offset == source.size();
}

std::optional<std::uint64_t> BerSpanReader::read_unsigned_integer(
    const BerTlvView& tlv) noexcept {
    if (tlv.value.size() > sizeof(std::uint64_t)) {
        return std::nullopt;
    }

    std::uint64_t value = 0U;
    for (const auto byte : tlv.value) {
        value = (value << 8U) | static_cast<std::uint64_t>(byte);
    }
    return value;
}

std::optional<std::int64_t> BerSpanReader::read_signed_integer(
    const BerTlvView& tlv) noexcept {
    if (tlv.value.empty()) {
        return std::int64_t{0};
    }
    if (tlv.value.size() > sizeof(std::int64_t)) {
        return std::nullopt;
    }

    std::uint64_t raw = 0U;
    for (const auto byte : tlv.value) {
        raw = (raw << 8U) | static_cast<std::uint64_t>(byte);
    }
    if ((tlv.value.front() & 0x80U) != 0U &&
        tlv.value.size() < sizeof(std::int64_t)) {
        const auto used_bits = static_cast<unsigned>(tlv.value.size() * 8U);
        raw |= (~std::uint64_t{0}) << used_bits;
    }
    return std::bit_cast<std::int64_t>(raw);
}

std::optional<std::uint32_t> BerSpanReader::read_uint32(
    const BerTlvView& tlv) noexcept {
    const auto value = read_unsigned_integer(tlv);
    if (!value || *value > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*value);
}

} // namespace ar::iec61850::asn1
