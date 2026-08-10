// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_server_session.hpp"
#include "ariec61850/mms/static_object_table.hpp"
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
#include <vector>

namespace {

using namespace ar::iec61850;

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

struct Options final {
    std::string bind_address{"0.0.0.0"};
    std::uint16_t port{8102U};
    std::uint8_t digital_input_mask{};
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
        if (ready_) {
            WSACleanup();
        }
#endif
    }

    SocketSystem(const SocketSystem&) = delete;
    SocketSystem& operator=(const SocketSystem&) = delete;

    [[nodiscard]] bool ready() const noexcept {
        return ready_;
    }

private:
    bool ready_{};
};

class OwnedSocket final {
public:
    OwnedSocket() noexcept = default;
    explicit OwnedSocket(const SocketHandle handle) noexcept : handle_{handle} {}

    ~OwnedSocket() {
        reset();
    }

    OwnedSocket(const OwnedSocket&) = delete;
    OwnedSocket& operator=(const OwnedSocket&) = delete;

    OwnedSocket(OwnedSocket&& other) noexcept : handle_{other.release()} {}

    OwnedSocket& operator=(OwnedSocket&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    [[nodiscard]] SocketHandle get() const noexcept {
        return handle_;
    }

    [[nodiscard]] bool valid() const noexcept {
        return handle_ != kInvalidSocket;
    }

    [[nodiscard]] SocketHandle release() noexcept {
        const auto handle = handle_;
        handle_ = kInvalidSocket;
        return handle;
    }

    void reset(const SocketHandle replacement = kInvalidSocket) noexcept {
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
    const auto requested = static_cast<int>(std::min<std::size_t>(
        destination.size(),
        static_cast<std::size_t>(std::numeric_limits<int>::max())));
    const auto received = recv(
        socket,
        reinterpret_cast<char*>(destination.data()),
        requested,
        0);
    if (received > 0) {
        return {
            embedded::IoStatus::ok,
            static_cast<std::size_t>(received)};
    }
    if (received == 0) {
        return {embedded::IoStatus::closed, 0U};
    }
    const auto error = last_socket_error();
    if (is_would_block(error)) {
        return {embedded::IoStatus::would_block, 0U};
    }
    if (is_timeout(error) || is_interrupted(error)) {
        return {embedded::IoStatus::timeout, 0U};
    }
    return {embedded::IoStatus::io_error, 0U};
}

[[nodiscard]] embedded::IoResult socket_send(
    void* context,
    const std::span<const std::uint8_t> bytes) noexcept {
    const auto socket = *static_cast<const SocketHandle*>(context);
    const auto requested = static_cast<int>(std::min<std::size_t>(
        bytes.size(),
        static_cast<std::size_t>(std::numeric_limits<int>::max())));
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    const auto sent = send(
        socket,
        reinterpret_cast<const char*>(bytes.data()),
        requested,
        flags);
    if (sent > 0) {
        return {embedded::IoStatus::ok, static_cast<std::size_t>(sent)};
    }
    if (sent == 0) {
        return {embedded::IoStatus::closed, 0U};
    }
    const auto error = last_socket_error();
    if (is_would_block(error)) {
        return {embedded::IoStatus::would_block, 0U};
    }
    if (is_timeout(error) || is_interrupted(error)) {
        return {embedded::IoStatus::timeout, 0U};
    }
    return {embedded::IoStatus::io_error, 0U};
}

void print_help() {
    std::cout
        << "arstack61850 static IEC 61850 IED server\n\n"
        << "Usage:\n"
        << "  ariec61850_static_ied_server [options]\n\n"
        << "Options:\n"
        << "  --bind ADDRESS     Bind address (default 0.0.0.0)\n"
        << "  --port PORT        TCP port (default 8102; use 102 for IEC 61850)\n"
        << "  --di-mask VALUE    Initial GGIO1 Ind1..Ind8 bit mask, decimal or 0xHEX\n"
        << "  --once             Serve one TCP connection, then exit\n"
        << "  --help             Show this help\n\n"
        << "Read-only MMS services: association, Initiate, GetNameList, "
           "GetVariableAccessAttributes, Read.\n";
}

template <typename Integer>
[[nodiscard]] bool parse_integer(
    const std::string_view text,
    Integer& output) noexcept {
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
            std::cerr << "Missing value after " << argument << "\n";
            return false;
        }
        const std::string_view value{argv[++index]};
        if (argument == "--bind") {
            options.bind_address.assign(value);
        } else if (argument == "--port") {
            if (!parse_integer(value, options.port) || options.port == 0U) {
                std::cerr << "Invalid TCP port: " << value << "\n";
                return false;
            }
        } else if (argument == "--di-mask") {
            if (!parse_integer(value, options.digital_input_mask)) {
                std::cerr << "Invalid DI mask: " << value << "\n";
                return false;
            }
        } else {
            std::cerr << "Unknown option: " << argument << "\n";
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
    const auto address_status = getaddrinfo(
        options.bind_address.empty() ? nullptr : options.bind_address.c_str(),
        service.c_str(),
        &hints,
        &addresses);
    if (address_status != 0) {
        std::cerr << "Unable to resolve bind address: "
                  << options.bind_address << "\n";
        return {};
    }

    OwnedSocket listener;
    for (auto* address = addresses; address != nullptr; address = address->ai_next) {
        OwnedSocket candidate{socket(
            address->ai_family,
            address->ai_socktype,
            address->ai_protocol)};
        if (!candidate.valid()) {
            continue;
        }
        constexpr int enabled = 1;
        static_cast<void>(setsockopt(
            candidate.get(),
            SOL_SOCKET,
            SO_REUSEADDR,
            reinterpret_cast<const char*>(&enabled),
            static_cast<int>(sizeof(enabled))));
        if (bind(
                candidate.get(),
                address->ai_addr,
                static_cast<int>(address->ai_addrlen)) == 0 &&
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
    static_cast<void>(setsockopt(
        socket,
        SOL_SOCKET,
        SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeout_ms),
        static_cast<int>(sizeof(timeout_ms))));
    static_cast<void>(setsockopt(
        socket,
        SOL_SOCKET,
        SO_SNDTIMEO,
        reinterpret_cast<const char*>(&timeout_ms),
        static_cast<int>(sizeof(timeout_ms))));
#else
    constexpr timeval timeout{1, 0};
    static_cast<void>(setsockopt(
        socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)));
    static_cast<void>(setsockopt(
        socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)));
#endif
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
    destination[0] = 0x83U;
    destination[1] = 0x01U;
    destination[2] = *static_cast<const std::uint8_t*>(context) != 0U
        ? 0xFFU
        : 0x00U;
    return {wire::EncodeStatus::ok, required, required};
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
    const auto bytes = static_cast<const EncodedValue*>(context)->bytes;
    if (destination.size() < bytes.size()) {
        return {wire::EncodeStatus::buffer_too_small, 0U, bytes.size()};
    }
    std::copy(bytes.begin(), bytes.end(), destination.begin());
    return {wire::EncodeStatus::ok, bytes.size(), bytes.size()};
}

[[nodiscard]] mms::MmsTypeSpecification boolean_component(
    const std::string& name) {
    mms::MmsTypeSpecification component;
    component.kind = mms::MmsTypeKind::boolean;
    component.name = name;
    return component;
}

[[nodiscard]] mms::MmsTypeSpecification structure_component(
    const std::string& name,
    std::vector<mms::MmsTypeSpecification> children) {
    mms::MmsTypeSpecification component;
    component.kind = mms::MmsTypeKind::structure;
    component.name = name;
    component.children = std::move(children);
    return component;
}

[[nodiscard]] std::vector<std::uint8_t> build_single_status_ln_type(
    const std::string& data_object) {
    mms::MmsTypeSpecification root;
    root.kind = mms::MmsTypeKind::structure;
    root.children.push_back(structure_component(
        "ST",
        {structure_component(
            data_object,
            {boolean_component("stVal")})}));
    return mms::MmsServiceCodec::encode_type_specification(root);
}

[[nodiscard]] std::vector<std::uint8_t> build_ggio_type() {
    std::vector<mms::MmsTypeSpecification> indications;
    indications.reserve(8U);
    for (std::size_t index = 0U; index < 8U; ++index) {
        indications.push_back(structure_component(
            "Ind" + std::to_string(index + 1U),
            {boolean_component("stVal")}));
    }
    mms::MmsTypeSpecification root;
    root.kind = mms::MmsTypeKind::structure;
    root.children.push_back(structure_component("ST", std::move(indications)));
    return mms::MmsServiceCodec::encode_type_specification(root);
}

[[nodiscard]] bool serve_client(
    SocketHandle client,
    const mms::MmsStaticApplicationDispatcher& dispatcher) {
    mms::MmsStaticConnectionRuntime runtime{dispatcher};
    embedded::TcpByteStream stream{&client, socket_send, socket_receive};
    std::array<std::uint8_t, 65'535U> receive{};
    std::array<std::uint8_t, 65'535U> response{};
    std::array<std::uint8_t, 65'535U> workspace{};
    mms::MmsStaticServerSession session{
        runtime,
        stream,
        {receive, response, workspace}};

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
        case mms::MmsStaticServerSessionStatus::transport_error:
            std::cerr << "Client transport failed.\n";
            return false;
        case mms::MmsStaticServerSessionStatus::protocol_error:
            std::cerr << "Malformed or unsupported connection sequence.\n";
            return false;
        case mms::MmsStaticServerSessionStatus::receive_buffer_full:
            std::cerr << "Client TPKT exceeds the bounded receive capacity.\n";
            return false;
        case mms::MmsStaticServerSessionStatus::invalid_configuration:
            std::cerr << "Server session configuration is invalid.\n";
            return false;
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    bool help{};
    if (!parse_options(argc, argv, options, help)) {
        return 2;
    }
    if (help) {
        print_help();
        return 0;
    }

    SocketSystem socket_system;
    if (!socket_system.ready()) {
        std::cerr << "Unable to initialize the platform socket API.\n";
        return 3;
    }

    constexpr std::array<std::uint8_t, 2U> boolean_type{0x83U, 0x00U};
    constexpr std::array<std::string_view, 8U> leaf_items{
        "GGIO1$ST$Ind1$stVal",
        "GGIO1$ST$Ind2$stVal",
        "GGIO1$ST$Ind3$stVal",
        "GGIO1$ST$Ind4$stVal",
        "GGIO1$ST$Ind5$stVal",
        "GGIO1$ST$Ind6$stVal",
        "GGIO1$ST$Ind7$stVal",
        "GGIO1$ST$Ind8$stVal"};
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
        ggio_data[offset + 4U] = values[index + 2U] != 0U ? 0xFFU : 0x00U;
    }
    const std::array<EncodedValue, 3U> root_values{
        EncodedValue{healthy_ln_data},
        EncodedValue{healthy_ln_data},
        EncodedValue{ggio_data}};

