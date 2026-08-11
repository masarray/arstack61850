// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/mms/static_server_session.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
namespace embedded = ar::iec61850::embedded;
namespace mms = ar::iec61850::mms;
namespace wire = ar::iec61850::wire;

std::atomic_bool g_stop{false};

void signal_handler(int) {
    g_stop.store(true, std::memory_order_relaxed);
}

struct SocketRuntime final {
#if defined(_WIN32)
    SocketRuntime() {
        WSADATA data{};
        if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("WSAStartup failed.");
        }
    }
    ~SocketRuntime() { ::WSACleanup(); }
#else
    SocketRuntime() = default;
#endif
};

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
#endif

void close_socket(const NativeSocket socket) noexcept {
    if (socket == kInvalidSocket) {
        return;
    }
#if defined(_WIN32)
    static_cast<void>(::closesocket(socket));
#else
    static_cast<void>(::close(socket));
#endif
}

[[nodiscard]] std::string socket_error_text() {
#if defined(_WIN32)
    return std::to_string(::WSAGetLastError());
#else
    return std::to_string(errno);
#endif
}

[[nodiscard]] NativeSocket create_listener(const std::uint16_t port) {
    const auto listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == kInvalidSocket) {
        throw std::runtime_error("socket() failed: " + socket_error_text());
    }

    int yes = 1;
#if defined(_WIN32)
    static_cast<void>(::setsockopt(
        listener,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char*>(&yes),
        static_cast<int>(sizeof(yes))));
#else
    static_cast<void>(::setsockopt(
        listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)));
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
#if defined(_WIN32)
    const auto address_size = static_cast<int>(sizeof(address));
#else
    const auto address_size = static_cast<socklen_t>(sizeof(address));
#endif
    if (::bind(
            listener,
            reinterpret_cast<const sockaddr*>(&address),
            address_size) != 0) {
        const auto error = socket_error_text();
        close_socket(listener);
        throw std::runtime_error("bind() failed: " + error);
    }
    if (::listen(listener, 4) != 0) {
        const auto error = socket_error_text();
        close_socket(listener);
        throw std::runtime_error("listen() failed: " + error);
    }
    return listener;
}

struct SocketStreamContext final {
    NativeSocket socket{kInvalidSocket};
};

[[nodiscard]] embedded::IoResult socket_receive(
    void* context,
    const std::span<std::uint8_t> destination) noexcept {
    if (context == nullptr || destination.empty()) {
        return {embedded::IoStatus::invalid_argument, 0U};
    }
    auto& stream = *static_cast<SocketStreamContext*>(context);
    if (stream.socket == kInvalidSocket) {
        return {embedded::IoStatus::closed, 0U};
    }

#if defined(_WIN32)
    const auto bounded = std::min<std::size_t>(
        destination.size(), static_cast<std::size_t>(INT_MAX));
    const auto count = ::recv(
        stream.socket,
        reinterpret_cast<char*>(destination.data()),
        static_cast<int>(bounded),
        0);
    if (count > 0) {
        return {embedded::IoStatus::ok, static_cast<std::size_t>(count)};
    }
    if (count == 0) {
        return {embedded::IoStatus::closed, 0U};
    }
    const auto error = ::WSAGetLastError();
    if (error == WSAEWOULDBLOCK || error == WSAEINTR) {
        return {embedded::IoStatus::would_block, 0U};
    }
    if (error == WSAETIMEDOUT) {
        return {embedded::IoStatus::timeout, 0U};
    }
#else
    const auto count = ::recv(stream.socket, destination.data(), destination.size(), 0);
    if (count > 0) {
        return {embedded::IoStatus::ok, static_cast<std::size_t>(count)};
    }
    if (count == 0) {
        return {embedded::IoStatus::closed, 0U};
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return {embedded::IoStatus::would_block, 0U};
    }
    if (errno == ETIMEDOUT) {
        return {embedded::IoStatus::timeout, 0U};
    }
#endif
    return {embedded::IoStatus::io_error, 0U};
}

