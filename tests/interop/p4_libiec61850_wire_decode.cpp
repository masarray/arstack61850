// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/mms/pdu_span.hpp"
#include "ariec61850/mms/services_span.hpp"
#include "ariec61850/osi/cotp_span.hpp"
#include "ariec61850/osi/presentation_span.hpp"
#include "ariec61850/osi/session_span.hpp"
#include "ariec61850/osi/tpkt_span.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>

int main() {
    using namespace ar::iec61850;
    constexpr std::array<std::uint8_t, 36> wire{{
        0x03,0x00,0x00,0x24,0x02,0xF0,0x80,0x01,0x00,0x01,0x00,0x61,
        0x17,0x30,0x15,0x02,0x01,0x03,0xA0,0x10,0xA0,0x0E,0x02,0x01,
        0x01,0xA1,0x09,0xA0,0x03,0x80,0x01,0x09,0xA1,0x02,0x80,0x00}};

    osi::TpktFrameView tpkt;
    if (!osi::TpktSpanCodec::try_decode_view(wire, tpkt)) {
        std::cerr << "P4_WIRE_DECODE_FAIL layer=tpkt\n";
        return 1;
    }
    osi::CotpTpduView cotp;
    if (!osi::CotpSpanCodec::try_decode_view(tpkt.payload, cotp)) {
        std::cerr << "P4_WIRE_DECODE_FAIL layer=cotp\n";
        return 2;
    }
    osi::SessionDataTransferView session;
    if (!osi::SessionSpanCodec::try_decode_data_transfer_view(cotp.user_data, session)) {
        std::cerr << "P4_WIRE_DECODE_FAIL layer=session\n";
        return 3;
    }
    osi::PresentationPdvView pdv;
    if (!osi::PresentationSpanCodec::try_decode_fully_encoded_data_view(
            session.presentation_payload, pdv)) {
        std::cerr << "P4_WIRE_DECODE_FAIL layer=presentation\n";
        return 4;
    }
    mms::MmsConfirmedPduView confirmed;
    if (!mms::MmsPduSpanCodec::try_decode_confirmed_request_view(
            pdv.single_asn1_type, confirmed)) {
        std::cerr << "P4_WIRE_DECODE_FAIL layer=confirmed-pdu\n";
        return 5;
    }
    mms::MmsGetNameListRequestView request;
    if (!mms::MmsServiceSpanCodec::try_decode_get_name_list_request(confirmed, request)) {
        std::cerr << "P4_WIRE_DECODE_FAIL layer=get-name-list\n";
        return 6;
    }
    if (pdv.context_id != 3U || confirmed.invoke_id != 1U ||
        confirmed.service() != mms::MmsWireConfirmedService::get_name_list ||
        request.object_class != mms::MmsNameListObjectClass::domain ||
        request.scope != mms::MmsNameScopeKind::vmd_specific) {
        std::cerr << "P4_WIRE_DECODE_FAIL layer=semantic-check\n";
        return 7;
    }

    std::cout << "P4_LIBIEC61850_WIRE_DECODE_PASS context=3 invoke=1 service=GetNameList objectClass=domain scope=vmdSpecific\n";
    return 0;
}
