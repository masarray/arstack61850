// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_data_set_table.hpp"
#include "ariec61850/mms/static_direct_control.hpp"
#include "ariec61850/mms/static_server_session.hpp"
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
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
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
constexpr SocketHandle k_invalid_socket = INVALID_SOCKET;
#else
using SocketHandle = int;
using SocketLength = socklen_t;
constexpr SocketHandle k_invalid_socket = -1;
#endif

struct Options final {
    std::string bind_address{"0.0.0.0"};
    std::uint16_t port{8102U};
    std::uint8_t digital_input_mask{};
    std::uint8_t digital_output_mask{};
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
    explicit OwnedSocket(const SocketHandle handle) noexcept : handle_{handle} {}
    ~OwnedSocket() { reset(); }
    OwnedSocket(const OwnedSocket&) = delete;
    OwnedSocket& operator=(const OwnedSocket&) = delete;
    OwnedSocket(OwnedSocket&& other) noexcept : handle_{other.release()} {}
    OwnedSocket& operator=(OwnedSocket&& other) noexcept {
        if (this != &other) reset(other.release());
        return *this;
    }
    [[nodiscard]] SocketHandle get() const noexcept { return handle_; }
    [[nodiscard]] bool valid() const noexcept { return handle_ != k_invalid_socket; }
    [[nodiscard]] SocketHandle release() noexcept {
        const auto value = handle_;
        handle_ = k_invalid_socket;
        return value;
    }
    void reset(const SocketHandle replacement = k_invalid_socket) noexcept {
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
    SocketHandle handle_{k_invalid_socket};
};

[[nodiscard]] int last_socket_error() noexcept {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

[[nodiscard]] bool is_would_block(const int error) noexcept {
#ifdef _WIN32
    return error == WSAEWOULDBLOCK;
#else
    return error == EAGAIN || error == EWOULDBLOCK;
#endif
}

[[nodiscard]] bool is_timeout(const int error) noexcept {
#ifdef _WIN32
    return error == WSAETIMEDOUT;
#else
    return error == ETIMEDOUT;
#endif
}

[[nodiscard]] bool is_interrupted(const int error) noexcept {
#ifdef _WIN32
    return error == WSAEINTR;
#else
    return error == EINTR;
#endif
}

[[nodiscard]] embedded::IoResult socket_receive(
    void* context,
    const std::span<std::uint8_t> destination) noexcept {
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
    const std::span<const std::uint8_t> bytes) noexcept {
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
    const auto sent = send(
        socket, reinterpret_cast<const char*>(bytes.data()), requested, flags);
    if (sent > 0) return {embedded::IoStatus::ok, static_cast<std::size_t>(sent)};
    if (sent == 0) return {embedded::IoStatus::closed, 0U};
    const auto error = last_socket_error();
    if (is_would_block(error)) return {embedded::IoStatus::would_block, 0U};
    if (is_timeout(error) || is_interrupted(error)) return {embedded::IoStatus::timeout, 0U};
    return {embedded::IoStatus::io_error, 0U};
}

template <typename Integer>
[[nodiscard]] bool parse_integer(const std::string_view text, Integer& output) noexcept {
    auto digits = text;
    int base = 10;
    if (digits.size() > 2U && digits[0] == '0' &&
        (digits[1] == 'x' || digits[1] == 'X')) {
        digits.remove_prefix(2U);
        base = 16;
    }
    unsigned int parsed{};
    const auto result = std::from_chars(
        digits.data(), digits.data() + digits.size(), parsed, base);
    if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size() ||
        parsed > static_cast<unsigned int>(std::numeric_limits<Integer>::max())) {
        return false;
    }
    output = static_cast<Integer>(parsed);
    return true;
}

void print_help() {
    std::cout
        << "ARStack61850 bounded Direct-Normal MMS control server\n\n"
        << "Usage:\n"
        << "  ariec61850_mms_direct_control_server [options]\n\n"
        << "Options:\n"
        << "  --bind ADDRESS   Bind address (default 0.0.0.0)\n"
        << "  --port PORT      TCP port (default 8102; use 102 for IEC 61850)\n"
        << "  --di-mask VALUE  Initial GGIO1 Ind1..Ind8 bit mask\n"
        << "  --do-mask VALUE  Initial GGIO1 SPCSO1..8 status bit mask\n"
        << "  --once           Serve one TCP connection, then exit\n"
        << "  --help           Show this help\n\n"
        << "Control objects: ESP32S3IOLD0/GGIO1.SPCSO1 .. SPCSO8\n"
        << "ctlModel: Direct-with-normal-security (1).\n"
        << "Check bits are fail-closed; Test=true is acknowledged without changing output.\n";
}

[[nodiscard]] bool parse_options(
    const int argc,
    char** argv,
    Options& options,
    bool& help) {
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
        if (index + 1 >= argc) {
            std::cerr << "Missing value after " << argument << '\n';
            return false;
        }
        const std::string_view value{argv[++index]};
        if (argument == "--bind") {
            options.bind_address.assign(value);
        } else if (argument == "--port") {
            if (!parse_integer(value, options.port) || options.port == 0U) return false;
        } else if (argument == "--di-mask") {
            if (!parse_integer(value, options.digital_input_mask)) return false;
        } else if (argument == "--do-mask") {
            if (!parse_integer(value, options.digital_output_mask)) return false;
        } else {
            std::cerr << "Unknown option: " << argument << '\n';
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
    if (getaddrinfo(
            options.bind_address.empty() ? nullptr : options.bind_address.c_str(),
            service.c_str(), &hints, &addresses) != 0) {
        return {};
    }

    OwnedSocket listener;
    for (auto* address = addresses; address != nullptr; address = address->ai_next) {
        OwnedSocket candidate{socket(
            address->ai_family, address->ai_socktype, address->ai_protocol)};
        if (!candidate.valid()) continue;
        constexpr int enabled = 1;
        static_cast<void>(setsockopt(
            candidate.get(), SOL_SOCKET, SO_REUSEADDR,
            reinterpret_cast<const char*>(&enabled),
            static_cast<SocketLength>(sizeof(enabled))));
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

void set_client_timeouts(const SocketHandle socket) noexcept {
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

[[nodiscard]] wire::EncodeResult read_boolean(
    const void* context,
    const std::span<std::uint8_t> destination) noexcept {
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
    const std::span<std::uint8_t> destination) noexcept {
    if (context == nullptr) return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    const auto bytes = static_cast<const EncodedValue*>(context)->bytes;
    if (destination.size() < bytes.size()) return {wire::EncodeStatus::buffer_too_small, 0U, bytes.size()};
    std::copy(bytes.begin(), bytes.end(), destination.begin());
    return {wire::EncodeStatus::ok, bytes.size(), bytes.size()};
}

[[nodiscard]] mms::MmsTypeSpecification scalar(
    const mms::MmsTypeKind kind,
    std::string name,
    const std::optional<std::uint32_t> size = std::nullopt) {
    mms::MmsTypeSpecification result;
    result.kind = kind;
    result.name = std::move(name);
    result.size = size;
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

[[nodiscard]] mms::MmsTypeSpecification direct_boolean_oper_spec() {
    return structure("Oper", {
        scalar(mms::MmsTypeKind::boolean, "ctlVal"),
        structure("origin", {
            scalar(mms::MmsTypeKind::unsigned_integer, "orCat"),
            scalar(mms::MmsTypeKind::octet_string, "orIdent", 64U),
        }),
        scalar(mms::MmsTypeKind::unsigned_integer, "ctlNum"),
        scalar(mms::MmsTypeKind::utc_time, "T"),
        scalar(mms::MmsTypeKind::boolean, "Test"),
        scalar(mms::MmsTypeKind::bit_string, "Check", 2U),
    });
}

[[nodiscard]] std::vector<std::uint8_t> build_single_status_ln_type(
    const std::string& data_object) {
    return mms::MmsServiceCodec::encode_type_specification(structure("", {
        structure("ST", {structure(data_object, {scalar(mms::MmsTypeKind::boolean, "stVal")})})
    }));
}

[[nodiscard]] std::vector<std::uint8_t> build_ggio_type() {
    std::vector<mms::MmsTypeSpecification> status;
    std::vector<mms::MmsTypeSpecification> config;
    std::vector<mms::MmsTypeSpecification> controls;
    status.reserve(16U);
    config.reserve(8U);
    controls.reserve(8U);
    for (std::size_t index = 0U; index < 8U; ++index) {
        const auto number = std::to_string(index + 1U);
        status.push_back(structure("Ind" + number, {scalar(mms::MmsTypeKind::boolean, "stVal")}));
        status.push_back(structure("SPCSO" + number, {scalar(mms::MmsTypeKind::boolean, "stVal")}));
        config.push_back(structure("SPCSO" + number, {
            scalar(mms::MmsTypeKind::unsigned_integer, "ctlModel")
        }));
        controls.push_back(structure("SPCSO" + number, {direct_boolean_oper_spec()}));
    }
    return mms::MmsServiceCodec::encode_type_specification(structure("", {
        structure("ST", std::move(status)),
        structure("CF", std::move(config)),
        structure("CO", std::move(controls)),
    }));
}

[[nodiscard]] bool serve_client(
    const SocketHandle client,
    const mms::MmsStaticApplicationDispatcher& dispatcher) {
    mms::MmsStaticConnectionRuntime runtime{dispatcher};
    auto socket_copy = client;
    embedded::TcpByteStream stream{&socket_copy, socket_send, socket_receive};
    std::array<std::uint8_t, 65'535U> receive{};
    std::array<std::uint8_t, 65'535U> response{};
    std::array<std::uint8_t, 65'535U> workspace{};
    mms::MmsStaticServerSession session{runtime, stream, {receive, response, workspace}};
    for (;;) {
        const auto result = session.poll_once();
        switch (result.status) {
        case mms::MmsStaticServerSessionStatus::progressed:
        case mms::MmsStaticServerSessionStatus::response_pending:
        case mms::MmsStaticServerSessionStatus::would_block:
        case mms::MmsStaticServerSessionStatus::timed_out:
            continue;
        case mms::MmsStaticServerSessionStatus::application_rejected:
            std::cerr << "MMS request rejected; connection remains open.\n";
            continue;
        case mms::MmsStaticServerSessionStatus::peer_closed:
            return true;
        default:
            return false;
        }
    }
}

[[nodiscard]] std::uint8_t output_mask(
    const std::array<mms::MmsStaticDirectBooleanControlState, 8U>& states) noexcept {
    std::uint8_t mask{};
    for (std::size_t index = 0U; index < states.size(); ++index) {
        if (states[index].value != 0U) mask = static_cast<std::uint8_t>(mask | (1U << index));
    }
    return mask;
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
    constexpr std::array<std::uint8_t, 2U> unsigned_type{0x86U, 0x00U};
    const auto oper_type = mms::MmsServiceCodec::encode_type_specification(direct_boolean_oper_spec());
    const auto lln0_type = build_single_status_ln_type("Mod");
    const auto lphd1_type = build_single_status_ln_type("PhyHealth");
    const auto ggio1_type = build_ggio_type();

    constexpr std::array<std::uint8_t, 9U> healthy_ln_data{
        0xA2U, 0x07U, 0xA2U, 0x05U, 0xA2U, 0x03U, 0x83U, 0x01U, 0xFFU};
    const std::array<EncodedValue, 2U> root_values{
        EncodedValue{healthy_ln_data}, EncodedValue{healthy_ln_data}};

    std::array<std::uint8_t, 10U> read_values{};
    read_values[0] = 1U;
    read_values[1] = 1U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        read_values[index + 2U] = static_cast<std::uint8_t>((options.digital_input_mask >> index) & 0x01U);
    }

    std::array<mms::MmsStaticDirectBooleanControlState, 8U> control_states{};
    std::array<mms::MmsStaticDirectBooleanControlBinding, 8U> control_bindings{};
    std::array<std::string, 8U> input_items{};
    std::array<std::string, 8U> output_status_items{};
    std::array<std::string, 8U> ctl_model_items{};
    std::array<std::string, 8U> oper_items{};

    for (std::size_t index = 0U; index < 8U; ++index) {
        const auto number = std::to_string(index + 1U);
        input_items[index] = "GGIO1$ST$Ind" + number + "$stVal";
        output_status_items[index] = "GGIO1$ST$SPCSO" + number + "$stVal";
        ctl_model_items[index] = "GGIO1$CF$SPCSO" + number + "$ctlModel";
        oper_items[index] = "GGIO1$CO$SPCSO" + number + "$Oper";
        control_states[index].value = static_cast<std::uint8_t>(
            (options.digital_output_mask >> index) & 0x01U);
        control_bindings[index].state = &control_states[index];
    }

    std::array<mms::MmsStaticObjectEntry, 37U> objects{};
    objects[0] = {domain, "LLN0", lln0_type, read_encoded, &root_values[0], false, nullptr, nullptr, false};
    objects[1] = {domain, "LPHD1", lphd1_type, read_encoded, &root_values[1], false, nullptr, nullptr, false};
    objects[2] = {domain, "GGIO1", ggio1_type, mms::mms_static_control_read_unavailable, nullptr, false, nullptr, nullptr, false};
    objects[3] = {domain, "LLN0$ST$Mod$stVal", boolean_type, read_boolean, &read_values[0]};
    objects[4] = {domain, "LPHD1$ST$PhyHealth$stVal", boolean_type, read_boolean, &read_values[1]};
    for (std::size_t index = 0U; index < 8U; ++index) {
        objects[5U + index] = {domain, input_items[index], boolean_type, read_boolean, &read_values[index + 2U]};
        const auto base = 13U + index * 3U;
        objects[base] = {domain, output_status_items[index], boolean_type,
            mms::mms_static_direct_boolean_read_state, &control_states[index]};
        objects[base + 1U] = {domain, ctl_model_items[index], unsigned_type,
            mms::mms_static_direct_normal_read_ctl_model, nullptr};
        objects[base + 2U] = {domain, oper_items[index], oper_type,
            mms::mms_static_control_read_unavailable, nullptr, false,
            mms::mms_static_direct_boolean_write_oper, &control_bindings[index]};
    }

    const mms::MmsStaticObjectTable object_table{objects};
    if (!object_table.valid()) {
        std::cerr << "Static MMS control model is invalid.\n";
        return 4;
    }

    std::array<mms::MmsStaticDataSetMember, 8U> output_members{};
    for (std::size_t index = 0U; index < output_members.size(); ++index) {
        output_members[index] = {domain, output_status_items[index]};
    }
    const std::array<mms::MmsStaticDataSetEntry, 1U> data_sets{{
        {domain, "LLN0$Outputs", output_members, false}
    }};
    const mms::MmsStaticDataSetTable data_set_table{data_sets};
    if (!data_set_table.valid_against(object_table)) {
        std::cerr << "Static output DataSet is invalid.\n";
        return 4;
    }

    mms::MmsStaticDispatchPolicy policy;
    policy.maximum_write_variables = 1U;
    const mms::MmsStaticApplicationDispatcher dispatcher{object_table, data_set_table, policy};

    auto listener = create_listener(options);
    if (!listener.valid()) {
        std::cerr << "Unable to bind/listen on " << options.bind_address << ':' << options.port << ".\n";
        return 5;
    }

    std::cout << "MMS_DIRECT_CONTROL_SERVER_READY bind=" << options.bind_address
              << " port=" << options.port
              << " domain=" << domain
              << " controls=8 ctlModel=1 dataset=ESP32S3IOLD0/LLN0.Outputs"
              << " doMask=" << static_cast<unsigned>(output_mask(control_states)) << '\n';

    do {
        sockaddr_storage peer{};
        SocketLength peer_size = static_cast<SocketLength>(sizeof(peer));
        OwnedSocket client{accept(
            listener.get(), reinterpret_cast<sockaddr*>(&peer), &peer_size)};
        if (!client.valid()) {
            const auto error = last_socket_error();
            if (is_interrupted(error)) continue;
            std::cerr << "TCP accept failed with platform error " << error << ".\n";
            return 6;
        }
        set_client_timeouts(client.get());
        std::cout << "MMS_DIRECT_CONTROL_CLIENT_ACCEPTED\n";
        const auto clean_close = serve_client(client.get(), dispatcher);
        std::size_t accepted{};
        std::size_t rejected{};
        for (const auto& state : control_states) {
            accepted += state.accepted_operations;
            rejected += state.rejected_operations;
        }
        std::cout << "MMS_DIRECT_CONTROL_STATE doMask="
                  << static_cast<unsigned>(output_mask(control_states))
                  << " acceptedOps=" << accepted
                  << " rejectedOps=" << rejected << '\n';
        std::cout << (clean_close ? "MMS_DIRECT_CONTROL_CLIENT_CLOSED"
                                  : "MMS_DIRECT_CONTROL_CLIENT_ABORTED") << '\n';
    } while (!options.serve_once);

    return 0;
}
