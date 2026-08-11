// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_data_set_table.hpp"
#include "ariec61850/mms/static_dispatcher.hpp"
#include "ariec61850/mms/static_report_connection.hpp"
#include "ariec61850/mms/static_server_session.hpp"
#include "ariec61850/mms/static_urcb_objects.hpp"
#include "ariec61850/mms/static_urcb_runtime.hpp"
#include "ariec61850/mms/services.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace ar::iec61850;

#ifdef _WIN32
using SocketHandle = SOCKET;
using SocketLength = int;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
using SocketLength = socklen_t;
constexpr SocketHandle kInvalidSocket = -1;
#endif

struct Options final {
    std::string bind_address{"0.0.0.0"};
    std::uint16_t port{8102U};
    std::uint8_t output_mask{};
    bool serve_once{};
};

class SocketSystem final {
public:
    SocketSystem() noexcept {
#ifdef _WIN32
        WSADATA data{};
        ready_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
        ready_ = true;
#endif
    }
    ~SocketSystem() {
#ifdef _WIN32
        if (ready_) WSACleanup();
#endif
    }
    SocketSystem(const SocketSystem&) = delete;
    SocketSystem& operator=(const SocketSystem&) = delete;
    [[nodiscard]] bool ready() const noexcept { return ready_; }
private:
    bool ready_{};
};

class OwnedSocket final {
public:
    OwnedSocket() noexcept = default;
    explicit OwnedSocket(SocketHandle handle) noexcept : handle_{handle} {}
    ~OwnedSocket() { reset(); }
    OwnedSocket(const OwnedSocket&) = delete;
    OwnedSocket& operator=(const OwnedSocket&) = delete;
    OwnedSocket(OwnedSocket&& other) noexcept : handle_{other.release()} {}
    OwnedSocket& operator=(OwnedSocket&& other) noexcept {
        if (this != &other) reset(other.release());
        return *this;
    }
    [[nodiscard]] SocketHandle get() const noexcept { return handle_; }
    [[nodiscard]] bool valid() const noexcept { return handle_ != kInvalidSocket; }
    [[nodiscard]] SocketHandle release() noexcept {
        const auto value = handle_;
        handle_ = kInvalidSocket;
        return value;
    }
    void reset(SocketHandle replacement = kInvalidSocket) noexcept {
        if (valid()) {
#ifdef _WIN32
            closesocket(handle_);
#else
            close(handle_);
#endif
        }
        handle_ = replacement;
    }
private:
    SocketHandle handle_{kInvalidSocket};
};