    std::array<mms::MmsStaticObjectEntry, 13U> objects{};
    objects[0] = mms::MmsStaticObjectEntry{
        "ESP32S3IOLD0", "LLN0", lln0_type,
        read_encoded, &root_values[0], false, nullptr, nullptr, false};
    objects[1] = mms::MmsStaticObjectEntry{
        "ESP32S3IOLD0", "LPHD1", lphd1_type,
        read_encoded, &root_values[1], false, nullptr, nullptr, false};
    objects[2] = mms::MmsStaticObjectEntry{
        "ESP32S3IOLD0", "GGIO1", ggio1_type,
        read_encoded, &root_values[2], false, nullptr, nullptr, false};
    objects[3] = mms::MmsStaticObjectEntry{
        "ESP32S3IOLD0", "LLN0$ST$Mod$stVal", boolean_type,
        read_boolean, &values[0]};
    objects[4] = mms::MmsStaticObjectEntry{
        "ESP32S3IOLD0", "LPHD1$ST$PhyHealth$stVal", boolean_type,
        read_boolean, &values[1]};
    for (std::size_t index = 0U; index < leaf_items.size(); ++index) {
        objects[index + 5U] = mms::MmsStaticObjectEntry{
            "ESP32S3IOLD0",
            leaf_items[index],
            boolean_type,
            read_boolean,
            &values[index + 2U]};
    }

