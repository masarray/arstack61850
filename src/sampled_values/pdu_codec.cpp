// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/sampled_values/pdu_codec.hpp"

#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/mms/utc_time.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ar::iec61850::sampled_values {
namespace {

constexpr std::int32_t sav_pdu_application_tag = 0;
constexpr std::int32_t sequence_tag_number = 16;

void write_context_primitive(
    asn1::BerWriter& writer,
    const std::int32_t tag_number,
    const std::span<const std::uint8_t> value) {
    writer.write_tlv(asn1::BerClass::context_specific, false, tag_number, value);
}

std::vector<std::uint8_t> encode_asdu(const SampledValueAsdu& asdu) {
    asn1::BerWriter writer;

    const auto sv_id = asn1::BerWriter::encode_ascii(asdu.sv_id);
    write_context_primitive(writer, 0, sv_id);

    if (!asdu.data_set_reference.empty()) {
        const auto data_set = asn1::BerWriter::encode_ascii(asdu.data_set_reference);
        write_context_primitive(writer, 1, data_set);
    }

    const auto sample_count = asn1::BerWriter::encode_unsigned_integer(asdu.sample_count);
    write_context_primitive(writer, 2, sample_count);

    const auto configuration_revision =
        asn1::BerWriter::encode_unsigned_integer(asdu.configuration_revision);
    write_context_primitive(writer, 3, configuration_revision);

    if (asdu.reference_time.has_value()) {
        const auto reference_time = asdu.reference_time->to_bytes();
        write_context_primitive(writer, 4, reference_time);
    }

    const auto sample_synchronization =
        asn1::BerWriter::encode_unsigned_integer(asdu.sample_synchronization);
    write_context_primitive(writer, 5, sample_synchronization);

    if (asdu.sample_rate.has_value()) {
        const auto sample_rate = asn1::BerWriter::encode_unsigned_integer(*asdu.sample_rate);
        write_context_primitive(writer, 6, sample_rate);
    }

    write_context_primitive(writer, 7, asdu.sample_payload);

    if (asdu.sample_mode.has_value()) {
        const auto sample_mode = asn1::BerWriter::encode_unsigned_integer(*asdu.sample_mode);
        write_context_primitive(writer, 8, sample_mode);
    }

    return writer.to_vector();
}

std::vector<std::uint8_t> encode_asdu_sequence(
    const std::span<const SampledValueAsdu> asdus) {
    asn1::BerWriter writer;
    for (const auto& asdu : asdus) {
        const auto encoded = encode_asdu(asdu);
        writer.write_tlv(asn1::BerClass::universal, true, sequence_tag_number, encoded);
    }
    return writer.to_vector();
}

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
            child.tag_number != sequence_tag_number ||
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

} // namespace

std::vector<std::uint8_t> SampledValuesPduCodec::encode(const SampledValuesPdu& pdu) {
    if (pdu.asdus.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw std::out_of_range("A Sampled Values PDU contains too many ASDUs.");
    }

    asn1::BerWriter content;
    const auto no_asdu = asn1::BerWriter::encode_unsigned_integer(
        static_cast<std::uint64_t>(pdu.asdus.size()));
    write_context_primitive(content, 0, no_asdu);

    const auto sequence = encode_asdu_sequence(pdu.asdus);
    content.write_tlv(asn1::BerClass::context_specific, true, 2, sequence);

    return asn1::BerWriter::encode_tlv(
        asn1::BerClass::application,
        true,
        sav_pdu_application_tag,
        content.bytes());
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
            outer.tag_number != sav_pdu_application_tag ||
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

} // namespace ar::iec61850::sampled_values