[[nodiscard]] int last_socket_error() noexcept {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

[[nodiscard]] bool is_would_block(int error) noexcept {
#ifdef _WIN32
    return error == WSAEWOULDBLOCK;
#else
    return error == EAGAIN || error == EWOULDBLOCK;
#endif
}

[[nodiscard]] bool is_timeout(int error) noexcept {
#ifdef _WIN32
    return error == WSAETIMEDOUT;
#else
    return error == ETIMEDOUT;
#endif
}

[[nodiscard]] bool is_interrupted(int error) noexcept {
#ifdef _WIN32
    return error == WSAEINTR;
#else
    return error == EINTR;
#endif
}

[[nodiscard]] embedded::IoResult socket_receive(
    void* context,
    std::span<std::uint8_t> destination) noexcept {
    const auto socket = *static_cast<const SocketHandle*>(context);
#ifdef _WIN32
    const auto requested = static_cast<int>(std::min<std::size_t>(
        destination.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
#else
    const auto requested = destination.size();
#endif
    const auto received = recv(
        socket, reinterpret_cast<char*>(destination.data()), requested, 0);
    if (received > 0) return {embedded::IoStatus::ok, static_cast<std::size_t>(received)};
    if (received == 0) return {embedded::IoStatus::closed, 0U};
    const auto error = last_socket_error();
    if (is_would_block(error)) return {embedded::IoStatus::would_block, 0U};
    if (is_timeout(error) || is_interrupted(error)) return {embedded::IoStatus::timeout, 0U};
    return {embedded::IoStatus::io_error, 0U};
}

[[nodiscard]] embedded::IoResult socket_send(
    void* context,
    std::span<const std::uint8_t> bytes) noexcept {
    const auto socket = *static_cast<const SocketHandle*>(context);
#ifdef _WIN32
    const auto requested = static_cast<int>(std::min<std::size_t>(
        bytes.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
#else
    const auto requested = bytes.size();
#endif
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    const auto sent = send(socket, reinterpret_cast<const char*>(bytes.data()), requested, flags);
    if (sent > 0) return {embedded::IoStatus::ok, static_cast<std::size_t>(sent)};
    if (sent == 0) return {embedded::IoStatus::closed, 0U};
    const auto error = last_socket_error();
    if (is_would_block(error)) return {embedded::IoStatus::would_block, 0U};
    if (is_timeout(error) || is_interrupted(error)) return {embedded::IoStatus::timeout, 0U};
    return {embedded::IoStatus::io_error, 0U};
}

[[nodiscard]] bool send_all(
    SocketHandle socket,
    std::span<const std::uint8_t> bytes) noexcept {
    std::size_t offset{};
    while (offset < bytes.size()) {
        auto socket_copy = socket;
        const auto result = socket_send(&socket_copy, bytes.subspan(offset));
        if (result.status != embedded::IoStatus::ok || result.bytes_transferred == 0U) return false;
        offset += result.bytes_transferred;
    }
    return true;
}

template <typename Integer>
[[nodiscard]] bool parse_integer(std::string_view text, Integer& output) noexcept {
    auto digits = text;
    int base = 10;
    if (digits.size() > 2U && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
        digits.remove_prefix(2U);
        base = 16;
    }
    unsigned int parsed{};
    const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), parsed, base);
    if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size() ||
        parsed > static_cast<unsigned int>(std::numeric_limits<Integer>::max())) {
        return false;
    }
    output = static_cast<Integer>(parsed);
    return true;
}

void print_help() {
    std::cout
        << "ARStack61850 bounded static URCB reporting server\n\n"
        << "Usage:\n"
        << "  ariec61850_mms_urcb_server [options]\n\n"
        << "Options:\n"
        << "  --bind ADDRESS   Bind address (default 0.0.0.0)\n"
        << "  --port PORT      TCP port (default 8102; use 102 for IEC 61850)\n"
        << "  --mask VALUE     Initial SPCSO1..8 Boolean status mask\n"
        << "  --once           Serve one TCP connection, then exit\n"
        << "  --help           Show this help\n\n"
        << "DataSet: ESP32S3IOLD0/LLN0.Outputs\n"
        << "URCB:    ESP32S3IOLD0/LLN0$RP$Outputs01\n"
        << "First live-report gate: reserve/enable the URCB and write GI=true.\n";
}

[[nodiscard]] bool parse_options(int argc, char** argv, Options& options, bool& help) {
    help = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--help" || argument == "-h") {
            help = true;
            return true;
        }
        if (argument == "--once") {
            options.serve_once = true;
            continue;
        }
        if (index + 1 >= argc) return false;
        const std::string_view value{argv[++index]};
        if (argument == "--bind") {
            options.bind_address.assign(value);
        } else if (argument == "--port") {
            if (!parse_integer(value, options.port) || options.port == 0U) return false;
        } else if (argument == "--mask") {
            if (!parse_integer(value, options.output_mask)) return false;
        } else {
            return false;
        }
    }
    return true;
}

[[nodiscard]] OwnedSocket create_listener(const Options& options) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;
    const auto service = std::to_string(options.port);
    addrinfo* addresses{};
    if (getaddrinfo(options.bind_address.c_str(), service.c_str(), &hints, &addresses) != 0) return {};
    OwnedSocket listener;
    for (auto* address = addresses; address != nullptr; address = address->ai_next) {
        OwnedSocket candidate{socket(address->ai_family, address->ai_socktype, address->ai_protocol)};
        if (!candidate.valid()) continue;
        constexpr int enabled = 1;
        static_cast<void>(setsockopt(candidate.get(), SOL_SOCKET, SO_REUSEADDR,
            reinterpret_cast<const char*>(&enabled), static_cast<SocketLength>(sizeof(enabled))));
        const auto address_length = static_cast<SocketLength>(address->ai_addrlen);
        if (bind(candidate.get(), address->ai_addr, address_length) == 0 &&
            listen(candidate.get(), 8) == 0) {
            listener = std::move(candidate);
            break;
        }
    }
    freeaddrinfo(addresses);
    return listener;
}

void set_client_timeouts(SocketHandle socket) noexcept {
#ifdef _WIN32
    constexpr DWORD timeout_ms = 1000U;
    static_cast<void>(setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeout_ms), static_cast<int>(sizeof(timeout_ms))));
    static_cast<void>(setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
        reinterpret_cast<const char*>(&timeout_ms), static_cast<int>(sizeof(timeout_ms))));
#else
    constexpr timeval timeout{1, 0};
    static_cast<void>(setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)));
    static_cast<void>(setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)));
#endif
}

[[nodiscard]] std::uint64_t monotonic_ms() noexcept {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count());
}

[[nodiscard]] std::uint64_t report_now_ms(const void*) noexcept {
    return monotonic_ms();
}

