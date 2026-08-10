// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/tcp_transport.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ar::iec61850::mms {
namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket invalid_socket = INVALID_SOCKET;
using SocketLength = int;

class WinsockLifetime final {
public:
    WinsockLifetime() {
        WSADATA data{};
        const int result = WSAStartup(MAKEWORD(2, 2), &data);
        if (result != 0) {
            throw TcpMmsTransportError(
                "WSAStartup failed with error " + std::to_string(result) + ".");
        }
    }

    ~WinsockLifetime() { WSACleanup(); }
};

void ensure_socket_runtime() {
    static WinsockLifetime lifetime;
    static_cast<void>(lifetime);
}

[[nodiscard]] int last_socket_error() noexcept { return WSAGetLastError(); }
[[nodiscard]] bool interrupted(const int error) noexcept { return error == WSAEINTR; }
[[nodiscard]] bool would_block(const int error) noexcept {
    return error == WSAEWOULDBLOCK;
}
[[nodiscard]] bool connect_in_progress(const int error) noexcept {
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS ||
           error == WSAEALREADY;
}
void close_native_socket(const NativeSocket socket) noexcept {
    if (socket != invalid_socket) {
        closesocket(socket);
    }
}
void shutdown_native_socket(const NativeSocket socket) noexcept {
    if (socket != invalid_socket) {
        shutdown(socket, SD_BOTH);
    }
}
#else
using NativeSocket = int;
constexpr NativeSocket invalid_socket = -1;
using SocketLength = socklen_t;

void ensure_socket_runtime() {}
[[nodiscard]] int last_socket_error() noexcept { return errno; }
[[nodiscard]] bool interrupted(const int error) noexcept { return error == EINTR; }
[[nodiscard]] bool would_block(const int error) noexcept {
    return error == EAGAIN || error == EWOULDBLOCK;
}
[[nodiscard]] bool connect_in_progress(const int error) noexcept {
    return error == EINPROGRESS || error == EALREADY || would_block(error);
}
void close_native_socket(const NativeSocket socket) noexcept {
    if (socket != invalid_socket) {
        static_cast<void>(::close(socket));
    }
}
void shutdown_native_socket(const NativeSocket socket) noexcept {
    if (socket != invalid_socket) {
        static_cast<void>(::shutdown(socket, SHUT_RDWR));
    }
}
#endif

[[nodiscard]] std::string socket_error_text(
    const std::string& operation,
    const int error) {
    return operation + " failed with socket error " + std::to_string(error) + ".";
}

void set_non_blocking(const NativeSocket socket) {
#ifdef _WIN32
    u_long enabled = 1UL;
    if (ioctlsocket(socket, FIONBIO, &enabled) != 0) {
        throw TcpMmsTransportError(
            socket_error_text("ioctlsocket(FIONBIO)", last_socket_error()));
    }
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0) {
        throw TcpMmsTransportError(
            socket_error_text("fcntl(O_NONBLOCK)", last_socket_error()));
    }
#endif
}

void set_boolean_socket_option(
    const NativeSocket socket,
    const int level,
    const int option,
    const bool enabled,
    const char* option_name) {
    const int value = enabled ? 1 : 0;
    const auto* raw = reinterpret_cast<const char*>(&value);
    if (setsockopt(
            socket,
            level,
            option,
            raw,
            static_cast<SocketLength>(sizeof(value))) != 0) {
        throw TcpMmsTransportError(
            socket_error_text(option_name, last_socket_error()));
    }
}

[[nodiscard]] std::chrono::milliseconds readiness_slice(
    const MmsByteTransport::Deadline deadline,
    const std::chrono::milliseconds poll_interval) {
    const auto now = MmsByteTransport::Clock::now();
    if (now >= deadline) {
        return std::chrono::milliseconds::zero();
    }
    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    if (remaining <= std::chrono::milliseconds::zero()) {
        remaining = std::chrono::milliseconds{1};
    }
    return std::min(remaining, poll_interval);
}

