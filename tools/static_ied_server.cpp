// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/mms/static_server_session.hpp"
#include "ariec61850/mms/static_report_connection.hpp"
#include "ariec61850/mms/static_brcb_connection.hpp"
#include "ariec61850/mms/static_brcb_objects.hpp"
#include "ariec61850/mms/static_brcb_runtime.hpp"
#include "ariec61850/mms/static_urcb_objects.hpp"
#include "ariec61850/mms/static_urcb_runtime.hpp"
#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/mms/services.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <climits>
#include <csignal>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <map>
#include <iostream>
#include <limits>
#include <mutex>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
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
std::mutex g_live_command_mutex;
std::deque<std::string> g_live_commands;

void start_live_command_reader() {
    std::thread([] {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (!line.starts_with("IEDSIM_CMD ")) continue;
            std::lock_guard lock{g_live_command_mutex};
            g_live_commands.push_back(std::move(line));
        }
    }).detach();
}

[[nodiscard]] std::vector<std::string> take_live_commands() {
    std::lock_guard lock{g_live_command_mutex};
    std::vector<std::string> result;
    result.reserve(g_live_commands.size());
    while (!g_live_commands.empty()) {
        result.push_back(std::move(g_live_commands.front()));
        g_live_commands.pop_front();
    }
    return result;
}

// Desktop/lab simulator profile. These bounds are deliberately local to this
// host executable; the strict embedded MmsStaticObjectTable/DataSetTable limits
// remain unchanged for deterministic MCU builds.
constexpr std::size_t kHostMaximumManifestObjects = 65'536U;
constexpr std::size_t kHostMaximumManifestDataSets = 4'096U;

[[nodiscard]] bool host_identifier_valid(const std::string_view value) noexcept {
    if (value.empty() || value.size() > mms::MmsServiceSpanCodec::maximum_identifier_bytes) return false;
    for (const char ch : value) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte == 0U || byte > 0x7FU) return false;
    }
    return true;
}

[[nodiscard]] bool host_manifest_tables_valid(
    const mms::MmsStaticObjectTable& objects,
    const mms::MmsStaticDataSetTable& data_sets) noexcept {
    const auto object_span = objects.objects();
    if (object_span.empty() || object_span.size() > kHostMaximumManifestObjects) return false;
    std::set<std::pair<std::string_view, std::string_view>> object_names;
    for (const auto& object : object_span) {
        if (!host_identifier_valid(object.domain) || !host_identifier_valid(object.item) ||
            object.type_specification.empty() || object.read == nullptr ||
            !object_names.emplace(object.domain, object.item).second) return false;
    }

    const auto data_set_span = data_sets.data_sets();
    if (data_set_span.size() > kHostMaximumManifestDataSets) return false;
    std::set<std::pair<std::string_view, std::string_view>> data_set_names;
    for (const auto& data_set : data_set_span) {
        if (!host_identifier_valid(data_set.domain) || !host_identifier_valid(data_set.item) ||
            data_set.members.empty() ||
            data_set.members.size() > mms::MmsDataSetSpanCodec::maximum_members ||
            !data_set_names.emplace(data_set.domain, data_set.item).second) return false;
        std::set<std::pair<std::string_view, std::string_view>> member_names;
        for (const auto& member : data_set.members) {
            if (!host_identifier_valid(member.domain) || !host_identifier_valid(member.item) ||
                !member_names.emplace(member.domain, member.item).second) return false;
            const mms::MmsObjectNameView name{
                mms::MmsObjectNameViewKind::domain_specific,
                std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(member.domain.data()), member.domain.size()},
                std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(member.item.data()), member.item.size()}};
            if (objects.find(name) == nullptr) return false;
        }
    }
    return true;
}

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
    std::array<std::uint8_t, 32'768U> report_response{};
    std::array<std::uint8_t, 32'768U> report_workspace{};
    std::array<std::uint8_t, 32'768U> brcb_capture_encode{};
    std::array<std::uint8_t, 16'384U> brcb_capture_workspace{};
};

struct ManifestValue final {
    std::string domain;
    std::string item;
    std::string raw_type;
    std::string normalized_type;
    std::string type_signature;
    std::string text;
    mms::MmsTypeSpecification type;
    std::optional<mms::MmsDataValue> data;
    std::vector<std::uint8_t> type_specification;
    std::vector<std::uint8_t> encoded;
    std::string quality{"Good"};
    std::string origin{"scl"};
    std::uint64_t timestamp_ms{};
    std::uint64_t live_revision{};
    bool mms_writable{};
    bool root{};
};

struct ManifestTypeNode final {
    std::vector<std::string> child_names;
    std::vector<ManifestTypeNode> children;
    std::optional<std::size_t> value_index;
};

[[nodiscard]] ManifestTypeNode& manifest_child(
    ManifestTypeNode& node, const std::string& name) {
    for (std::size_t index = 0U; index < node.child_names.size(); ++index) {
        if (node.child_names[index] == name) return node.children[index];
    }
    node.child_names.push_back(name);
    node.children.emplace_back();
    return node.children.back();
}

struct ManifestDataSetStorage final {
    std::string domain;
    std::string item;
    std::vector<std::pair<std::string, std::string>> member_names;
    std::vector<mms::MmsStaticDataSetMember> members;
};

struct ManifestReportControl final {
    std::string domain;
    std::string item;
    bool buffered{};
    std::string report_id;
    std::string data_set_domain;
    std::string data_set_item;
    std::uint32_t conf_rev{1U};
    std::uint32_t buffer_time_ms{};
    std::uint32_t integrity_period_ms{};
    bool indexed{};
    std::uint8_t trigger_options{};
    std::array<std::uint8_t, 2U> optional_fields{};
};

struct ManifestModel final {
    std::string path;
    std::uint64_t revision{};
    std::vector<ManifestValue> values;
    std::vector<mms::MmsStaticObjectEntry> objects;
    std::vector<ManifestTypeNode> root_trees;
    std::vector<std::size_t> root_value_indices;
    std::unordered_map<std::string, std::size_t> value_indices;
    std::vector<ManifestDataSetStorage> data_set_storage;
    std::vector<mms::MmsStaticDataSetEntry> data_sets;
    std::vector<ManifestReportControl> report_controls;
    std::uint64_t live_revision{};
    std::uint64_t logical_time_ms{};
    std::size_t declared_entries{};
};

ManifestModel* g_active_manifest_model{};

[[nodiscard]] std::vector<std::string> split_fields(
    const std::string_view text,
    const char delimiter) {
    std::vector<std::string> result;
    std::size_t offset{};
    while (offset <= text.size()) {
        const auto end = text.find(delimiter, offset);
        result.emplace_back(text.substr(
            offset,
            end == std::string_view::npos ? text.size() - offset : end - offset));
        if (end == std::string_view::npos) break;
        offset = end + 1U;
    }
    return result;
}

[[nodiscard]] char hex_digit(const std::uint8_t value) noexcept {
    return static_cast<char>(value < 10U ? '0' + value : 'a' + (value - 10U));
}

[[nodiscard]] std::string hex_encode(const std::string_view text) {
    std::string result;
    result.reserve(text.size() * 2U);
    for (const auto ch : text) {
        const auto byte = static_cast<std::uint8_t>(static_cast<unsigned char>(ch));
        result.push_back(hex_digit(static_cast<std::uint8_t>((byte >> 4U) & 0x0FU)));
        result.push_back(hex_digit(static_cast<std::uint8_t>(byte & 0x0FU)));
    }
    return result;
}

[[nodiscard]] std::uint8_t hex_value(const char ch) {
    if (ch >= '0' && ch <= '9') return static_cast<std::uint8_t>(ch - '0');
    if (ch >= 'a' && ch <= 'f') return static_cast<std::uint8_t>(10 + ch - 'a');
    if (ch >= 'A' && ch <= 'F') return static_cast<std::uint8_t>(10 + ch - 'A');
    throw std::runtime_error("Invalid live-state hex field.");
}

[[nodiscard]] std::string hex_decode(const std::string_view text) {
    if ((text.size() % 2U) != 0U) throw std::runtime_error("Odd live-state hex field.");
    std::string result;
    result.reserve(text.size() / 2U);
    for (std::size_t index = 0U; index < text.size(); index += 2U) {
        result.push_back(static_cast<char>(
            (hex_value(text[index]) << 4U) | hex_value(text[index + 1U])));
    }
    return result;
}

[[nodiscard]] std::string upper_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

