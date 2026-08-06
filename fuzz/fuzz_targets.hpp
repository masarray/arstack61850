// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/capture/pcap.hpp"
#include "ariec61850/comtrade/reader.hpp"
#include "ariec61850/evidence/pcap_equivalence.hpp"
#include "ariec61850/goose/frame_codec.hpp"
#include "ariec61850/goose/pdu_codec.hpp"
#include "ariec61850/sampled_values/frame_codec.hpp"
#include "ariec61850/sampled_values/pdu_codec.hpp"
#include "ariec61850/scl/parser.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <sstream>
#include <string>

namespace ar::iec61850::fuzzing {

inline void exercise_ber(const std::span<const std::uint8_t> bytes) {
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const auto previous = offset;
        asn1::BerTlv tlv;
        if (!asn1::BerReader::try_read_tlv(bytes, offset, tlv)) {
            break;
        }
        static_cast<void>(asn1::BerReader::read_boolean(tlv));
        static_cast<void>(asn1::BerReader::read_signed_integer(tlv));
        static_cast<void>(asn1::BerReader::read_unsigned_integer(tlv));
        if (tlv.constructed) {
            try {
                static_cast<void>(asn1::BerReader::read_children(tlv.value));
            } catch (...) {
            }
        }
        if (offset <= previous) {
            break;
        }
    }
    try {
        static_cast<void>(asn1::BerReader::read_children(bytes));
    } catch (...) {
    }
}

inline void exercise_goose(const std::span<const std::uint8_t> bytes) {
    goose::GooseFrame frame;
    if (goose::GooseFrameCodec::try_decode(bytes, frame)) {
        const auto encoded = goose::GooseFrameCodec::encode(frame);
        goose::GooseFrame second;
        static_cast<void>(goose::GooseFrameCodec::try_decode(encoded, second));
    }
    goose::GoosePdu pdu;
    if (goose::GoosePduCodec::try_decode(bytes, pdu)) {
        const auto encoded = goose::GoosePduCodec::encode(pdu);
        goose::GoosePdu second;
        static_cast<void>(goose::GoosePduCodec::try_decode(encoded, second));
    }
}

inline void exercise_sampled_values(const std::span<const std::uint8_t> bytes) {
    sampled_values::SampledValuesFrame frame;
    if (sampled_values::SampledValuesFrameCodec::try_decode(bytes, frame)) {
        const auto encoded = sampled_values::SampledValuesFrameCodec::encode(frame);
        sampled_values::SampledValuesFrame second;
        static_cast<void>(sampled_values::SampledValuesFrameCodec::try_decode(encoded, second));
    }
    sampled_values::SampledValuesPdu pdu;
    if (sampled_values::SampledValuesPduCodec::try_decode(bytes, pdu)) {
        const auto encoded = sampled_values::SampledValuesPduCodec::encode(pdu);
        sampled_values::SampledValuesPdu second;
        static_cast<void>(sampled_values::SampledValuesPduCodec::try_decode(encoded, second));
    }
}

inline void exercise_pcap(const std::span<const std::uint8_t> bytes) {
    std::string input;
    if (!bytes.empty()) {
        input.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    std::istringstream stream{input, std::ios::in | std::ios::binary};
    try {
        const auto packets = capture::PcapReader::read_all(stream);
        const auto report = evidence::PcapEquivalenceAnalyzer::analyze(packets);
        static_cast<void>(evidence::PcapEquivalenceAnalyzer::to_json(report));
    } catch (...) {
    }
}

inline void exercise_scl(const std::span<const std::uint8_t> bytes) {
    std::string input;
    if (!bytes.empty()) {
        input.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    try {
        const auto document = scl::SclParser{}.parse(input, "fuzz-input.scl");
        static_cast<void>(document.ieds.size());
        static_cast<void>(document.data_sets.size());
        static_cast<void>(document.goose_streams.size());
        static_cast<void>(document.sampled_values_streams.size());
        static_cast<void>(document.report_controls.size());
    } catch (...) {
    }
}

inline void exercise_comtrade(const std::span<const std::uint8_t> bytes) {
    std::string input;
    if (!bytes.empty()) {
        input.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    try {
        const auto configuration = comtrade::Reader{}.parse_configuration(
            input, "fuzz-input.cfg");
        if (configuration.data_file_type == comtrade::DataFileType::ascii) {
            static_cast<void>(comtrade::Reader{}.read_ascii_data(input, configuration));
        } else if (configuration.data_file_type != comtrade::DataFileType::unknown) {
            static_cast<void>(comtrade::Reader{}.read_binary_data(bytes, configuration));
        }
    } catch (...) {
    }
}

} // namespace ar::iec61850::fuzzing
