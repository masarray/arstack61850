// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/embedded/io.hpp"
#include "ariec61850/mms/connection_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::mms {

enum class MmsStaticServerSessionStatus : std::uint8_t {
    progressed,
    response_pending,
    would_block,
    timed_out,
    application_rejected,
    peer_closed,
    transport_error,
    protocol_error,
    receive_buffer_full,
    invalid_configuration,
};

struct MmsStaticServerSessionResult final {
    MmsStaticServerSessionStatus status{
        MmsStaticServerSessionStatus::invalid_configuration};
    MmsStaticConnectionStatus connection_status{
        MmsStaticConnectionStatus::need_more};
    std::size_t bytes_received{};
    std::size_t bytes_sent{};
    std::size_t buffered_input_bytes{};
    std::size_t pending_output_bytes{};

    [[nodiscard]] constexpr bool terminal() const noexcept {
        return status == MmsStaticServerSessionStatus::peer_closed ||
            status == MmsStaticServerSessionStatus::transport_error ||
            status == MmsStaticServerSessionStatus::protocol_error ||
            status == MmsStaticServerSessionStatus::receive_buffer_full ||
            status == MmsStaticServerSessionStatus::invalid_configuration;
    }
};

struct MmsStaticServerSessionBuffers final {
    std::span<std::uint8_t> receive;
    std::span<std::uint8_t> response;
    std::span<std::uint8_t> workspace;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return !receive.empty() && !response.empty() && !workspace.empty();
    }
};

// Allocation-free bridge between a connected TCP byte stream and the portable
// MMS connection state machine. Listen/accept/close remain platform-owned.
// One instance is used per accepted connection, which lets desktop servers,
// FreeRTOS tasks and lwIP callbacks choose their own concurrency policy.
class MmsStaticServerSession final {
public:
    constexpr MmsStaticServerSession(
        MmsStaticConnectionRuntime& runtime,
        const embedded::TcpByteStream stream,
        const MmsStaticServerSessionBuffers buffers) noexcept
        : runtime_{runtime}, stream_{stream}, buffers_{buffers} {}

    // Performs at most one transport send/receive or one protocol dispatch.
    // Call repeatedly from the platform event/task loop. Partial sends and
    // fragmented/coalesced TPKT input are retained across calls.
    [[nodiscard]] MmsStaticServerSessionResult poll_once() noexcept;

    void reset() noexcept;

    [[nodiscard]] constexpr std::size_t buffered_input_bytes() const noexcept {
        return receive_size_;
    }

    [[nodiscard]] constexpr std::size_t pending_output_bytes() const noexcept {
        return response_size_ - response_offset_;
    }

private:
    [[nodiscard]] MmsStaticServerSessionResult make_result(
        MmsStaticServerSessionStatus status,
        MmsStaticConnectionStatus connection_status,
        std::size_t bytes_received = 0U,
        std::size_t bytes_sent = 0U) const noexcept;

    void consume_input(std::size_t consumed) noexcept;

    MmsStaticConnectionRuntime& runtime_;
    embedded::TcpByteStream stream_{};
    MmsStaticServerSessionBuffers buffers_{};
    std::size_t receive_size_{};
    std::size_t response_offset_{};
    std::size_t response_size_{};
    bool terminal_{};
};

} // namespace ar::iec61850::mms
