// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/mms/static_server_session.hpp"
#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/mms/services.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
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

struct ManifestValue final {
    std::string domain;
    std::string item;
    std::string raw_type;
    std::string normalized_type;
    std::string text;
    mms::MmsTypeSpecification type;
    std::optional<mms::MmsDataValue> data;
    std::vector<std::uint8_t> type_specification;
    std::vector<std::uint8_t> encoded;
    bool root{};
};

struct ManifestTypeNode final {
    std::map<std::string, ManifestTypeNode> children;
    std::optional<std::size_t> value_index;
};

struct ManifestDataSetStorage final {
    std::string domain;
    std::string item;
    std::vector<std::pair<std::string, std::string>> member_names;
    std::vector<mms::MmsStaticDataSetMember> members;
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
    std::size_t declared_entries{};
};

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

[[nodiscard]] mms::MmsDataValue manifest_data(
    const mms::MmsTypeSpecification& type,
    const std::string& text) {
    switch (type.kind) {
    case mms::MmsTypeKind::boolean:
        return mms::MmsDataValue::boolean(text_boolean(text));
    case mms::MmsTypeKind::bit_string: {
        constexpr std::array<std::uint8_t, 2U> good_quality{};
        return mms::MmsDataValue::bit_string(3U, good_quality);
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
            return mms::MmsDataValue::floating_point(0.0F);
        }
    case mms::MmsTypeKind::utc_time:
        return mms::MmsDataValue::utc_time(
            mms::Iec61850UtcTime{std::chrono::system_clock::now(), 0U});
    default:
        return mms::MmsDataValue::visible_string(text == "---" ? std::string{} : text);
    }
}

void encode_manifest_value(ManifestValue& value) {
    value.type = manifest_type(value.raw_type, value.normalized_type);
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
    for (const auto& [child_name, child] : node.children) {
        result.children.push_back(node_type(child, model, child_name));
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
    for (const auto& [name, child] : node.children) {
        static_cast<void>(name);
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
    std::set<std::pair<std::string, std::string>> unique_roots;
    std::set<std::pair<std::string, std::string>> unique_objects;
    std::string line;
    if (std::getline(input, line)) model.revision = manifest_revision(line);
    while (std::getline(input, line)) {
        const auto fields = split_fields(line, '\t');
        if (fields.size() >= 3U && fields[0] == "LN") {
            ++model.declared_entries;
            if (!fields[1].empty() && !fields[2].empty() &&
                unique_roots.emplace(fields[1], fields[2]).second) {
                roots.emplace_back(fields[1], fields[2]);
            }
        } else if (fields.size() >= 6U && fields[0] == "OBJ") {
            ++model.declared_entries;
            if (!fields[1].empty() && !fields[2].empty() &&
                unique_objects.emplace(fields[1], fields[2]).second) {
                parsed_objects.push_back({fields[1], fields[2], fields[3], fields[4], fields[5]});
            }
        } else if (fields.size() >= 5U && fields[0] == "DS") {
            parsed_members.push_back({fields[1], fields[2], fields[3], fields[4]});
        }
    }
    if (roots.empty()) {
        throw std::runtime_error("Model manifest contains no usable logical-node entries.");
    }

    model.values.reserve(mms::MmsStaticObjectTable::maximum_objects);
    model.root_trees.reserve(roots.size());
    model.root_value_indices.reserve(roots.size());
    std::unordered_map<std::string, std::size_t> root_indices;
    for (const auto& [domain, item] : roots) {
        if (model.values.size() >= mms::MmsStaticObjectTable::maximum_objects) break;
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
        if (model.values.size() >= mms::MmsStaticObjectTable::maximum_objects) break;
        const auto key = object_key(parsed.domain, parsed.item);
        if (model.value_indices.contains(key)) continue;
        ManifestValue value;
        value.domain = parsed.domain;
        value.item = parsed.item;
        value.raw_type = parsed.raw_type;
        value.normalized_type = parsed.normalized_type;
        value.text = parsed.text;
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
            node = &node->children[parts[part]];
        }
        node->value_index = value_index;
    }
    rebuild_manifest_roots(model);

    model.objects.reserve(model.values.size());
    for (auto& value : model.values) {
        model.objects.push_back(mms::MmsStaticObjectEntry{
            value.domain,
            value.item,
            value.type_specification,
            read_manifest_value,
            &value});
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
        grouped_members.size(), mms::MmsStaticDataSetTable::maximum_data_sets));
    for (auto& [name, members] : grouped_members) {
        if (model.data_set_storage.size() >= mms::MmsStaticDataSetTable::maximum_data_sets) break;
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
        if (value.text == fields[5]) continue;
        value.text = fields[5];
        value.data = manifest_data(value.type, value.text);
        value.encoded = mms::MmsDataCodec::encode(*value.data);
        ++changed;
    }
    model.revision = revision;
    if (changed != 0U) {
        rebuild_manifest_roots(model);
        // Re-encoding a root may reallocate its byte vector. Refresh the
        // object-table spans so later attribute/root reads never retain
        // storage from the previous manifest revision.
        for (const auto value_index : model.root_value_indices) {
            model.objects[value_index].type_specification =
                model.values[value_index].type_specification;
        }
    }
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
    auto next_model_refresh = std::chrono::steady_clock::now();
    std::size_t total_received = 0U;
    std::size_t total_sent = 0U;
    while (!g_stop.load(std::memory_order_relaxed)) {
        const auto now = std::chrono::steady_clock::now();
        if (manifest_model != nullptr && now >= next_model_refresh) {
            next_model_refresh = now + std::chrono::milliseconds{25};
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
            : std::span<const mms::MmsStaticDataSetEntry>{manifest_model.data_sets};
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
                manifest_model.objects.empty() ? nullptr : &manifest_model,
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
