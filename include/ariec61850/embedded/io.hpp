// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::embedded {

enum class IoStatus : std::uint8_t {
    ok,
    would_block,
    timeout,
    closed,
    io_error,
    invalid_argument,
    buffer_too_small,
};

struct IoResult final {
    IoStatus status{IoStatus::ok};
    std::size_t transferred{};

    [[nodiscard]] constexpr bool success() const noexcept {
        return status == IoStatus::ok;
    }
};

using MonotonicMicrosecondsFn = std::uint64_t (*)(void* context) noexcept;
using UtcMillisecondsFn = std::uint64_t (*)(void* context) noexcept;
using RawEthernetTransmitFn = IoResult (*)(
    void* context,
    std::span<const std::uint8_t> frame) noexcept;
using TcpSendFn = IoResult (*)(
    void* context,
    std::span<const std::uint8_t> bytes) noexcept;
using TcpReceiveFn = IoResult (*)(
    void* context,
    std::span<std::uint8_t> destination) noexcept;

struct Clock final {
    void* context{};
    MonotonicMicrosecondsFn monotonic_microseconds{};
    UtcMillisecondsFn utc_milliseconds{};

    [[nodiscard]] std::uint64_t monotonic_us() const noexcept {
        return monotonic_microseconds == nullptr ? 0U : monotonic_microseconds(context);
    }

    [[nodiscard]] std::uint64_t utc_ms() const noexcept {
        return utc_milliseconds == nullptr ? 0U : utc_milliseconds(context);
    }
};

struct RawEthernetPort final {
    void* context{};
    RawEthernetTransmitFn transmit{};

    [[nodiscard]] IoResult send(
        const std::span<const std::uint8_t> frame) const noexcept {
        if (transmit == nullptr || frame.empty()) {
            return {IoStatus::invalid_argument, 0U};
        }
        return transmit(context, frame);
    }
};

// Represents an already-connected TCP byte stream. Connection setup/listen/
// accept remains platform-owned so the protocol engine is independent of
// lwIP, BSD sockets, WinSock, FreeRTOS and ESP-IDF.
struct TcpByteStream final {
    void* context{};
    TcpSendFn send_callback{};
    TcpReceiveFn receive_callback{};

    [[nodiscard]] IoResult send(
        const std::span<const std::uint8_t> bytes) const noexcept {
        if (send_callback == nullptr || bytes.empty()) {
            return {IoStatus::invalid_argument, 0U};
        }
        return send_callback(context, bytes);
    }

    [[nodiscard]] IoResult receive(
        const std::span<std::uint8_t> destination) const noexcept {
        if (receive_callback == nullptr || destination.empty()) {
            return {IoStatus::invalid_argument, 0U};
        }
        return receive_callback(context, destination);
    }
};

static_assert(sizeof(IoStatus) == 1U);

} // namespace ar::iec61850::embedded
