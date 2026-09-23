// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/capture/passive_mms.hpp"
#include "ariec61850/mms/services.hpp"
#include "ariec61850/osi/cotp.hpp"
#include "ariec61850/osi/session.hpp"
#include "ariec61850/osi/tpkt.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
void require(bool ok, const char* label) {
    if (!ok) {
        std::cerr << "PASSIVE_MMS_FAIL " << label << '\n';
        std::exit(1);
    }
}
void put16(Bytes& b, std::size_t i, std::uint16_t n) {
    b[i] = static_cast<std::uint8_t>(n >> 8U);
    b[i+1U] = static_cast<std::uint8_t>(n);
}
void put32(Bytes& b, std::size_t i, std::uint32_t n) {
    b[i] = static_cast<std::uint8_t>(n >> 24U);
    b[i+1U] = static_cast<std::uint8_t>(n >> 16U);
    b[i+2U] = static_cast<std::uint8_t>(n >> 8U);
    b[i+3U] = static_cast<std::uint8_t>(n);
}
Bytes packet(std::span<const std::uint8_t> payload, std::uint32_t sequence,
             std::uint16_t source_port = 41000U, std::uint16_t dest_port = 102U) {
    Bytes b(14U+20U+20U+payload.size(), 0U);
    put16(b,12U,0x0800U);
    b[14U]=0x45U;
    put16(b,16U,static_cast<std::uint16_t>(40U+payload.size()));
    b[22U]=64U;
    b[23U]=6U;
    b[26U]=192U; b[27U]=0U; b[28U]=2U; b[29U]=10U;
    b[30U]=192U; b[31U]=0U; b[32U]=2U; b[33U]=20U;
    put16(b,34U,source_port); put16(b,36U,dest_port);
    put32(b,38U,sequence);
    b[46U]=0x50U; b[47U]=0x18U;
    std::copy(payload.begin(),payload.end(),b.begin()+54);
    return b;
}
Bytes request() {
    namespace mms=ar::iec61850::mms;
    namespace osi=ar::iec61850::osi;
    mms::MmsReadRequest req;
    req.invoke_id=42U;
    req.variables.push_back(mms::MmsObjectName::domain_specific(
        "LD0","LLN0$ST$Mod$stVal"));
    const auto p_data=mms::MmsServiceCodec::encode_read_request_p_data(req);
    const auto session=osi::SessionCodec::encode_data_transfer(p_data);
    const auto cotp=osi::CotpFrameCodec::encode_data(session);
    return osi::TpktFrameCodec::encode(cotp);
}
} // namespace

int main() {
    using ar::iec61850::capture::PassiveMmsDecoder;
    const auto now=std::chrono::system_clock::now();
    const auto wire=request();
    require(wire.size()>24U,"fixture_size");
    PassiveMmsDecoder decoder;
    decoder.ingest(packet(std::span<const std::uint8_t>{wire}.first(8U),1000U),now);
    decoder.ingest(packet(std::span<const std::uint8_t>{wire}.subspan(16U),1016U),now);
    require(decoder.counters().out_of_order==1U,"out_of_order_count");
    decoder.ingest(packet(std::span<const std::uint8_t>{wire}.subspan(8U,8U),1008U),now);
    require(decoder.counters().mms_messages==1U,"reassembled_message");
    require(decoder.counters().decoded_requests==1U,"read_request");
    require(decoder.events().size()==1U,"event_count");
    require(decoder.events().front().service=="Read","read_service");
    require(decoder.events().front().invoke_id.value_or(0U)==42U,"invoke_id");
    decoder.ingest(packet(wire,1000U),now);
    require(decoder.counters().mms_messages==1U,"deduplicated_retransmission");
    require(decoder.counters().retransmissions>=1U,"retransmission_count");

    PassiveMmsDecoder coalesced;
    Bytes double_wire=wire;
    double_wire.insert(double_wire.end(),wire.begin(),wire.end());
    coalesced.ingest(packet(double_wire,500U),now);
    require(coalesced.counters().mms_messages==2U,"coalesced_tpkt");
    coalesced.ingest(packet({},9999U,80U,443U),now);
    require(coalesced.counters().mms_messages==2U,"unrelated_tcp_ignored");
    auto bad=packet(wire,3000U);
    bad.resize(25U);
    coalesced.ingest(bad,now);
    require(coalesced.counters().malformed>=1U,"truncated_ipv4");
    std::cout<<"PASSIVE_MMS_PASS split=true out_of_order=true retransmission=true "
             <<"coalesced=true read=true invoke=42 unrelated_ignored=true malformed=true "
             <<"active_association=false\n";
}
