// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/asn1/ber.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace ar::iec61850::asn1 {
namespace {

void write_u32_be(std::span<std::uint8_t, 4> out, const std::uint32_t value) noexcept {
    out[0] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    out[1] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    out[2] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    out[3] = static_cast<std::uint8_t>(value & 0xFFU);
}

} // namespace

bool BerReader::try_read_tlv(
    const std::span<const std::uint8_t> source,
    std::size_t& offset,
    BerTlv& tlv) noexcept {
    tlv = {};

    if (offset >= source.size()) {
        return false;
    }

    const auto encoded_tag = source[offset++];
    std::int32_t tag_number = static_cast<std::int32_t>(encoded_tag & 0x1FU);

    if (tag_number == 0x1F) {
        tag_number = 0;
        bool read_any = false;
        bool terminated = false;

        while (offset < source.size()) {
            const auto byte = source[offset++];
            read_any = true;

            if (tag_number > (1'000'000 >> 7)) {
                return false;
            }

            const auto next_tag = (static_cast<std::uint32_t>(tag_number) << 7U) |
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

    if (offset >= source.size()) {
        return false;
    }

    const auto length_byte = source[offset++];
    std::size_t length = 0;

    if ((length_byte & 0x80U) == 0U) {
        length = length_byte;
    } else {
        const auto length_bytes = static_cast<std::size_t>(length_byte & 0x7FU);
        if (length_bytes == 0 || length_bytes > 4 || offset + length_bytes > source.size()) {
            return false;
        }

        for (std::size_t index = 0; index < length_bytes; ++index) {
            length = (length << 8U) | source[offset++];
        }
    }

    if (length > source.size() - offset) {
        return false;
    }

    tlv.encoded_tag = encoded_tag;
    tlv.tag_class = static_cast<BerClass>((encoded_tag >> 6U) & 0x03U);
    tlv.constructed = (encoded_tag & 0x20U) != 0U;
    tlv.tag_number = tag_number;
    tlv.value.assign(source.begin() + static_cast<std::ptrdiff_t>(offset),
                     source.begin() + static_cast<std::ptrdiff_t>(offset + length));
    offset += length;
    return true;
}

std::vector<BerTlv> BerReader::read_children(const std::span<const std::uint8_t> source) {
    std::vector<BerTlv> result;
    std::size_t offset = 0;

    while (offset < source.size()) {
        BerTlv tlv;
        const auto failed_offset = offset;
        if (!try_read_tlv(source, offset, tlv)) {
            throw BerFormatError("Invalid BER TLV at offset " + std::to_string(failed_offset) + ".");
        }
        result.push_back(std::move(tlv));
    }

    return result;
}

std::string BerReader::read_ascii_string(const BerTlv& tlv) {
    return {tlv.value.begin(), tlv.value.end()};
}

std::optional<bool> BerReader::read_boolean(const BerTlv& tlv) noexcept {
    if (tlv.value.size() != 1U) {
        return std::nullopt;
    }
    return tlv.value[0] != 0U;
}

std::optional<std::uint64_t> BerReader::read_unsigned_integer(const BerTlv& tlv) noexcept {
    if (tlv.value.size() > sizeof(std::uint64_t)) {
        return std::nullopt;
    }

    std::uint64_t value = 0;
    for (const auto byte : tlv.value) {
        value = (value << 8U) | byte;
    }
    return value;
}

std::optional<std::int64_t> BerReader::read_signed_integer(const BerTlv& tlv) noexcept {
    if (tlv.value.empty()) {
        return 0;
    }
    if (tlv.value.size() > sizeof(std::int64_t)) {
        return std::nullopt;
    }

    std::uint64_t raw = 0;
    for (const auto byte : tlv.value) {
        raw = (raw << 8U) | byte;
    }

    if ((tlv.value.front() & 0x80U) != 0U && tlv.value.size() < sizeof(std::int64_t)) {
        const auto used_bits = static_cast<unsigned>(tlv.value.size() * 8U);
        raw |= (~std::uint64_t{0}) << used_bits;
    }

    return std::bit_cast<std::int64_t>(raw);
}

std::optional<std::uint16_t> BerReader::read_uint16(const BerTlv& tlv) noexcept {
    const auto value = read_unsigned_integer(tlv);
    if (!value || *value > std::numeric_limits<std::uint16_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(*value);
}

std::optional<std::uint32_t> BerReader::read_uint32(const BerTlv& tlv) noexcept {
    const auto value = read_unsigned_integer(tlv);
    if (!value || *value > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*value);
}

std::uint32_t BerReader::read_uint32_big_endian(const std::span<const std::uint8_t> source) {
    if (source.size() != 4U) {
        throw std::invalid_argument("A 32-bit integer requires exactly four bytes.");
    }

    return (static_cast<std::uint32_t>(source[0]) << 24U) |
           (static_cast<std::uint32_t>(source[1]) << 16U) |
           (static_cast<std::uint32_t>(source[2]) << 8U) |
           static_cast<std::uint32_t>(source[3]);
}

void BerWriter::write_tlv(const BerClass tag_class, const bool constructed,
                          const std::int32_t tag_number,
                          const std::span<const std::uint8_t> value) {
    write_identifier(tag_class, constructed, tag_number);
    write_length(value.size());
    write_bytes(value);
}

void BerWriter::write_tlv(const std::uint8_t encoded_tag,
                          const std::span<const std::uint8_t> value) {
    write_byte(encoded_tag);
    write_length(value.size());
    write_bytes(value);
}

void BerWriter::write_raw(const std::span<const std::uint8_t> bytes) {
    write_bytes(bytes);
}

std::vector<std::uint8_t> BerWriter::encode_tlv(
    const BerClass tag_class, const bool constructed, const std::int32_t tag_number,
    const std::span<const std::uint8_t> value) {
    BerWriter writer;
    writer.write_tlv(tag_class, constructed, tag_number, value);
    return writer.to_vector();
}

std::vector<std::uint8_t> BerWriter::encode_tlv(
    const std::uint8_t encoded_tag, const std::span<const std::uint8_t> value) {
    BerWriter writer;
    writer.write_tlv(encoded_tag, value);
    return writer.to_vector();
}

std::uint8_t BerWriter::encode_identifier(
    const BerClass tag_class, const bool constructed, const std::int32_t tag_number) {
    if (tag_number < 0 || tag_number > 30) {
        throw std::out_of_range("Use write_tlv/encode_tlv for high-tag-number BER identifiers.");
    }

    const auto identifier = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(tag_class)) << 6U) |
                            (constructed ? 0x20U : 0x00U) |
                            static_cast<std::uint32_t>(tag_number);
    return static_cast<std::uint8_t>(identifier);
}

std::vector<std::uint8_t> BerWriter::encode_ascii(const std::string& value) {
    return {value.begin(), value.end()};
}

std::vector<std::uint8_t> BerWriter::encode_boolean(const bool value) {
    return {value ? std::uint8_t{0x01} : std::uint8_t{0x00}};
}

std::vector<std::uint8_t> BerWriter::encode_unsigned_integer(const std::uint64_t value) {
    std::array<std::uint8_t, 8> buffer{};
    auto remaining = value;
    for (std::size_t index = buffer.size(); index-- > 0;) {
        buffer[index] = static_cast<std::uint8_t>(remaining & 0xFFU);
        remaining >>= 8U;
    }

    std::size_t first = 0;
    while (first < buffer.size() - 1U && buffer[first] == 0U) {
        ++first;
    }

    return {buffer.begin() + static_cast<std::ptrdiff_t>(first), buffer.end()};
}

std::vector<std::uint8_t> BerWriter::encode_signed_integer(const std::int64_t value) {
    const auto raw = std::bit_cast<std::uint64_t>(value);
    std::array<std::uint8_t, 8> buffer{};
    for (std::size_t index = 0; index < buffer.size(); ++index) {
        const auto shift = static_cast<unsigned>((buffer.size() - 1U - index) * 8U);
        buffer[index] = static_cast<std::uint8_t>((raw >> shift) & 0xFFU);
    }

    std::size_t first = 0;
    while (first < buffer.size() - 1U) {
        const auto current = buffer[first];
        const auto next = buffer[first + 1U];
        const bool redundant_positive = current == 0x00U && (next & 0x80U) == 0U;
        const bool redundant_negative = current == 0xFFU && (next & 0x80U) != 0U;
        if (!redundant_positive && !redundant_negative) {
            break;
        }
        ++first;
    }

    return {buffer.begin() + static_cast<std::ptrdiff_t>(first), buffer.end()};
}

std::vector<std::uint8_t> BerWriter::encode_single_precision_float(const float value) {
    std::vector<std::uint8_t> result(5U);
    result[0] = 0x08U;
    const auto raw = std::bit_cast<std::uint32_t>(value);
    std::span<std::uint8_t, 4> payload{result.data() + 1, 4U};
    write_u32_be(payload, raw);
    return result;
}

std::vector<std::uint8_t> BerWriter::encode_utc_time(
    const std::chrono::system_clock::time_point value, const std::uint8_t quality) {
    using seconds_type = std::chrono::seconds;
    const auto whole_seconds = std::chrono::floor<seconds_type>(value);
    auto seconds = whole_seconds.time_since_epoch().count();
    const auto fractional = value - whole_seconds;
    auto fraction = static_cast<std::uint32_t>(std::llround(
        std::chrono::duration<double>(fractional).count() * 16'777'216.0));

    if (fraction >= 16'777'216U) {
        ++seconds;
        fraction = 0;
    }
    if (seconds < 0 || static_cast<std::uint64_t>(seconds) > std::numeric_limits<std::uint32_t>::max()) {
        throw std::out_of_range("UTC time seconds are outside the IEC 61850 32-bit range.");
    }

    std::vector<std::uint8_t> result(8U);
    std::span<std::uint8_t, 4> seconds_span{result.data(), 4U};
    write_u32_be(seconds_span, static_cast<std::uint32_t>(seconds));
    result[4] = static_cast<std::uint8_t>((fraction >> 16U) & 0xFFU);
    result[5] = static_cast<std::uint8_t>((fraction >> 8U) & 0xFFU);
    result[6] = static_cast<std::uint8_t>(fraction & 0xFFU);
    result[7] = quality;
    return result;
}

void BerWriter::write_identifier(const BerClass tag_class, const bool constructed,
                                 const std::int32_t tag_number) {
    if (tag_number < 0) {
        throw std::out_of_range("BER tag number cannot be negative.");
    }

    if (tag_number <= 30) {
        write_byte(encode_identifier(tag_class, constructed, tag_number));
        return;
    }

    const auto high_tag_identifier =
        (static_cast<std::uint32_t>(static_cast<std::uint8_t>(tag_class)) << 6U) |
        (constructed ? 0x20U : 0x00U) | 0x1FU;
    write_byte(static_cast<std::uint8_t>(high_tag_identifier));

    std::array<std::uint8_t, 5> buffer{};
    std::size_t count = 0;
    auto remaining = static_cast<std::uint32_t>(tag_number);
    do {
        if (count >= buffer.size()) {
            throw std::out_of_range("BER tag number is too large.");
        }
        buffer[count++] = static_cast<std::uint8_t>(remaining & 0x7FU);
        remaining >>= 7U;
    } while (remaining > 0U);

    for (std::size_t index = count; index-- > 0;) {
        auto byte = buffer[index];
        if (index != 0U) {
            byte = static_cast<std::uint8_t>(byte | 0x80U);
        }
        write_byte(byte);
    }
}

void BerWriter::write_length(const std::size_t length) {
    if (length < 0x80U) {
        write_byte(static_cast<std::uint8_t>(length));
        return;
    }
    if (length > std::numeric_limits<std::uint32_t>::max()) {
        throw std::out_of_range("BER length exceeds the supported 32-bit range.");
    }

    std::array<std::uint8_t, 4> buffer{};
    const auto raw_length = static_cast<std::uint32_t>(length);
    std::span<std::uint8_t, 4> length_span{buffer};
    write_u32_be(length_span, raw_length);

    std::size_t first = 0;
    while (first < buffer.size() - 1U && buffer[first] == 0U) {
        ++first;
    }

    const auto count = buffer.size() - first;
    write_byte(static_cast<std::uint8_t>(0x80U | static_cast<std::uint8_t>(count)));
    write_bytes(std::span<const std::uint8_t>{buffer}.subspan(first));
}

void BerWriter::write_byte(const std::uint8_t value) {
    buffer_.push_back(value);
}

void BerWriter::write_bytes(const std::span<const std::uint8_t> value) {
    buffer_.insert(buffer_.end(), value.begin(), value.end());
}

} // namespace ar::iec61850::asn1