[[nodiscard]] wire::EncodeResult read_boolean(
    const void* context,
    std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 3U;
    if (context == nullptr) return {wire::EncodeStatus::value_out_of_range, 0U, required};
    if (destination.size() < required) return {wire::EncodeStatus::buffer_too_small, 0U, required};
    destination[0] = 0x83U;
    destination[1] = 0x01U;
    destination[2] = *static_cast<const std::uint8_t*>(context) != 0U ? 0xFFU : 0x00U;
    return {wire::EncodeStatus::ok, required, required};
}

struct EncodedValue final { std::span<const std::uint8_t> bytes; };

[[nodiscard]] wire::EncodeResult read_encoded(
    const void* context,
    std::span<std::uint8_t> destination) noexcept {
    if (context == nullptr) return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    const auto bytes = static_cast<const EncodedValue*>(context)->bytes;
    if (destination.size() < bytes.size()) return {wire::EncodeStatus::buffer_too_small, 0U, bytes.size()};
    std::copy(bytes.begin(), bytes.end(), destination.begin());
    return {wire::EncodeStatus::ok, bytes.size(), bytes.size()};
}

[[nodiscard]] mms::MmsTypeSpecification scalar(mms::MmsTypeKind kind, std::string name) {
    mms::MmsTypeSpecification result;
    result.kind = kind;
    result.name = std::move(name);
    return result;
}

[[nodiscard]] mms::MmsTypeSpecification structure(
    std::string name,
    std::vector<mms::MmsTypeSpecification> children) {
    mms::MmsTypeSpecification result;
    result.kind = mms::MmsTypeKind::structure;
    result.name = std::move(name);
    result.children = std::move(children);
    return result;
}

[[nodiscard]] std::vector<std::uint8_t> build_ggio_type() {
    std::vector<mms::MmsTypeSpecification> statuses;
    statuses.reserve(8U);
    for (std::size_t index = 0U; index < 8U; ++index) {
        statuses.push_back(structure("SPCSO" + std::to_string(index + 1U), {
            scalar(mms::MmsTypeKind::boolean, "stVal")
        }));
    }
    return mms::MmsServiceCodec::encode_type_specification(structure("", {
        structure("ST", std::move(statuses))
    }));
}

