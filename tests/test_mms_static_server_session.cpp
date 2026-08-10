// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_server_session.hpp"
#include "ariec61850/mms/static_object_table.hpp"
#include "ariec61850/osi/cotp_span.hpp"
#include "ariec61850/osi/tpkt_span.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

using namespace ar::iec61850;

struct FakeTcp final {
    std::span<const std::uint8_t> inbound;
    std::span<std::uint8_t> outbound;
    std::size_t inbound_offset{};
    std::size_t outbound_size{};
    std::size_t maximum_receive_chunk{2U};
    std::size_t maximum_send_chunk{3U};
    bool close_when_empty{true};
};

[[nodiscard]] embedded::IoResult fake_receive(
    void* context,
    const std::span<std::uint8_t> destination) noexcept {
    auto& tcp = *static_cast<FakeTcp*>(context);
    if (tcp.inbound_offset == tcp.inbound.size()) {
        return {
            tcp.close_when_empty ? embedded::IoStatus::closed
                                 : embedded::IoStatus::would_block,
            0U};
    }
    const auto count = std::min({
        destination.size(),
        tcp.maximum_receive_chunk,
        tcp.inbound.size() - tcp.inbound_offset});
    std::copy_n(
        tcp.inbound.begin() + static_cast<std::ptrdiff_t>(tcp.inbound_offset),
        count,
        destination.begin());
    tcp.inbound_offset += count;
    return {embedded::IoStatus::ok, count};
}

[[nodiscard]] embedded::IoResult fake_send(
    void* context,
    const std::span<const std::uint8_t> bytes) noexcept {
    auto& tcp = *static_cast<FakeTcp*>(context);
    if (tcp.outbound_size == tcp.outbound.size()) {
        return {embedded::IoStatus::would_block, 0U};
    }
    const auto count = std::min({
        bytes.size(),
        tcp.maximum_send_chunk,
        tcp.outbound.size() - tcp.outbound_size});
    std::copy_n(
        bytes.begin(),
        count,
        tcp.outbound.begin() + static_cast<std::ptrdiff_t>(tcp.outbound_size));
    tcp.outbound_size += count;
    return {embedded::IoStatus::ok, count};
}