[[nodiscard]] embedded::IoResult socket_send(
    void* context,
    const std::span<const std::uint8_t> bytes) noexcept {
    if (context == nullptr || bytes.empty()) {
        return {embedded::IoStatus::invalid_argument, 0U};
    }
    auto& stream = *static_cast<SocketStreamContext*>(context);
    if (stream.socket == kInvalidSocket) {
        return {embedded::IoStatus::closed, 0U};
    }

#if defined(_WIN32)
    const auto bounded = std::min<std::size_t>(
        bytes.size(), static_cast<std::size_t>(INT_MAX));
    const auto count = ::send(
        stream.socket,
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<int>(bounded),
        0);
    if (count > 0) {
        return {embedded::IoStatus::ok, static_cast<std::size_t>(count)};
    }
    if (count == 0) {
        return {embedded::IoStatus::closed, 0U};
    }
    const auto error = ::WSAGetLastError();
    if (error == WSAEWOULDBLOCK || error == WSAEINTR) {
        return {embedded::IoStatus::would_block, 0U};
    }
    if (error == WSAETIMEDOUT) {
        return {embedded::IoStatus::timeout, 0U};
    }
#else
    const auto count = ::send(stream.socket, bytes.data(), bytes.size(), 0);
    if (count > 0) {
        return {embedded::IoStatus::ok, static_cast<std::size_t>(count)};
    }
    if (count == 0) {
        return {embedded::IoStatus::closed, 0U};
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return {embedded::IoStatus::would_block, 0U};
    }
    if (errno == ETIMEDOUT) {
        return {embedded::IoStatus::timeout, 0U};
    }
#endif
    return {embedded::IoStatus::io_error, 0U};
}

struct CliOptions final {
    std::uint16_t port{102U};
    std::uint8_t digital_input_mask{};
    std::size_t maximum_connections{};
};

[[nodiscard]] std::uint32_t parse_u32(
    const std::string& option,
    const std::string& text,
    const std::uint32_t maximum) {
    std::size_t consumed = 0U;
    const auto value = std::stoull(text, &consumed, 0);
    if (consumed != text.size() || value > maximum) {
        throw std::invalid_argument(option + " is outside the supported range.");
    }
    return static_cast<std::uint32_t>(value);
}

void print_usage() {
    std::cout
        << "Usage: ariec61850_static_ied_server [options]\n\n"
        << "Options:\n"
        << "  --port N                  TCP listen port (default 102).\n"
        << "  --digital-input-mask N    GGIO1 Ind1..Ind8 bit mask (default 0).\n"
        << "  --max-connections N       Exit after N accepted TCP connections (default unlimited).\n"
        << "  -h, --help                Show this help.\n\n"
        << "Portable bounded static IEC 61850 MMS server for lab/interoperability work.\n"
        << "The tool exposes a fixed static object model; it does not claim IEC 61850 conformance.\n";
}

[[nodiscard]] CliOptions parse_cli(const int argc, char** argv) {
    CliOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "-h" || option == "--help") {
            print_usage();
            std::exit(0);
        }
        if (option == "--port" || option == "--digital-input-mask" ||
            option == "--max-connections") {
            if (++index >= argc) {
                throw std::invalid_argument(option + " requires a value.");
            }
            const std::string value = argv[index];
            if (option == "--port") {
                const auto parsed = parse_u32(option, value, 65'535U);
                if (parsed == 0U) {
                    throw std::invalid_argument("--port must be 1..65535.");
                }
                options.port = static_cast<std::uint16_t>(parsed);
            } else if (option == "--digital-input-mask") {
                options.digital_input_mask =
                    static_cast<std::uint8_t>(parse_u32(option, value, 0xFFU));
            } else {
                options.maximum_connections = static_cast<std::size_t>(
                    parse_u32(
                        option,
                        value,
                        std::numeric_limits<std::uint32_t>::max()));
            }
            continue;
        }
        throw std::invalid_argument("Unknown option: " + option);
    }
    return options;
}

