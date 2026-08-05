// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/data_codec.hpp"

#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/mms/data_value.hpp"
#include "ariec61850/mms/utc_time.hpp"

#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ar::iec61850::mms {
namespace {

std::uint32_t read_u32_be(const std::span<const std::uint8_t> bytes) noexcept {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
           (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) |
           static_cast<std::uint32_t>(bytes[3]);
}

std::string bytes_to_hex(const std::span<const std::uint8_t> bytes) {
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setfill('0');
    for (const auto byte : bytes) {
        stream << std::setw(2) << static_cast<unsigned>(byte);
    }
    return stream.str();
}

std::string trim_float_text(std::string text) {
    const auto decimal = text.find('.');
    if (decimal == std::string::npos) {
        return text;
    }
    while (!text.empty() && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text;
}

} // namespace

std::vector<std::uint8_t> MmsDataCodec::encode(const MmsDataValue& value) {
    const auto content = encode_content(value);
    const bool constructed =
        value.kind() == MmsDataKind::array || value.kind() == MmsDataKind::structure;
    return asn1::BerWriter::encode_tlv(
        asn1::BerClass::context_specific, constructed, tag_for(value), content);
}

std::vector<std::uint8_t> MmsDataCodec::encode_all(
    const std::span<const MmsDataValue> values) {
    asn1::BerWriter writer;
    for (const auto& value : values) {
        writer.write_raw(encode(value));
    }
    return writer.to_vector();
}

MmsDataValue MmsDataCodec::decode(const asn1::BerTlv& tlv) {
    if (tlv.tag_class != asn1::BerClass::context_specific) {
        return MmsDataValue::unknown(tlv.tag_number, tlv.value);
    }

    switch (tlv.tag_number) {
    case 1:
        return MmsDataValue::array(decode_all(tlv.value));
    case 2:
        return MmsDataValue::structure(decode_all(tlv.value));
    case 3:
        return MmsDataValue::boolean(asn1::BerReader::read_boolean(tlv).value_or(false));
    case 4:
        return decode_bit_string(tlv.value);
    case 5:
        return MmsDataValue::integer(asn1::BerReader::read_signed_integer(tlv).value_or(0));
    case 6:
        return MmsDataValue::unsigned_integer(
            asn1::BerReader::read_unsigned_integer(tlv).value_or(0));
    case 7:
        return MmsDataValue::floating_point(decode_floating_point(tlv.value));
    case 9:
        return MmsDataValue::octet_string(tlv.value);
    case 10:
        return MmsDataValue::visible_string(asn1::BerReader::read_ascii_string(tlv));
    case 12:
        return MmsDataValue::binary_time(tlv.value);
    case 16:
        return MmsDataValue::mms_string(asn1::BerReader::read_ascii_string(tlv));
    case 17:
        return MmsDataValue::utc_time(Iec61850UtcTime::from_bytes(tlv.value));
    default:
        return MmsDataValue::unknown(tlv.tag_number, tlv.value);
    }
}

std::vector<MmsDataValue> MmsDataCodec::decode_all(
    const std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) {
        return {};
    }

    const auto children = asn1::BerReader::read_children(bytes);
    std::vector<MmsDataValue> values;
    values.reserve(children.size());
    for (const auto& child : children) {
        values.push_back(decode(child));
    }
    return values;
}

