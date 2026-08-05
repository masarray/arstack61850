// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/goose/pdu_codec.hpp"

#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/mms/utc_time.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ar::iec61850::goose {
namespace {

constexpr std::int32_t goose_pdu_application_tag = 1;

void write_context_primitive(
    asn1::BerWriter& writer,
    const std::int32_t tag_number,
    const std::span<const std::uint8_t> value) {
    writer.write_tlv(asn1::BerClass::context_specific, false, tag_number, value);
}

} // namespace

std::vector<std::uint8_t> GoosePduCodec::encode(const GoosePdu& pdu) {
    asn1::BerWriter content;

    const auto go_cb_ref = asn1::BerWriter::encode_ascii(pdu.go_cb_ref);
    write_context_primitive(content, 0, go_cb_ref);

    const auto time_allowed = asn1::BerWriter::encode_unsigned_integer(
        pdu.time_allowed_to_live_milliseconds);
    write_context_primitive(content, 1, time_allowed);

    const auto data_set = asn1::BerWriter::encode_ascii(pdu.data_set_reference);
    write_context_primitive(content, 2, data_set);

    const auto go_id = asn1::BerWriter::encode_ascii(pdu.go_id);
    write_context_primitive(content, 3, go_id);

    const auto timestamp = pdu.timestamp.to_bytes();
    write_context_primitive(content, 4, timestamp);

    const auto state_number = asn1::BerWriter::encode_unsigned_integer(pdu.state_number);
    write_context_primitive(content, 5, state_number);

    const auto sequence_number = asn1::BerWriter::encode_unsigned_integer(pdu.sequence_number);
    write_context_primitive(content, 6, sequence_number);

    const auto test = asn1::BerWriter::encode_boolean(pdu.test);
    write_context_primitive(content, 7, test);

    const auto configuration_revision = asn1::BerWriter::encode_unsigned_integer(
        pdu.configuration_revision);
    write_context_primitive(content, 8, configuration_revision);

    const auto needs_commissioning = asn1::BerWriter::encode_boolean(
        pdu.needs_commissioning);
    write_context_primitive(content, 9, needs_commissioning);

    const auto entry_count = asn1::BerWriter::encode_unsigned_integer(
        static_cast<std::uint64_t>(pdu.values.size()));
    write_context_primitive(content, 10, entry_count);

    const auto all_data = mms::MmsDataCodec::encode_all(pdu.values);
    content.write_tlv(asn1::BerClass::context_specific, true, 11, all_data);

    return asn1::BerWriter::encode_tlv(
        asn1::BerClass::application,
        true,
        goose_pdu_application_tag,
        content.bytes());
}

bool GoosePduCodec::try_decode(
    const std::span<const std::uint8_t> apdu, GoosePdu& pdu) noexcept {
    pdu = {};

    try {
        std::size_t offset = 0U;
        asn1::BerTlv outer;
        if (!asn1::BerReader::try_read_tlv(apdu, offset, outer) ||
            outer.tag_class != asn1::BerClass::application ||
            outer.tag_number != goose_pdu_application_tag ||
            !outer.constructed) {
            return false;
        }

        std::string go_cb_ref;
        std::uint32_t time_allowed_to_live = 0U;
        std::string data_set;
        std::string go_id;
        mms::Iec61850UtcTime timestamp{};
        std::uint32_t state_number = 0U;
        std::uint32_t sequence_number = 0U;
        bool test = false;
        std::uint32_t configuration_revision = 0U;
        bool needs_commissioning = false;
        std::uint32_t num_data_set_entries = 0U;
        std::vector<mms::MmsDataValue> values;

        for (const auto& field : asn1::BerReader::read_children(outer.value)) {
            if (field.tag_class != asn1::BerClass::context_specific) {
                continue;
            }

            switch (field.tag_number) {
            case 0:
                go_cb_ref = asn1::BerReader::read_ascii_string(field);
                break;
            case 1:
                time_allowed_to_live = asn1::BerReader::read_uint32(field).value_or(0U);
                break;
            case 2:
                data_set = asn1::BerReader::read_ascii_string(field);
                break;
            case 3:
                go_id = asn1::BerReader::read_ascii_string(field);
                break;
            case 4:
                timestamp = mms::Iec61850UtcTime::from_bytes(field.value);
                break;
            case 5:
                state_number = asn1::BerReader::read_uint32(field).value_or(0U);
                break;
            case 6:
                sequence_number = asn1::BerReader::read_uint32(field).value_or(0U);
                break;
            case 7:
                test = asn1::BerReader::read_boolean(field).value_or(false);
                break;
            case 8:
                configuration_revision = asn1::BerReader::read_uint32(field).value_or(0U);
                break;
            case 9:
                needs_commissioning = asn1::BerReader::read_boolean(field).value_or(false);
                break;
            case 10:
                num_data_set_entries = asn1::BerReader::read_uint32(field).value_or(0U);
                break;
            case 11:
                values = mms::MmsDataCodec::decode_all(field.value);
                break;
            default:
                break;
            }
        }

        if (num_data_set_entries != 0U &&
            values.size() != static_cast<std::size_t>(num_data_set_entries)) {
            return false;
        }

        pdu.go_cb_ref = std::move(go_cb_ref);
        pdu.time_allowed_to_live_milliseconds = time_allowed_to_live;
        pdu.data_set_reference = std::move(data_set);
        pdu.go_id = std::move(go_id);
        pdu.timestamp = timestamp;
        pdu.state_number = state_number;
        pdu.sequence_number = sequence_number;
        pdu.test = test;
        pdu.configuration_revision = configuration_revision;
        pdu.needs_commissioning = needs_commissioning;
        pdu.values = std::move(values);
        return true;
    } catch (...) {
        pdu = {};
        return false;
    }
}

} // namespace ar::iec61850::goose
