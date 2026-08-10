// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Prevent the Windows SDK from defining the legacy min/max function-like
// macros. They otherwise corrupt std::min and numeric_limits<T>::max calls in
// the built-in TCP transport when compiling with MSVC.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include "ariec61850/mms/association_runtime.hpp"

#include <chrono>
#include <cstddef>
#include <memory>

namespace ar::iec61850::mms {

class TcpMmsTransportError final : public MmsAssociationRuntimeError {
public:
    using MmsAssociationRuntimeError::MmsAssociationRuntimeError;
};

struct TcpMmsTransportOptions final {
    std::size_t receive_chunk_size{64U * 1024U};
    std::chrono::milliseconds cancellation_poll_interval{25};
    bool no_delay{true};
    bool keep_alive{true};
};

// Cross-platform TCP transport for MmsAssociationRuntime. The socket is kept
// non-blocking and readiness is awaited in bounded slices so deadlines and
// std::stop_token cancellation remain observable during connect/send/receive.
// One runtime operation may use the transport at a time.
class TcpMmsByteTransport final : public MmsByteTransport {
public:
    explicit TcpMmsByteTransport(TcpMmsTransportOptions options = {});
    ~TcpMmsByteTransport() override;

    TcpMmsByteTransport(const TcpMmsByteTransport&) = delete;
    TcpMmsByteTransport& operator=(const TcpMmsByteTransport&) = delete;
    TcpMmsByteTransport(TcpMmsByteTransport&&) = delete;
    TcpMmsByteTransport& operator=(TcpMmsByteTransport&&) = delete;

    void connect(
        const MmsEndpoint& endpoint,
        Deadline deadline,
        std::stop_token stop_token) override;
    void send(
        std::span<const std::uint8_t> bytes,
        Deadline deadline,
        std::stop_token stop_token) override;
    [[nodiscard]] std::vector<std::uint8_t> receive(
        Deadline deadline,
        std::stop_token stop_token) override;
    void close() noexcept override;
    [[nodiscard]] bool connected() const noexcept override;

    [[nodiscard]] const TcpMmsTransportOptions& options() const noexcept {
        return options_;
    }

private:
    struct Impl;

    TcpMmsTransportOptions options_;
    std::unique_ptr<Impl> impl_;
};

} // namespace ar::iec61850::mms
