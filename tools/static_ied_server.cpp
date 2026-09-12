// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/mms/static_server_session.hpp"
#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/mms/services.hpp"
#include "ariec61850/mms/simulator_manifest_codec.hpp"
#include "ariec61850/mms/static_direct_control.hpp"
#include "ariec61850/mms/static_brcb_connection.hpp"
#include "ariec61850/mms/static_brcb_control.hpp"
#include "ariec61850/mms/static_brcb_objects.hpp"
#include "ariec61850/mms/static_brcb_runtime.hpp"
#include "ariec61850/mms/static_report_connection.hpp"
#include "ariec61850/mms/static_urcb_objects.hpp"
#include "ariec61850/mms/static_urcb_runtime.hpp"

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
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <syncstream>
#include <thread>
#include <unordered_map>
#include <utility>
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
    if (socket == kInvalidSocket) return;
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
    if (for_read) FD_SET(socket, &read_set);
    else FD_SET(socket, &write_set);

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
    if (result > 0) return SocketWaitStatus::ready;
    if (result == 0) return SocketWaitStatus::timeout;
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
    if (::listen(listener, 16) != 0) {
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
    if (count == 0) return {embedded::IoStatus::closed, 0U};
    const auto error = ::WSAGetLastError();
    if (error == WSAEWOULDBLOCK || error == WSAEINTR) {
        return {embedded::IoStatus::would_block, 0U};
    }
    if (error == WSAETIMEDOUT) return {embedded::IoStatus::timeout, 0U};
#else
    const auto count = ::recv(stream.socket, destination.data(), destination.size(), 0);
    if (count > 0) {
        return {embedded::IoStatus::ok, static_cast<std::size_t>(count)};
    }
    if (count == 0) return {embedded::IoStatus::closed, 0U};
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return {embedded::IoStatus::would_block, 0U};
    }
    if (errno == ETIMEDOUT) return {embedded::IoStatus::timeout, 0U};
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
    if (count == 0) return {embedded::IoStatus::closed, 0U};
    const auto error = ::WSAGetLastError();
    if (error == WSAEWOULDBLOCK || error == WSAEINTR) {
        return {embedded::IoStatus::would_block, 0U};
    }
    if (error == WSAETIMEDOUT) return {embedded::IoStatus::timeout, 0U};
#else
    const auto count = ::send(stream.socket, bytes.data(), bytes.size(), 0);
    if (count > 0) {
        return {embedded::IoStatus::ok, static_cast<std::size_t>(count)};
    }
    if (count == 0) return {embedded::IoStatus::closed, 0U};
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return {embedded::IoStatus::would_block, 0U};
    }
    if (errno == ETIMEDOUT) return {embedded::IoStatus::timeout, 0U};
#endif
    return {embedded::IoStatus::io_error, 0U};
}

[[nodiscard]] bool send_all(
    const NativeSocket socket,
    const std::span<const std::uint8_t> bytes) noexcept {
    SocketStreamContext context{socket};
    std::size_t offset{};
    std::size_t consecutive_waits{};
    while (offset < bytes.size() && !g_stop.load(std::memory_order_relaxed)) {
        const auto result = socket_send(&context, bytes.subspan(offset));
        if (result.status == embedded::IoStatus::ok && result.transferred != 0U) {
            offset += result.transferred;
            consecutive_waits = 0U;
            continue;
        }
        if (result.status == embedded::IoStatus::timeout ||
            result.status == embedded::IoStatus::would_block) {
            if (++consecutive_waits <= 50U) continue;
        }
        return false;
    }
    return offset == bytes.size();
}

[[nodiscard]] std::uint64_t monotonic_ms() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

[[nodiscard]] std::uint64_t report_now_ms(const void*) noexcept {
    return monotonic_ms();
}