void wait_socket_ready(
    const NativeSocket socket,
    const bool read_ready,
    const bool write_ready,
    const MmsByteTransport::Deadline deadline,
    const std::chrono::milliseconds poll_interval,
    const std::stop_token stop_token,
    const char* operation) {
    for (;;) {
        if (stop_token.stop_requested()) {
            throw MmsTransportCancelledError(
                std::string{operation} + " was cancelled.");
        }

        const auto slice = readiness_slice(deadline, poll_interval);
        if (slice <= std::chrono::milliseconds::zero()) {
            throw MmsTransportTimeoutError(
                std::string{operation} + " exceeded its deadline.");
        }

        fd_set read_set;
        fd_set write_set;
        FD_ZERO(&read_set);
        FD_ZERO(&write_set);
        if (read_ready) {
            FD_SET(socket, &read_set);
        }
        if (write_ready) {
            FD_SET(socket, &write_set);
        }

        const auto total_microseconds =
            std::chrono::duration_cast<std::chrono::microseconds>(slice).count();
        constexpr auto microseconds_per_second = 1'000'000LL;
        timeval timeout{};
        timeout.tv_sec = static_cast<long>(
            total_microseconds / microseconds_per_second);
        timeout.tv_usec = static_cast<long>(
            total_microseconds % microseconds_per_second);

#ifdef _WIN32
        const int selected = select(
            0,
            read_ready ? &read_set : nullptr,
            write_ready ? &write_set : nullptr,
            nullptr,
            &timeout);
#else
        const int selected = select(
            socket + 1,
            read_ready ? &read_set : nullptr,
            write_ready ? &write_set : nullptr,
            nullptr,
            &timeout);
#endif
        if (selected > 0) {
            return;
        }
        if (selected == 0) {
            continue;
        }

        const int error = last_socket_error();
        if (interrupted(error)) {
            continue;
        }
        throw TcpMmsTransportError(socket_error_text(operation, error));
    }
}

struct AddressInfoDeleter final {
    void operator()(addrinfo* value) const noexcept {
        if (value != nullptr) {
            freeaddrinfo(value);
        }
    }
};

[[nodiscard]] std::ptrdiff_t socket_send(
    const NativeSocket socket,
    const std::uint8_t* bytes,
    const std::size_t count) {
    const std::size_t bounded = std::min(
        count,
        static_cast<std::size_t>(std::numeric_limits<int>::max()));
#ifdef _WIN32
    return static_cast<std::ptrdiff_t>(::send(
        socket,
        reinterpret_cast<const char*>(bytes),
        static_cast<int>(bounded),
        0));
#else
#ifdef MSG_NOSIGNAL
    constexpr int send_flags = MSG_NOSIGNAL;
#else
    constexpr int send_flags = 0;
#endif
    return static_cast<std::ptrdiff_t>(::send(
        socket,
        bytes,
        bounded,
        send_flags));
#endif
}

[[nodiscard]] std::ptrdiff_t socket_receive(
    const NativeSocket socket,
    std::uint8_t* bytes,
    const std::size_t count) {
    const std::size_t bounded = std::min(
        count,
        static_cast<std::size_t>(std::numeric_limits<int>::max()));
#ifdef _WIN32
    return static_cast<std::ptrdiff_t>(::recv(
        socket,
        reinterpret_cast<char*>(bytes),
        static_cast<int>(bounded),
        0));
#else
    return static_cast<std::ptrdiff_t>(::recv(socket, bytes, bounded, 0));
#endif
}

} // namespace

struct TcpMmsByteTransport::Impl final {
    NativeSocket socket{invalid_socket};
    bool connected{};
};

TcpMmsByteTransport::TcpMmsByteTransport(TcpMmsTransportOptions options)
    : options_{std::move(options)}, impl_{std::make_unique<Impl>()} {
    if (options_.receive_chunk_size == 0U ||
        options_.receive_chunk_size >
            static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        options_.cancellation_poll_interval <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("TCP MMS transport options are invalid.");
    }
}

TcpMmsByteTransport::~TcpMmsByteTransport() { close(); }

