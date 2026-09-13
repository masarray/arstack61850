// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/ethernet/ethernet.hpp"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

#if defined(__linux__)
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ar::iec61850::goose {

// Minimal RAII Layer-2 sender for IEC 61850 GOOSE. The caller must bind an
// explicit interface; this class never guesses, falls back, or opens a wider
// protocol surface than the selected link. On platforms without a supported
// Layer-2 backend open() fails closed with a diagnostic rather than silently
// degrading to an IP socket.
class RawEthernetPublisher final {
public:
    RawEthernetPublisher() = default;
    ~RawEthernetPublisher() { close(); }

    RawEthernetPublisher(const RawEthernetPublisher&) = delete;
    RawEthernetPublisher& operator=(const RawEthernetPublisher&) = delete;

    RawEthernetPublisher(RawEthernetPublisher&& other) noexcept { move_from(other); }
    RawEthernetPublisher& operator=(RawEthernetPublisher&& other) noexcept {
        if (this != &other) {
            close();
            move_from(other);
        }
        return *this;
    }

    [[nodiscard]] bool open(std::string_view interface_name, std::string& error) {
        close();
        error.clear();
        if (interface_name.empty()) {
            error = "GOOSE publication requires an explicit Ethernet interface.";
            return false;
        }
#if defined(__linux__)
        if (interface_name.size() >= IFNAMSIZ) {
            error = "Ethernet interface name exceeds IFNAMSIZ.";
            return false;
        }

        const std::string name{interface_name};
        const unsigned int index = ::if_nametoindex(name.c_str());
        if (index == 0U) {
            error = "Ethernet interface was not found: " + name;
            return false;
        }

        const int socket_fd = ::socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
        if (socket_fd < 0) {
            error = "Could not open AF_PACKET raw socket for GOOSE publication: " +
                std::string{std::strerror(errno)};
            return false;
        }

        struct ifreq request {};
        std::strncpy(request.ifr_name, name.c_str(), IFNAMSIZ - 1);
        if (::ioctl(socket_fd, SIOCGIFHWADDR, &request) != 0) {
            error = "Could not read Ethernet source MAC for " + name + ": " +
                std::string{std::strerror(errno)};
            ::close(socket_fd);
            return false;
        }

        struct sockaddr_ll bind_address {};
        bind_address.sll_family = AF_PACKET;
        bind_address.sll_protocol = htons(ethernet::goose_ethertype);
        bind_address.sll_ifindex = static_cast<int>(index);
        if (::bind(
                socket_fd,
                reinterpret_cast<const struct sockaddr*>(&bind_address),
                sizeof(bind_address)) != 0) {
            error = "Could not bind GOOSE raw socket to " + name + ": " +
                std::string{std::strerror(errno)};
            ::close(socket_fd);
            return false;
        }

        socket_fd_ = socket_fd;
        interface_index_ = static_cast<int>(index);
        interface_name_ = name;
        const auto* bytes = reinterpret_cast<const unsigned char*>(request.ifr_hwaddr.sa_data);
        for (std::size_t offset = 0; offset < source_mac_.size(); ++offset) {
            source_mac_[offset] = static_cast<std::uint8_t>(bytes[offset]);
        }
        return true;
#else
        (void)interface_name;
        error = "Raw Ethernet GOOSE publication is unavailable on this platform; "
                "the current production backend requires Linux AF_PACKET.";
        return false;
#endif
    }

    [[nodiscard]] bool transmit(
        const std::span<const std::uint8_t> ethernet_frame,
        std::string& error) noexcept {
        error.clear();
        if (ethernet_frame.size() < 14U) {
            error = "GOOSE Ethernet frame is shorter than the Ethernet header.";
            return false;
        }
#if defined(__linux__)
        if (socket_fd_ < 0 || interface_index_ <= 0) {
            error = "GOOSE raw Ethernet publisher is not open.";
            return false;
        }
        struct sockaddr_ll destination {};
        destination.sll_family = AF_PACKET;
        destination.sll_protocol = htons(ethernet::goose_ethertype);
        destination.sll_ifindex = interface_index_;
        destination.sll_halen = ETH_ALEN;
        std::memcpy(destination.sll_addr, ethernet_frame.data(), ETH_ALEN);

        const auto sent = ::sendto(
            socket_fd_,
            ethernet_frame.data(),
            ethernet_frame.size(),
            0,
            reinterpret_cast<const struct sockaddr*>(&destination),
            sizeof(destination));
        if (sent < 0 || static_cast<std::size_t>(sent) != ethernet_frame.size()) {
            error = "GOOSE raw Ethernet transmit failed on " + interface_name_ + ": " +
                std::string{std::strerror(errno)};
            return false;
        }
        return true;
#else
        (void)ethernet_frame;
        error = "GOOSE raw Ethernet publisher is unavailable on this platform.";
        return false;
#endif
    }

    void close() noexcept {
#if defined(__linux__)
        if (socket_fd_ >= 0) ::close(socket_fd_);
        socket_fd_ = -1;
        interface_index_ = 0;
#endif
        interface_name_.clear();
        source_mac_.fill(0U);
    }

    [[nodiscard]] bool active() const noexcept {
#if defined(__linux__)
        return socket_fd_ >= 0 && interface_index_ > 0;
#else
        return false;
#endif
    }
    [[nodiscard]] const std::string& interface_name() const noexcept { return interface_name_; }
    [[nodiscard]] ethernet::MacAddress source_mac() const {
        return ethernet::MacAddress{std::span<const std::uint8_t>{source_mac_}};
    }

private:
    void move_from(RawEthernetPublisher& other) noexcept {
#if defined(__linux__)
        socket_fd_ = other.socket_fd_;
        interface_index_ = other.interface_index_;
        other.socket_fd_ = -1;
        other.interface_index_ = 0;
#endif
        interface_name_ = std::move(other.interface_name_);
        source_mac_ = other.source_mac_;
        other.interface_name_.clear();
        other.source_mac_.fill(0U);
    }

#if defined(__linux__)
    int socket_fd_{-1};
    int interface_index_{};
#endif
    std::string interface_name_;
    std::array<std::uint8_t, 6> source_mac_{};
};

} // namespace ar::iec61850::goose
