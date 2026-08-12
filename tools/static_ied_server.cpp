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
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <span>
#include <sstream>
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
#include <sys/select.h>
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

enum class SocketWaitStatus : std::uint8_t {
    ready,
    timeout,
    interrupted,
    error,
};

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

[[nodiscard]] bool socket_interrupted() noexcept {
#if defined(_WIN32)
    return ::WSAGetLastError() == WSAEINTR;
#else
    return errno == EINTR;
#endif
}

[[nodiscard]] SocketWaitStatus wait_socket(
    const NativeSocket socket,
    const bool for_read,
    const std::uint32_t timeout_ms) noexcept {
    fd_set read_set;
    fd_set write_set;
    FD_ZERO(&read_set);
    FD_ZERO(&write_set);
    if (for_read) {
        FD_SET(socket, &read_set);
    } else {
        FD_SET(socket, &write_set);
    }

    timeval timeout{};
    timeout.tv_sec = static_cast<long>(timeout_ms / 1'000U);
    timeout.tv_usec = static_cast<long>((timeout_ms % 1'000U) * 1'000U);
#if defined(_WIN32)
    const auto result = ::select(
        0,
        for_read ? &read_set : nullptr,
        for_read ? nullptr : &write_set,
        nullptr,
        &timeout);
#else
    const auto result = ::select(
        socket + 1,
        for_read ? &read_set : nullptr,
        for_read ? nullptr : &write_set,
        nullptr,
        &timeout);
#endif
    if (result > 0) {
        return SocketWaitStatus::ready;
    }
    if (result == 0) {
        return SocketWaitStatus::timeout;
    }
    return socket_interrupted()
        ? SocketWaitStatus::interrupted
        : SocketWaitStatus::error;
}

[[nodiscard]] NativeSocket create_listener(
    const std::string_view bind_address,
    const std::uint16_t port) {
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
    if (bind_address == "0.0.0.0" || bind_address.empty()) {
        address.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (::inet_pton(
                   AF_INET,
                   std::string{bind_address}.c_str(),
                   &address.sin_addr) != 1) {
        close_socket(listener);
        throw std::runtime_error(
            "Invalid IPv4 bind address: " + std::string{bind_address});
    }
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

    const auto readiness = wait_socket(stream.socket, true, 100U);
    if (readiness == SocketWaitStatus::timeout) {
        return {embedded::IoStatus::timeout, 0U};
    }
    if (readiness == SocketWaitStatus::interrupted) {
        return {embedded::IoStatus::would_block, 0U};
    }
    if (readiness != SocketWaitStatus::ready) {
        return {embedded::IoStatus::io_error, 0U};
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

    const auto readiness = wait_socket(stream.socket, false, 100U);
    if (readiness == SocketWaitStatus::timeout) {
        return {embedded::IoStatus::timeout, 0U};
    }
    if (readiness == SocketWaitStatus::interrupted) {
        return {embedded::IoStatus::would_block, 0U};
    }
    if (readiness != SocketWaitStatus::ready) {
        return {embedded::IoStatus::io_error, 0U};
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
    std::string bind_address{"0.0.0.0"};
    std::string model_manifest;
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
        << "  --host IPv4               IPv4 listen address (default 0.0.0.0).\n"
        << "  --port N                  TCP listen port (default 102).\n"
        << "  --model-manifest PATH     Host model manifest emitted by the Qt simulator.\n"
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
        if (option == "--host" || option == "--model-manifest" ||
            option == "--port" || option == "--digital-input-mask" ||
            option == "--max-connections") {
            if (++index >= argc) {
                throw std::invalid_argument(option + " requires a value.");
            }
            const std::string value = argv[index];
            if (option == "--host") {
                options.bind_address = value;
            } else if (option == "--model-manifest") {
                options.model_manifest = value;
            } else if (option == "--port") {
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
        0x80U,
        std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>("stVal"), 5U});
    const auto da_type = make_tlv(0xA1U, boolean_type);
    const auto da = make_tlv(0x30U, concat({da_name, da_type}));
    const auto da_list = make_tlv(0xA1U, da);
    const auto data_object_type = make_tlv(0xA2U, da_list);
    const auto do_name_tlv = make_tlv(
        0x80U,
        std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(do_name.data()), do_name.size()});
    const auto do_type = make_tlv(0xA1U, data_object_type);
    const auto do_entry = make_tlv(0x30U, concat({do_name_tlv, do_type}));
    const auto do_list = make_tlv(0xA1U, do_entry);
    return make_tlv(0xA2U, do_list);
}

[[nodiscard]] std::vector<std::uint8_t> build_ggio_type() {
    std::vector<std::uint8_t> do_entries;
    for (std::size_t index = 1U; index <= 8U; ++index) {
        const auto do_name = std::string{"Ind"} + std::to_string(index);
        const auto encoded = build_single_status_ln_type(do_name);
        // A single-status LN is A2/A1/SEQUENCE. Reuse the complete named DO
        // component while building the GGIO structure component list.
        const auto structure_fields = std::span<const std::uint8_t>{encoded}.subspan(2U);
        const auto component_list = structure_fields.subspan(2U);
        do_entries.insert(do_entries.end(), component_list.begin(), component_list.end());
    }
    return make_tlv(0xA2U, make_tlv(0xA1U, do_entries));
}

struct ConnectionBuffers final {
    std::array<std::uint8_t, 32'768U> receive{};
    std::array<std::uint8_t, 32'768U> response{};
    std::array<std::uint8_t, 8'192U> workspace{};
};

struct ManifestModel final {
    std::vector<std::string> domains;
    std::vector<std::string> items;
    std::vector<mms::MmsStaticObjectEntry> objects;
    std::size_t declared_entries{};
};

[[nodiscard]] ManifestModel load_manifest_model(
    const std::string& path,
    const std::span<const std::uint8_t> type_specification,
    const EncodedValue& value) {
    ManifestModel model;
    if (path.empty()) {
        return model;
    }
    std::ifstream input{path};
    if (!input) {
        throw std::runtime_error("Could not open model manifest: " + path);
    }

    std::vector<std::pair<std::string, std::string>> names;
    std::set<std::pair<std::string, std::string>> unique_names;
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("LN\t", 0U) != 0U) {
            continue;
        }
        std::istringstream fields{line};
        std::string kind;
        std::string domain;
        std::string item;
        std::getline(fields, kind, '\t');
        std::getline(fields, domain, '\t');
        std::getline(fields, item, '\t');
        if (domain.empty() || item.empty()) {
            continue;
        }
        ++model.declared_entries;
        if (unique_names.emplace(domain, item).second &&
            names.size() < mms::MmsStaticObjectTable::maximum_objects) {
            names.emplace_back(std::move(domain), std::move(item));
        }
    }
    if (names.empty()) {
        throw std::runtime_error(
            "Model manifest contains no usable logical-node entries.");
    }

    model.domains.reserve(names.size());
    model.items.reserve(names.size());
    for (const auto& [domain, item] : names) {
        model.domains.push_back(domain);
        model.items.push_back(item);
    }
    model.objects.reserve(names.size());
    for (std::size_t index = 0U; index < names.size(); ++index) {
        model.objects.push_back(mms::MmsStaticObjectEntry{
            model.domains[index],
            model.items[index],
            type_specification,
            read_encoded,
            &value});
    }
    return model;
}

[[nodiscard]] std::string_view connection_state_text(
    const mms::MmsStaticConnectionState state) noexcept {
    switch (state) {
    case mms::MmsStaticConnectionState::awaiting_cotp_connect: return "tcp";
    case mms::MmsStaticConnectionState::awaiting_association: return "cotp";
    case mms::MmsStaticConnectionState::established: return "mms";
    case mms::MmsStaticConnectionState::closed: return "closed";
    case mms::MmsStaticConnectionState::fault: return "fault";
    }
    return "unknown";
}

[[nodiscard]] std::string_view service_text(
    const mms::MmsWireConfirmedService service) noexcept {
    switch (service) {
    case mms::MmsWireConfirmedService::get_name_list: return "GetNameList";
    case mms::MmsWireConfirmedService::identify: return "Identify";
    case mms::MmsWireConfirmedService::read: return "Read";
    case mms::MmsWireConfirmedService::write: return "Write";
    case mms::MmsWireConfirmedService::get_variable_access_attributes:
        return "GetVariableAccessAttributes";
    case mms::MmsWireConfirmedService::get_named_variable_list_attributes:
        return "GetNamedVariableListAttributes";
    case mms::MmsWireConfirmedService::file_directory: return "FileDirectory";
    case mms::MmsWireConfirmedService::unknown: return "Unknown";
    }
    return "Unknown";
}

[[nodiscard]] std::string peer_address(const sockaddr_in& peer) {
    std::array<char, INET_ADDRSTRLEN> text{};
    if (::inet_ntop(AF_INET, &peer.sin_addr, text.data(), text.size()) == nullptr) {
        return "unknown";
    }
    return std::string{text.data()} + ':' + std::to_string(ntohs(peer.sin_port));
}

void serve_connection(
    const NativeSocket socket,
    const mms::MmsStaticObjectTable& object_table,
    const mms::MmsStaticDataSetTable& data_sets,
    const std::uint64_t association_id,
    const std::string_view remote) {
    mms::MmsStaticDispatchPolicy dispatch_policy;
    const mms::MmsStaticApplicationDispatcher dispatcher{
        object_table, data_sets, dispatch_policy};

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

    auto previous_state = runtime.state();
    std::size_t total_received = 0U;
    std::size_t total_sent = 0U;
    while (!g_stop.load(std::memory_order_relaxed)) {
        const auto result = session.poll_once();
        total_received += result.bytes_received;
        total_sent += result.bytes_sent;
        const auto current_state = runtime.state();
        if (current_state != previous_state) {
            std::cout << "IEDSIM_EVENT kind=protocol_stage association="
                      << association_id << " remote=" << remote
                      << " stage=" << connection_state_text(current_state) << '\n';
            std::cout.flush();
            previous_state = current_state;
        }
        if (result.application_service != mms::MmsWireConfirmedService::unknown) {
            std::cout << "IEDSIM_EVENT kind=mms_service association="
                      << association_id << " remote=" << remote
                      << " service=" << service_text(result.application_service)
                      << " invoke=" << result.invoke_id
                      << " accepted="
                      << (result.status == mms::MmsStaticServerSessionStatus::application_rejected
                              ? "false" : "true")
                      << '\n';
            std::cout.flush();
        }
        if (result.terminal()) {
            std::cout << "IEDSIM_EVENT kind=client_closed association="
                      << association_id << " remote=" << remote
                      << " rx=" << total_received << " tx=" << total_sent
                      << " state=" << connection_state_text(runtime.state()) << '\n';
            std::cout.flush();
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

        const auto manifest_type = build_single_status_ln_type("Mod");
        const EncodedValue manifest_value{healthy_ln_data};
        auto manifest_model = load_manifest_model(
            options.model_manifest, manifest_type, manifest_value);

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

        const auto object_span = manifest_model.objects.empty()
            ? std::span<const mms::MmsStaticObjectEntry>{objects}
            : std::span<const mms::MmsStaticObjectEntry>{manifest_model.objects};
        const auto data_set_span = manifest_model.objects.empty()
            ? std::span<const mms::MmsStaticDataSetEntry>{data_set_entries}
            : std::span<const mms::MmsStaticDataSetEntry>{};
        const mms::MmsStaticObjectTable object_table{object_span};
        const mms::MmsStaticDataSetTable data_sets{data_set_span};
        if (!object_table.valid() ||
            !data_sets.valid() ||
            !data_sets.valid_against(object_table)) {
            throw std::runtime_error("Static MMS server model is invalid.");
        }

        const auto listener = create_listener(options.bind_address, options.port);
        std::set<std::string_view> domain_names;
        for (const auto& object : object_span) domain_names.insert(object.domain);
        const auto truncated = manifest_model.declared_entries > object_span.size()
            ? manifest_model.declared_entries - object_span.size()
            : 0U;
        std::cout << "IEDSIM_EVENT kind=server_ready bind="
                  << options.bind_address << " port=" << options.port
                  << " objects=" << object_span.size()
                  << " domains=" << domain_names.size()
                  << " datasets=" << data_set_span.size()
                  << " truncated=" << truncated
                  << " profile=iedscout" << '\n';
        std::cout.flush();

        std::size_t connection_count = 0U;
        while (!g_stop.load(std::memory_order_relaxed) &&
               (options.maximum_connections == 0U ||
                connection_count < options.maximum_connections)) {
            const auto readiness = wait_socket(listener, true, 200U);
            if (readiness == SocketWaitStatus::timeout ||
                readiness == SocketWaitStatus::interrupted) {
                continue;
            }
            if (readiness != SocketWaitStatus::ready) {
                throw std::runtime_error(
                    "select(listener) failed: " + socket_error_text());
            }

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
                if (g_stop.load(std::memory_order_relaxed) || socket_interrupted()) {
                    continue;
                }
                std::cerr << "accept() failed: " << socket_error_text() << '\n';
                continue;
            }
            ++connection_count;
            const auto remote = peer_address(peer);
            std::cout << "IEDSIM_EVENT kind=client_connected association="
                      << connection_count << " remote=" << remote << '\n';
            std::cout.flush();
            serve_connection(
                client,
                object_table,
                data_sets,
                static_cast<std::uint64_t>(connection_count),
                remote);
            close_socket(client);
        }

        close_socket(listener);
        std::cout << "IEDSIM_EVENT kind=server_stopped connections="
                  << connection_count << '\n';
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Static IED server failed: " << exception.what() << '\n';
        return 2;
    }
}
