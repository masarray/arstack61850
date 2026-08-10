// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/acse/association.hpp"
#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/capture/pcap.hpp"
#include "ariec61850/comtrade/reader.hpp"
#include "ariec61850/evidence/pcap_equivalence.hpp"
#include "ariec61850/goose/frame_codec.hpp"
#include "ariec61850/goose/pdu_codec.hpp"
#include "ariec61850/mms/file_service.hpp"
#include "ariec61850/mms/invoke_router.hpp"
#include "ariec61850/mms/pdu.hpp"
#include "ariec61850/mms/reporting.hpp"
#include "ariec61850/mms/services.hpp"
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


inline void exercise_mms_services(const std::span<const std::uint8_t> bytes) {
    try {
        mms::MmsPduEnvelope envelope;
        if (mms::MmsPduCodec::try_decode_envelope(bytes, envelope, nullptr)) {
            switch (envelope.kind) {
            case mms::MmsPduKind::initiate_request: {
                const auto request = mms::MmsPduCodec::decode_initiate_request(
                    envelope.mms_payload);
                static_cast<void>(mms::MmsPduCodec::encode_initiate_request(request));
                break;
            }
            case mms::MmsPduKind::initiate_response: {
                const auto response = mms::MmsPduCodec::decode_initiate_response(
                    envelope.mms_payload);
                static_cast<void>(mms::MmsPduCodec::encode_initiate_response(response));
                break;
            }
            case mms::MmsPduKind::confirmed_request:
                if (envelope.service_tag == 1) {
                    const auto request = mms::MmsServiceCodec::decode_get_name_list_request(bytes);
                    static_cast<void>(mms::MmsServiceCodec::encode_get_name_list_request_pdu(request));
                } else if (envelope.service_tag == 4) {
                    const auto request = mms::MmsServiceCodec::decode_read_request(bytes);
                    static_cast<void>(mms::MmsServiceCodec::encode_read_request_pdu(request));
                } else if (envelope.service_tag == 5) {
                    const auto request = mms::MmsServiceCodec::decode_write_request(bytes);
                    static_cast<void>(mms::MmsServiceCodec::encode_write_request_pdu(request));
                } else if (envelope.service_tag == 6) {
                    const auto request =
                        mms::MmsServiceCodec::decode_variable_access_attributes_request(bytes);
                    static_cast<void>(
                        mms::MmsServiceCodec::encode_variable_access_attributes_request_pdu(request));
                }
                break;
            case mms::MmsPduKind::confirmed_response:
                if (envelope.service_tag == 1) {
                    const auto response = mms::MmsServiceCodec::decode_get_name_list_response(bytes);
                    static_cast<void>(mms::MmsServiceCodec::encode_get_name_list_response_pdu(response));
                } else if (envelope.service_tag == 4) {
                    const auto response = mms::MmsServiceCodec::decode_read_response(bytes);
                    static_cast<void>(mms::MmsServiceCodec::encode_read_response_pdu(response));
                } else if (envelope.service_tag == 5) {
                    const auto response = mms::MmsServiceCodec::decode_write_response(bytes);
                    static_cast<void>(mms::MmsServiceCodec::encode_write_response_pdu(response));
                } else if (envelope.service_tag == 6) {
                    const auto response =
                        mms::MmsServiceCodec::decode_variable_access_attributes_response(bytes);
                    static_cast<void>(
                        mms::MmsServiceCodec::encode_variable_access_attributes_response_pdu(response));
                }
                break;
            case mms::MmsPduKind::confirmed_error: {
                const auto error = mms::MmsPduCodec::decode_confirmed_error(
                    envelope.mms_payload);
                static_cast<void>(mms::MmsPduCodec::encode_confirmed_error(error));
                break;
            }
            default:
                break;
            }

            mms::MmsInvokeRouter router{16U, 4U};
            static_cast<void>(router.route(bytes));
        }
    } catch (...) {
    }

    try {
        const auto type = mms::MmsServiceCodec::decode_type_specification(bytes);
        static_cast<void>(mms::MmsServiceCodec::encode_type_specification(type));
    } catch (...) {
    }
}

inline void exercise_mms_file_service(const std::span<const std::uint8_t> bytes) {
    std::uint32_t invoke_id = 1U;
    try {
        const auto envelope = mms::MmsPduCodec::decode_envelope(bytes);
        if (envelope.invoke_id && *envelope.invoke_id != 0U) {
            invoke_id = *envelope.invoke_id;
        }
    } catch (...) {
    }
    try {
        static_cast<void>(mms::MmsFileServiceCodec::decode_file_directory_response(
            bytes, invoke_id));
    } catch (...) {
    }
    try {
        static_cast<void>(mms::MmsFileServiceCodec::decode_file_open_response(
            bytes, invoke_id));
    } catch (...) {
    }
    try {
        static_cast<void>(mms::MmsFileServiceCodec::decode_file_read_response(
            bytes, invoke_id));
    } catch (...) {
    }
    try {
        static_cast<void>(mms::MmsFileServiceCodec::decode_file_close_response(
            bytes, invoke_id));
    } catch (...) {
    }
}


inline void exercise_reporting(const std::span<const std::uint8_t> bytes) {
    try {
        mms::MmsInformationReport report;
        if (mms::MmsInformationReportCodec::try_decode(bytes, report, nullptr)) {
            const auto encoded = mms::MmsInformationReportCodec::encode_pdu(report);
            const auto decoded = mms::MmsInformationReportCodec::decode(encoded);
            try {
                const auto frame = mms::MmsReportFrameMapper::map(decoded, {});
                mms::MmsOfflineReportMonitor monitor{{8U, 16U}};
                static_cast<void>(monitor.ingest(frame));
            } catch (...) {
            }
        }
    } catch (...) {
    }

    try {
        const auto envelope = mms::MmsPduCodec::decode_envelope(bytes);
        if (envelope.kind == mms::MmsPduKind::confirmed_request && envelope.service_tag == 12) {
            const auto request = mms::MmsDataSetDirectoryCodec::decode_request(bytes);
            static_cast<void>(mms::MmsDataSetDirectoryCodec::encode_request_pdu(request));
        } else if (envelope.kind == mms::MmsPduKind::confirmed_response && envelope.service_tag == 12) {
            const auto response = mms::MmsDataSetDirectoryCodec::decode_response(bytes);
            static_cast<void>(mms::MmsDataSetDirectoryCodec::encode_response_pdu(response));
        }
    } catch (...) {
    }
}

} // namespace ar::iec61850::fuzzing