    const mms::MmsStaticObjectTable object_table{objects};
    if (!object_table.valid()) {
        std::cerr << "Static MMS model is invalid.\n";
        return 4;
    }
    const mms::MmsStaticApplicationDispatcher dispatcher{object_table};
    auto listener = create_listener(options);
    if (!listener.valid()) {
        std::cerr << "Unable to bind/listen on " << options.bind_address
                  << ':' << options.port << ".\n";
        return 5;
    }

    std::cout << "STATIC_IED_SERVER_READY bind=" << options.bind_address
              << " port=" << options.port
              << " domain=ESP32S3IOLD0 listed_variables=10 root_types=3"
                 " readable_leaves=10\n";
    do {
        sockaddr_storage peer{};
        socklen_t peer_size = sizeof(peer);
        OwnedSocket client{accept(
            listener.get(),
            reinterpret_cast<sockaddr*>(&peer),
            &peer_size)};
        if (!client.valid()) {
            const auto error = last_socket_error();
            if (is_interrupted(error)) {
                continue;
            }
            std::cerr << "TCP accept failed with platform error " << error << ".\n";
            return 6;
        }
        set_client_timeouts(client.get());
        std::cout << "STATIC_IED_CLIENT_ACCEPTED\n";
        const auto clean_close = serve_client(client.get(), dispatcher);
        std::cout << (clean_close ? "STATIC_IED_CLIENT_CLOSED"
                                  : "STATIC_IED_CLIENT_ABORTED")
                  << "\n";
    } while (!options.serve_once);

    return 0;
}
