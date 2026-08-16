// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/embedded/io.hpp"
#include "ariec61850/mms/connection_runtime.hpp"
#include "ariec61850/mms/static_dispatcher.hpp"
#include "ariec61850/mms/static_object_table.hpp"
#include "ariec61850/mms/static_server_session.hpp"
#include "ariec61850/osi/tpkt_span.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

namespace {
using namespace ar::iec61850;

wire::EncodeResult read_stub(const void*, std::span<std::uint8_t> destination) noexcept {
    constexpr std::array<std::uint8_t, 3> value{{0x83, 0x01, 0x00}};
    if (destination.size() < value.size()) {
        return {wire::EncodeStatus::buffer_too_small, 0U, value.size()};
    }
    std::copy(value.begin(), value.end(), destination.begin());
    return {wire::EncodeStatus::ok, value.size(), value.size()};
}

struct StreamState final {
    std::vector<std::uint8_t> input;
    std::size_t input_offset{};
    std::vector<std::uint8_t> output;
};

embedded::IoResult receive(void* raw, std::span<std::uint8_t> destination) noexcept {
    auto* state = static_cast<StreamState*>(raw);
    if (state == nullptr) return {embedded::IoStatus::invalid_argument, 0U};
    if (state->input_offset >= state->input.size()) {
        return {embedded::IoStatus::timeout, 0U};
    }
    const auto remaining = state->input.size() - state->input_offset;
    const auto count = std::min(remaining, destination.size());
    std::copy_n(state->input.begin() + static_cast<std::ptrdiff_t>(state->input_offset),
                count, destination.begin());
    state->input_offset += count;
    return {embedded::IoStatus::ok, count};
}

embedded::IoResult send(void* raw, std::span<const std::uint8_t> bytes) noexcept {
    auto* state = static_cast<StreamState*>(raw);
    if (state == nullptr || bytes.empty()) return {embedded::IoStatus::invalid_argument, 0U};
    state->output.insert(state->output.end(), bytes.begin(), bytes.end());
    return {embedded::IoStatus::ok, bytes.size()};
}

std::size_t count_tpkt_frames(std::span<const std::uint8_t> bytes) noexcept {
    std::size_t count{};
    std::size_t offset{};
    while (offset < bytes.size()) {
        const auto peek = osi::TpktSpanCodec::peek_frame(bytes.subspan(offset));
        if (peek.status != osi::TpktPeekStatus::ready || peek.frame_bytes == 0U) return 0U;
        offset += peek.frame_bytes;
        ++count;
    }
    return offset == bytes.size() ? count : 0U;
}
}

