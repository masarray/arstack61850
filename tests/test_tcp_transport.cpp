// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/tcp_transport.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

using namespace ar::iec61850;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            throw std::runtime_error( \
                std::string{"CHECK failed: "} + #condition + \
                " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (false)

template <typename Exception, typename Callable>
void check_throws(Callable&& callable) {
    try {
        std::invoke(std::forward<Callable>(callable));
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error("Expected exception was not thrown.");
}

void cancelled_connect_is_observable() {
    mms::TcpMmsByteTransport transport;
    std::stop_source source;
    source.request_stop();
    check_throws<mms::MmsTransportCancelledError>([&] {
        transport.connect(
            {"127.0.0.1", 9U},
            mms::MmsByteTransport::Clock::now() + std::chrono::seconds{1},
            source.get_token());
    });
    CHECK(!transport.connected());
}

#ifndef _WIN32

class LoopbackServer final {
public:
    LoopbackServer() {
        socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_ < 0) {
            throw std::runtime_error("Unable to create loopback server socket.");
        }
        const int enabled = 1;
        static_cast<void>(setsockopt(
            socket_, SOL_SOCKET, SO_REUSEADDR, &enabled, static_cast<socklen_t>(sizeof(enabled))));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(
                socket_,
                reinterpret_cast<const sockaddr*>(&address),
                sizeof(address)) != 0 ||
            ::listen(socket_, 1) != 0) {
            ::close(socket_);
            throw std::runtime_error("Unable to bind/listen loopback server socket.");
        }

        socklen_t length = sizeof(address);
        if (getsockname(
                socket_,
                reinterpret_cast<sockaddr*>(&address),
                &length) != 0) {
            ::close(socket_);
            throw std::runtime_error("Unable to read loopback server port.");
        }
        port_ = ntohs(address.sin_port);
    }

    ~LoopbackServer() {
        if (socket_ >= 0) {
            ::close(socket_);
        }
    }

    [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

    void serve_once() {
        const int client = ::accept(socket_, nullptr, nullptr);
        if (client < 0) {
            throw std::runtime_error("Loopback server accept failed.");
        }

        std::array<std::uint8_t, 4> request{};
        std::size_t offset = 0U;
        while (offset < request.size()) {
            const auto read = ::recv(
                client,
                request.data() + offset,
                request.size() - offset,
                0);
            if (read <= 0) {
                ::close(client);
                throw std::runtime_error("Loopback server receive failed.");
            }
            offset += static_cast<std::size_t>(read);
        }
        if (request != std::array<std::uint8_t, 4>{'p', 'i', 'n', 'g'}) {
            ::close(client);
            throw std::runtime_error("Loopback server received unexpected bytes.");
        }

        const std::array<std::uint8_t, 4> response{'p', 'o', 'n', 'g'};
        const auto written = ::send(
            client, response.data(), response.size(), 0);
        ::close(client);
        if (written < 0 || static_cast<std::size_t>(written) != response.size()) {
            throw std::runtime_error("Loopback server send failed.");
        }
    }

private:
    int socket_{-1};
    std::uint16_t port_{};
};

void non_blocking_loopback_round_trip() {
    LoopbackServer server;
    std::exception_ptr server_error;
    std::jthread server_thread([&] {
        try {
            server.serve_once();
        } catch (...) {
            server_error = std::current_exception();
        }
    });

    mms::TcpMmsTransportOptions options;
    options.cancellation_poll_interval = std::chrono::milliseconds{5};
    mms::TcpMmsByteTransport transport{options};
    const auto deadline =
        mms::MmsByteTransport::Clock::now() + std::chrono::seconds{2};
    transport.connect({"127.0.0.1", server.port()}, deadline, {});
    CHECK(transport.connected());

    const std::array<std::uint8_t, 4> request{'p', 'i', 'n', 'g'};
    transport.send(request, deadline, {});
    const auto response = transport.receive(deadline, {});
    CHECK(response == std::vector<std::uint8_t>({'p', 'o', 'n', 'g'}));
    transport.close();
    CHECK(!transport.connected());

    server_thread.join();
    if (server_error) {
        std::rethrow_exception(server_error);
    }
}

#endif

} // namespace

int main() {
    try {
        cancelled_connect_is_observable();
#ifndef _WIN32
        non_blocking_loopback_round_trip();
#endif
        std::cout << "TCP MMS transport tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