std::string MmsDataCodec::to_display_string(const MmsDataValue& value) {
    switch (value.kind()) {
    case MmsDataKind::boolean:
        return std::get<bool>(value.value()) ? "true" : "false";
    case MmsDataKind::integer:
        return std::to_string(std::get<std::int64_t>(value.value()));
    case MmsDataKind::unsigned_integer:
        return std::to_string(std::get<std::uint64_t>(value.value()));
    case MmsDataKind::floating_point: {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(3);
        if (const auto* single = std::get_if<float>(&value.value())) {
            stream << *single;
        } else if (const auto* double_value = std::get_if<double>(&value.value())) {
            stream << *double_value;
        }
        return trim_float_text(stream.str());
    }
    case MmsDataKind::visible_string:
    case MmsDataKind::mms_string:
        return std::get<std::string>(value.value());
    case MmsDataKind::utc_time: {
        const auto& utc = std::get<Iec61850UtcTime>(value.value());
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            utc.value.time_since_epoch());
        std::ostringstream stream;
        stream << "unix-ms=" << milliseconds.count()
               << " UTC (q=0x" << std::uppercase << std::hex << std::setw(2)
               << std::setfill('0') << static_cast<unsigned>(utc.quality) << ')';
        return stream.str();
    }
    case MmsDataKind::array:
    case MmsDataKind::structure: {
        std::ostringstream stream;
        stream << (value.kind() == MmsDataKind::array ? '[' : '{');
        for (std::size_t index = 0; index < value.children().size(); ++index) {
            if (index != 0U) {
                stream << ", ";
            }
            stream << to_display_string(value.children()[index]);
        }
        stream << (value.kind() == MmsDataKind::array ? ']' : '}');
        return stream.str();
    }
    default:
        return bytes_to_hex(value.raw_value());
    }
}

std::vector<std::uint8_t> MmsDataCodec::encode_content(const MmsDataValue& value) {
    switch (value.kind()) {
    case MmsDataKind::array:
    case MmsDataKind::structure:
        return encode_all(value.children());
    case MmsDataKind::boolean:
        return asn1::BerWriter::encode_boolean(std::get<bool>(value.value()));
    case MmsDataKind::bit_string:
    case MmsDataKind::octet_string:
    case MmsDataKind::binary_time:
    case MmsDataKind::unknown:
        return value.raw_value();
    case MmsDataKind::integer:
        return asn1::BerWriter::encode_signed_integer(
            std::get<std::int64_t>(value.value()));
    case MmsDataKind::unsigned_integer:
        return asn1::BerWriter::encode_unsigned_integer(
            std::get<std::uint64_t>(value.value()));
    case MmsDataKind::floating_point: {
        float result = 0.0F;
        if (const auto* single = std::get_if<float>(&value.value())) {
            result = *single;
        } else if (const auto* double_value = std::get_if<double>(&value.value())) {
            result = static_cast<float>(*double_value);
        } else {
            throw std::invalid_argument("MMS floating-point scalar is missing.");
        }
        return asn1::BerWriter::encode_single_precision_float(result);
    }
    case MmsDataKind::visible_string:
    case MmsDataKind::mms_string:
        return asn1::BerWriter::encode_ascii(std::get<std::string>(value.value()));
    case MmsDataKind::utc_time:
        return std::get<Iec61850UtcTime>(value.value()).to_bytes();
    default:
        throw std::logic_error("MMS data kind is not supported yet.");
    }
}

std::int32_t MmsDataCodec::tag_for(const MmsDataValue& value) {
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
    case MmsDataKind::bcd: return 13;
    case MmsDataKind::boolean_array: return 14;
    case MmsDataKind::object_id: return 15;
    case MmsDataKind::mms_string: return 16;
    case MmsDataKind::utc_time: return 17;
    case MmsDataKind::unknown:
        if (!value.unknown_tag_number().has_value()) {
            throw std::invalid_argument("Unknown MMS values require a tag number.");
        }
        return *value.unknown_tag_number();
    default:
        throw std::logic_error("MMS data kind is not supported yet.");
    }
}

MmsDataValue MmsDataCodec::decode_bit_string(
    const std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) {
        return MmsDataValue::bit_string(0U, {});
    }
    return MmsDataValue::bit_string(bytes[0], bytes.subspan(1U));
}

float MmsDataCodec::decode_floating_point(
    const std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() == 5U) {
        return std::bit_cast<float>(read_u32_be(bytes.subspan(1U, 4U)));
    }
    if (bytes.size() == 4U) {
        return std::bit_cast<float>(read_u32_be(bytes));
    }
    return std::numeric_limits<float>::quiet_NaN();
}

} // namespace ar::iec61850::mms
