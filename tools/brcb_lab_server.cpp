// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_brcb_connection.hpp"
#include "ariec61850/mms/static_brcb_objects.hpp"
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
#include <iomanip>
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
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
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

[[nodiscard]] bool interrupted_socket_error() noexcept {
#if defined(_WIN32)
    return ::WSAGetLastError() == WSAEINTR;
#else
    return errno == EINTR;
#endif
}

[[nodiscard]] bool wait_readable(
    const NativeSocket socket,
    const std::uint32_t timeout_ms) noexcept {
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(socket, &read_set);
    timeval timeout{};
    timeout.tv_sec = static_cast<long>(timeout_ms / 1'000U);
    timeout.tv_usec = static_cast<long>((timeout_ms % 1'000U) * 1'000U);
#if defined(_WIN32)
    const auto result = ::select(0, &read_set, nullptr, nullptr, &timeout);
#else
    const auto result = ::select(socket + 1, &read_set, nullptr, nullptr, &timeout);
#endif
    return result > 0 && FD_ISSET(socket, &read_set) != 0;
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
    if (!wait_readable(stream.socket, 100U)) {
        return {embedded::IoStatus::timeout, 0U};
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
    std::uint16_t port{8102U};
    std::uint32_t report_period_ms{1'000U};
    std::size_t maximum_connections{};
    bool self_test{};
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
        << "Usage: ariec61850_brcb_lab_server [options]\n\n"
        << "Options:\n"
        << "  --port N               TCP listen port (default 8102).\n"
        << "  --report-period-ms N   Toggle GGIO1.Ind1 every N ms while RptEna=true (default 1000; 0 disables).\n"
        << "  --max-connections N    Exit after N accepted TCP connections (default unlimited).\n"
        << "  --self-test            Validate the lab model without opening a socket.\n"
        << "  -h, --help             Show this help.\n\n"
        << "Dedicated IEC 61850 BRCB interoperability lab server. No conformance claim.\n";
}

[[nodiscard]] CliOptions parse_cli(const int argc, char** argv) {
    CliOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "-h" || option == "--help") {
            print_usage();
            std::exit(0);
        }
        if (option == "--self-test") {
            options.self_test = true;
            continue;
        }
        if (option == "--port" || option == "--report-period-ms" ||
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
            } else if (option == "--report-period-ms") {
                options.report_period_ms = parse_u32(option, value, 3'600'000U);
            } else {
                options.maximum_connections = static_cast<std::size_t>(
                    parse_u32(option, value, std::numeric_limits<std::uint32_t>::max()));
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
        return {wire::EncodeStatus::buffer_too_small, 0U, value.bytes.size()};
    }
    std::copy(value.bytes.begin(), value.bytes.end(), destination.begin());
    return {wire::EncodeStatus::ok, value.bytes.size(), value.bytes.size()};
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

[[nodiscard]] std::vector<std::uint8_t> encode_ber_length(const std::size_t length) {
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

void update_ggio_data(
    const std::array<std::uint8_t, 10U>& values,
    std::array<std::uint8_t, 44U>& ggio_data) noexcept {
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
        ggio_data[offset + 4U] = values[index + 2U] != 0U ? 0xFFU : 0x00U;
    }
}

[[nodiscard]] std::uint64_t monotonic_ms(const void*) noexcept {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

struct LifecycleSink final {
    mms::MmsStaticBrcbControl* control{};
};

void association_closed(
    void* context,
    const std::uint64_t association_id,
    const std::uint64_t now_ms) noexcept {
    auto* sink = static_cast<LifecycleSink*>(context);
    if (sink != nullptr && sink->control != nullptr) {
        sink->control->on_association_closed(association_id, now_ms);
    }
}

[[nodiscard]] std::array<std::uint8_t, 4U> peer_owner(const sockaddr_in& peer) noexcept {
    const auto host = ntohl(peer.sin_addr.s_addr);
    return {
        static_cast<std::uint8_t>((host >> 24U) & 0xFFU),
        static_cast<std::uint8_t>((host >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((host >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(host & 0xFFU)};
}

void print_entry_id(const std::array<std::uint8_t, 8U>& entry_id) {
    const auto flags = std::cout.flags();
    const auto fill = std::cout.fill();
    for (const auto byte : entry_id) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned>(byte);
    }
    std::cout.flags(flags);
    std::cout.fill(fill);
}

struct ConnectionBuffers final {
    std::array<std::uint8_t, 32'768U> receive{};
    std::array<std::uint8_t, 32'768U> response{};
    std::array<std::uint8_t, 16'384U> workspace{};
    std::array<std::uint8_t, 32'768U> report_frame{};
    std::array<std::uint8_t, 32'768U> report_workspace{};
    std::array<std::uint8_t, 16'384U> capture_encode{};
    std::array<std::uint8_t, 8'192U> capture_workspace{};
};

void serve_connection(
    const NativeSocket socket,
    const sockaddr_in& peer,
    const mms::MmsStaticObjectTable& object_table,
    const mms::MmsStaticDataSetTable& data_sets,
    mms::MmsStaticBrcbRuntime& reports,
    mms::MmsStaticBrcbControl& control,
    std::array<std::uint8_t, 10U>& values,
    std::array<std::uint8_t, 44U>& ggio_data,
    const std::uint64_t association_id,
    const std::uint32_t report_period_ms) {
    const mms::MmsStaticApplicationDispatcher dispatcher{object_table, data_sets};
    const auto owner = peer_owner(peer);
    LifecycleSink lifecycle{&control};

    mms::MmsStaticConnectionPolicy policy;
    policy.association_id = association_id;
    policy.owner_size = owner.size();
    std::copy(owner.begin(), owner.end(), policy.owner.begin());
    policy.now_ms = monotonic_ms;
    policy.association_closed = association_closed;
    policy.association_closed_context = &lifecycle;

    mms::MmsStaticConnectionRuntime runtime{dispatcher, policy};
    SocketStreamContext socket_context{socket};
    const embedded::TcpByteStream stream{&socket_context, socket_send, socket_receive};
    ConnectionBuffers buffers{};
    mms::MmsStaticServerSession session{
        runtime,
        stream,
        {buffers.receive, buffers.response, buffers.workspace}};

    mms::MmsStaticBrcbConnectionResult staged{};
    std::size_t report_offset = 0U;
    bool report_active = false;
    auto next_event = monotonic_ms(nullptr) + report_period_ms;

    while (!g_stop.load(std::memory_order_relaxed)) {
        const auto now = monotonic_ms(nullptr);

        if (report_active) {
            const auto remaining = std::span<const std::uint8_t>{buffers.report_frame}
                .subspan(report_offset, staged.bytes_written - report_offset);
            const auto sent = stream.send(remaining);
            if (sent.status == embedded::IoStatus::would_block ||
                sent.status == embedded::IoStatus::timeout) {
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
                continue;
            }
            if (!sent.success() || sent.transferred == 0U ||
                sent.transferred > remaining.size()) {
                runtime.close_transport();
                return;
            }
            report_offset += sent.transferred;
            if (report_offset == staged.bytes_written) {
                const auto committed = mms::MmsStaticBrcbConnection::commit_sent(
                    runtime, control, reports, now, staged);
                std::cout << (committed ? "BRCB_REPORT_SENT" : "BRCB_REPORT_SENT_UNCOMMITTED")
                          << " seq=" << static_cast<unsigned>(staged.sequence_number)
                          << " entry=";
                print_entry_id(staged.entry_id);
                std::cout << " bytes=" << staged.bytes_written
                          << " queue=" << reports.queue_size() << '\n';
                std::cout.flush();
                if (!committed) {
                    runtime.close_transport();
                    return;
                }
                report_active = false;
                report_offset = 0U;
                staged = {};
            }
            continue;
        }

        const auto result = session.poll_once();
        if (result.terminal()) {
            return;
        }

        if (report_period_ms != 0U && now >= next_event) {
            values[2] = values[2] == 0U ? 1U : 0U;
            update_ggio_data(values, ggio_data);
            const auto notified = reports.notify(
                0U, mms::MmsStaticBrcbEventReason::data_change, now);
            if (notified == mms::MmsStaticBrcbStatus::ok) {
                std::cout << "BRCB_EVENT member=GGIO1.Ind1 value="
                          << static_cast<unsigned>(values[2]) << '\n';
            }
            next_event = now + report_period_ms;
        }

        mms::MmsStaticBrcbCapturePlan plan;
        if (reports.next_due(now, plan)) {
            const std::span<const std::uint8_t> no_timestamp;
            const auto captured = reports.capture(
                plan,
                no_timestamp,
                buffers.capture_encode,
                buffers.capture_workspace);
            if (!captured.success()) {
                std::cerr << "BRCB_CAPTURE_FAILED status="
                          << static_cast<unsigned>(captured.status) << '\n';
                runtime.close_transport();
                return;
            }
            std::cout << "BRCB_CAPTURED seq=" << static_cast<unsigned>(plan.sequence_number)
                      << " entry=";
            print_entry_id(captured.entry_id);
            std::cout << " retained=" << reports.retained_size()
                      << " queue=" << reports.queue_size() << '\n';
            std::cout.flush();
        }

        const bool idle =
            session.pending_output_bytes() == 0U &&
            session.buffered_input_bytes() == 0U &&
            (result.status == mms::MmsStaticServerSessionStatus::would_block ||
             result.status == mms::MmsStaticServerSessionStatus::timed_out);
        if (idle && runtime.state() == mms::MmsStaticConnectionState::established) {
            staged = mms::MmsStaticBrcbConnection::poll(
                runtime,
                control,
                reports,
                now,
                buffers.report_frame,
                buffers.report_workspace);
            if (staged.response_ready()) {
                report_active = true;
                report_offset = 0U;
                continue;
            }
            if (staged.status == mms::MmsStaticBrcbConnectionStatus::peer_limit_exceeded ||
                staged.status == mms::MmsStaticBrcbConnectionStatus::frame_encode_failed) {
                std::cerr << "BRCB_STAGE_FAILED status="
                          << static_cast<unsigned>(staged.status) << '\n';
                runtime.close_transport();
                return;
            }
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

        const auto lln0_type = build_single_status_ln_type("Mod");
        const auto lphd1_type = build_single_status_ln_type("PhyHealth");
        const auto ggio1_type = build_ggio_type();
        constexpr std::array<std::uint8_t, 9U> healthy_ln_data{
            0xA2U, 0x07U, 0xA2U, 0x05U, 0xA2U, 0x03U, 0x83U, 0x01U, 0xFFU};
        std::array<std::uint8_t, 44U> ggio_data{};
        update_ggio_data(values, ggio_data);
        const std::array<EncodedValue, 3U> root_values{
            EncodedValue{healthy_ln_data},
            EncodedValue{healthy_ln_data},
            EncodedValue{ggio_data}};

        std::array<mms::MmsStaticObjectEntry, 13U> base_objects{};
        base_objects[0] = mms::MmsStaticObjectEntry{
            "ESP32S3IOLD0", "LLN0", lln0_type, read_encoded, &root_values[0]};
        base_objects[1] = mms::MmsStaticObjectEntry{
            "ESP32S3IOLD0", "LPHD1", lphd1_type, read_encoded, &root_values[1]};
        base_objects[2] = mms::MmsStaticObjectEntry{
            "ESP32S3IOLD0", "GGIO1", ggio1_type, read_encoded, &root_values[2]};
        base_objects[3] = mms::MmsStaticObjectEntry{
            "ESP32S3IOLD0", "LLN0$ST$Mod$stVal", boolean_type, read_boolean, &values[0]};
        base_objects[4] = mms::MmsStaticObjectEntry{
            "ESP32S3IOLD0", "LPHD1$ST$PhyHealth$stVal", boolean_type, read_boolean, &values[1]};
        constexpr std::array<std::string_view, 8U> leaf_items{
            "GGIO1$ST$Ind1$stVal", "GGIO1$ST$Ind2$stVal",
            "GGIO1$ST$Ind3$stVal", "GGIO1$ST$Ind4$stVal",
            "GGIO1$ST$Ind5$stVal", "GGIO1$ST$Ind6$stVal",
            "GGIO1$ST$Ind7$stVal", "GGIO1$ST$Ind8$stVal"};
        for (std::size_t index = 0U; index < leaf_items.size(); ++index) {
            base_objects[index + 5U] = mms::MmsStaticObjectEntry{
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
        const mms::MmsStaticObjectTable base_table{base_objects};
        const mms::MmsStaticDataSetTable data_sets{data_set_entries};
        if (!base_table.valid() || !data_sets.valid_against(base_table)) {
            throw std::runtime_error("BRCB lab base model is invalid.");
        }

        const mms::MmsStaticBrcbDefinition definition{
            "ESP32S3IOLD0",
            "LLN0$BR$BRCB1",
            "ESP32S3IOLD0/LLN0$BR$BRCB1",
            "ESP32S3IOLD0",
            "LLN0$EventData",
            1U,
            {0x5FU, 0x80U},
            0U,
            0x70U};
        std::array<std::array<std::uint8_t, 4'096U>, 8U> slot_bytes{};
        std::array<mms::MmsStaticBrcbSlot, 8U> slots{};
        for (std::size_t index = 0U; index < slots.size(); ++index) {
            slots[index].storage = slot_bytes[index];
        }
        mms::MmsStaticBrcbPendingState pending{};
        mms::MmsStaticBrcbRuntime reports{
            definition, pending, slots, base_table, data_sets};
        if (!reports.initialize()) {
            throw std::runtime_error("BRCB runtime initialization failed.");
        }
        mms::MmsStaticBrcbControl control{reports};

        std::array<mms::MmsStaticObjectEntry, 21U> combined_objects{};
        std::array<mms::MmsStaticBrcbObjectContext, 8U> brcb_contexts{};
        std::array<char, 512U> brcb_names{};
        mms::MmsStaticBrcbObjectBank bank{
            definition,
            reports,
            control,
            base_objects,
            combined_objects,
            brcb_contexts,
            brcb_names,
            monotonic_ms,
            nullptr};
        if (!bank.initialize() || !bank.table().valid() ||
            !data_sets.valid_against(bank.table())) {
            throw std::runtime_error("BRCB object bank initialization failed.");
        }

        if (options.self_test) {
            std::cout << "BRCB_LAB_SELF_TEST PASS objects=" << bank.object_count()
                      << " datasets=" << data_set_entries.size()
                      << " brcb=ESP32S3IOLD0/LLN0$BR$BRCB1\n";
            return 0;
        }

        const auto listener = create_listener(options.port);
        std::cout << "BRCB_LAB_SERVER_READY port=" << options.port
                  << " domain=ESP32S3IOLD0"
                  << " brcb=LLN0$BR$BRCB1"
                  << " dataset=LLN0$EventData"
                  << " objects=" << bank.object_count()
                  << " reportPeriodMs=" << options.report_period_ms << '\n';
        std::cout.flush();

        std::size_t connection_count = 0U;
        while (!g_stop.load(std::memory_order_relaxed) &&
               (options.maximum_connections == 0U ||
                connection_count < options.maximum_connections)) {
            if (!wait_readable(listener, 200U)) {
                continue;
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
                if (g_stop.load(std::memory_order_relaxed) || interrupted_socket_error()) {
                    continue;
                }
                std::cerr << "accept() failed: " << socket_error_text() << '\n';
                continue;
            }
            ++connection_count;
            const auto owner = peer_owner(peer);
            std::cout << "CONNECTION_ACCEPTED count=" << connection_count
                      << " owner="
                      << static_cast<unsigned>(owner[0]) << '.'
                      << static_cast<unsigned>(owner[1]) << '.'
                      << static_cast<unsigned>(owner[2]) << '.'
                      << static_cast<unsigned>(owner[3]) << '\n';
            serve_connection(
                client,
                peer,
                bank.table(),
                data_sets,
                reports,
                control,
                values,
                ggio_data,
                static_cast<std::uint64_t>(connection_count),
                options.report_period_ms);
            close_socket(client);
            std::cout << "CONNECTION_CLOSED count=" << connection_count
                      << " retained=" << reports.retained_size()
                      << " queue=" << reports.queue_size() << '\n';
            std::cout.flush();
        }

        close_socket(listener);
        std::cout << "BRCB_LAB_SERVER_STOPPED connections=" << connection_count
                  << " retained=" << reports.retained_size() << '\n';
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "BRCB lab server failed: " << exception.what() << '\n';
        return 2;
    }
}