[[nodiscard]] wire::EncodeResult read_boolean(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 3U;
    if (context == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    destination[0] = 0x83U;
    destination[1] = 0x01U;
    destination[2] = *static_cast<const bool*>(context) ? 0xFFU : 0x00U;
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] bool fragmented_connection_request_is_pumped() noexcept {
    constexpr std::array<std::uint8_t, 2U> boolean_type{0x83U, 0x00U};
    const bool value = true;
    const std::array<mms::MmsStaticObjectEntry, 1U> objects{
        mms::MmsStaticObjectEntry{
            "ESP32S3IOLD0",
            "GGIO1$ST$Ind1$stVal",
            boolean_type,
            read_boolean,
            &value}};
    const mms::MmsStaticObjectTable table{objects};
    const mms::MmsStaticApplicationDispatcher dispatcher{table};
    mms::MmsStaticConnectionRuntime runtime{dispatcher};

    constexpr std::array<std::uint8_t, 1U> tpdu_size{0x0AU};
    constexpr std::array<std::uint8_t, 2U> source_tsap{0x00U, 0x01U};
    constexpr std::array<std::uint8_t, 2U> destination_tsap{0x00U, 0x01U};
    const std::array<osi::CotpParameterView, 3U> parameters{
        osi::CotpParameterView{osi::CotpSpanCodec::tpdu_size_parameter, tpdu_size},
        osi::CotpParameterView{osi::CotpSpanCodec::source_tsap_parameter, source_tsap},
        osi::CotpParameterView{
            osi::CotpSpanCodec::destination_tsap_parameter,
            destination_tsap}};

    std::array<std::uint8_t, 128U> cotp_bytes{};
    std::array<std::uint8_t, 128U> inbound{};
    const auto cotp = osi::CotpSpanCodec::encode_connection_request_into(
        0x0001U, parameters, cotp_bytes);
    const auto tpkt = cotp.success()
        ? osi::TpktSpanCodec::encode_into(
              std::span<const std::uint8_t>{cotp_bytes}.first(cotp.bytes_written),
              inbound)
        : wire::EncodeResult{};
    if (!cotp.success() || !tpkt.success()) {
        return false;
    }

    std::array<std::uint8_t, 256U> outbound{};
    FakeTcp fake{
        std::span<const std::uint8_t>{inbound}.first(tpkt.bytes_written),
        outbound};
    const embedded::TcpByteStream stream{&fake, fake_send, fake_receive};
    std::array<std::uint8_t, 64U> receive{};
    std::array<std::uint8_t, 256U> response{};
    std::array<std::uint8_t, 256U> workspace{};
    mms::MmsStaticServerSession session{
        runtime,
        stream,
        {receive, response, workspace}};

    for (std::size_t iteration = 0U; iteration < 256U; ++iteration) {
        const auto result = session.poll_once();
        if (result.terminal()) {
            return false;
        }
        const auto peek = osi::TpktSpanCodec::peek_frame(
            std::span<const std::uint8_t>{outbound}.first(fake.outbound_size));
        if (peek.status == osi::TpktPeekStatus::ready) {
            osi::TpktFrameView response_tpkt;
            osi::CotpTpduView response_cotp;
            return fake.inbound_offset == tpkt.bytes_written &&
                session.pending_output_bytes() == 0U &&
                runtime.state() ==
                    mms::MmsStaticConnectionState::awaiting_association &&
                osi::TpktSpanCodec::try_decode_view(
                    std::span<const std::uint8_t>{outbound}.first(peek.frame_bytes),
                    response_tpkt) &&
                osi::CotpSpanCodec::try_decode_view(
                    response_tpkt.payload,
                    response_cotp) &&
                response_cotp.kind == osi::CotpWireKind::connection_confirm &&
                response_cotp.destination_reference == 0x0001U;
        }
    }
    return false;
}

[[nodiscard]] bool oversized_frame_is_bounded() noexcept {
    const bool value = false;
    constexpr std::array<std::uint8_t, 2U> boolean_type{0x83U, 0x00U};
    const std::array<mms::MmsStaticObjectEntry, 1U> objects{
        mms::MmsStaticObjectEntry{
            "LD0", "X", boolean_type, read_boolean, &value}};
    const mms::MmsStaticObjectTable table{objects};
    const mms::MmsStaticApplicationDispatcher dispatcher{table};
    mms::MmsStaticConnectionRuntime runtime{dispatcher};

    constexpr std::array<std::uint8_t, 8U> declared_large_tpkt{
        0x03U, 0x00U, 0x04U, 0x00U, 0x02U, 0xF0U, 0x80U, 0x00U};
    std::array<std::uint8_t, 8U> discarded_output{};
    FakeTcp fake{declared_large_tpkt, discarded_output};
    fake.maximum_receive_chunk = declared_large_tpkt.size();
    const embedded::TcpByteStream stream{&fake, fake_send, fake_receive};
    std::array<std::uint8_t, 8U> receive{};
    std::array<std::uint8_t, 64U> response{};
    std::array<std::uint8_t, 64U> workspace{};
    mms::MmsStaticServerSession session{
        runtime,
        stream,
        {receive, response, workspace}};

    const auto first = session.poll_once();
    const auto second = session.poll_once();
    return first.status == mms::MmsStaticServerSessionStatus::progressed &&
        second.status ==
            mms::MmsStaticServerSessionStatus::receive_buffer_full &&
        second.terminal();
}

[[nodiscard]] bool peer_close_is_reported() noexcept {
    const bool value = false;
    constexpr std::array<std::uint8_t, 2U> boolean_type{0x83U, 0x00U};
    const std::array<mms::MmsStaticObjectEntry, 1U> objects{
        mms::MmsStaticObjectEntry{
            "LD0", "X", boolean_type, read_boolean, &value}};
    const mms::MmsStaticObjectTable table{objects};
    const mms::MmsStaticApplicationDispatcher dispatcher{table};
    mms::MmsStaticConnectionRuntime runtime{dispatcher};
    std::array<std::uint8_t, 1U> empty_inbound{};
    std::array<std::uint8_t, 8U> outbound{};
    FakeTcp fake{std::span<const std::uint8_t>{empty_inbound}.first(0U), outbound};
    const embedded::TcpByteStream stream{&fake, fake_send, fake_receive};
    std::array<std::uint8_t, 8U> receive{};
    std::array<std::uint8_t, 8U> response{};
    std::array<std::uint8_t, 8U> workspace{};
    mms::MmsStaticServerSession session{
        runtime,
        stream,
        {receive, response, workspace}};
    const auto result = session.poll_once();
    return result.status == mms::MmsStaticServerSessionStatus::peer_closed &&
        result.terminal();
}

} // namespace

int main() {
    if (!fragmented_connection_request_is_pumped()) {
        return 1;
    }
    if (!oversized_frame_is_bounded()) {
        return 2;
    }
    if (!peer_close_is_reported()) {
        return 3;
    }
    return 0;
}
