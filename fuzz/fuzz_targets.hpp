// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/acse/association.hpp"
#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/capture/pcap.hpp"
#include "ariec61850/comtrade/reader.hpp"
#include "ariec61850/evidence/pcap_equivalence.hpp"
#include "ariec61850/goose/frame_codec.hpp"
#include "ariec61850/goose/pdu_codec.hpp"
#include "ariec61850/osi/cotp.hpp"
#include "ariec61850/osi/presentation.hpp"
#include "ariec61850/osi/session.hpp"
#include "ariec61850/osi/tpkt.hpp"
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

inline void exercise_transport(const std::span<const std::uint8_t> bytes) {
    try {
        osi::TpktFrame frame;
        std::string error;
        if (osi::TpktFrameCodec::try_decode(bytes, frame, &error)) {
            const auto encoded = osi::TpktFrameCodec::encode(frame.payload);
            osi::TpktFrame second;
            static_cast<void>(osi::TpktFrameCodec::try_decode(encoded, second, nullptr));

            osi::CotpTpdu nested;
            if (osi::CotpFrameCodec::try_decode(frame.payload, nested, nullptr) &&
                nested.kind == osi::CotpTpduKind::data) {
                const auto data = osi::CotpFrameCodec::encode_data(
                    nested.user_data,
                    nested.end_of_transmission,
                    nested.tpdu_number);
                osi::CotpTpdu decoded;
                static_cast<void>(osi::CotpFrameCodec::try_decode(data, decoded, nullptr));
            }
        }
    } catch (...) {
    }

    try {
        osi::CotpTpdu tpdu;
        if (osi::CotpFrameCodec::try_decode(bytes, tpdu, nullptr)) {
            switch (tpdu.kind) {
            case osi::CotpTpduKind::connection_request:
                static_cast<void>(osi::CotpFrameCodec::encode_connection_request(
                    tpdu.source_reference,
                    tpdu.parameters,
                    tpdu.destination_reference,
                    tpdu.class_or_reason));
                break;
            case osi::CotpTpduKind::connection_confirm:
                static_cast<void>(osi::CotpFrameCodec::encode_connection_confirm(
                    tpdu.destination_reference,
                    tpdu.source_reference));
                break;
            case osi::CotpTpduKind::data: {
                const auto encoded = osi::CotpFrameCodec::encode_data(
                    tpdu.user_data,
                    tpdu.end_of_transmission,
                    tpdu.tpdu_number);
                osi::CotpDataReassembler reassembler{1U << 20U, 4096U, 32U};
                reassembler.append_encoded(encoded);
                if (reassembler.is_complete()) {
                    static_cast<void>(reassembler.complete());
                }
                break;
            }
            case osi::CotpTpduKind::disconnect_request:
                static_cast<void>(osi::CotpFrameCodec::encode_disconnect_request(
                    tpdu.destination_reference,
                    tpdu.source_reference,
                    tpdu.class_or_reason,
                    tpdu.parameters));
                break;
            case osi::CotpTpduKind::error:
            case osi::CotpTpduKind::unknown:
                break;
            }
        }
    } catch (...) {
    }

    try {
        osi::TpktStreamDecoder decoder{1U << 20U};
        const auto midpoint = bytes.size() / 2U;
        decoder.append(bytes.first(midpoint));
        decoder.append(bytes.subspan(midpoint));
        osi::TpktFrame frame;
        while (decoder.try_pop(frame)) {
            static_cast<void>(frame.payload.size());
        }
    } catch (...) {
    }
}


inline void exercise_association(const std::span<const std::uint8_t> bytes) {
    try {
        osi::SessionSpdu spdu;
        std::size_t consumed = 0U;
        if (osi::SessionCodec::try_decode_prefix(bytes, spdu, consumed, nullptr)) {
            if ((spdu.kind == osi::SessionSpduKind::connect ||
                 spdu.kind == osi::SessionSpduKind::accept) &&
                !spdu.user_data.empty()) {
                if (spdu.kind == osi::SessionSpduKind::connect) {
                    osi::PresentationCp cp;
                    if (osi::PresentationCodec::try_decode_cp(spdu.user_data, cp, nullptr)) {
                        acse::AcseAarq aarq;
                        if (acse::AcseAssociationCodec::try_decode_aarq(
                                cp.user_data.single_asn1_type, aarq, nullptr)) {
                            static_cast<void>(acse::AcseAssociationCodec::encode_aarq(aarq));
                        }
                    }
                } else {
                    osi::PresentationCpa cpa;
                    if (osi::PresentationCodec::try_decode_cpa(spdu.user_data, cpa, nullptr)) {
                        acse::AcseAare aare;
                        if (acse::AcseAssociationCodec::try_decode_aare(
                                cpa.user_data.single_asn1_type, aare, nullptr)) {
                            static_cast<void>(acse::AcseAssociationCodec::encode_aare(aare));
                        }
                    }
                }
            }
        }
    } catch (...) {
    }

    try {
        acse::AssociationRequestEnvelope request;
        if (acse::AcseAssociationCodec::try_decode_association_request(
                bytes, request, nullptr)) {
            const auto response = acse::AcseAssociationCodec::build_accept_response(request);
            acse::AssociationResponseEnvelope decoded;
            static_cast<void>(acse::AcseAssociationCodec::try_decode_association_response(
                response.payload, decoded, nullptr));
        }
    } catch (...) {
    }

    try {
        acse::AssociationResponseEnvelope response;
        static_cast<void>(acse::AcseAssociationCodec::try_decode_association_response(
            bytes, response, nullptr));
    } catch (...) {
    }

    try {
        osi::PresentationPdv pdv;
        if (osi::PresentationCodec::try_decode_p_data(bytes, pdv, nullptr)) {
            static_cast<void>(osi::PresentationCodec::encode_p_data(
                pdv.single_asn1_type, pdv.context_id));
        }
    } catch (...) {
    }
}

} // namespace ar::iec61850::fuzzing