[[nodiscard]] bool text_boolean(const std::string_view text) noexcept {
    return text == "1" || text == "true" || text == "TRUE" ||
           text == "on" || text == "ON" || text == "closed" || text == "CLOSED";
}

[[nodiscard]] std::int64_t text_integer(const std::string& text) noexcept {
    const auto upper = upper_copy(text);
    if (upper == "INTERMEDIATE-STATE") return 0;
    if (upper == "OFF" || upper == "OPEN") return 1;
    if (upper == "ON" || upper == "CLOSED") return 2;
    if (upper == "BAD-STATE") return 3;
    try {
        return std::stoll(text);
    } catch (...) {
        return 0;
    }
}

[[nodiscard]] mms::MmsTypeSpecification manifest_type(
    const std::string& raw_type,
    const std::string& normalized_type,
    std::string name = {}) {
    const auto raw = upper_copy(raw_type);
    const auto normalized = upper_copy(normalized_type);
    mms::MmsTypeSpecification result;
    result.name = std::move(name);
    if (normalized == "ENUMERATION" || raw.find("ENUM") != std::string::npos) {
        result.kind = mms::MmsTypeKind::integer;
        result.size = 32U;
    } else if (normalized == "BOOLEAN" || raw.find("BOOL") != std::string::npos) {
        result.kind = mms::MmsTypeKind::boolean;
    } else if (normalized == "QUALITY") {
        result.kind = mms::MmsTypeKind::bit_string;
        result.size = 13U;
    } else if (normalized == "TIMESTAMP") {
        result.kind = mms::MmsTypeKind::utc_time;
    } else if (raw.find("FLOAT64") != std::string::npos) {
        result.kind = mms::MmsTypeKind::floating_point;
        result.size = 64U;
        result.exponent_width = 11U;
    } else if (raw.find("FLOAT") != std::string::npos) {
        result.kind = mms::MmsTypeKind::floating_point;
        result.size = 32U;
        result.exponent_width = 8U;
    } else if (raw.find("INT") != std::string::npos &&
               (raw.ends_with('U') || raw.find("UINT") != std::string::npos)) {
        result.kind = mms::MmsTypeKind::unsigned_integer;
        result.size = 32U;
    } else if (normalized == "NUMBER" || raw.find("INT") != std::string::npos) {
        result.kind = mms::MmsTypeKind::integer;
        result.size = 32U;
    } else {
        result.kind = mms::MmsTypeKind::visible_string;
        result.size = 255U;
    }
    return result;
}

[[nodiscard]] std::uint32_t signature_size(
    const std::string_view text, const std::string_view prefix) {
    if (!text.starts_with(prefix)) {
        throw std::runtime_error("Invalid exact MMS type signature: " + std::string{text});
    }
    const auto suffix = text.substr(prefix.size());
    if (suffix.empty()) {
        throw std::runtime_error("Missing size in exact MMS type signature: " + std::string{text});
    }
    const auto value = std::stoull(std::string{suffix});
    if (value == 0U || value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("Invalid size in exact MMS type signature: " + std::string{text});
    }
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] mms::MmsTypeSpecification exact_manifest_type(
    const std::string& signature,
    std::string name = {}) {
    mms::MmsTypeSpecification result;
    result.name = std::move(name);
    if (signature == "boolean") {
        result.kind = mms::MmsTypeKind::boolean;
        return result;
    }
    if (signature == "utc-time") {
        result.kind = mms::MmsTypeKind::utc_time;
        return result;
    }
    if (signature.starts_with("integer:")) {
        result.kind = mms::MmsTypeKind::integer;
        result.size = signature_size(signature, "integer:");
        return result;
    }
    if (signature.starts_with("unsigned-integer:")) {
        result.kind = mms::MmsTypeKind::unsigned_integer;
        result.size = signature_size(signature, "unsigned-integer:");
        return result;
    }
    if (signature.starts_with("bit-string:")) {
        result.kind = mms::MmsTypeKind::bit_string;
        result.size = signature_size(signature, "bit-string:");
        return result;
    }
    if (signature.starts_with("octet-string:")) {
        result.kind = mms::MmsTypeKind::octet_string;
        result.size = signature_size(signature, "octet-string:");
        return result;
    }
    if (signature.starts_with("visible-string:")) {
        result.kind = mms::MmsTypeKind::visible_string;
        result.size = signature_size(signature, "visible-string:");
        return result;
    }
    if (signature.starts_with("mms-string:")) {
        result.kind = mms::MmsTypeKind::mms_string;
        result.size = signature_size(signature, "mms-string:");
        return result;
    }
    if (signature.starts_with("floating-point:")) {
        const auto parts = split_fields(signature, ':');
        if (parts.size() != 3U) {
            throw std::runtime_error("Invalid floating-point exact MMS type signature: " + signature);
        }
        result.kind = mms::MmsTypeKind::floating_point;
        result.size = static_cast<std::uint32_t>(std::stoul(parts[1]));
        result.exponent_width = static_cast<std::uint32_t>(std::stoul(parts[2]));
        return result;
    }
    if (signature.starts_with("array:")) {
        const auto first = signature.find(':', 6U);
        if (first == std::string::npos || first + 1U >= signature.size()) {
            throw std::runtime_error("Invalid array exact MMS type signature: " + signature);
        }
        const auto count = std::stoull(signature.substr(6U, first - 6U));
        if (count == 0U || count > 65'535U) {
            throw std::runtime_error("Invalid array bound in exact MMS type signature: " + signature);
        }
        result.kind = mms::MmsTypeKind::array;
        result.size = static_cast<std::uint32_t>(count);
        result.children.push_back(exact_manifest_type(signature.substr(first + 1U)));
        return result;
    }
    throw std::runtime_error("Unsupported exact MMS type signature: " + signature);
}

[[nodiscard]] mms::MmsDataValue manifest_data(
    const mms::MmsTypeSpecification& type,
    const std::string& text) {
    switch (type.kind) {
    case mms::MmsTypeKind::array: {
        if (type.children.size() != 1U) return mms::MmsDataValue::array({});
        std::vector<mms::MmsDataValue> children;
        children.reserve(type.size.value_or(0U));
        for (std::uint32_t index = 0U; index < type.size.value_or(0U); ++index) {
            children.push_back(manifest_data(type.children.front(), text));
        }
        return mms::MmsDataValue::array(std::move(children));
    }
    case mms::MmsTypeKind::boolean:
        return mms::MmsDataValue::boolean(text_boolean(text));
    case mms::MmsTypeKind::bit_string: {
        const auto bits = type.size.value_or(0U);
        const auto bytes = static_cast<std::size_t>((bits + 7U) / 8U);
        std::vector<std::uint8_t> raw(bytes, 0U);
        const auto unused = static_cast<std::uint8_t>(bytes == 0U ? 0U : bytes * 8U - bits);
        return mms::MmsDataValue::bit_string(unused, raw);
    }
    case mms::MmsTypeKind::integer:
        return mms::MmsDataValue::integer(text_integer(text));
    case mms::MmsTypeKind::unsigned_integer:
        return mms::MmsDataValue::unsigned_integer(
            static_cast<std::uint64_t>(std::max<std::int64_t>(0, text_integer(text))));
    case mms::MmsTypeKind::floating_point:
        try {
            return type.size.value_or(32U) == 64U
                ? mms::MmsDataValue::floating_point(std::stod(text))
                : mms::MmsDataValue::floating_point(std::stof(text));
        } catch (...) {
            return type.size.value_or(32U) == 64U
                ? mms::MmsDataValue::floating_point(0.0)
                : mms::MmsDataValue::floating_point(0.0F);
        }
    case mms::MmsTypeKind::octet_string: {
        std::vector<std::uint8_t> bytes(type.size.value_or(0U), 0U);
        return mms::MmsDataValue::octet_string(bytes);
    }
    case mms::MmsTypeKind::mms_string:
        return mms::MmsDataValue::mms_string(text == "---" ? std::string{} : text);
    case mms::MmsTypeKind::utc_time:
        return mms::MmsDataValue::utc_time(
            mms::Iec61850UtcTime{std::chrono::system_clock::time_point{}, 0U});
    default:
        return mms::MmsDataValue::visible_string(text == "---" ? std::string{} : text);
    }
}