struct EncodedValue final {
    std::span<const std::uint8_t> bytes;
};

[[nodiscard]] wire::EncodeResult read_encoded(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
    if (context == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto& value = *static_cast<const EncodedValue*>(context);
    if (destination.size() < value.bytes.size()) {
        return {
            wire::EncodeStatus::buffer_too_small,
            0U,
            value.bytes.size()};
    }
    std::copy(value.bytes.begin(), value.bytes.end(), destination.begin());
    return {
        wire::EncodeStatus::ok,
        value.bytes.size(),
        value.bytes.size()};
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
    const auto value = *static_cast<const std::uint8_t*>(context) != 0U;
    destination[0] = 0x83U;
    destination[1] = 0x01U;
    destination[2] = value ? 0xFFU : 0x00U;
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] std::vector<std::uint8_t> encode_ber_length(
    const std::size_t length) {
    if (length < 0x80U) {
        return {static_cast<std::uint8_t>(length)};
    }
    if (length <= 0xFFU) {
        return {0x81U, static_cast<std::uint8_t>(length)};
    }
    return {
        0x82U,
        static_cast<std::uint8_t>((length >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(length & 0xFFU)};
}

[[nodiscard]] std::vector<std::uint8_t> make_tlv(
    const std::uint8_t tag,
    const std::span<const std::uint8_t> content) {
    std::vector<std::uint8_t> bytes;
    const auto length = encode_ber_length(content.size());
    bytes.reserve(1U + length.size() + content.size());
    bytes.push_back(tag);
    bytes.insert(bytes.end(), length.begin(), length.end());
    bytes.insert(bytes.end(), content.begin(), content.end());
    return bytes;
}

[[nodiscard]] std::vector<std::uint8_t> concat(
    const std::initializer_list<std::span<const std::uint8_t>> parts) {
    std::size_t total = 0U;
    for (const auto part : parts) {
        total += part.size();
    }
    std::vector<std::uint8_t> result;
    result.reserve(total);
    for (const auto part : parts) {
        result.insert(result.end(), part.begin(), part.end());
    }
    return result;
}

[[nodiscard]] std::vector<std::uint8_t> build_single_status_ln_type(
    const std::string_view do_name) {
    constexpr std::array<std::uint8_t, 2U> boolean_type{0x83U, 0x00U};
    const auto da_name = make_tlv(
        0x1AU,
        std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>("stVal"), 5U});
    const auto da = make_tlv(0xA2U, concat({da_name, boolean_type}));
    const auto da_list = make_tlv(0xA2U, da);
    const auto do_name_tlv = make_tlv(
        0x1AU,
        std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(do_name.data()), do_name.size()});
    const auto do_entry = make_tlv(0xA2U, concat({do_name_tlv, da_list}));
    return make_tlv(0xA2U, do_entry);
}

[[nodiscard]] std::vector<std::uint8_t> build_ggio_type() {
    std::vector<std::uint8_t> do_entries;
    for (std::size_t index = 1U; index <= 8U; ++index) {
        const auto do_name = std::string{"Ind"} + std::to_string(index);
        const auto encoded = build_single_status_ln_type(do_name);
        const auto inner = std::span<const std::uint8_t>{encoded}.subspan(2U);
        do_entries.insert(do_entries.end(), inner.begin(), inner.end());
    }
    return make_tlv(0xA2U, do_entries);
}

struct ConnectionBuffers final {
    std::array<std::uint8_t, 32'768U> receive{};
    std::array<std::uint8_t, 32'768U> response{};
    std::array<std::uint8_t, 8'192U> workspace{};
};

void serve_connection(
    const NativeSocket socket,
    const mms::MmsStaticObjectTable& object_table,
    const mms::MmsStaticDataSetTable& data_sets,
    const std::uint64_t association_id) {
    const mms::MmsStaticApplicationDispatcher dispatcher{object_table, data_sets};

    mms::MmsStaticConnectionPolicy policy;
    policy.association_id = association_id;
    policy.owner_size = 8U;
    for (std::size_t index = 0U; index < policy.owner_size; ++index) {
        const auto shift = static_cast<unsigned>((policy.owner_size - 1U - index) * 8U);
        policy.owner[index] = static_cast<std::uint8_t>(
            (association_id >> shift) & 0xFFU);
    }

    mms::MmsStaticConnectionRuntime runtime{dispatcher, policy};
    SocketStreamContext socket_context{socket};
    const embedded::TcpByteStream stream{
        &socket_context,
        socket_send,
        socket_receive};
    ConnectionBuffers buffers{};
    mms::MmsStaticServerSession session{
        runtime,
        stream,
        {buffers.receive, buffers.response, buffers.workspace}};

    while (!g_stop.load(std::memory_order_relaxed)) {
        const auto result = session.poll_once();
        if (result.terminal()) {
            return;
        }
        if (result.status == mms::MmsStaticServerSessionStatus::would_block ||
            result.status == mms::MmsStaticServerSessionStatus::timed_out) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }
    runtime.close_transport();
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_cli(argc, argv);
        [[maybe_unused]] SocketRuntime socket_runtime;
        std::signal(SIGINT, signal_handler);
#if !defined(_WIN32)
        std::signal(SIGTERM, signal_handler);
#endif

        constexpr std::array<std::uint8_t, 2U> boolean_type{0x83U, 0x00U};
        std::array<std::uint8_t, 10U> values{};
        values[0] = 1U;
        values[1] = 1U;
        for (std::size_t index = 0U; index < 8U; ++index) {
            values[index + 2U] = static_cast<std::uint8_t>(
                (options.digital_input_mask >> index) & 0x01U);
        }

        const auto lln0_type = build_single_status_ln_type("Mod");
        const auto lphd1_type = build_single_status_ln_type("PhyHealth");
        const auto ggio1_type = build_ggio_type();
        constexpr std::array<std::uint8_t, 9U> healthy_ln_data{
            0xA2U, 0x07U, 0xA2U, 0x05U, 0xA2U, 0x03U, 0x83U, 0x01U, 0xFFU};
        std::array<std::uint8_t, 44U> ggio_data{};
        ggio_data[0] = 0xA2U;
        ggio_data[1] = 0x2AU;
        ggio_data[2] = 0xA2U;
        ggio_data[3] = 0x28U;
        for (std::size_t index = 0U; index < 8U; ++index) {
            const auto offset = 4U + index * 5U;
            ggio_data[offset] = 0xA2U;
            ggio_data[offset + 1U] = 0x03U;
            ggio_data[offset + 2U] = 0x83U;
            ggio_data[offset + 3U] = 0x01U;
            ggio_data[offset + 4U] =
                values[index + 2U] != 0U ? 0xFFU : 0x00U;
        }
        const std::array<EncodedValue, 3U> root_values{
            EncodedValue{healthy_ln_data},
            EncodedValue{healthy_ln_data},
            EncodedValue{ggio_data}};

        std::array<mms::MmsStaticObjectEntry, 13U> objects{};
        objects[0] = mms::MmsStaticObjectEntry{
            "ESP32S3IOLD0",
            "LLN0",
            lln0_type,
            read_encoded,
            &root_values[0]};
        objects[1] = mms::MmsStaticObjectEntry{
            "ESP32S3IOLD0",
            "LPHD1",
            lphd1_type,
            read_encoded,
            &root_values[1]};
        objects[2] = mms::MmsStaticObjectEntry{
            "ESP32S3IOLD0",
            "GGIO1",
            ggio1_type,
            read_encoded,
            &root_values[2]};
        objects[3] = mms::MmsStaticObjectEntry{
            "ESP32S3IOLD0",
            "LLN0$ST$Mod$stVal",
            boolean_type,
            read_boolean,
            &values[0]};
        objects[4] = mms::MmsStaticObjectEntry{
            "ESP32S3IOLD0",
            "LPHD1$ST$PhyHealth$stVal",
            boolean_type,
            read_boolean,
            &values[1]};
        constexpr std::array<std::string_view, 8U> leaf_items{
            "GGIO1$ST$Ind1$stVal",
            "GGIO1$ST$Ind2$stVal",
            "GGIO1$ST$Ind3$stVal",
            "GGIO1$ST$Ind4$stVal",
            "GGIO1$ST$Ind5$stVal",
            "GGIO1$ST$Ind6$stVal",
            "GGIO1$ST$Ind7$stVal",
            "GGIO1$ST$Ind8$stVal"};
        for (std::size_t index = 0U; index < leaf_items.size(); ++index) {
            objects[index + 5U] = mms::MmsStaticObjectEntry{
                "ESP32S3IOLD0",
                leaf_items[index],
                boolean_type,
                read_boolean,
                &values[index + 2U]};
        }

        constexpr std::array<mms::MmsStaticDataSetMember, 8U> data_set_members{{
            {"ESP32S3IOLD0", "GGIO1$ST$Ind1$stVal"},
            {"ESP32S3IOLD0", "GGIO1$ST$Ind2$stVal"},
            {"ESP32S3IOLD0", "GGIO1$ST$Ind3$stVal"},
            {"ESP32S3IOLD0", "GGIO1$ST$Ind4$stVal"},
            {"ESP32S3IOLD0", "GGIO1$ST$Ind5$stVal"},
            {"ESP32S3IOLD0", "GGIO1$ST$Ind6$stVal"},
            {"ESP32S3IOLD0", "GGIO1$ST$Ind7$stVal"},
            {"ESP32S3IOLD0", "GGIO1$ST$Ind8$stVal"}}};
        const std::array<mms::MmsStaticDataSetEntry, 1U> data_set_entries{{
            {"ESP32S3IOLD0", "LLN0$EventData", data_set_members, false}}};

        const mms::MmsStaticObjectTable object_table{objects};
        const mms::MmsStaticDataSetTable data_sets{data_set_entries};
        if (!object_table.valid() ||
            !data_sets.valid() ||
            !data_sets.valid_against(object_table)) {
            throw std::runtime_error("Static MMS server model is invalid.");
        }

        const auto listener = create_listener(options.port);
        std::cout << "STATIC_IED_SERVER_READY port=" << options.port
                  << " objects=" << objects.size()
                  << " datasets=" << data_set_entries.size() << '\n';
        std::cout.flush();

        std::size_t connection_count = 0U;
        while (!g_stop.load(std::memory_order_relaxed) &&
               (options.maximum_connections == 0U ||
                connection_count < options.maximum_connections)) {
            sockaddr_in peer{};
#if defined(_WIN32)
            int peer_size = static_cast<int>(sizeof(peer));
#else
            socklen_t peer_size = static_cast<socklen_t>(sizeof(peer));
#endif
            const auto client = ::accept(
                listener,
                reinterpret_cast<sockaddr*>(&peer),
                &peer_size);
            if (client == kInvalidSocket) {
                if (g_stop.load(std::memory_order_relaxed)) {
                    break;
                }
                std::cerr << "accept() failed: " << socket_error_text() << '\n';
                continue;
            }
            ++connection_count;
            std::cout << "CONNECTION_ACCEPTED count=" << connection_count << '\n';
            serve_connection(
                client,
                object_table,
                data_sets,
                static_cast<std::uint64_t>(connection_count));
            close_socket(client);
            std::cout << "CONNECTION_CLOSED count=" << connection_count << '\n';
        }

        close_socket(listener);
        std::cout << "STATIC_IED_SERVER_STOPPED connections=" << connection_count << '\n';
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Static IED server failed: " << exception.what() << '\n';
        return 2;
    }
}