int main() {
    using namespace ar::iec61850;

    constexpr std::array<std::uint8_t, 22> cotp_cr{{
        0x03,0x00,0x00,0x16,0x11,0xE0,0x00,0x00,0x00,0x01,0x00,
        0xC0,0x01,0x0D,0xC2,0x02,0x00,0x01,0xC1,0x02,0x00,0x01}};
    constexpr std::array<std::uint8_t, 187> association{{
        0x03,0x00,0x00,0xBB,0x02,0xF0,0x80,0x0D,0xB2,0x05,0x06,0x13,0x01,0x00,0x16,0x01,0x02,0x14,0x02,0x00,0x02,0x33,0x02,0x00,0x01,0x34,0x02,0x00,0x01,0xC1,0x9C,0x31,0x81,0x99,0xA0,0x03,0x80,0x01,0x01,0xA2,0x81,0x91,0x81,0x04,0x00,0x00,0x00,0x01,0x82,0x04,0x00,0x00,0x00,0x01,0xA4,0x23,0x30,0x0F,0x02,0x01,0x01,0x06,0x04,0x52,0x01,0x00,0x01,0x30,0x04,0x06,0x02,0x51,0x01,0x30,0x10,0x02,0x01,0x03,0x06,0x05,0x28,0xCA,0x22,0x02,0x01,0x30,0x04,0x06,0x02,0x51,0x01,0x61,0x5E,0x30,0x5C,0x02,0x01,0x01,0xA0,0x57,0x60,0x55,0xA1,0x07,0x06,0x05,0x28,0xCA,0x22,0x02,0x03,0xA2,0x07,0x06,0x05,0x29,0x01,0x87,0x67,0x01,0xA3,0x03,0x02,0x01,0x0C,0xA6,0x06,0x06,0x04,0x29,0x01,0x87,0x67,0xA7,0x03,0x02,0x01,0x0C,0xBE,0x2F,0x28,0x2D,0x02,0x01,0x03,0xA0,0x28,0xA8,0x26,0x80,0x03,0x00,0xFD,0xE8,0x81,0x01,0x05,0x82,0x01,0x05,0x83,0x01,0x0A,0xA4,0x16,0x80,0x01,0x01,0x81,0x03,0x05,0xF1,0x00,0x82,0x0C,0x03,0xEE,0x1C,0x00,0x00,0x04,0x08,0x00,0x00,0x79,0xEF,0x18}};
    constexpr std::array<std::uint8_t, 36> get_name_list{{
        0x03,0x00,0x00,0x24,0x02,0xF0,0x80,0x01,0x00,0x01,0x00,0x61,
        0x17,0x30,0x15,0x02,0x01,0x03,0xA0,0x10,0xA0,0x0E,0x02,0x01,
        0x01,0xA1,0x09,0xA0,0x03,0x80,0x01,0x09,0xA1,0x02,0x80,0x00}};

    StreamState stream_state;
    stream_state.input.reserve(cotp_cr.size() + association.size() + get_name_list.size());
    stream_state.input.insert(stream_state.input.end(), cotp_cr.begin(), cotp_cr.end());
    stream_state.input.insert(stream_state.input.end(), association.begin(), association.end());
    stream_state.input.insert(stream_state.input.end(), get_name_list.begin(), get_name_list.end());

    constexpr std::array<std::uint8_t, 2> bool_type{{0x83, 0x00}};
    const std::array<mms::MmsStaticObjectEntry, 1> entries{{
        {"IED1LD0", "LLN0$ST$Health$stVal", bool_type, read_stub, nullptr}
    }};
    const mms::MmsStaticObjectTable table{entries};
    mms::MmsStaticDispatchPolicy dispatch_policy{};
    dispatch_policy.advertise_flattened_child_aliases = true;
    const mms::MmsStaticApplicationDispatcher dispatcher{table, dispatch_policy};
    mms::MmsStaticConnectionRuntime runtime{dispatcher};

    std::array<std::uint8_t, 32768> receive_buffer{};
    std::array<std::uint8_t, 32768> response_buffer{};
    std::array<std::uint8_t, 8192> workspace{};
    const embedded::TcpByteStream stream{&stream_state, send, receive};
    const mms::MmsStaticServerSessionBuffers buffers{
        receive_buffer, response_buffer, workspace};
    mms::MmsStaticServerSession session{runtime, stream, buffers};

    bool terminal = false;
    for (std::size_t iteration = 0U; iteration < 24U; ++iteration) {
        const auto result = session.poll_once();
        std::cout << "P4_SERVER_SESSION_STEP iteration=" << iteration
                  << " status=" << static_cast<unsigned>(result.status)
                  << " connection_status=" << static_cast<unsigned>(result.connection_status)
                  << " received=" << result.bytes_received
                  << " sent=" << result.bytes_sent
                  << " buffered=" << result.buffered_input_bytes
                  << " pending=" << result.pending_output_bytes << '\n';
        if (result.terminal()) {
            terminal = true;
            break;
        }
        if (stream_state.input_offset == stream_state.input.size() &&
            session.buffered_input_bytes() == 0U &&
            session.pending_output_bytes() == 0U &&
            count_tpkt_frames(stream_state.output) >= 3U) {
            break;
        }
    }

    const auto frames = count_tpkt_frames(stream_state.output);
    if (terminal || runtime.state() != mms::MmsStaticConnectionState::established ||
        stream_state.input_offset != stream_state.input.size() ||
        session.buffered_input_bytes() != 0U || session.pending_output_bytes() != 0U ||
        frames != 3U || stream_state.output.size() <= 167U) {
        std::cerr << "P4_SERVER_SESSION_FAIL terminal=" << (terminal ? 1 : 0)
                  << " state=" << static_cast<unsigned>(runtime.state())
                  << " input=" << stream_state.input_offset << '/' << stream_state.input.size()
                  << " buffered=" << session.buffered_input_bytes()
                  << " pending=" << session.pending_output_bytes()
                  << " frames=" << frames
                  << " output=" << stream_state.output.size() << '\n';
        return 1;
    }

    std::cout << "P4_LIBIEC61850_SERVER_SESSION_PASS coalesced_rx=245 responses=3 association=persistent discovery=response_ready\n";
    return 0;
}