void TcpMmsByteTransport::connect(
    const MmsEndpoint& endpoint,
    const Deadline deadline,
    const std::stop_token stop_token) {
    if (endpoint.host.empty()) {
        throw std::invalid_argument("TCP MMS endpoint host must not be empty.");
    }
    if (stop_token.stop_requested()) {
        throw MmsTransportCancelledError("TCP MMS connect was cancelled.");
    }
    if (Clock::now() >= deadline) {
        throw MmsTransportTimeoutError("TCP MMS connect deadline has expired.");
    }

    close();
    ensure_socket_runtime();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* raw_addresses = nullptr;
    const std::string port = std::to_string(endpoint.port);
    const int resolve_result = getaddrinfo(
        endpoint.host.c_str(), port.c_str(), &hints, &raw_addresses);
    if (resolve_result != 0) {
#ifdef _WIN32
        throw TcpMmsTransportError(
            "getaddrinfo failed with error " + std::to_string(resolve_result) + ".");
#else
        throw TcpMmsTransportError(
            std::string{"getaddrinfo failed: "} + gai_strerror(resolve_result) + ".");
#endif
    }
    std::unique_ptr<addrinfo, AddressInfoDeleter> addresses{raw_addresses};

    int last_error = 0;
    for (auto* address = addresses.get(); address != nullptr; address = address->ai_next) {
        if (stop_token.stop_requested()) {
            throw MmsTransportCancelledError("TCP MMS connect was cancelled.");
        }

        NativeSocket candidate = ::socket(
            address->ai_family, address->ai_socktype, address->ai_protocol);
        if (candidate == invalid_socket) {
            last_error = last_socket_error();
            continue;
        }

        try {
            set_non_blocking(candidate);
            const int result = ::connect(
                candidate,
                address->ai_addr,
                static_cast<SocketLength>(address->ai_addrlen));
            if (result != 0) {
                const int error = last_socket_error();
                if (!connect_in_progress(error)) {
                    last_error = error;
                    close_native_socket(candidate);
                    continue;
                }

                wait_socket_ready(
                    candidate,
                    false,
                    true,
                    deadline,
                    options_.cancellation_poll_interval,
                    stop_token,
                    "TCP MMS connect");

                int socket_error = 0;
                SocketLength socket_error_length =
                    static_cast<SocketLength>(sizeof(socket_error));
                auto* socket_error_bytes = reinterpret_cast<char*>(&socket_error);
                if (getsockopt(
                        candidate,
                        SOL_SOCKET,
                        SO_ERROR,
                        socket_error_bytes,
                        &socket_error_length) != 0) {
                    throw TcpMmsTransportError(socket_error_text(
                        "getsockopt(SO_ERROR)", last_socket_error()));
                }
                if (socket_error != 0) {
                    last_error = socket_error;
                    close_native_socket(candidate);
                    continue;
                }
            }

            if (options_.no_delay) {
                set_boolean_socket_option(
                    candidate, IPPROTO_TCP, TCP_NODELAY, true, "setsockopt(TCP_NODELAY)");
            }
            if (options_.keep_alive) {
                set_boolean_socket_option(
                    candidate, SOL_SOCKET, SO_KEEPALIVE, true, "setsockopt(SO_KEEPALIVE)");
            }

            impl_->socket = candidate;
            impl_->connected = true;
            return;
        } catch (...) {
            close_native_socket(candidate);
            throw;
        }
    }

    throw TcpMmsTransportError(socket_error_text(
        "TCP MMS connect to " + endpoint.host + ":" + port,
        last_error));
}

void TcpMmsByteTransport::send(
    const std::span<const std::uint8_t> bytes,
    const Deadline deadline,
    const std::stop_token stop_token) {
    if (!connected()) {
        throw TcpMmsTransportError("TCP MMS transport is not connected.");
    }

    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        wait_socket_ready(
            impl_->socket,
            false,
            true,
            deadline,
            options_.cancellation_poll_interval,
            stop_token,
            "TCP MMS send");

        const auto sent = socket_send(
            impl_->socket, bytes.data() + offset, bytes.size() - offset);
        if (sent > 0) {
            offset += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent == 0) {
            close();
            throw TcpMmsTransportError(
                "TCP MMS peer closed the connection during send.");
        }

        const int error = last_socket_error();
        if (interrupted(error) || would_block(error)) {
            continue;
        }
        close();
        throw TcpMmsTransportError(socket_error_text("TCP MMS send", error));
    }
}

std::vector<std::uint8_t> TcpMmsByteTransport::receive(
    const Deadline deadline,
    const std::stop_token stop_token) {
    if (!connected()) {
        throw TcpMmsTransportError("TCP MMS transport is not connected.");
    }

    for (;;) {
        wait_socket_ready(
            impl_->socket,
            true,
            false,
            deadline,
            options_.cancellation_poll_interval,
            stop_token,
            "TCP MMS receive");

        std::vector<std::uint8_t> bytes(options_.receive_chunk_size);
        const auto received = socket_receive(
            impl_->socket, bytes.data(), bytes.size());
        if (received > 0) {
            bytes.resize(static_cast<std::size_t>(received));
            return bytes;
        }
        if (received == 0) {
            close();
            throw TcpMmsTransportError(
                "Remote IEC 61850 peer closed the TCP connection.");
        }

        const int error = last_socket_error();
        if (interrupted(error) || would_block(error)) {
            continue;
        }
        close();
        throw TcpMmsTransportError(socket_error_text("TCP MMS receive", error));
    }
}

void TcpMmsByteTransport::close() noexcept {
    if (!impl_) {
        return;
    }
    const NativeSocket socket = impl_->socket;
    impl_->socket = invalid_socket;
    impl_->connected = false;
    shutdown_native_socket(socket);
    close_native_socket(socket);
}

bool TcpMmsByteTransport::connected() const noexcept {
    return impl_ && impl_->connected && impl_->socket != invalid_socket;
}

} // namespace ar::iec61850::mms
