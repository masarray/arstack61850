// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/sampled_values/pdu_codec.hpp"

#if !defined(ARIEC61850_NO_EXCEPTIONS)
#include "ariec61850/asn1/ber.hpp"
#endif
#include "ariec61850/mms/utc_time.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#if !defined(ARIEC61850_NO_EXCEPTIONS)
#include <stdexcept>
#include <utility>
#include <vector>
#endif

namespace ar::iec61850::sampled_values {
namespace {

constexpr std::uint8_t sav_pdu_application_tag = 0x60U;
constexpr std::uint8_t sequence_tag = 0x30U;
constexpr std::uint8_t context_primitive_base = 0x80U;
constexpr std::uint8_t context_constructed_base = 0xA0U;

[[nodiscard]] bool checked_add(
    std::size_t& total,
    const std::size_t value) noexcept {
    if (value > std::numeric_limits<std::size_t>::max() - total) {
        return false;
    }
    total += value;
    return true;
}

[[nodiscard]] std::optional<std::size_t> ber_length_octets(
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

[[nodiscard]] std::optional<std::size_t> tlv_size(
    const std::size_t value_length) noexcept {
    const auto length_size = ber_length_octets(value_length);
    if (!length_size) {
        return std::nullopt;
    }
    std::size_t total = 1U;
    if (!checked_add(total, *length_size) ||
        !checked_add(total, value_length)) {
        return std::nullopt;
    }
    return total;
}

[[nodiscard]] std::size_t unsigned_integer_size(std::uint64_t value) noexcept {
    std::size_t size = 1U;
    while (value > 0xFFU) {
        ++size;
        value >>= 8U;
    }
    return size;
}

[[nodiscard]] std::optional<std::size_t> context_bytes_size(
    const std::size_t value_length) noexcept {
    return tlv_size(value_length);
}

[[nodiscard]] std::optional<std::size_t> context_unsigned_size(
    const std::uint64_t value) noexcept {
    return tlv_size(unsigned_integer_size(value));
}

[[nodiscard]] std::optional<std::size_t> asdu_content_size(
    const SampledValueAsdu& asdu) noexcept {
    std::size_t total = 0U;
    const auto add_tlv = [&total](const std::optional<std::size_t> size) noexcept {
        return size.has_value() && checked_add(total, *size);
    };

    if (!add_tlv(context_bytes_size(asdu.sv_id.size()))) {
        return std::nullopt;
    }
    if (!asdu.data_set_reference.empty() &&
        !add_tlv(context_bytes_size(asdu.data_set_reference.size()))) {
        return std::nullopt;
    }
    if (!add_tlv(context_unsigned_size(asdu.sample_count)) ||
        !add_tlv(context_unsigned_size(asdu.configuration_revision))) {
        return std::nullopt;
    }
    if (asdu.reference_time.has_value()) {
        std::array<std::uint8_t, 8> timestamp{};
        if (!asdu.reference_time->try_write_bytes(timestamp) ||
            !add_tlv(context_bytes_size(timestamp.size()))) {
            return std::nullopt;
        }
    }
    if (!add_tlv(context_unsigned_size(asdu.sample_synchronization))) {
        return std::nullopt;
    }
    if (asdu.sample_rate.has_value() &&
        !add_tlv(context_unsigned_size(*asdu.sample_rate))) {
        return std::nullopt;
    }
    if (!add_tlv(context_bytes_size(asdu.sample_payload.size()))) {
        return std::nullopt;
    }
    if (asdu.sample_mode.has_value() &&
        !add_tlv(context_unsigned_size(*asdu.sample_mode))) {
        return std::nullopt;
    }
    return total;
}

[[nodiscard]] std::optional<std::size_t> asdu_encoded_size(
    const SampledValueAsdu& asdu) noexcept {
    const auto content = asdu_content_size(asdu);
    return content ? tlv_size(*content) : std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> sequence_value_size(
    const SampledValuesPdu& pdu) noexcept {
    std::size_t total = 0U;
    for (const auto& asdu : pdu.asdus) {
        const auto size = asdu_encoded_size(asdu);
        if (!size || !checked_add(total, *size)) {
            return std::nullopt;
        }
    }
    return total;
}

void write_length(
    const std::span<std::uint8_t> destination,
    std::size_t& offset,
    const std::size_t value_length) noexcept {
    if (value_length < 0x80U) {
        destination[offset++] = static_cast<std::uint8_t>(value_length);
        return;
    }

    auto value = static_cast<std::uint32_t>(value_length);
    std::array<std::uint8_t, 4> bytes{};
    std::size_t count = 0U;
    while (value != 0U) {
        bytes[bytes.size() - 1U - count] =
            static_cast<std::uint8_t>(value & 0xFFU);
        value >>= 8U;
        ++count;
    }
    destination[offset++] = static_cast<std::uint8_t>(0x80U | count);
    const auto first = bytes.size() - count;
    for (std::size_t index = first; index < bytes.size(); ++index) {
        destination[offset++] = bytes[index];
    }
}

void write_tlv_header(
    const std::span<std::uint8_t> destination,
    std::size_t& offset,
    const std::uint8_t tag,
    const std::size_t value_length) noexcept {
    destination[offset++] = tag;
    write_length(destination, offset, value_length);
}

void write_bytes(
    const std::span<std::uint8_t> destination,
    std::size_t& offset,
    const std::span<const std::uint8_t> value) noexcept {
    std::copy(value.begin(), value.end(), destination.begin() +
        static_cast<std::ptrdiff_t>(offset));
    offset += value.size();
}

void write_string(
    const std::span<std::uint8_t> destination,
    std::size_t& offset,
    const std::uint8_t tag_number,
    const std::string& value) noexcept {
    write_tlv_header(
        destination, offset,
        static_cast<std::uint8_t>(context_primitive_base | tag_number),
        value.size());
    for (const char character : value) {
        destination[offset++] = static_cast<std::uint8_t>(character);
    }
}

void write_unsigned(
    const std::span<std::uint8_t> destination,
    std::size_t& offset,
    const std::uint8_t tag_number,
    const std::uint64_t value) noexcept {
    const auto value_size = unsigned_integer_size(value);
    write_tlv_header(
        destination, offset,
        static_cast<std::uint8_t>(context_primitive_base | tag_number),
        value_size);

    for (std::size_t index = value_size; index-- > 0U;) {
        const auto shift = static_cast<unsigned>(index * 8U);
        destination[offset++] = static_cast<std::uint8_t>((value >> shift) & 0xFFU);
    }
}

[[nodiscard]] bool write_asdu(
    const SampledValueAsdu& asdu,
    const std::span<std::uint8_t> destination,
    std::size_t& offset) noexcept {
    const auto content_size = asdu_content_size(asdu);
    if (!content_size) {
        return false;
    }
    write_tlv_header(destination, offset, sequence_tag, *content_size);

    write_string(destination, offset, 0U, asdu.sv_id);
    if (!asdu.data_set_reference.empty()) {
        write_string(destination, offset, 1U, asdu.data_set_reference);
    }
    write_unsigned(destination, offset, 2U, asdu.sample_count);
    write_unsigned(destination, offset, 3U, asdu.configuration_revision);

    if (asdu.reference_time.has_value()) {
        std::array<std::uint8_t, 8> timestamp{};
        if (!asdu.reference_time->try_write_bytes(timestamp)) {
            return false;
        }
        write_tlv_header(
            destination, offset,
            static_cast<std::uint8_t>(context_primitive_base | 4U),
            timestamp.size());
        write_bytes(destination, offset, timestamp);
    }

    write_unsigned(destination, offset, 5U, asdu.sample_synchronization);
    if (asdu.sample_rate.has_value()) {
        write_unsigned(destination, offset, 6U, *asdu.sample_rate);
    }

    write_tlv_header(
        destination, offset,
        static_cast<std::uint8_t>(context_primitive_base | 7U),
        asdu.sample_payload.size());
    write_bytes(destination, offset, asdu.sample_payload);

    if (asdu.sample_mode.has_value()) {
        write_unsigned(destination, offset, 8U, *asdu.sample_mode);
    }
    return true;
}

#if !defined(ARIEC61850_NO_EXCEPTIONS)
bool read_unsigned_exact(
    const asn1::BerTlv& field,
    const std::uint64_t maximum,
    std::uint64_t& value) noexcept {
    const auto decoded = asn1::BerReader::read_unsigned_integer(field);
    if (!decoded.has_value() || *decoded > maximum) {
        return false;
    }
    value = *decoded;
    return true;
}

bool read_asdu(
    const std::span<const std::uint8_t> asdu_value,
    SampledValueAsdu& asdu) {
    asdu = {};
    asdu.configuration_revision = 0U;
    asdu.sample_synchronization = 0U;

    for (const auto& field : asn1::BerReader::read_children(asdu_value)) {
        if (field.tag_class != asn1::BerClass::context_specific || field.constructed) {
            continue;
        }

        std::uint64_t integer = 0U;
        switch (field.tag_number) {
        case 0:
            asdu.sv_id = asn1::BerReader::read_ascii_string(field);
            break;
        case 1:
            asdu.data_set_reference = asn1::BerReader::read_ascii_string(field);
            break;
        case 2:
            if (!read_unsigned_exact(field, std::numeric_limits<std::uint16_t>::max(), integer)) {
                return false;
            }
            asdu.sample_count = static_cast<std::uint16_t>(integer);
            break;
        case 3:
            if (!read_unsigned_exact(field, std::numeric_limits<std::uint32_t>::max(), integer)) {
                return false;
            }
            asdu.configuration_revision = static_cast<std::uint32_t>(integer);
            break;
        case 4:
            asdu.reference_time = mms::Iec61850UtcTime::from_bytes(field.value);
            break;
        case 5:
            if (!read_unsigned_exact(field, std::numeric_limits<std::uint8_t>::max(), integer)) {
                return false;
            }
            asdu.sample_synchronization = static_cast<std::uint8_t>(integer);
            break;
        case 6:
            if (!read_unsigned_exact(field, std::numeric_limits<std::uint16_t>::max(), integer)) {
                return false;
            }
            asdu.sample_rate = static_cast<std::uint16_t>(integer);
            break;
        case 7:
            asdu.sample_payload = field.value;
            break;
        case 8:
            if (!read_unsigned_exact(field, std::numeric_limits<std::uint16_t>::max(), integer)) {
                return false;
            }
            asdu.sample_mode = static_cast<std::uint16_t>(integer);
            break;
        default:
            break;
        }
    }

    return true;
}

bool read_asdu_sequence(
    const std::span<const std::uint8_t> sequence_value,
    std::vector<SampledValueAsdu>& asdus) {
    for (const auto& child : asn1::BerReader::read_children(sequence_value)) {
        if (child.tag_class != asn1::BerClass::universal ||
            child.tag_number != 16 ||
            !child.constructed) {
            return false;
        }

        SampledValueAsdu asdu;
        if (!read_asdu(child.value, asdu)) {
            return false;
        }
        asdus.push_back(std::move(asdu));
    }
    return true;
}
#endif

} // namespace

std::optional<std::size_t> SampledValuesPduCodec::encoded_size(
    const SampledValuesPdu& pdu) noexcept {
    if (pdu.asdus.size() > std::numeric_limits<std::uint16_t>::max()) {
        return std::nullopt;
    }

    const auto sequence_size = sequence_value_size(pdu);
    if (!sequence_size) {
        return std::nullopt;
    }

    const auto count_field_size = context_unsigned_size(
        static_cast<std::uint64_t>(pdu.asdus.size()));
    const auto sequence_field_size = tlv_size(*sequence_size);
    if (!count_field_size || !sequence_field_size) {
        return std::nullopt;
    }

    std::size_t content_size = 0U;
    if (!checked_add(content_size, *count_field_size) ||
        !checked_add(content_size, *sequence_field_size)) {
        return std::nullopt;
    }
    return tlv_size(content_size);
}

wire::EncodeResult SampledValuesPduCodec::encode_into(
    const SampledValuesPdu& pdu,
    const std::span<std::uint8_t> destination) noexcept {
    const auto required = encoded_size(pdu);
    if (!required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (destination.size() < *required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, *required};
    }

    const auto sequence_size = sequence_value_size(pdu);
    const auto count_field_size = context_unsigned_size(
        static_cast<std::uint64_t>(pdu.asdus.size()));
    const auto sequence_field_size = sequence_size ? tlv_size(*sequence_size) : std::nullopt;
    if (!sequence_size || !count_field_size || !sequence_field_size) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    const auto content_size = *count_field_size + *sequence_field_size;
    std::size_t offset = 0U;
    write_tlv_header(destination, offset, sav_pdu_application_tag, content_size);
    write_unsigned(
        destination, offset, 0U,
        static_cast<std::uint64_t>(pdu.asdus.size()));
    write_tlv_header(
        destination, offset,
        static_cast<std::uint8_t>(context_constructed_base | 2U),
        *sequence_size);

    for (const auto& asdu : pdu.asdus) {
        if (!write_asdu(asdu, destination, offset)) {
            return {wire::EncodeStatus::value_out_of_range, 0U, *required};
        }
    }

    return {wire::EncodeStatus::ok, offset, *required};
}

#if !defined(ARIEC61850_NO_EXCEPTIONS)
std::vector<std::uint8_t> SampledValuesPduCodec::encode(
    const SampledValuesPdu& pdu) {
    const auto required = encoded_size(pdu);
    if (!required) {
        throw std::out_of_range("A Sampled Values PDU exceeds the supported BER wire range.");
    }

    std::vector<std::uint8_t> bytes(*required);
    const auto result = encode_into(pdu, bytes);
    if (!result.success() || result.bytes_written != bytes.size()) {
        throw std::runtime_error("Failed to encode Sampled Values PDU into sized buffer.");
    }
    return bytes;
}

bool SampledValuesPduCodec::try_decode(
    const std::span<const std::uint8_t> apdu,
    SampledValuesPdu& pdu) noexcept {
    pdu = {};

    try {
        std::size_t offset = 0U;
        asn1::BerTlv outer;
        if (!asn1::BerReader::try_read_tlv(apdu, offset, outer) ||
            offset != apdu.size() ||
            outer.tag_class != asn1::BerClass::application ||
            outer.tag_number != 0 ||
            !outer.constructed) {
            return false;
        }

        std::optional<std::uint16_t> declared_asdu_count;
        std::vector<SampledValueAsdu> asdus;

        for (const auto& field : asn1::BerReader::read_children(outer.value)) {
            if (field.tag_class != asn1::BerClass::context_specific) {
                continue;
            }

            if (field.tag_number == 0 && !field.constructed) {
                std::uint64_t count = 0U;
                if (!read_unsigned_exact(
                        field,
                        std::numeric_limits<std::uint16_t>::max(),
                        count)) {
                    return false;
                }
                declared_asdu_count = static_cast<std::uint16_t>(count);
            } else if (field.tag_number == 2 && field.constructed) {
                if (!read_asdu_sequence(field.value, asdus)) {
                    return false;
                }
            }
        }

        if (declared_asdu_count.has_value() &&
            asdus.size() != static_cast<std::size_t>(*declared_asdu_count)) {
            return false;
        }

        pdu.asdus = std::move(asdus);
        return true;
    } catch (...) {
        pdu = {};
        return false;
    }
}
#endif

} // namespace ar::iec61850::sampled_values