[[nodiscard]] bool serve_client(
    SocketHandle client,
    const mms::MmsStaticApplicationDispatcher& dispatcher,
    mms::MmsStaticUrcbRuntime& urcbs) {
    mms::MmsStaticConnectionRuntime runtime{dispatcher};
    auto socket_copy = client;
    embedded::TcpByteStream stream{&socket_copy, socket_send, socket_receive};
    std::array<std::uint8_t, 65'535U> receive{};
    std::array<std::uint8_t, 65'535U> response{};
    std::array<std::uint8_t, 65'535U> workspace{};
    std::array<std::uint8_t, 65'535U> report_frame{};
    std::array<std::uint8_t, 65'535U> report_workspace{};
    mms::MmsStaticServerSession session{runtime, stream, {receive, response, workspace}};

    for (;;) {
        const auto session_result = session.poll_once();
        switch (session_result.status) {
        case mms::MmsStaticServerSessionStatus::peer_closed:
            return true;
        case mms::MmsStaticServerSessionStatus::transport_error:
        case mms::MmsStaticServerSessionStatus::protocol_error:
        case mms::MmsStaticServerSessionStatus::receive_buffer_full:
        case mms::MmsStaticServerSessionStatus::invalid_configuration:
            return false;
        default:
            break;
        }

        if (session.pending_output_bytes() != 0U) continue;

        const auto report = mms::MmsStaticReportConnection::poll(
            runtime, urcbs, monotonic_ms(), {}, report_frame, report_workspace);
        if (report.status == mms::MmsStaticReportConnectionStatus::response_ready) {
            if (!send_all(client, std::span<const std::uint8_t>{
                    report_frame.data(), report.bytes_written})) {
                return false;
            }
            std::cout << "MMS_URCB_REPORT_SENT index=" << report.control_block_index
                      << " sqNum=" << static_cast<unsigned>(report.sequence_number)
                      << " reason=" << static_cast<unsigned>(report.reason)
                      << " bytes=" << report.bytes_written << '\n';
        } else if (report.status == mms::MmsStaticReportConnectionStatus::report_encode_failed ||
                   report.status == mms::MmsStaticReportConnectionStatus::response_buffer_too_small ||
                   report.status == mms::MmsStaticReportConnectionStatus::workspace_too_small) {
            std::cerr << "MMS_URCB_REPORT_ERROR status=" << static_cast<unsigned>(report.status)
                      << " urcbStatus=" << static_cast<unsigned>(report.urcb_status) << '\n';
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    bool help{};
    if (!parse_options(argc, argv, options, help)) return 2;
    if (help) {
        print_help();
        return 0;
    }

    SocketSystem sockets;
    if (!sockets.ready()) return 3;

    constexpr std::string_view domain{"ESP32S3IOLD0"};
    constexpr std::array<std::uint8_t, 2U> boolean_type{0x83U, 0x00U};
    const auto ggio_type = build_ggio_type();
    const EncodedValue ggio_root{ggio_type};

    std::array<std::uint8_t, 8U> values{};
    std::array<std::string, 8U> status_items{};
    std::array<mms::MmsStaticObjectEntry, 9U> base_objects{};
    base_objects[0] = {domain, "GGIO1", ggio_type, read_encoded, &ggio_root};
    for (std::size_t index = 0U; index < 8U; ++index) {
        values[index] = static_cast<std::uint8_t>((options.output_mask >> index) & 0x01U);
        status_items[index] = "GGIO1$ST$SPCSO" + std::to_string(index + 1U) + "$stVal";
        base_objects[index + 1U] = {
            domain, status_items[index], boolean_type, read_boolean, &values[index]
        };
    }
    const mms::MmsStaticObjectTable base_table{base_objects};
    if (!base_table.valid()) return 4;

    std::array<mms::MmsStaticDataSetMember, 8U> members{};
    for (std::size_t index = 0U; index < members.size(); ++index) {
        members[index] = {domain, status_items[index]};
    }
    const std::array<mms::MmsStaticDataSetEntry, 1U> data_sets{{
        {domain, "LLN0$Outputs", members, false}
    }};
    const mms::MmsStaticDataSetTable data_set_table{data_sets};
    if (!data_set_table.valid_against(base_table)) return 4;

    constexpr std::array<std::uint8_t, 2U> opt_fields{0x5CU, 0x80U};
    const std::array<mms::MmsStaticUrcbDefinition, 1U> definitions{{
        {domain, "LLN0$RP$Outputs01", "ARSTACK-Outputs01",
         domain, "LLN0$Outputs", 1U, opt_fields, 0U, 0U, 0U}
    }};
    std::array<mms::MmsStaticUrcbState, 1U> states{};
    mms::MmsStaticUrcbRuntime urcbs{definitions, states, base_table, data_set_table};
    if (!urcbs.initialize()) return 4;

    std::array<mms::MmsStaticObjectEntry, 20U> object_storage{};
    std::array<mms::MmsStaticUrcbObjectContext, 11U> context_storage{};
    std::array<char, 512U> name_storage{};
    mms::MmsStaticUrcbObjectBank object_bank{
        urcbs, base_objects, object_storage, context_storage, name_storage,
        report_now_ms, nullptr};
    if (!object_bank.initialize()) return 4;

    mms::MmsStaticDispatchPolicy policy;
    policy.maximum_write_variables = 1U;
    const mms::MmsStaticApplicationDispatcher dispatcher{
        object_bank.table(), data_set_table, policy};

    auto listener = create_listener(options);
    if (!listener.valid()) {
        std::cerr << "Unable to bind/listen on " << options.bind_address << ':' << options.port << ".\n";
        return 5;
    }

    std::cout << "MMS_URCB_SERVER_READY bind=" << options.bind_address
              << " port=" << options.port
              << " dataset=ESP32S3IOLD0/LLN0.Outputs"
              << " urcb=ESP32S3IOLD0/LLN0$RP$Outputs01"
              << " members=8 mask=" << static_cast<unsigned>(options.output_mask) << '\n';

    do {
        static_cast<void>(urcbs.set_enabled(0U, false, monotonic_ms()));
        static_cast<void>(urcbs.set_reserved(0U, false));
        sockaddr_storage peer{};
        SocketLength peer_size = static_cast<SocketLength>(sizeof(peer));
        OwnedSocket client{accept(listener.get(), reinterpret_cast<sockaddr*>(&peer), &peer_size)};
        if (!client.valid()) {
            const auto error = last_socket_error();
            if (is_interrupted(error)) continue;
            return 6;
        }
        set_client_timeouts(client.get());
        std::cout << "MMS_URCB_CLIENT_ACCEPTED\n";
        const auto clean_close = serve_client(client.get(), dispatcher, urcbs);
        const auto state = urcbs.state(0U);
        std::cout << "MMS_URCB_STATE enabled=" << (state != nullptr && state->enabled ? "true" : "false")
                  << " reserved=" << (state != nullptr && state->reserved ? "true" : "false")
                  << " sqNum=" << (state != nullptr ? static_cast<unsigned>(state->sequence_number) : 0U)
                  << '\n';
        static_cast<void>(urcbs.set_enabled(0U, false, monotonic_ms()));
        static_cast<void>(urcbs.set_reserved(0U, false));
        std::cout << (clean_close ? "MMS_URCB_CLIENT_CLOSED" : "MMS_URCB_CLIENT_ABORTED") << '\n';
    } while (!options.serve_once);

    return 0;
}