void encode_manifest_value(ManifestValue& value) {
    value.type = value.type_signature.empty()
        ? manifest_type(value.raw_type, value.normalized_type)
        : exact_manifest_type(value.type_signature);
    value.data = manifest_data(value.type, value.text);
    value.type_specification = mms::MmsServiceCodec::encode_type_specification(value.type);
    value.encoded = mms::MmsDataCodec::encode(*value.data);
}

[[nodiscard]] wire::EncodeResult read_manifest_value(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
    if (context == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto& value = *static_cast<const ManifestValue*>(context);
    if (destination.size() < value.encoded.size()) {
        return {
            wire::EncodeStatus::buffer_too_small,
            0U,
            value.encoded.size()};
    }
    std::copy(value.encoded.begin(), value.encoded.end(), destination.begin());
    return {wire::EncodeStatus::ok, value.encoded.size(), value.encoded.size()};
}

[[nodiscard]] mms::MmsTypeSpecification node_type(
    const ManifestTypeNode& node,
    const ManifestModel& model,
    std::string name) {
    if (node.children.empty() && node.value_index.has_value()) {
        auto result = model.values[*node.value_index].type;
        result.name = std::move(name);
        return result;
    }
    mms::MmsTypeSpecification result;
    result.kind = mms::MmsTypeKind::structure;
    result.name = std::move(name);
    result.children.reserve(node.children.size());
    for (std::size_t index = 0U; index < node.children.size(); ++index) {
        result.children.push_back(
            node_type(node.children[index], model, node.child_names[index]));
    }
    return result;
}

[[nodiscard]] mms::MmsDataValue node_data(
    const ManifestTypeNode& node,
    const ManifestModel& model) {
    if (node.children.empty() && node.value_index.has_value()) {
        return *model.values[*node.value_index].data;
    }
    std::vector<mms::MmsDataValue> children;
    children.reserve(node.children.size());
    for (const auto& child : node.children) {
        children.push_back(node_data(child, model));
    }
    return mms::MmsDataValue::structure(std::move(children));
}

void rebuild_manifest_roots(ManifestModel& model) {
    for (std::size_t index = 0U; index < model.root_trees.size(); ++index) {
        const auto value_index = model.root_value_indices[index];
        const auto& tree = model.root_trees[index];
        if (tree.children.empty()) continue;
        auto& root = model.values[value_index];
        root.type = node_type(tree, model, {});
        root.data = node_data(tree, model);
        root.type_specification = mms::MmsServiceCodec::encode_type_specification(root.type);
        root.encoded = mms::MmsDataCodec::encode(*root.data);
    }
}

[[nodiscard]] std::uint64_t manifest_revision(const std::string& header) noexcept {
    const auto fields = split_fields(header, '\t');
    if (fields.size() < 3U || fields[0] != "ARSTACK_IED_MODEL") return 0U;
    try {
        return std::stoull(fields[2]);
    } catch (...) {
        return 0U;
    }
}

[[nodiscard]] std::string object_key(
    const std::string_view domain,
    const std::string_view item) {
    std::string result;
    result.reserve(domain.size() + item.size() + 1U);
    result.append(domain);
    result.push_back('\n');
    result.append(item);
    return result;
}

[[nodiscard]] bool integer_text_valid(
    const std::string& text,
    const std::optional<std::uint32_t> bits,
    const bool is_unsigned) noexcept {
    const auto upper = upper_copy(text);
    if (!is_unsigned &&
        (upper == "INTERMEDIATE-STATE" || upper == "OFF" || upper == "ON" ||
         upper == "OPEN" || upper == "CLOSED" || upper == "BAD-STATE")) {
        return true;
    }
    try {
        std::size_t consumed{};
        if (is_unsigned) {
            if (!text.empty() && text.front() == '-') return false;
            const auto value = std::stoull(text, &consumed, 10);
            if (consumed != text.size()) return false;
            if (bits.has_value() && *bits < 64U) {
                const auto maximum = (std::uint64_t{1U} << *bits) - 1U;
                return value <= maximum;
            }
            return true;
        }
        const auto value = std::stoll(text, &consumed, 10);
        if (consumed != text.size()) return false;
        if (bits.has_value() && *bits > 0U && *bits < 64U) {
            const auto magnitude = std::int64_t{1} << (*bits - 1U);
            return value >= -magnitude && value <= magnitude - 1;
        }
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool live_text_valid(
    const mms::MmsTypeSpecification& type,
    const std::string& text) noexcept {
    switch (type.kind) {
    case mms::MmsTypeKind::boolean: {
        const auto upper = upper_copy(text);
        return upper == "TRUE" || upper == "FALSE" || upper == "1" || upper == "0" ||
            upper == "ON" || upper == "OFF" || upper == "OPEN" || upper == "CLOSED";
    }
    case mms::MmsTypeKind::integer:
        return integer_text_valid(text, type.size, false);
    case mms::MmsTypeKind::unsigned_integer:
        return integer_text_valid(text, type.size, true);
    case mms::MmsTypeKind::floating_point:
        try {
            std::size_t consumed{};
            static_cast<void>(std::stod(text, &consumed));
            return consumed == text.size();
        } catch (...) {
            return false;
        }
    case mms::MmsTypeKind::visible_string:
    case mms::MmsTypeKind::mms_string:
        return !type.size.has_value() || text.size() <= *type.size;
    default:
        return false;
    }
}

[[nodiscard]] bool live_data_compatible(
    const mms::MmsTypeSpecification& type,
    const mms::MmsDataValue& data) noexcept {
    switch (type.kind) {
    case mms::MmsTypeKind::boolean: return data.kind() == mms::MmsDataKind::boolean;
    case mms::MmsTypeKind::integer: return data.kind() == mms::MmsDataKind::integer;
    case mms::MmsTypeKind::unsigned_integer:
        return data.kind() == mms::MmsDataKind::unsigned_integer;
    case mms::MmsTypeKind::floating_point:
        return data.kind() == mms::MmsDataKind::floating_point;
    case mms::MmsTypeKind::visible_string:
        return data.kind() == mms::MmsDataKind::visible_string;
    case mms::MmsTypeKind::mms_string: return data.kind() == mms::MmsDataKind::mms_string;
    case mms::MmsTypeKind::bit_string: return data.kind() == mms::MmsDataKind::bit_string;
    case mms::MmsTypeKind::octet_string:
        return data.kind() == mms::MmsDataKind::octet_string;
    case mms::MmsTypeKind::utc_time: return data.kind() == mms::MmsDataKind::utc_time;
    case mms::MmsTypeKind::array: return data.kind() == mms::MmsDataKind::array;
    case mms::MmsTypeKind::structure: return data.kind() == mms::MmsDataKind::structure;
    default: return false;
    }
}

[[nodiscard]] std::string canonical_live_text(
    const ManifestValue& value,
    const mms::MmsDataValue& data,
    const std::string_view preferred) {
    if (!preferred.empty()) return std::string{preferred};
    if (upper_copy(value.normalized_type) == "ENUMERATION" &&
        data.kind() == mms::MmsDataKind::integer) {
        if (const auto* integer = std::get_if<std::int64_t>(&data.value())) {
            switch (*integer) {
            case 0: return "intermediate-state";
            case 1: return "off";
            case 2: return "on";
            case 3: return "bad-state";
            default: break;
            }
        }
    }
    return mms::MmsDataCodec::to_display_string(data);
}

void notify_active_brcb_mutation(
    std::string_view domain, std::string_view item) noexcept;

void emit_live_state(const ManifestValue& value, const std::uint64_t request_id) {
    std::cout << "IEDSIM_EVENT kind=value_state request=" << request_id
              << " domain=" << hex_encode(value.domain)
              << " item=" << hex_encode(value.item)
              << " value=" << hex_encode(value.text)
              << " quality=" << hex_encode(value.quality)
              << " origin=" << hex_encode(value.origin)
              << " timestamp_ms=" << value.timestamp_ms
              << " revision=" << value.live_revision << '\n';
    std::cout.flush();
}

void emit_live_rejected(const std::uint64_t request_id, const std::string_view reason) {
    std::cout << "IEDSIM_EVENT kind=value_rejected request=" << request_id
              << " reason=" << hex_encode(reason) << '\n';
    std::cout.flush();
}

[[nodiscard]] bool apply_live_data(
    ManifestModel& model,
    const std::size_t value_index,
    mms::MmsDataValue data,
    const std::string_view preferred_text,
    std::string quality,
    std::string origin,
    const std::uint64_t request_id,
    const std::optional<std::uint64_t> expected_revision) {
    if (value_index >= model.values.size()) return false;
    auto& value = model.values[value_index];
    if (value.root || !live_data_compatible(value.type, data)) return false;
    if (expected_revision.has_value() && value.live_revision != *expected_revision) return false;

    ++model.live_revision;
    ++model.logical_time_ms;
    value.data = std::move(data);
    value.encoded = mms::MmsDataCodec::encode(*value.data);
    value.text = canonical_live_text(value, *value.data, preferred_text);
    value.quality = std::move(quality);
    value.origin = std::move(origin);
    value.timestamp_ms = model.logical_time_ms;
    value.live_revision = model.live_revision;
    rebuild_manifest_roots(model);
    for (const auto root_index : model.root_value_indices) {
        model.objects[root_index].type_specification =
            model.values[root_index].type_specification;
    }
    emit_live_state(value, request_id);
    notify_active_brcb_mutation(value.domain, value.item);
    return true;
}

[[nodiscard]] mms::MmsStaticWriteResult write_manifest_value(
    void* context,
    const std::span<const std::uint8_t> encoded_data) noexcept {
    if (context == nullptr || g_active_manifest_model == nullptr) return {false, 10U};
    auto& value = *static_cast<ManifestValue*>(context);
    if (!value.mms_writable) return {false, 3U};
    try {
        const auto decoded = mms::MmsDataCodec::decode_all(encoded_data);
        if (decoded.size() != 1U || !live_data_compatible(value.type, decoded.front())) {
            return {false, 3U};
        }
        const auto found = g_active_manifest_model->value_indices.find(
            object_key(value.domain, value.item));
        if (found == g_active_manifest_model->value_indices.end()) return {false, 10U};
        if (!apply_live_data(
                *g_active_manifest_model,
                found->second,
                decoded.front(),
                {},
                "Good",
                "mms-write",
                0U,
                std::nullopt)) {
            return {false, 10U};
        }
        return {true, 0U};
    } catch (...) {
        return {false, 10U};
    }
}

struct LiveCommand final {
    std::uint64_t request{};
    std::string domain;
    std::string item;
    std::string value;
    std::string quality{"Good"};
    std::string origin{"gui"};
    std::optional<std::uint64_t> expected_revision;
};

[[nodiscard]] LiveCommand parse_live_command(const std::string& line) {
    if (!line.starts_with("IEDSIM_CMD ")) throw std::runtime_error("Invalid live command prefix.");
    std::map<std::string, std::string> fields;
    std::istringstream stream{line.substr(11U)};
    std::string token;
    while (stream >> token) {
        const auto separator = token.find('=');
        if (separator == std::string::npos || separator == 0U) continue;
        fields.emplace(token.substr(0U, separator), token.substr(separator + 1U));
    }
    if (fields["kind"] != "set") throw std::runtime_error("Unsupported live command kind.");
    LiveCommand command;
    command.request = std::stoull(fields.at("request"));
    command.domain = hex_decode(fields.at("domain"));
    command.item = hex_decode(fields.at("item"));
    command.value = hex_decode(fields.at("value"));
    command.quality = hex_decode(fields.at("quality"));
    command.origin = hex_decode(fields.at("origin"));
    if (const auto expected = fields.find("expected"); expected != fields.end()) {
        command.expected_revision = std::stoull(expected->second);
    }
    return command;
}

void drain_live_commands(ManifestModel& model) {
    for (const auto& line : take_live_commands()) {
        std::uint64_t request_id{};
        try {
            const auto command = parse_live_command(line);
            request_id = command.request;
            const auto found = model.value_indices.find(object_key(command.domain, command.item));
            if (found == model.value_indices.end()) {
                emit_live_rejected(request_id, "unknown-object");
                continue;
            }
            auto& value = model.values[found->second];
            if (command.expected_revision.has_value() &&
                value.live_revision != *command.expected_revision) {
                emit_live_rejected(request_id, "stale-revision");
                continue;
            }
            if (!live_text_valid(value.type, command.value)) {
                emit_live_rejected(request_id, "invalid-value");
                continue;
            }
            auto data = manifest_data(value.type, command.value);
            if (!apply_live_data(
                    model,
                    found->second,
                    std::move(data),
                    command.value,
                    command.quality,
                    command.origin,
                    request_id,
                    command.expected_revision)) {
                emit_live_rejected(request_id, "mutation-failed");
            }
        } catch (const std::exception& exception) {
            emit_live_rejected(request_id, exception.what());
        }
    }
}

[[nodiscard]] ManifestModel load_manifest_model(
    const std::string& path,
    const std::span<const std::uint8_t> fallback_type_specification,
    const EncodedValue& fallback_value) {
    ManifestModel model;
    model.path = path;
    if (path.empty()) return model;

    std::ifstream input{path};
    if (!input) throw std::runtime_error("Could not open model manifest: " + path);

    struct ParsedObject final {
        std::string domain;
        std::string item;
        std::string raw_type;
        std::string normalized_type;
        std::string type_signature;
        std::string text;
    };
    struct ParsedDataSetMember final {
        std::string domain;
        std::string item;
        std::string member_domain;
        std::string member_item;
    };
    std::vector<std::pair<std::string, std::string>> roots;
    std::vector<ParsedObject> parsed_objects;
    std::vector<ParsedDataSetMember> parsed_members;
    std::vector<ManifestReportControl> parsed_rcbs;
    std::set<std::pair<std::string, std::string>> unique_roots;
    std::set<std::pair<std::string, std::string>> unique_objects;
    std::set<std::pair<std::string, std::string>> writable_objects;
    std::string line;
    std::uint32_t manifest_version{};
    if (std::getline(input, line)) {
        const auto header_fields = split_fields(line, '\t');
        model.revision = manifest_revision(line);
        if (header_fields.size() >= 2U) {
            try { manifest_version = static_cast<std::uint32_t>(std::stoul(header_fields[1])); }
            catch (...) { manifest_version = 0U; }
        }
    }
    while (std::getline(input, line)) {
        const auto fields = split_fields(line, '\t');
        if (fields.size() >= 3U && fields[0] == "LN") {
            ++model.declared_entries;
            if (!fields[1].empty() && !fields[2].empty() &&
                unique_roots.emplace(fields[1], fields[2]).second) {
                roots.emplace_back(fields[1], fields[2]);
            }
        } else if (fields[0] == "OBJ" && fields.size() >= 6U) {
            ++model.declared_entries;
            if (fields[1].empty() || fields[2].empty() ||
                !unique_objects.emplace(fields[1], fields[2]).second) continue;
            if (manifest_version >= 4U) {
                if (fields.size() < 7U || fields[5].empty()) {
                    throw std::runtime_error("Manifest v4 OBJ is missing exact MMS type metadata.");
                }
                parsed_objects.push_back({
                    fields[1], fields[2], fields[3], fields[4], fields[5], fields[6]});
            } else {
                parsed_objects.push_back({
                    fields[1], fields[2], fields[3], fields[4], {}, fields[5]});
            }
        } else if (fields.size() >= 3U && fields[0] == "MUT") {
            if (!fields[1].empty() && !fields[2].empty()) {
                writable_objects.emplace(fields[1], fields[2]);
            }
        } else if (fields.size() >= 5U && fields[0] == "DS") {
            parsed_members.push_back({fields[1], fields[2], fields[3], fields[4]});
        } else if (fields.size() >= 11U && fields[0] == "RCB") {
            ManifestReportControl rcb;
            rcb.domain = fields[1];
            rcb.item = fields[2];
            rcb.buffered = fields[3] == "1";
            rcb.report_id = fields[4];
            rcb.data_set_domain = fields[5];
            rcb.data_set_item = fields[6];
            try { rcb.conf_rev = static_cast<std::uint32_t>(std::stoul(fields[7])); } catch (...) {}
            try { rcb.buffer_time_ms = static_cast<std::uint32_t>(std::stoul(fields[8])); } catch (...) {}
            try { rcb.integrity_period_ms = static_cast<std::uint32_t>(std::stoul(fields[9])); } catch (...) {}
            rcb.indexed = fields[10] == "1";
            if (fields.size() >= 12U) {
                try { rcb.trigger_options = static_cast<std::uint8_t>(std::stoul(fields[11])); } catch (...) {}
            }
            if (fields.size() >= 13U) {
                try { rcb.optional_fields[0] = static_cast<std::uint8_t>(std::stoul(fields[12])); } catch (...) {}
            }
            if (fields.size() >= 14U) {
                try { rcb.optional_fields[1] = static_cast<std::uint8_t>(std::stoul(fields[13])); } catch (...) {}
            }
            if (!rcb.domain.empty() && !rcb.item.empty()) parsed_rcbs.push_back(std::move(rcb));
        }
    }

    const auto add_rcb_object = [&](const ManifestReportControl& rcb,
                                    const std::string& suffix,
                                    const std::string& signature,
                                    const std::string& text) {
        const auto item = rcb.item + "$" + suffix;
        if (!unique_objects.emplace(rcb.domain, item).second) return;
        parsed_objects.push_back({rcb.domain, item, {}, {}, signature, text});
    };
    for (const auto& rcb : parsed_rcbs) {
        const auto data_set_ref = rcb.data_set_domain.empty()
            ? rcb.data_set_item
            : rcb.data_set_domain + "/" + rcb.data_set_item;
        add_rcb_object(rcb, "RptID", "visible-string:255", rcb.report_id);
        add_rcb_object(rcb, "RptEna", "boolean", "false");
        add_rcb_object(rcb, "DatSet", "visible-string:255", data_set_ref);
        add_rcb_object(rcb, "ConfRev", "unsigned-integer:32", std::to_string(rcb.conf_rev));
        add_rcb_object(rcb, "OptFlds", "bit-string:10", "0");
        add_rcb_object(rcb, "BufTm", "unsigned-integer:32", std::to_string(rcb.buffer_time_ms));
        add_rcb_object(rcb, "TrgOps", "bit-string:6", "0");
        add_rcb_object(rcb, "IntgPd", "unsigned-integer:32", std::to_string(rcb.integrity_period_ms));
        if (rcb.buffered) {
            add_rcb_object(rcb, "PurgeBuf", "boolean", "false");
            add_rcb_object(rcb, "EntryID", "octet-string:8", "");
            add_rcb_object(rcb, "TimeOfEntry", "utc-time", "1970-01-01 00:00:00.000");
        } else {
            add_rcb_object(rcb, "Resv", "boolean", "false");
            add_rcb_object(rcb, "GI", "boolean", "false");
            add_rcb_object(rcb, "SqNum", "unsigned-integer:32", "0");
        }
        ++model.declared_entries;
    }
    model.report_controls = parsed_rcbs;

    // Manifest v4 is object-driven: derive logical-node roots from the first
    // item component instead of requiring a separate LN scaffold.
    if (manifest_version >= 4U) {
        for (const auto& object : parsed_objects) {
            const auto parts = split_fields(object.item, '$');
            if (parts.empty() || parts[0].empty()) continue;
            if (unique_roots.emplace(object.domain, parts[0]).second) {
                roots.emplace_back(object.domain, parts[0]);
            }
        }
    }
    if (roots.empty()) {
        throw std::runtime_error("Model manifest contains no usable structural MMS objects.");
    }

    model.values.reserve(std::min<std::size_t>(
        kHostMaximumManifestObjects, roots.size() + parsed_objects.size()));
    model.root_trees.reserve(roots.size());
    model.root_value_indices.reserve(roots.size());
    std::unordered_map<std::string, std::size_t> root_indices;
    for (const auto& [domain, item] : roots) {
        if (model.values.size() >= kHostMaximumManifestObjects) break;
        ManifestValue root;
        root.domain = domain;
        root.item = item;
        root.root = true;
        root.type_specification.assign(
            fallback_type_specification.begin(), fallback_type_specification.end());
        root.encoded.assign(fallback_value.bytes.begin(), fallback_value.bytes.end());
        const auto value_index = model.values.size();
        model.values.push_back(std::move(root));
        root_indices.emplace(object_key(domain, item), model.root_trees.size());
        model.root_value_indices.push_back(value_index);
        model.root_trees.emplace_back();
    }

    for (const auto& parsed : parsed_objects) {
        if (model.values.size() >= kHostMaximumManifestObjects) break;
        const auto key = object_key(parsed.domain, parsed.item);
        if (model.value_indices.contains(key)) continue;
        ManifestValue value;
        value.domain = parsed.domain;
        value.item = parsed.item;
        value.raw_type = parsed.raw_type;
        value.normalized_type = parsed.normalized_type;
        value.type_signature = parsed.type_signature;
        value.text = parsed.text;
        value.mms_writable = writable_objects.contains({parsed.domain, parsed.item});
        encode_manifest_value(value);
        const auto value_index = model.values.size();
        model.values.push_back(std::move(value));
        model.value_indices.emplace(key, value_index);

        const auto parts = split_fields(parsed.item, '$');
        if (parts.size() < 2U) continue;
        const auto found_root = root_indices.find(object_key(parsed.domain, parts[0]));
        if (found_root == root_indices.end()) continue;
        auto* node = &model.root_trees[found_root->second];
        for (std::size_t part = 1U; part < parts.size(); ++part) {
            node = &manifest_child(*node, parts[part]);
        }
        node->value_index = value_index;
    }
    rebuild_manifest_roots(model);

    model.objects.reserve(model.values.size());
    for (auto& value : model.values) {
        mms::MmsStaticObjectEntry entry{
            value.domain,
            value.item,
            value.type_specification,
            read_manifest_value,
            &value};
        if (value.mms_writable && !value.root) {
            entry.write = write_manifest_value;
            entry.write_context = &value;
        }
        model.objects.push_back(entry);
    }

    std::map<std::pair<std::string, std::string>, std::vector<std::pair<std::string, std::string>>>
        grouped_members;
    for (const auto& member : parsed_members) {
        if (!model.value_indices.contains(object_key(member.member_domain, member.member_item))) {
            continue;
        }
        grouped_members[{member.domain, member.item}].emplace_back(
            member.member_domain, member.member_item);
    }
    model.data_set_storage.reserve(std::min<std::size_t>(
        grouped_members.size(), kHostMaximumManifestDataSets));
    for (auto& [name, members] : grouped_members) {
        if (model.data_set_storage.size() >= kHostMaximumManifestDataSets) break;
        if (members.empty()) continue;
        ManifestDataSetStorage storage;
        storage.domain = std::move(name.first);
        storage.item = std::move(name.second);
        storage.member_names = std::move(members);
        model.data_set_storage.push_back(std::move(storage));
    }
    model.data_sets.reserve(model.data_set_storage.size());
    for (auto& storage : model.data_set_storage) {
        storage.members.reserve(storage.member_names.size());
        for (const auto& [domain, item] : storage.member_names) {
            storage.members.push_back({domain, item});
        }
        model.data_sets.push_back({
            storage.domain, storage.item, storage.members, false});
    }
    return model;
}


struct HostUrcbControl final {
    ManifestReportControl* manifest{};
    std::vector<mms::MmsStaticObjectEntry> member_objects;
    std::array<mms::MmsStaticDataSetEntry, 1U> data_set_entries{};
    mms::MmsStaticObjectTable object_table{std::span<const mms::MmsStaticObjectEntry>{}};
    mms::MmsStaticDataSetTable data_set_table{};
    std::array<mms::MmsStaticUrcbDefinition, 1U> definitions{};
    std::array<mms::MmsStaticUrcbState, 1U> states{};
    std::unique_ptr<mms::MmsStaticUrcbRuntime> runtime;
    std::array<mms::MmsStaticObjectEntry,
        mms::MmsStaticUrcbObjectBank::attributes_per_control_block> bank_objects{};
    std::array<mms::MmsStaticUrcbObjectContext,
        mms::MmsStaticUrcbObjectBank::attributes_per_control_block> bank_contexts{};
    std::array<char, 4'096U> bank_names{};
    std::unique_ptr<mms::MmsStaticUrcbObjectBank> bank;
};

struct HostUrcbReporting final {
    std::chrono::steady_clock::time_point epoch{std::chrono::steady_clock::now()};
    std::vector<std::unique_ptr<HostUrcbControl>> controls;
};

[[nodiscard]] std::uint64_t report_now_ms(const void* raw) noexcept {
    const auto* reporting = static_cast<const HostUrcbReporting*>(raw);
    if (reporting == nullptr) return 0U;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - reporting->epoch).count();
    return elapsed <= 0 ? 0U : static_cast<std::uint64_t>(elapsed);
}

[[nodiscard]] std::array<std::uint8_t,
    mms::MmsInformationReportSpanCodec::binary_time_bytes> report_binary_time() noexcept {
    constexpr std::uint64_t kMillisecondsPerDay = 86'400'000ULL;
    constexpr std::uint64_t kUnixDaysTo1984 = 5'113ULL;
    const auto raw = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto milliseconds = raw <= 0 ? 0ULL : static_cast<std::uint64_t>(raw);
    const auto unix_days = milliseconds / kMillisecondsPerDay;
    const auto since_midnight = static_cast<std::uint32_t>(milliseconds % kMillisecondsPerDay);
    const auto days_1984 = static_cast<std::uint16_t>(std::min<std::uint64_t>(
        unix_days > kUnixDaysTo1984 ? unix_days - kUnixDaysTo1984 : 0ULL,
        std::numeric_limits<std::uint16_t>::max()));
    return {
        static_cast<std::uint8_t>((since_midnight >> 24U) & 0xFFU),
        static_cast<std::uint8_t>((since_midnight >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((since_midnight >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(since_midnight & 0xFFU),
        static_cast<std::uint8_t>((days_1984 >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(days_1984 & 0xFFU)};
}

[[nodiscard]] std::string_view report_reason_text(
    const mms::MmsStaticUrcbReportReason reason) noexcept {
    switch (reason) {
    case mms::MmsStaticUrcbReportReason::general_interrogation: return "gi";
    case mms::MmsStaticUrcbReportReason::integrity: return "integrity";
    case mms::MmsStaticUrcbReportReason::none: return "none";
    }
    return "unknown";
}

[[nodiscard]] HostUrcbReporting initialize_host_urcb_reporting(ManifestModel& model) {
    HostUrcbReporting reporting;
    for (auto& report : model.report_controls) {
        if (report.buffered) continue;
        const auto data_set = std::find_if(
            model.data_sets.begin(), model.data_sets.end(), [&report](const auto& candidate) {
                return candidate.domain == report.data_set_domain &&
                    candidate.item == report.data_set_item;
            });
        if (data_set == model.data_sets.end() || data_set->members.empty()) {
            throw std::runtime_error(
                "URCB references a missing/empty DataSet: " + report.domain + "/" + report.item);
        }

        auto control = std::make_unique<HostUrcbControl>();
        control->manifest = &report;
        control->member_objects.reserve(data_set->members.size());
        for (const auto& member : data_set->members) {
            const auto object = std::find_if(
                model.objects.begin(), model.objects.end(), [&member](const auto& candidate) {
                    return candidate.domain == member.domain && candidate.item == member.item;
                });
            if (object == model.objects.end()) {
                throw std::runtime_error(
                    "URCB DataSet member is absent from the live object table.");
            }
            control->member_objects.push_back(*object);
        }
        if (control->member_objects.empty() ||
            control->member_objects.size() > mms::MmsStaticObjectTable::maximum_objects) {
            throw std::runtime_error(
                "URCB DataSet exceeds the bounded static reporting object profile.");
        }
        control->object_table = mms::MmsStaticObjectTable{control->member_objects};
        control->data_set_entries[0] = *data_set;
        control->data_set_table = mms::MmsStaticDataSetTable{control->data_set_entries};
        if (!control->object_table.valid() || !control->data_set_table.valid() ||
            !control->data_set_table.valid_against(control->object_table)) {
            throw std::runtime_error("URCB reporting subset failed strict static-table validation.");
        }

        if (report.report_id.empty()) {
            report.report_id = report.domain + "/" + report.item;
        }
        control->definitions[0] = mms::MmsStaticUrcbDefinition{
            report.domain,
            report.item,
            report.report_id,
            report.data_set_domain,
            report.data_set_item,
            report.conf_rev,
            report.optional_fields,
            report.buffer_time_ms,
            report.trigger_options,
            report.integrity_period_ms};
        control->runtime = std::make_unique<mms::MmsStaticUrcbRuntime>(
            control->definitions,
            control->states,
            control->object_table,
            control->data_set_table);
        if (!control->runtime->initialize()) {
            throw std::runtime_error(
                "MmsStaticUrcbRuntime rejected SCL report definition: " +
                report.domain + "/" + report.item);
        }
        control->bank = std::make_unique<mms::MmsStaticUrcbObjectBank>(
            *control->runtime,
            std::span<const mms::MmsStaticObjectEntry>{},
            control->bank_objects,
            control->bank_contexts,
            control->bank_names,
            report_now_ms,
            &reporting);
        if (!control->bank->initialize()) {
            throw std::runtime_error(
                "MmsStaticUrcbObjectBank failed to materialize dynamic RCB attributes.");
        }

        for (const auto& dynamic : control->bank->table().objects()) {
            const auto existing = std::find_if(
                model.objects.begin(), model.objects.end(), [&dynamic](const auto& candidate) {
                    return candidate.domain == dynamic.domain && candidate.item == dynamic.item;
                });
            if (existing == model.objects.end()) {
                if (model.objects.size() >= kHostMaximumManifestObjects) {
                    throw std::runtime_error("Dynamic URCB attributes exceed host object bound.");
                }
                model.objects.push_back(dynamic);
            } else {
                *existing = dynamic;
            }
        }
        reporting.controls.push_back(std::move(control));
    }
    return reporting;
}

[[nodiscard]] bool send_complete_report_frame(
    const embedded::TcpByteStream& stream,
    const std::span<const std::uint8_t> frame,
    std::size_t& total_sent) noexcept {
    std::size_t offset{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (offset < frame.size() && std::chrono::steady_clock::now() < deadline) {
        const auto sent = stream.send(frame.subspan(offset));
        if (sent.success() && sent.transferred != 0U) {
            offset += std::min(sent.transferred, frame.size() - offset);
            continue;
        }
        if (sent.status == embedded::IoStatus::would_block ||
            sent.status == embedded::IoStatus::timeout) {
            continue;
        }
        return false;
    }
    if (offset != frame.size()) return false;
    total_sent += frame.size();
    return true;
}

[[nodiscard]] bool poll_host_urcb_reports(
    HostUrcbReporting& reporting,
    const mms::MmsStaticConnectionRuntime& connection,
    const embedded::TcpByteStream& stream,
    ConnectionBuffers& buffers,
    const std::uint64_t association_id,
    const std::string_view remote,
    std::size_t& total_sent) {
    const auto now = report_now_ms(&reporting);
    const auto report_time = report_binary_time();
    for (auto& control : reporting.controls) {
        const auto result = mms::MmsStaticReportConnection::poll(
            connection,
            *control->runtime,
            now,
            report_time,
            buffers.report_response,
            buffers.report_workspace);
        if (result.status == mms::MmsStaticReportConnectionStatus::no_report_due ||
            result.status == mms::MmsStaticReportConnectionStatus::not_established) {
            continue;
        }
        if (!result.response_ready()) {
            std::cerr << "IEDSIM_EVENT kind=report_error association=" << association_id
                      << " rcb=" << control->manifest->domain << '/' << control->manifest->item
                      << " status=" << static_cast<unsigned>(result.status)
                      << " urcb_status=" << static_cast<unsigned>(result.urcb_status) << '\n';
            return false;
        }
        if (!send_complete_report_frame(
                stream,
                std::span<const std::uint8_t>{buffers.report_response.data(), result.bytes_written},
                total_sent)) {
            std::cerr << "IEDSIM_EVENT kind=report_send_error association=" << association_id
                      << " rcb=" << control->manifest->domain << '/' << control->manifest->item
                      << " remote=" << remote << '\n';
            return false;
        }
        std::cout << "IEDSIM_EVENT kind=report_sent association=" << association_id
                  << " remote=" << remote
                  << " rcb=" << control->manifest->domain << '/' << control->manifest->item
                  << " reason=" << report_reason_text(result.reason)
                  << " sequence=" << static_cast<unsigned>(result.sequence_number)
                  << " bytes=" << result.bytes_written << '\n';
        std::cout.flush();
        return true;
    }
    return true;
}

void reset_host_urcb_connection(
    HostUrcbReporting* reporting) noexcept {
    if (reporting == nullptr) return;
    const auto now = report_now_ms(reporting);
    for (auto& control : reporting->controls) {
        static_cast<void>(control->runtime->set_enabled(0U, false, now));
        static_cast<void>(control->runtime->set_reserved(0U, false));
    }
}


struct HostBrcbControl final {
    ManifestReportControl* manifest{};
    std::vector<mms::MmsStaticObjectEntry> member_objects;
    std::vector<std::string> member_keys;
    std::array<mms::MmsStaticDataSetEntry, 1U> data_set_entries{};
    mms::MmsStaticObjectTable object_table{std::span<const mms::MmsStaticObjectEntry>{}};
    mms::MmsStaticDataSetTable data_set_table{};
    mms::MmsStaticBrcbDefinition definition{};
    std::array<std::array<std::uint8_t, 16'384U>, 8U> slot_bytes{};
    std::array<mms::MmsStaticBrcbSlot, 8U> slots{};
    mms::MmsStaticBrcbPendingState pending{};
    std::unique_ptr<mms::MmsStaticBrcbRuntime> runtime;
    std::unique_ptr<mms::MmsStaticBrcbControl> control;
    std::vector<mms::MmsStaticObjectEntry> bank_objects;
    std::array<mms::MmsStaticBrcbObjectContext,
        mms::MmsStaticBrcbObjectBank::attributes_per_control_block> bank_contexts{};
    std::array<char, 4'096U> bank_names{};
    std::unique_ptr<mms::MmsStaticBrcbObjectBank> bank;
};

struct HostBrcbReporting final {
    std::vector<std::unique_ptr<HostBrcbControl>> controls;
};

HostBrcbReporting* g_active_brcb_reporting{};

[[nodiscard]] std::uint64_t brcb_now_ms(const void*) noexcept {
    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    const auto count = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    return count <= 0 ? 0U : static_cast<std::uint64_t>(count);
}

[[nodiscard]] HostBrcbReporting initialize_host_brcb_reporting(ManifestModel& model) {
    HostBrcbReporting reporting;
    for (auto& report : model.report_controls) {
        if (!report.buffered) continue;
        const auto data_set = std::find_if(
            model.data_sets.begin(), model.data_sets.end(), [&report](const auto& candidate) {
                return candidate.domain == report.data_set_domain &&
                    candidate.item == report.data_set_item;
            });
        if (data_set == model.data_sets.end() || data_set->members.empty()) {
            throw std::runtime_error(
                "BRCB references a missing/empty DataSet: " + report.domain + "/" + report.item);
        }

        auto host = std::make_unique<HostBrcbControl>();
        host->manifest = &report;
        host->member_objects.reserve(data_set->members.size());
        host->member_keys.reserve(data_set->members.size());
        for (const auto& member : data_set->members) {
            const auto object = std::find_if(
                model.objects.begin(), model.objects.end(), [&member](const auto& candidate) {
                    return candidate.domain == member.domain && candidate.item == member.item;
                });
            if (object == model.objects.end()) {
                throw std::runtime_error("BRCB DataSet member is absent from the live object table.");
            }
            host->member_objects.push_back(*object);
            host->member_keys.push_back(object_key(member.domain, member.item));
        }
        if (host->member_objects.empty() ||
            host->member_objects.size() > mms::MmsStaticObjectTable::maximum_objects) {
            throw std::runtime_error("BRCB DataSet exceeds the bounded static reporting object profile.");
        }
        host->object_table = mms::MmsStaticObjectTable{host->member_objects};
        host->data_set_entries[0] = *data_set;
        host->data_set_table = mms::MmsStaticDataSetTable{host->data_set_entries};
        if (!host->object_table.valid() || !host->data_set_table.valid() ||
            !host->data_set_table.valid_against(host->object_table)) {
            throw std::runtime_error("BRCB reporting subset failed strict static-table validation.");
        }

        if (report.report_id.empty()) report.report_id = report.domain + "/" + report.item;
        host->definition = mms::MmsStaticBrcbDefinition{
            report.domain,
            report.item,
            report.report_id,
            report.data_set_domain,
            report.data_set_item,
            report.conf_rev,
            report.optional_fields,
            report.buffer_time_ms,
            report.trigger_options};
        for (std::size_t index = 0U; index < host->slots.size(); ++index) {
            host->slots[index].storage = host->slot_bytes[index];
        }
        host->runtime = std::make_unique<mms::MmsStaticBrcbRuntime>(
            host->definition,
            host->pending,
            host->slots,
            host->object_table,
            host->data_set_table);
        if (!host->runtime->initialize()) {
            throw std::runtime_error(
                "MmsStaticBrcbRuntime rejected SCL report definition: " +
                report.domain + "/" + report.item);
        }
        host->control = std::make_unique<mms::MmsStaticBrcbControl>(*host->runtime);
        host->bank_objects.resize(
            host->member_objects.size() + mms::MmsStaticBrcbObjectBank::attributes_per_control_block);
        host->bank = std::make_unique<mms::MmsStaticBrcbObjectBank>(
            host->definition,
            *host->runtime,
            *host->control,
            host->member_objects,
            host->bank_objects,
            host->bank_contexts,
            host->bank_names,
            brcb_now_ms,
            nullptr);
        if (!host->bank->initialize()) {
            throw std::runtime_error("MmsStaticBrcbObjectBank failed to materialize dynamic BRCB attributes.");
        }

        const auto attribute_prefix = report.item + "$";
        for (const auto& dynamic : host->bank->table().objects()) {
            if (!dynamic.item.starts_with(attribute_prefix)) continue;
            const auto existing = std::find_if(
                model.objects.begin(), model.objects.end(), [&dynamic](const auto& candidate) {
                    return candidate.domain == dynamic.domain && candidate.item == dynamic.item;
                });
            if (existing == model.objects.end()) {
                if (model.objects.size() >= kHostMaximumManifestObjects) {
                    throw std::runtime_error("Dynamic BRCB attributes exceed host object bound.");
                }
                model.objects.push_back(dynamic);
            } else {
                *existing = dynamic;
            }
        }
        reporting.controls.push_back(std::move(host));
    }
    return reporting;
}

void notify_active_brcb_mutation(
    const std::string_view domain,
    const std::string_view item) noexcept {
    if (g_active_brcb_reporting == nullptr) return;
    const auto key = object_key(domain, item);
    const auto now = brcb_now_ms(nullptr);
    for (auto& host : g_active_brcb_reporting->controls) {
        for (std::size_t index = 0U; index < host->member_keys.size(); ++index) {
            if (host->member_keys[index] != key) continue;
            const auto status = host->runtime->notify(
                index, mms::MmsStaticBrcbEventReason::data_change, now);
            if (status == mms::MmsStaticBrcbStatus::ok) {
                std::cout << "IEDSIM_EVENT kind=brcb_event rcb="
                          << host->manifest->domain << '/' << host->manifest->item
                          << " member=" << domain << '/' << item << '\n';
                std::cout.flush();
            }
        }
    }
}

void host_brcb_association_closed(
    void* context,
    const std::uint64_t association_id,
    const std::uint64_t now_ms) noexcept {
    auto* reporting = static_cast<HostBrcbReporting*>(context);
    if (reporting == nullptr) return;
    for (auto& host : reporting->controls) {
        host->control->on_association_closed(association_id, now_ms);
    }
}

[[nodiscard]] bool capture_host_brcb_reports(
    HostBrcbReporting& reporting,
    ConnectionBuffers& buffers) {
    const auto now = brcb_now_ms(nullptr);
    const auto report_time = report_binary_time();
    for (auto& host : reporting.controls) {
        host->control->tick(now);
        mms::MmsStaticBrcbCapturePlan plan;
        if (!host->runtime->next_due(now, plan)) continue;
        const auto captured = host->runtime->capture(
            plan,
            report_time,
            buffers.brcb_capture_encode,
            buffers.brcb_capture_workspace);
        if (!captured.success()) {
            std::cerr << "IEDSIM_EVENT kind=brcb_capture_error rcb="
                      << host->manifest->domain << '/' << host->manifest->item
                      << " status=" << static_cast<unsigned>(captured.status) << '\n';
            return false;
        }
        std::cout << "IEDSIM_EVENT kind=brcb_captured rcb="
                  << host->manifest->domain << '/' << host->manifest->item
                  << " sequence=" << static_cast<unsigned>(plan.sequence_number)
                  << " retained=" << host->runtime->retained_size()
                  << " queue=" << host->runtime->queue_size() << '\n';
        std::cout.flush();
    }
    return true;
}

[[nodiscard]] bool poll_host_brcb_reports(
    HostBrcbReporting& reporting,
    const mms::MmsStaticConnectionRuntime& connection,
    const embedded::TcpByteStream& stream,
    ConnectionBuffers& buffers,
    const std::uint64_t association_id,
    const std::string_view remote,
    std::size_t& total_sent) {
    const auto now = brcb_now_ms(nullptr);
    for (auto& host : reporting.controls) {
        const auto staged = mms::MmsStaticBrcbConnection::poll(
            connection,
            *host->control,
            *host->runtime,
            now,
            buffers.report_response,
            buffers.report_workspace);
        if (staged.status == mms::MmsStaticBrcbConnectionStatus::no_report_available ||
            staged.status == mms::MmsStaticBrcbConnectionStatus::reporting_disabled ||
            staged.status == mms::MmsStaticBrcbConnectionStatus::not_established ||
            staged.status == mms::MmsStaticBrcbConnectionStatus::access_denied) {
            continue;
        }
        if (!staged.response_ready()) {
            std::cerr << "IEDSIM_EVENT kind=brcb_stage_error association=" << association_id
                      << " rcb=" << host->manifest->domain << '/' << host->manifest->item
                      << " status=" << static_cast<unsigned>(staged.status) << '\n';
            return false;
        }
        if (!send_complete_report_frame(
                stream,
                std::span<const std::uint8_t>{buffers.report_response.data(), staged.bytes_written},
                total_sent)) {
            std::cerr << "IEDSIM_EVENT kind=brcb_send_error association=" << association_id
                      << " rcb=" << host->manifest->domain << '/' << host->manifest->item
                      << " remote=" << remote << '\n';
            return false;
        }
        if (!mms::MmsStaticBrcbConnection::commit_sent(
                connection, *host->control, *host->runtime, now, staged)) {
            std::cerr << "IEDSIM_EVENT kind=brcb_commit_error association=" << association_id
                      << " rcb=" << host->manifest->domain << '/' << host->manifest->item << '\n';
            return false;
        }
        std::cout << "IEDSIM_EVENT kind=brcb_report_sent association=" << association_id
                  << " remote=" << remote
                  << " rcb=" << host->manifest->domain << '/' << host->manifest->item
                  << " sequence=" << static_cast<unsigned>(staged.sequence_number)
                  << " bytes=" << staged.bytes_written
                  << " retained=" << host->runtime->retained_size()
                  << " queue=" << host->runtime->queue_size() << '\n';
        std::cout.flush();
        return true;
    }
    return true;
}

[[nodiscard]] std::size_t refresh_manifest_values(ManifestModel& model) {
    if (model.path.empty()) return 0U;
    std::ifstream input{model.path};
    if (!input) return 0U;
    std::string line;
    if (!std::getline(input, line)) return 0U;
    const auto revision = manifest_revision(line);
    if (revision == 0U || revision == model.revision) return 0U;

    std::size_t changed{};
    while (std::getline(input, line)) {
        const auto fields = split_fields(line, '\t');
        if (fields.size() < 6U || fields[0] != "OBJ") continue;
        const auto found = model.value_indices.find(object_key(fields[1], fields[2]));
        if (found == model.value_indices.end()) continue;
        auto& value = model.values[found->second];
        const auto text_index = fields.size() >= 7U ? 6U : 5U;
        if (value.text == fields[text_index]) continue;
        if (!live_text_valid(value.type, fields[text_index])) continue;
        auto data = manifest_data(value.type, fields[text_index]);
        if (apply_live_data(
                model,
                found->second,
                std::move(data),
                fields[text_index],
                "Good",
                "manifest-sync",
                0U,
                std::nullopt)) {
            ++changed;
        }
    }
    model.revision = revision;
    return changed;
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
    ManifestModel* const manifest_model,
    HostUrcbReporting* const reporting,
    HostBrcbReporting* const brcb_reporting,
    const std::uint64_t association_id,
    const std::string_view remote) {
    mms::MmsStaticDispatchPolicy dispatch_policy;
    dispatch_policy.advertise_flattened_child_aliases = manifest_model != nullptr;
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
    if (brcb_reporting != nullptr) {
        policy.now_ms = brcb_now_ms;
        policy.association_closed = host_brcb_association_closed;
        policy.association_closed_context = brcb_reporting;
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
    auto next_manifest_refresh = std::chrono::steady_clock::now();
    std::size_t total_received = 0U;
    std::size_t total_sent = 0U;
    while (!g_stop.load(std::memory_order_relaxed)) {
        if (manifest_model != nullptr) {
            drain_live_commands(*manifest_model);
            const auto now = std::chrono::steady_clock::now();
            if (now >= next_manifest_refresh) {
                next_manifest_refresh = now + std::chrono::milliseconds{25};
                try {
                    const auto changed = refresh_manifest_values(*manifest_model);
                    if (changed != 0U) {
                        std::cout << "IEDSIM_EVENT kind=value_sync association="
                                  << association_id << " changed=" << changed
                                  << " revision=" << manifest_model->revision << '\n';
                        std::cout.flush();
                    }
                } catch (const std::exception& exception) {
                    std::cerr << "IEDSIM_EVENT kind=value_sync_error association="
                              << association_id << " message=" << exception.what() << '\n';
                }
            }
        }
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
        if (brcb_reporting != nullptr && !capture_host_brcb_reports(*brcb_reporting, buffers)) {
            reset_host_urcb_connection(reporting);
            runtime.close_transport();
            return;
        }
        if (reporting != nullptr && runtime.state() == mms::MmsStaticConnectionState::established &&
            session.pending_output_bytes() == 0U && session.buffered_input_bytes() == 0U &&
            !poll_host_urcb_reports(
                *reporting, runtime, stream, buffers, association_id, remote, total_sent)) {
            reset_host_urcb_connection(reporting);
            runtime.close_transport();
            return;
        }
        if (brcb_reporting != nullptr && runtime.state() == mms::MmsStaticConnectionState::established &&
            session.pending_output_bytes() == 0U && session.buffered_input_bytes() == 0U &&
            !poll_host_brcb_reports(
                *brcb_reporting, runtime, stream, buffers, association_id, remote, total_sent)) {
            reset_host_urcb_connection(reporting);
            runtime.close_transport();
            return;
        }
        if (result.terminal()) {
            reset_host_urcb_connection(reporting);
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
    reset_host_urcb_connection(reporting);
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
        if (!manifest_model.objects.empty()) {
            g_active_manifest_model = &manifest_model;
            start_live_command_reader();
        }

        auto urcb_reporting = initialize_host_urcb_reporting(manifest_model);
        auto brcb_reporting = initialize_host_brcb_reporting(manifest_model);
        g_active_brcb_reporting = brcb_reporting.controls.empty() ? nullptr : &brcb_reporting;

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
            : std::span<const mms::MmsStaticDataSetEntry>{manifest_model.data_sets};
        const mms::MmsStaticObjectTable object_table{object_span};
        const mms::MmsStaticDataSetTable data_sets{data_set_span};
        const bool model_valid = manifest_model.objects.empty()
            ? (object_table.valid() && data_sets.valid() && data_sets.valid_against(object_table))
            : host_manifest_tables_valid(object_table, data_sets);
        if (!model_valid) {
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
        if (g_active_manifest_model != nullptr) {
            std::cout << "IEDSIM_EVENT kind=reporting_ready urcb="
                      << urcb_reporting.controls.size()
                      << " brcb=" << brcb_reporting.controls.size()
                      << " runtime=static-urcb+brcb-core" << '\n';
            std::cout.flush();
            std::cout << "IEDSIM_EVENT kind=state_ready values="
                      << manifest_model.value_indices.size()
                      << " revision=" << manifest_model.live_revision
                      << " clock_ms=" << manifest_model.logical_time_ms << '\n';
            std::cout.flush();
        }

        std::size_t connection_count = 0U;
        while (!g_stop.load(std::memory_order_relaxed) &&
               (options.maximum_connections == 0U ||
                connection_count < options.maximum_connections)) {
            if (g_active_manifest_model != nullptr) drain_live_commands(manifest_model);
            const auto readiness = wait_socket(
                listener, true, g_active_manifest_model != nullptr ? 25U : 200U);
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
                manifest_model.objects.empty() ? nullptr : &manifest_model,
                urcb_reporting.controls.empty() ? nullptr : &urcb_reporting,
                brcb_reporting.controls.empty() ? nullptr : &brcb_reporting,
                static_cast<std::uint64_t>(connection_count),
                remote);
            close_socket(client);
        }

        close_socket(listener);
        g_active_brcb_reporting = nullptr;
        std::cout << "IEDSIM_EVENT kind=server_stopped connections="
                  << connection_count << '\n';
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Static IED server failed: " << exception.what() << '\n';
        return 2;
    }
}