[[nodiscard]] std::array<std::uint8_t, 6U> report_binary_time() noexcept {
    constexpr std::int64_t milliseconds_per_day = 86'400'000LL;
    constexpr std::int64_t unix_days_to_1984 = 5'113LL;
    auto unix_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::int64_t unix_days = unix_ms / milliseconds_per_day;
    std::int64_t day_ms = unix_ms % milliseconds_per_day;
    if (day_ms < 0) {
        day_ms += milliseconds_per_day;
        --unix_days;
    }
    const auto days_since_1984 = std::clamp<std::int64_t>(
        unix_days - unix_days_to_1984, 0LL, 65'535LL);
    const auto millis = static_cast<std::uint32_t>(day_ms);
    const auto days = static_cast<std::uint16_t>(days_since_1984);
    return {
        static_cast<std::uint8_t>((millis >> 24U) & 0xFFU),
        static_cast<std::uint8_t>((millis >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((millis >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(millis & 0xFFU),
        static_cast<std::uint8_t>((days >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(days & 0xFFU)};
}

struct CliOptions final {
    std::string bind_address{"0.0.0.0"};
    std::string model_manifest;
    std::uint16_t port{102U};
    std::uint8_t digital_input_mask{};
    std::size_t maximum_connections{};
    std::size_t maximum_active_connections{8U};
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
        << "  --max-active N            Maximum concurrent associations (default 8, max 64).\n"
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
            option == "--max-connections" || option == "--max-active") {
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
            } else if (option == "--max-active") {
                const auto parsed = parse_u32(option, value, 64U);
                if (parsed == 0U) {
                    throw std::invalid_argument("--max-active must be 1..64.");
                }
                options.maximum_active_connections = static_cast<std::size_t>(parsed);
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

[[nodiscard]] wire::EncodeResult read_atomic_boolean(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 3U;
    if (context == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    const auto value = static_cast<const std::atomic<std::uint8_t>*>(context)->load(
        std::memory_order_relaxed) != 0U;
    destination[0] = 0x83U;
    destination[1] = 0x01U;
    destination[2] = value ? 0xFFU : 0x00U;
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] bool apply_atomic_boolean(void* context, const bool value) noexcept {
    if (context == nullptr) return false;
    static_cast<std::atomic<std::uint8_t>*>(context)->store(
        value ? 1U : 0U, std::memory_order_relaxed);
    return true;
}

[[nodiscard]] std::vector<std::uint8_t> encode_ber_length(
    const std::size_t length) {
    if (length < 0x80U) return {static_cast<std::uint8_t>(length)};
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
    for (const auto part : parts) total += part.size();
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
        const auto structure_fields = std::span<const std::uint8_t>{encoded}.subspan(2U);
        const auto component_list = structure_fields.subspan(2U);
        do_entries.insert(do_entries.end(), component_list.begin(), component_list.end());
    }
    return make_tlv(0xA2U, make_tlv(0xA1U, do_entries));
}

[[nodiscard]] mms::MmsTypeSpecification control_scalar(
    const mms::MmsTypeKind kind,
    std::string name,
    const std::optional<std::uint32_t> size = std::nullopt) {
    mms::MmsTypeSpecification result;
    result.kind = kind;
    result.name = std::move(name);
    result.size = size;
    return result;
}

[[nodiscard]] mms::MmsTypeSpecification control_structure(
    std::string name,
    std::vector<mms::MmsTypeSpecification> children) {
    mms::MmsTypeSpecification result;
    result.kind = mms::MmsTypeKind::structure;
    result.name = std::move(name);
    result.children = std::move(children);
    return result;
}

[[nodiscard]] std::vector<std::uint8_t> direct_boolean_oper_type_specification() {
    return mms::MmsServiceCodec::encode_type_specification(control_structure("Oper", {
        control_scalar(mms::MmsTypeKind::boolean, "ctlVal"),
        control_structure("origin", {
            control_scalar(mms::MmsTypeKind::unsigned_integer, "orCat"),
            control_scalar(mms::MmsTypeKind::octet_string, "orIdent", 64U),
        }),
        control_scalar(mms::MmsTypeKind::unsigned_integer, "ctlNum"),
        control_scalar(mms::MmsTypeKind::utc_time, "T"),
        control_scalar(mms::MmsTypeKind::boolean, "Test"),
        control_scalar(mms::MmsTypeKind::bit_string, "Check", 2U),
    }));
}

struct ConnectionBuffers final {
    std::array<std::uint8_t, 32'768U> receive{};
    std::array<std::uint8_t, 32'768U> response{};
    std::array<std::uint8_t, 8'192U> workspace{};
    std::array<std::uint8_t, 65'535U> report_frame{};
    std::array<std::uint8_t, 65'535U> report_workspace{};
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

struct ManifestReportControlStorage final {
    std::string domain;
    std::string item;
    std::string report_id;
    std::string data_set_domain;
    std::string data_set_item;
    std::uint32_t conf_revision{1U};
    std::array<std::uint8_t, 2U> optional_fields{};
    std::uint32_t buffer_time_ms{};
    std::uint8_t trigger_options{};
    std::uint32_t integrity_period_ms{};
};

struct ManifestDirectControlStorage final {
    std::string domain;
    std::string logical_node;
    std::string data_object;
    std::string cdc;
    std::uint8_t control_model{};
    std::string status_item;
    std::string ctl_model_item;
    std::string oper_item;
    std::vector<std::uint8_t> oper_type_specification;
    std::shared_ptr<std::atomic<std::uint8_t>> process_value;
};

constexpr std::size_t kMaximumSimulatorDirectControls = 64U;
constexpr std::size_t kMaximumSimulatorBrcbs = 16U;
constexpr std::size_t kBrcbRetainedEntries = 4U;
constexpr std::size_t kBrcbSlotBytes = 32U * 1024U;

struct ManifestModel final {
    std::string path;
    std::uint64_t revision{};
    std::vector<ManifestValue> values;
    std::vector<mms::MmsStaticObjectEntry> objects;
    std::vector<ManifestTypeNode> root_trees;
    std::vector<std::size_t> root_value_indices;
    std::unordered_map<std::string, std::size_t> value_indices;
    std::vector<ManifestDirectControlStorage> direct_control_storage;
    std::size_t omitted_direct_controls{};
    std::vector<ManifestDataSetStorage> data_set_storage;
    std::vector<mms::MmsStaticDataSetEntry> data_sets;
    std::vector<ManifestReportControlStorage> report_control_storage;
    std::vector<mms::MmsStaticUrcbDefinition> urcb_definitions;
    std::vector<ManifestReportControlStorage> brcb_control_storage;
    std::vector<mms::MmsStaticBrcbDefinition> brcb_definitions;
    std::size_t buffered_report_controls{};
    std::size_t omitted_urcbs{};
    std::size_t omitted_brcbs{};
    std::size_t declared_entries{};
};

struct BrcbAssociationRuntime final {
    const mms::MmsStaticBrcbDefinition* definition{};
    const mms::MmsStaticDataSetEntry* data_set{};
    mms::MmsStaticBrcbPendingState pending{};
    std::array<std::array<std::uint8_t, kBrcbSlotBytes>, kBrcbRetainedEntries>
        slot_storage{};
    std::array<mms::MmsStaticBrcbSlot, kBrcbRetainedEntries> slots{};
    std::unique_ptr<mms::MmsStaticBrcbRuntime> reports;
    std::unique_ptr<mms::MmsStaticBrcbControl> control;
    std::vector<mms::MmsStaticObjectEntry> object_storage;
    std::array<mms::MmsStaticBrcbObjectContext,
        mms::MmsStaticBrcbObjectBank::attributes_per_control_block> context_storage{};
    std::vector<char> name_storage;
    std::unique_ptr<mms::MmsStaticBrcbObjectBank> bank;
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

void encode_manifest_value(ManifestValue& value) {
    value.type = mms::MmsSimulatorManifestCodec::type(
        value.raw_type, value.normalized_type);
    value.data = mms::MmsSimulatorManifestCodec::data(
        value.type, value.raw_type, value.normalized_type, value.text);
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
    struct ParsedControl final {
        std::string domain;
        std::string logical_node;
        std::string data_object;
        std::string cdc;
        std::uint8_t control_model{};
    };
    struct ParsedDataSetMember final {
        std::string domain;
        std::string item;
        std::string member_domain;
        std::string member_item;
    };
    struct ParsedReportControl final {
        std::string domain;
        std::string item;
        bool buffered{};
        std::string report_id;
        std::string data_set_domain;
        std::string data_set_item;
        std::uint32_t conf_revision{};
        std::uint32_t buffer_time_ms{};
        std::uint32_t integrity_period_ms{};
        std::uint8_t trigger_options{};
        std::array<std::uint8_t, 2U> optional_fields{};
    };
    std::vector<std::pair<std::string, std::string>> roots;
    std::vector<ParsedObject> parsed_objects;
    std::vector<ParsedControl> parsed_controls;
    std::vector<ParsedDataSetMember> parsed_members;
    std::vector<ParsedReportControl> parsed_reports;
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
        } else if (fields.size() >= 6U && fields[0] == "CTL") {
            ++model.declared_entries;
            if (fields[1].empty() || fields[2].empty() || fields[3].empty() || fields[4].empty()) {
                throw std::runtime_error("Model manifest contains a malformed CTL entry.");
            }
            parsed_controls.push_back({
                fields[1],
                fields[2],
                fields[3],
                fields[4],
                static_cast<std::uint8_t>(parse_u32("CTL ctlModel", fields[5], 4U))});
        } else if (fields.size() >= 5U && fields[0] == "DS") {
            parsed_members.push_back({fields[1], fields[2], fields[3], fields[4]});
        } else if (fields.size() >= 13U && fields[0] == "RCB") {
            if (fields[1].empty() || fields[2].empty() || fields[4].empty() ||
                fields[5].empty() || fields[6].empty() ||
                (fields[3] != "0" && fields[3] != "1")) {
                throw std::runtime_error("Model manifest contains a malformed RCB entry.");
            }
            ParsedReportControl report;
            report.domain = fields[1];
            report.item = fields[2];
            report.buffered = fields[3] == "1";
            report.report_id = fields[4];
            report.data_set_domain = fields[5];
            report.data_set_item = fields[6];
            report.conf_revision = parse_u32("RCB ConfRev", fields[7],
                std::numeric_limits<std::uint32_t>::max());
            report.buffer_time_ms = parse_u32("RCB BufTm", fields[8],
                std::numeric_limits<std::uint32_t>::max());
            report.integrity_period_ms = parse_u32("RCB IntgPd", fields[9],
                std::numeric_limits<std::uint32_t>::max());
            report.trigger_options = static_cast<std::uint8_t>(
                parse_u32("RCB TrgOps", fields[10], 0xFFU));
            report.optional_fields[0] = static_cast<std::uint8_t>(
                parse_u32("RCB OptFlds[0]", fields[11], 0xFFU));
            report.optional_fields[1] = static_cast<std::uint8_t>(
                parse_u32("RCB OptFlds[1]", fields[12], 0xFFU));
            parsed_reports.push_back(std::move(report));
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

    // Compile virtual service objects from SCL configured ctlModel metadata.
    // Phase one intentionally exposes only SPC Direct-with-normal-security;
    // other configured models remain visible as structural CF data but are not
    // falsely advertised as executable server controls.
    const auto oper_type = direct_boolean_oper_type_specification();
    std::set<std::pair<std::string, std::string>> unique_direct_controls;
    for (const auto& parsed : parsed_controls) {
        if (parsed.control_model != 1U || parsed.cdc != "SPC") {
            ++model.omitted_direct_controls;
            continue;
        }
        if (model.direct_control_storage.size() >= kMaximumSimulatorDirectControls) {
            ++model.omitted_direct_controls;
            continue;
        }
        const auto status_item = parsed.logical_node + "$ST$" + parsed.data_object + "$stVal";
        const auto ctl_model_item = parsed.logical_node + "$CF$" + parsed.data_object + "$ctlModel";
        const auto oper_item = parsed.logical_node + "$CO$" + parsed.data_object + "$Oper";
        if (!unique_direct_controls.emplace(parsed.domain, oper_item).second) continue;
        const auto status = model.value_indices.find(object_key(parsed.domain, status_item));
        const auto ctl_model = model.value_indices.find(object_key(parsed.domain, ctl_model_item));
        if (status == model.value_indices.end() || ctl_model == model.value_indices.end() ||
            model.values[status->second].type.kind != mms::MmsTypeKind::boolean) {
            ++model.omitted_direct_controls;
            continue;
        }
        const auto& encoded_status = model.values[status->second].encoded;
        const auto initial = encoded_status.size() >= 3U && encoded_status[0] == 0x83U &&
            encoded_status.back() != 0U;
        ManifestDirectControlStorage control;
        control.domain = parsed.domain;
        control.logical_node = parsed.logical_node;
        control.data_object = parsed.data_object;
        control.cdc = parsed.cdc;
        control.control_model = parsed.control_model;
        control.status_item = status_item;
        control.ctl_model_item = ctl_model_item;
        control.oper_item = oper_item;
        control.oper_type_specification = oper_type;
        control.process_value = std::make_shared<std::atomic<std::uint8_t>>(initial ? 1U : 0U);
        model.direct_control_storage.push_back(std::move(control));
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

    std::set<std::pair<std::string, std::string>> available_data_sets;
    for (const auto& data_set : model.data_sets) {
        available_data_sets.emplace(data_set.domain, data_set.item);
    }

    if (model.objects.size() + model.direct_control_storage.size() >
        mms::MmsStaticObjectTable::maximum_objects) {
        throw std::runtime_error("Configured Direct-Normal controls exceed MMS object capacity.");
    }
    auto remaining_object_slots = mms::MmsStaticObjectTable::maximum_objects -
        model.objects.size() - model.direct_control_storage.size();
    const auto available_urcb_slots = std::min<std::size_t>(
        mms::MmsStaticUrcbRuntime::maximum_control_blocks,
        remaining_object_slots /
            mms::MmsStaticUrcbObjectBank::attributes_per_control_block);
    model.report_control_storage.reserve(available_urcb_slots);
    for (auto& report : parsed_reports) {
        if (report.buffered) continue;
        if (!available_data_sets.contains({report.data_set_domain, report.data_set_item}) ||
            model.report_control_storage.size() >= available_urcb_slots) {
            ++model.omitted_urcbs;
            continue;
        }
        ManifestReportControlStorage storage;
        storage.domain = report.domain;
        storage.item = report.item;
        storage.report_id = report.report_id;
        storage.data_set_domain = report.data_set_domain;
        storage.data_set_item = report.data_set_item;
        storage.conf_revision = report.conf_revision;
        storage.optional_fields = report.optional_fields;
        storage.buffer_time_ms = report.buffer_time_ms;
        storage.trigger_options = report.trigger_options;
        storage.integrity_period_ms = report.integrity_period_ms;
        model.report_control_storage.push_back(std::move(storage));
    }
    model.urcb_definitions.reserve(model.report_control_storage.size());
    for (const auto& storage : model.report_control_storage) {
        model.urcb_definitions.push_back(mms::MmsStaticUrcbDefinition{
            storage.domain,
            storage.item,
            storage.report_id,
            storage.data_set_domain,
            storage.data_set_item,
            storage.conf_revision,
            storage.optional_fields,
            storage.buffer_time_ms,
            storage.trigger_options,
            storage.integrity_period_ms});
    }
    remaining_object_slots -= model.urcb_definitions.size() *
        mms::MmsStaticUrcbObjectBank::attributes_per_control_block;

    const auto available_brcb_slots = std::min<std::size_t>(
        kMaximumSimulatorBrcbs,
        remaining_object_slots /
            mms::MmsStaticBrcbObjectBank::attributes_per_control_block);
    model.brcb_control_storage.reserve(available_brcb_slots);
    for (auto& report : parsed_reports) {
        if (!report.buffered) continue;
        ++model.buffered_report_controls;
        if (!available_data_sets.contains({report.data_set_domain, report.data_set_item}) ||
            model.brcb_control_storage.size() >= available_brcb_slots) {
            ++model.omitted_brcbs;
            continue;
        }
        ManifestReportControlStorage storage;
        storage.domain = report.domain;
        storage.item = report.item;
        storage.report_id = report.report_id;
        storage.data_set_domain = report.data_set_domain;
        storage.data_set_item = report.data_set_item;
        storage.conf_revision = report.conf_revision;
        storage.optional_fields = report.optional_fields;
        storage.buffer_time_ms = report.buffer_time_ms;
        storage.trigger_options = report.trigger_options;
        storage.integrity_period_ms = report.integrity_period_ms;
        model.brcb_control_storage.push_back(std::move(storage));
    }
    model.brcb_definitions.reserve(model.brcb_control_storage.size());
    for (const auto& storage : model.brcb_control_storage) {
        model.brcb_definitions.push_back(mms::MmsStaticBrcbDefinition{
            storage.domain,
            storage.item,
            storage.report_id,
            storage.data_set_domain,
            storage.data_set_item,
            storage.conf_revision,
            storage.optional_fields,
            storage.buffer_time_ms,
            storage.trigger_options});
    }
    return model;
}

[[nodiscard]] std::size_t refresh_manifest_values(
    ManifestModel& model,
    std::vector<std::size_t>* const changed_value_indices = nullptr) {
    if (changed_value_indices != nullptr) changed_value_indices->clear();
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
        value.data = mms::MmsSimulatorManifestCodec::data(
            value.type, value.raw_type, value.normalized_type, value.text);
        value.encoded = mms::MmsDataCodec::encode(*value.data);
        if (changed_value_indices != nullptr) {
            changed_value_indices->push_back(found->second);
        }
        ++changed;
    }
    model.revision = revision;
    if (changed != 0U) {
        rebuild_manifest_roots(model);
        for (const auto value_index : model.root_value_indices) {
            model.objects[value_index].type_specification =
                model.values[value_index].type_specification;
        }
    }
    return changed;
}

[[nodiscard]] const mms::MmsStaticDataSetEntry* find_data_set(
    const mms::MmsStaticDataSetTable& data_sets,
    const std::string_view domain,
    const std::string_view item) noexcept {
    for (const auto& data_set : data_sets.data_sets()) {
        if (data_set.domain == domain && data_set.item == item) return &data_set;
    }
    return nullptr;
}

[[nodiscard]] mms::MmsStaticBrcbEventReason brcb_event_reason(
    const ManifestValue& value) noexcept {
    return value.normalized_type == "Quality" || value.item.ends_with("$q")
        ? mms::MmsStaticBrcbEventReason::quality_change
        : mms::MmsStaticBrcbEventReason::data_change;
}

void notify_brcb_changes(
    const ManifestModel& model,
    const std::span<const std::size_t> changed_value_indices,
    const std::span<std::unique_ptr<BrcbAssociationRuntime>> brcbs,
    const std::uint64_t now_ms) {
    for (const auto value_index : changed_value_indices) {
        if (value_index >= model.values.size()) continue;
        const auto& value = model.values[value_index];
        const auto reason = brcb_event_reason(value);
        for (const auto& brcb : brcbs) {
            if (brcb == nullptr || brcb->reports == nullptr || brcb->data_set == nullptr) continue;
            for (std::size_t member_index = 0U;
                 member_index < brcb->data_set->members.size();
                 ++member_index) {
                const auto& member = brcb->data_set->members[member_index];
                if (member.domain != value.domain || member.item != value.item) continue;
                const auto status = brcb->reports->notify(member_index, reason, now_ms);
                if (status != mms::MmsStaticBrcbStatus::ok &&
                    status != mms::MmsStaticBrcbStatus::trigger_not_selected &&
                    status != mms::MmsStaticBrcbStatus::temporarily_unavailable) {
                    std::osyncstream{std::cerr}
                        << "IEDSIM_EVENT kind=brcb_notify_error rcb="
                        << (brcb->definition == nullptr
                                ? std::string_view{"unknown"}
                                : brcb->definition->item)
                        << " status=" << static_cast<unsigned>(status) << '\n';
                }
                break;
            }
        }
    }
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
    std::vector<mms::MmsStaticUrcbState> urcb_states;
    std::vector<mms::MmsStaticObjectEntry> urcb_object_storage;
    std::vector<mms::MmsStaticUrcbObjectContext> urcb_context_storage;
    std::vector<char> urcb_name_storage;
    std::unique_ptr<mms::MmsStaticUrcbRuntime> urcb_runtime;
    std::unique_ptr<mms::MmsStaticUrcbObjectBank> urcb_bank;
    std::vector<std::unique_ptr<BrcbAssociationRuntime>> brcb_runtimes;
    std::vector<mms::MmsStaticDirectBooleanControlState> direct_control_states;
    std::vector<mms::MmsStaticDirectBooleanControlBinding> direct_control_bindings;
    std::vector<mms::MmsStaticObjectEntry> direct_control_objects;
    std::unique_ptr<mms::MmsStaticObjectTable> direct_control_table;

    mms::MmsStaticDispatchPolicy dispatch_policy;
    dispatch_policy.maximum_write_variables = 1U;
    const mms::MmsStaticObjectTable* dispatch_objects = &object_table;
    if (manifest_model != nullptr && !manifest_model->direct_control_storage.empty()) {
        direct_control_states.resize(manifest_model->direct_control_storage.size());
        direct_control_bindings.resize(manifest_model->direct_control_storage.size());
        direct_control_objects.assign(object_table.objects().begin(), object_table.objects().end());
        constexpr std::array<std::uint8_t, 2U> unsigned_type{0x86U, 0x00U};

        for (std::size_t index = 0U; index < manifest_model->direct_control_storage.size(); ++index) {
            auto& control = manifest_model->direct_control_storage[index];
            auto& state = direct_control_states[index];
            auto& binding = direct_control_bindings[index];
            state.value = control.process_value->load(std::memory_order_relaxed);
            binding.state = &state;
            binding.apply = apply_atomic_boolean;
            binding.apply_context = control.process_value.get();

            bool status_found{};
            bool ctl_model_found{};
            for (auto& object : direct_control_objects) {
                if (object.domain != control.domain) continue;
                if (object.item == control.status_item) {
                    object.read = read_atomic_boolean;
                    object.context = control.process_value.get();
                    object.write = nullptr;
                    object.write_context = nullptr;
                    object.contextual_write = nullptr;
                    status_found = true;
                } else if (object.item == control.ctl_model_item) {
                    object.type_specification = std::span<const std::uint8_t>{unsigned_type};
                    object.read = mms::mms_static_direct_normal_read_ctl_model;
                    object.context = nullptr;
                    object.write = nullptr;
                    object.write_context = nullptr;
                    object.contextual_write = nullptr;
                    ctl_model_found = true;
                }
            }
            if (!status_found || !ctl_model_found) {
                throw std::runtime_error("Configured Direct-Normal control is missing ST/CF backing objects.");
            }
            direct_control_objects.push_back(mms::MmsStaticObjectEntry{
                control.domain,
                control.oper_item,
                control.oper_type_specification,
                mms::mms_static_control_read_unavailable,
                nullptr,
                false,
                mms::mms_static_direct_boolean_write_oper,
                &binding,
                nullptr});
        }
        direct_control_table = std::make_unique<mms::MmsStaticObjectTable>(
            std::span<const mms::MmsStaticObjectEntry>{direct_control_objects});
        if (!direct_control_table->valid()) {
            throw std::runtime_error("Configured Direct-Normal MMS object table is invalid.");
        }
        dispatch_objects = direct_control_table.get();
        dispatch_policy.advertise_flattened_child_aliases = true;
    }
    const auto* process_objects = dispatch_objects;
    if (manifest_model != nullptr && !manifest_model->urcb_definitions.empty()) {
        urcb_states.resize(manifest_model->urcb_definitions.size());
        urcb_runtime = std::make_unique<mms::MmsStaticUrcbRuntime>(
            std::span<const mms::MmsStaticUrcbDefinition>{manifest_model->urcb_definitions},
            std::span<mms::MmsStaticUrcbState>{urcb_states},
            *process_objects,
            data_sets);
        if (!urcb_runtime->initialize()) {
            throw std::runtime_error("Could not initialize per-association URCB runtime.");
        }

        mms::MmsStaticUrcbObjectBank sizing_bank{
            *urcb_runtime,
            object_table.objects(),
            std::span<mms::MmsStaticObjectEntry>{},
            std::span<mms::MmsStaticUrcbObjectContext>{},
            std::span<char>{},
            report_now_ms,
            nullptr};
        const auto required_objects = sizing_bank.required_object_capacity();
        const auto required_contexts = sizing_bank.required_context_capacity();
        const auto required_names = sizing_bank.required_name_bytes();
        if (required_objects == std::numeric_limits<std::size_t>::max() ||
            required_contexts == std::numeric_limits<std::size_t>::max() ||
            required_names == std::numeric_limits<std::size_t>::max()) {
            throw std::runtime_error("URCB object-bank capacity calculation failed.");
        }
        urcb_object_storage.resize(required_objects);
        urcb_context_storage.resize(required_contexts);
        urcb_name_storage.resize(required_names);
        urcb_bank = std::make_unique<mms::MmsStaticUrcbObjectBank>(
            *urcb_runtime,
            process_objects->objects(),
            std::span<mms::MmsStaticObjectEntry>{urcb_object_storage},
            std::span<mms::MmsStaticUrcbObjectContext>{urcb_context_storage},
            std::span<char>{urcb_name_storage},
            report_now_ms,
            nullptr);
        if (!urcb_bank->initialize()) {
            throw std::runtime_error("Could not expose URCB MMS attribute objects.");
        }
        dispatch_objects = &urcb_bank->table();
        dispatch_policy.advertise_flattened_child_aliases = true;
    }

    if (manifest_model != nullptr && !manifest_model->brcb_definitions.empty()) {
        brcb_runtimes.reserve(manifest_model->brcb_definitions.size());
        for (const auto& definition : manifest_model->brcb_definitions) {
            auto brcb = std::make_unique<BrcbAssociationRuntime>();
            brcb->definition = &definition;
            brcb->data_set = find_data_set(
                data_sets, definition.data_set_domain, definition.data_set_item);
            if (brcb->data_set == nullptr) {
                throw std::runtime_error("BRCB references an unavailable DataSet.");
            }
            for (std::size_t slot = 0U; slot < brcb->slots.size(); ++slot) {
                brcb->slots[slot] = mms::MmsStaticBrcbSlot{brcb->slot_storage[slot]};
            }
            brcb->reports = std::make_unique<mms::MmsStaticBrcbRuntime>(
                definition,
                brcb->pending,
                std::span<mms::MmsStaticBrcbSlot>{brcb->slots},
                *process_objects,
                data_sets);
            if (!brcb->reports->initialize()) {
                throw std::runtime_error("Could not initialize per-association BRCB runtime.");
            }
            brcb->control = std::make_unique<mms::MmsStaticBrcbControl>(*brcb->reports);

            mms::MmsStaticBrcbObjectBank sizing_bank{
                definition,
                *brcb->reports,
                *brcb->control,
                dispatch_objects->objects(),
                std::span<mms::MmsStaticObjectEntry>{},
                std::span<mms::MmsStaticBrcbObjectContext>{},
                std::span<char>{},
                report_now_ms,
                nullptr};
            const auto required_objects = sizing_bank.required_object_capacity();
            const auto required_names = sizing_bank.required_name_bytes();
            if (required_objects == std::numeric_limits<std::size_t>::max() ||
                required_names == std::numeric_limits<std::size_t>::max()) {
                throw std::runtime_error("BRCB object-bank capacity calculation failed.");
            }
            brcb->object_storage.resize(required_objects);
            brcb->name_storage.resize(required_names);
            brcb->bank = std::make_unique<mms::MmsStaticBrcbObjectBank>(
                definition,
                *brcb->reports,
                *brcb->control,
                dispatch_objects->objects(),
                std::span<mms::MmsStaticObjectEntry>{brcb->object_storage},
                std::span<mms::MmsStaticBrcbObjectContext>{brcb->context_storage},
                std::span<char>{brcb->name_storage},
                report_now_ms,
                nullptr);
            if (!brcb->bank->initialize()) {
                throw std::runtime_error("Could not expose BRCB MMS attribute objects.");
            }
            dispatch_objects = &brcb->bank->table();
            brcb_runtimes.push_back(std::move(brcb));
        }
        dispatch_policy.advertise_flattened_child_aliases = true;
    }

    const mms::MmsStaticApplicationDispatcher dispatcher{
        *dispatch_objects, data_sets, dispatch_policy};

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

    const auto close_brcbs = [&] {
        const auto now_ms = monotonic_ms();
        for (auto& brcb : brcb_runtimes) {
            if (brcb != nullptr && brcb->control != nullptr) {
                brcb->control->on_association_closed(association_id, now_ms);
            }
        }
    };

    std::vector<std::size_t> changed_value_indices;
    if (manifest_model != nullptr) {
        changed_value_indices.reserve(manifest_model->values.size());
    }
    auto previous_state = runtime.state();
    auto next_model_refresh = std::chrono::steady_clock::now();
    std::size_t total_received = 0U;
    std::size_t total_sent = 0U;
    while (!g_stop.load(std::memory_order_relaxed)) {
        const auto now = std::chrono::steady_clock::now();
        if (manifest_model != nullptr && now >= next_model_refresh) {
            next_model_refresh = now + std::chrono::milliseconds{25};
            try {
                const auto changed = refresh_manifest_values(
                    *manifest_model, &changed_value_indices);
                if (changed != 0U) {
                    if (urcb_bank != nullptr && !urcb_bank->initialize()) {
                        throw std::runtime_error(
                            "URCB object bank could not refresh its base model views.");
                    }
                    for (auto& brcb : brcb_runtimes) {
                        if (brcb != nullptr && brcb->bank != nullptr &&
                            !brcb->bank->initialize()) {
                            throw std::runtime_error(
                                "BRCB object bank could not refresh its base model views.");
                        }
                    }
                    notify_brcb_changes(
                        *manifest_model,
                        changed_value_indices,
                        brcb_runtimes,
                        monotonic_ms());
                    std::osyncstream{std::cout}
                        << "IEDSIM_EVENT kind=value_sync association="
                        << association_id << " changed=" << changed
                        << " revision=" << manifest_model->revision << '\n';
                }
            } catch (const std::exception& exception) {
                std::osyncstream{std::cerr}
                    << "IEDSIM_EVENT kind=value_sync_error association="
                    << association_id << " message=" << exception.what() << '\n';
            }
        }

        const auto result = session.poll_once();
        total_received += result.bytes_received;
        total_sent += result.bytes_sent;
        const auto current_state = runtime.state();
        if (current_state != previous_state) {
            std::osyncstream{std::cout}
                << "IEDSIM_EVENT kind=protocol_stage association="
                << association_id << " remote=" << remote
                << " stage=" << connection_state_text(current_state) << '\n';
            previous_state = current_state;
        }
        if (result.application_service != mms::MmsWireConfirmedService::unknown) {
            std::osyncstream{std::cout}
                << "IEDSIM_EVENT kind=mms_service association="
                << association_id << " remote=" << remote
                << " service=" << service_text(result.application_service)
                << " invoke=" << result.invoke_id
                << " accepted="
                << (result.status == mms::MmsStaticServerSessionStatus::application_rejected
                        ? "false" : "true")
                << '\n';
        }
        if (result.terminal()) {
            std::osyncstream{std::cout}
                << "IEDSIM_EVENT kind=client_closed association="
                << association_id << " remote=" << remote
                << " rx=" << total_received << " tx=" << total_sent
                << " state=" << connection_state_text(runtime.state()) << '\n';
            close_brcbs();
            return;
        }

        const auto now_ms = monotonic_ms();
        if (!brcb_runtimes.empty()) {
            const auto binary_time = report_binary_time();
            for (auto& brcb : brcb_runtimes) {
                if (brcb == nullptr || brcb->reports == nullptr) continue;
                mms::MmsStaticBrcbCapturePlan plan;
                if (!brcb->reports->next_due(now_ms, plan)) continue;
                const auto capture = brcb->reports->capture(
                    plan,
                    binary_time,
                    buffers.report_frame,
                    buffers.report_workspace);
                if (!capture.success()) {
                    std::osyncstream{std::cerr}
                        << "IEDSIM_EVENT kind=brcb_capture_error association="
                        << association_id << " rcb="
                        << (brcb->definition == nullptr
                                ? std::string_view{"unknown"}
                                : brcb->definition->item)
                        << " status=" << static_cast<unsigned>(capture.status)
                        << " required=" << capture.required_bytes << '\n';
                }
            }
        }

        if (urcb_runtime != nullptr && session.pending_output_bytes() == 0U) {
            const auto binary_time = report_binary_time();
            const auto report = mms::MmsStaticReportConnection::poll(
                runtime,
                *urcb_runtime,
                now_ms,
                binary_time,
                buffers.report_frame,
                buffers.report_workspace);
            if (report.response_ready()) {
                if (!send_all(
                        socket,
                        std::span<const std::uint8_t>{buffers.report_frame}.first(
                            report.bytes_written))) {
                    std::osyncstream{std::cerr}
                        << "IEDSIM_EVENT kind=report_send_error association="
                        << association_id << " remote=" << remote << '\n';
                    close_brcbs();
                    return;
                }
                total_sent += report.bytes_written;
                const auto* definition = urcb_runtime->definition(report.control_block_index);
                std::osyncstream{std::cout}
                    << "IEDSIM_EVENT kind=report_sent association="
                    << association_id << " buffered=false rcb="
                    << (definition == nullptr ? std::string_view{"unknown"} : definition->item)
                    << " sqnum=" << static_cast<unsigned>(report.sequence_number)
                    << " reason=" << static_cast<unsigned>(report.reason)
                    << " bytes=" << report.bytes_written << '\n';
            } else if (
                report.status == mms::MmsStaticReportConnectionStatus::response_buffer_too_small ||
                report.status == mms::MmsStaticReportConnectionStatus::workspace_too_small ||
                report.status == mms::MmsStaticReportConnectionStatus::report_encode_failed) {
                std::osyncstream{std::cerr}
                    << "IEDSIM_EVENT kind=report_error association="
                    << association_id
                    << " status=" << static_cast<unsigned>(report.status)
                    << " urcb_status=" << static_cast<unsigned>(report.urcb_status)
                    << '\n';
            }
        }

        if (!brcb_runtimes.empty() && session.pending_output_bytes() == 0U) {
            for (auto& brcb : brcb_runtimes) {
                if (brcb == nullptr || brcb->reports == nullptr || brcb->control == nullptr) {
                    continue;
                }
                const auto staged = mms::MmsStaticBrcbConnection::poll(
                    runtime,
                    *brcb->control,
                    *brcb->reports,
                    now_ms,
                    buffers.report_frame,
                    buffers.report_workspace);
                if (!staged.response_ready()) continue;
                if (!send_all(
                        socket,
                        std::span<const std::uint8_t>{buffers.report_frame}.first(
                            staged.bytes_written))) {
                    std::osyncstream{std::cerr}
                        << "IEDSIM_EVENT kind=brcb_send_error association="
                        << association_id << " remote=" << remote << '\n';
                    close_brcbs();
                    return;
                }
                if (!mms::MmsStaticBrcbConnection::commit_sent(
                        runtime,
                        *brcb->control,
                        *brcb->reports,
                        now_ms,
                        staged)) {
                    std::osyncstream{std::cerr}
                        << "IEDSIM_EVENT kind=brcb_commit_error association="
                        << association_id << " rcb="
                        << (brcb->definition == nullptr
                                ? std::string_view{"unknown"}
                                : brcb->definition->item)
                        << '\n';
                    close_brcbs();
                    return;
                }
                total_sent += staged.bytes_written;
                std::osyncstream{std::cout}
                    << "IEDSIM_EVENT kind=report_sent association="
                    << association_id << " buffered=true rcb="
                    << (brcb->definition == nullptr
                            ? std::string_view{"unknown"}
                            : brcb->definition->item)
                    << " sqnum=" << static_cast<unsigned>(staged.sequence_number)
                    << " bytes=" << staged.bytes_written << '\n';
                break;
            }
        }

        if (result.status == mms::MmsStaticServerSessionStatus::would_block ||
            result.status == mms::MmsStaticServerSessionStatus::timed_out) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }
    close_brcbs();
    runtime.close_transport();
}

struct WorkerSlot final {
    std::jthread thread;
    std::shared_ptr<std::atomic_bool> done;
};

[[nodiscard]] WorkerSlot* available_worker(std::vector<WorkerSlot>& workers) {
    for (auto& worker : workers) {
        if (!worker.thread.joinable()) return &worker;
        if (worker.done != nullptr && worker.done->load(std::memory_order_acquire)) {
            worker.thread.join();
            worker.done.reset();
            return &worker;
        }
    }
    return nullptr;
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
            "ESP32S3IOLD0", "LLN0", lln0_type, read_encoded, &root_values[0]};
        objects[1] = mms::MmsStaticObjectEntry{
            "ESP32S3IOLD0", "LPHD1", lphd1_type, read_encoded, &root_values[1]};
        objects[2] = mms::MmsStaticObjectEntry{
            "ESP32S3IOLD0", "GGIO1", ggio1_type, read_encoded, &root_values[2]};
        objects[3] = mms::MmsStaticObjectEntry{
            "ESP32S3IOLD0", "LLN0$ST$Mod$stVal", boolean_type, read_boolean, &values[0]};
        objects[4] = mms::MmsStaticObjectEntry{
            "ESP32S3IOLD0", "LPHD1$ST$PhyHealth$stVal", boolean_type, read_boolean, &values[1]};
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
        const auto exposed_objects = object_span.size() +
            manifest_model.urcb_definitions.size() *
                mms::MmsStaticUrcbObjectBank::attributes_per_control_block +
            manifest_model.brcb_definitions.size() *
                mms::MmsStaticBrcbObjectBank::attributes_per_control_block;
        std::osyncstream{std::cout}
            << "IEDSIM_EVENT kind=server_ready bind="
            << options.bind_address << " port=" << options.port
            << " objects=" << exposed_objects
            << " domains=" << domain_names.size()
            << " datasets=" << data_set_span.size()
            << " urcbs=" << manifest_model.urcb_definitions.size()
            << " brcbs=" << manifest_model.brcb_definitions.size()
            << " declared_brcbs=" << manifest_model.buffered_report_controls
            << " omitted_urcbs=" << manifest_model.omitted_urcbs
            << " omitted_brcbs=" << manifest_model.omitted_brcbs
            << " truncated=" << truncated
            << " max_active=" << options.maximum_active_connections
            << " profile=iedscout" << '\n';

        std::vector<WorkerSlot> workers(options.maximum_active_connections);
        std::size_t connection_count = 0U;
        while (!g_stop.load(std::memory_order_relaxed) &&
               (options.maximum_connections == 0U ||
                connection_count < options.maximum_connections)) {
            auto* worker = available_worker(workers);
            if (worker == nullptr) {
                std::this_thread::sleep_for(std::chrono::milliseconds{2});
                continue;
            }

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
                if (g_stop.load(std::memory_order_relaxed) || socket_interrupted()) continue;
                std::osyncstream{std::cerr}
                    << "accept() failed: " << socket_error_text() << '\n';
                continue;
            }

            ++connection_count;
            const auto association_id = static_cast<std::uint64_t>(connection_count);
            const auto remote = peer_address(peer);
            std::osyncstream{std::cout}
                << "IEDSIM_EVENT kind=client_connected association="
                << association_id << " remote=" << remote << '\n';

            worker->done = std::make_shared<std::atomic_bool>(false);
            const auto done = worker->done;
            worker->thread = std::jthread([
                &options,
                &manifest_type,
                &manifest_value,
                &object_table,
                &data_sets,
                client,
                association_id,
                remote,
                done] {
                try {
                    if (!options.model_manifest.empty()) {
                        auto local_model = load_manifest_model(
                            options.model_manifest, manifest_type, manifest_value);
                        const auto local_object_span =
                            std::span<const mms::MmsStaticObjectEntry>{local_model.objects};
                        const auto local_data_set_span =
                            std::span<const mms::MmsStaticDataSetEntry>{local_model.data_sets};
                        const mms::MmsStaticObjectTable local_object_table{local_object_span};
                        const mms::MmsStaticDataSetTable local_data_sets{local_data_set_span};
                        if (!local_object_table.valid() ||
                            !local_data_sets.valid() ||
                            !local_data_sets.valid_against(local_object_table)) {
                            throw std::runtime_error(
                                "Per-association MMS manifest model is invalid.");
                        }
                        serve_connection(
                            client,
                            local_object_table,
                            local_data_sets,
                            &local_model,
                            association_id,
                            remote);
                    } else {
                        serve_connection(
                            client,
                            object_table,
                            data_sets,
                            nullptr,
                            association_id,
                            remote);
                    }
                } catch (const std::exception& exception) {
                    std::osyncstream{std::cerr}
                        << "IEDSIM_EVENT kind=client_error association="
                        << association_id << " remote=" << remote
                        << " message=" << exception.what() << '\n';
                }
                close_socket(client);
                done->store(true, std::memory_order_release);
            });
        }

        close_socket(listener);
        for (auto& worker : workers) {
            if (worker.thread.joinable()) worker.thread.join();
        }
        std::osyncstream{std::cout}
            << "IEDSIM_EVENT kind=server_stopped connections="
            << connection_count << '\n';
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Static IED server failed: " << exception.what() << '\n';
        return 2;
    }
}
