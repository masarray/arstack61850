// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/ethernet/ethernet.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/filter.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif

namespace ar::iec61850::goose {

enum class RawEthernetReceiveResult : std::uint8_t {
    none,
    frame,
    failure,
};

// Minimal RAII Layer-2 receiver for IEC 61850 GOOSE. The caller must bind an
// explicit interface. Linux uses AF_PACKET with a kernel BPF for untagged and
// 802.1Q-tagged GOOSE. Windows loads Npcap dynamically and installs the same
// logical capture filter, avoiding a build-time Npcap dependency.
class RawEthernetSubscriber final {
public:
    RawEthernetSubscriber() = default;
    ~RawEthernetSubscriber() { close(); }

    RawEthernetSubscriber(const RawEthernetSubscriber&) = delete;
    RawEthernetSubscriber& operator=(const RawEthernetSubscriber&) = delete;

    RawEthernetSubscriber(RawEthernetSubscriber&& other) noexcept { move_from(other); }
    RawEthernetSubscriber& operator=(RawEthernetSubscriber&& other) noexcept {
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
            error = "GOOSE monitoring requires an explicit Ethernet interface.";
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
            error = "Could not open AF_PACKET raw socket for GOOSE monitoring: " +
                std::string{std::strerror(errno)};
            return false;
        }

        struct sockaddr_ll bind_address {};
        bind_address.sll_family = AF_PACKET;
        bind_address.sll_protocol = htons(ETH_P_ALL);
        bind_address.sll_ifindex = static_cast<int>(index);
        if (::bind(
                socket_fd,
                reinterpret_cast<const struct sockaddr*>(&bind_address),
                sizeof(bind_address)) != 0) {
            error = "Could not bind GOOSE monitor to " + name + ": " +
                std::string{std::strerror(errno)};
            ::close(socket_fd);
            return false;
        }

        struct sock_filter filter[] = {
            BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 12),
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ethernet::goose_ethertype, 4, 0),
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ethernet::vlan_tag_ethertype, 0, 2),
            BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 16),
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ethernet::goose_ethertype, 1, 0),
            BPF_STMT(BPF_RET | BPF_K, 0),
            BPF_STMT(BPF_RET | BPF_K, 0xFFFF),
        };
        struct sock_fprog program {};
        program.len = static_cast<unsigned short>(std::size(filter));
        program.filter = filter;
        if (::setsockopt(
                socket_fd,
                SOL_SOCKET,
                SO_ATTACH_FILTER,
                &program,
                static_cast<socklen_t>(sizeof(program))) != 0) {
            error = "Could not install the GOOSE EtherType capture filter on " + name + ": " +
                std::string{std::strerror(errno)};
            ::close(socket_fd);
            return false;
        }

        const int flags = ::fcntl(socket_fd, F_GETFL, 0);
        if (flags < 0 || ::fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) != 0) {
            error = "Could not make the GOOSE monitor socket non-blocking on " + name + ": " +
                std::string{std::strerror(errno)};
            ::close(socket_fd);
            return false;
        }

        socket_fd_ = socket_fd;
        interface_index_ = static_cast<int>(index);
        interface_name_ = name;
        return true;
#elif defined(_WIN32)
        const std::string requested_name{interface_name};
        wpcap_module_ = ::LoadLibraryA("wpcap.dll");
        if (wpcap_module_ == nullptr) {
            error = "Npcap wpcap.dll was not found. Install Npcap to monitor Layer-2 GOOSE on Windows.";
            return false;
        }
        pcap_open_live_ = reinterpret_cast<PcapOpenLive>(
            ::GetProcAddress(wpcap_module_, "pcap_open_live"));
        pcap_next_ex_ = reinterpret_cast<PcapNextEx>(
            ::GetProcAddress(wpcap_module_, "pcap_next_ex"));
        pcap_close_ = reinterpret_cast<PcapClose>(
            ::GetProcAddress(wpcap_module_, "pcap_close"));
        pcap_geterr_ = reinterpret_cast<PcapGetErr>(
            ::GetProcAddress(wpcap_module_, "pcap_geterr"));
        pcap_findalldevs_ = reinterpret_cast<PcapFindAllDevs>(
            ::GetProcAddress(wpcap_module_, "pcap_findalldevs"));
        pcap_freealldevs_ = reinterpret_cast<PcapFreeAllDevs>(
            ::GetProcAddress(wpcap_module_, "pcap_freealldevs"));
        pcap_compile_ = reinterpret_cast<PcapCompile>(
            ::GetProcAddress(wpcap_module_, "pcap_compile"));
        pcap_setfilter_ = reinterpret_cast<PcapSetFilter>(
            ::GetProcAddress(wpcap_module_, "pcap_setfilter"));
        pcap_freecode_ = reinterpret_cast<PcapFreeCode>(
            ::GetProcAddress(wpcap_module_, "pcap_freecode"));
        if (pcap_open_live_ == nullptr || pcap_next_ex_ == nullptr || pcap_close_ == nullptr ||
            pcap_geterr_ == nullptr || pcap_findalldevs_ == nullptr || pcap_freealldevs_ == nullptr ||
            pcap_compile_ == nullptr || pcap_setfilter_ == nullptr || pcap_freecode_ == nullptr) {
            error = "Npcap is present but required packet-capture/filter exports are unavailable.";
            close();
            return false;
        }

        std::array<char, 256> errbuf{};
        PcapIf* devices{};
        if (pcap_findalldevs_(&devices, errbuf.data()) != 0 || devices == nullptr) {
            error = "Npcap could not enumerate Ethernet adapters";
            if (errbuf.front() != '\0') error += ": " + std::string{errbuf.data()};
            close();
            return false;
        }

        std::string pcap_name;
        for (auto* device = devices; device != nullptr; device = device->next) {
            const std::string device_name = device->name == nullptr ? std::string{} : device->name;
            const std::string description =
                device->description == nullptr ? std::string{} : device->description;
            if (!windows_interface_matches(requested_name, device_name, description)) continue;
            pcap_name = device_name;
            break;
        }
        pcap_freealldevs_(devices);
        devices = nullptr;
        if (pcap_name.empty()) {
            error = "Npcap could not resolve the selected Ethernet interface: " + requested_name;
            close();
            return false;
        }

        errbuf.fill('\0');
        pcap_handle_ = pcap_open_live_(pcap_name.c_str(), 65'535, 1, 1, errbuf.data());
        if (pcap_handle_ == nullptr) {
            error = "Npcap could not open adapter " + requested_name + ": " + std::string{errbuf.data()};
            close();
            return false;
        }

        BpfProgram program{};
        constexpr const char* filter = "(ether proto 0x88b8) or (vlan and ether proto 0x88b8)";
        if (pcap_compile_(pcap_handle_, &program, filter, 1, 0xFFFFFFFFU) != 0) {
            error = "Npcap could not compile the GOOSE EtherType filter";
            const char* detail = pcap_geterr_(pcap_handle_);
            if (detail != nullptr && *detail != '\0') error += ": " + std::string{detail};
            close();
            return false;
        }
        const int filter_result = pcap_setfilter_(pcap_handle_, &program);
        pcap_freecode_(&program);
        if (filter_result != 0) {
            error = "Npcap could not install the GOOSE EtherType filter";
            const char* detail = pcap_geterr_(pcap_handle_);
            if (detail != nullptr && *detail != '\0') error += ": " + std::string{detail};
            close();
            return false;
        }

        interface_name_ = requested_name;
        return true;
#else
        (void)interface_name;
        error = "Raw Ethernet GOOSE monitoring is unavailable on this platform.";
        return false;
#endif
    }

    [[nodiscard]] RawEthernetReceiveResult receive(
        std::vector<std::uint8_t>& ethernet_frame,
        std::string& error) noexcept {
        ethernet_frame.clear();
        error.clear();
#if defined(__linux__)
        if (socket_fd_ < 0 || interface_index_ <= 0) {
            error = "GOOSE raw Ethernet subscriber is not open.";
            return RawEthernetReceiveResult::failure;
        }
        std::array<std::uint8_t, 65'535> buffer{};
        const auto received = ::recv(socket_fd_, buffer.data(), buffer.size(), 0);
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return RawEthernetReceiveResult::none;
            }
            error = "GOOSE raw Ethernet receive failed on " + interface_name_ + ": " +
                std::string{std::strerror(errno)};
            return RawEthernetReceiveResult::failure;
        }
        if (received == 0) return RawEthernetReceiveResult::none;
        ethernet_frame.assign(buffer.begin(), buffer.begin() + received);
        return RawEthernetReceiveResult::frame;
#elif defined(_WIN32)
        if (pcap_handle_ == nullptr || pcap_next_ex_ == nullptr) {
            error = "GOOSE Npcap subscriber is not open.";
            return RawEthernetReceiveResult::failure;
        }
        PcapPacketHeader* header{};
        const unsigned char* data{};
        const int result = pcap_next_ex_(pcap_handle_, &header, &data);
        if (result == 0) return RawEthernetReceiveResult::none;
        if (result != 1 || header == nullptr || data == nullptr) {
            error = "GOOSE Npcap receive failed on " + interface_name_;
            const char* detail = pcap_geterr_ != nullptr ? pcap_geterr_(pcap_handle_) : nullptr;
            if (detail != nullptr && *detail != '\0') error += ": " + std::string{detail};
            return RawEthernetReceiveResult::failure;
        }
        if (header->caplen < 14U || header->caplen > 65'535U) {
            error = "GOOSE Npcap capture returned an invalid frame length.";
            return RawEthernetReceiveResult::failure;
        }
        ethernet_frame.assign(data, data + header->caplen);
        return RawEthernetReceiveResult::frame;
#else
        error = "Raw Ethernet GOOSE monitoring is unavailable on this platform.";
        return RawEthernetReceiveResult::failure;
#endif
    }

    void close() noexcept {
#if defined(__linux__)
        if (socket_fd_ >= 0) ::close(socket_fd_);
        socket_fd_ = -1;
        interface_index_ = 0;
#elif defined(_WIN32)
        if (pcap_handle_ != nullptr && pcap_close_ != nullptr) pcap_close_(pcap_handle_);
        pcap_handle_ = nullptr;
        pcap_open_live_ = nullptr;
        pcap_next_ex_ = nullptr;
        pcap_close_ = nullptr;
        pcap_geterr_ = nullptr;
        pcap_findalldevs_ = nullptr;
        pcap_freealldevs_ = nullptr;
        pcap_compile_ = nullptr;
        pcap_setfilter_ = nullptr;
        pcap_freecode_ = nullptr;
        if (wpcap_module_ != nullptr) ::FreeLibrary(wpcap_module_);
        wpcap_module_ = nullptr;
#endif
        interface_name_.clear();
    }

    [[nodiscard]] bool active() const noexcept {
#if defined(__linux__)
        return socket_fd_ >= 0 && interface_index_ > 0;
#elif defined(_WIN32)
        return pcap_handle_ != nullptr;
#else
        return false;
#endif
    }

    [[nodiscard]] const std::string& interface_name() const noexcept { return interface_name_; }

private:
#if defined(_WIN32)
    struct pcap;
    struct pcap_addr;
    struct PcapIf final {
        PcapIf* next;
        char* name;
        char* description;
        pcap_addr* addresses;
        unsigned int flags;
    };
    struct PcapPacketHeader final {
        timeval ts;
        unsigned int caplen;
        unsigned int len;
    };
    struct BpfProgram final {
        unsigned int bf_len;
        void* bf_insns;
    };
    using PcapHandle = pcap;
    using PcapOpenLive = PcapHandle* (__cdecl *)(const char*, int, int, int, char*);
    using PcapNextEx = int (__cdecl *)(PcapHandle*, PcapPacketHeader**, const unsigned char**);
    using PcapClose = void (__cdecl *)(PcapHandle*);
    using PcapGetErr = char* (__cdecl *)(PcapHandle*);
    using PcapFindAllDevs = int (__cdecl *)(PcapIf**, char*);
    using PcapFreeAllDevs = void (__cdecl *)(PcapIf*);
    using PcapCompile = int (__cdecl *)(PcapHandle*, BpfProgram*, const char*, int, unsigned int);
    using PcapSetFilter = int (__cdecl *)(PcapHandle*, BpfProgram*);
    using PcapFreeCode = void (__cdecl *)(BpfProgram*);

    [[nodiscard]] static std::string ascii_lower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    [[nodiscard]] static std::string normalized_windows_interface(std::string value) {
        value = ascii_lower(std::move(value));
        constexpr std::string_view prefix{"\\\\device\\npf_"};
        if (value.rfind(prefix.data(), 0U) == 0U) value.erase(0U, prefix.size());
        return value;
    }

    [[nodiscard]] static bool windows_interface_matches(
        const std::string& requested,
        const std::string& pcap_name,
        const std::string& description) {
        const auto wanted = normalized_windows_interface(requested);
        const auto device = normalized_windows_interface(pcap_name);
        const auto label = ascii_lower(description);
        if (wanted == device || ascii_lower(requested) == label) return true;
        const auto brace = device.find('{');
        return brace != std::string::npos && wanted.find(device.substr(brace)) != std::string::npos;
    }

    HMODULE wpcap_module_{};
    PcapHandle* pcap_handle_{};
    PcapOpenLive pcap_open_live_{};
    PcapNextEx pcap_next_ex_{};
    PcapClose pcap_close_{};
    PcapGetErr pcap_geterr_{};
    PcapFindAllDevs pcap_findalldevs_{};
    PcapFreeAllDevs pcap_freealldevs_{};
    PcapCompile pcap_compile_{};
    PcapSetFilter pcap_setfilter_{};
    PcapFreeCode pcap_freecode_{};
#elif defined(__linux__)
    int socket_fd_{-1};
    int interface_index_{};
#endif
    std::string interface_name_;

    void move_from(RawEthernetSubscriber& other) noexcept {
#if defined(__linux__)
        socket_fd_ = std::exchange(other.socket_fd_, -1);
        interface_index_ = std::exchange(other.interface_index_, 0);
#elif defined(_WIN32)
        wpcap_module_ = std::exchange(other.wpcap_module_, nullptr);
        pcap_handle_ = std::exchange(other.pcap_handle_, nullptr);
        pcap_open_live_ = std::exchange(other.pcap_open_live_, nullptr);
        pcap_next_ex_ = std::exchange(other.pcap_next_ex_, nullptr);
        pcap_close_ = std::exchange(other.pcap_close_, nullptr);
        pcap_geterr_ = std::exchange(other.pcap_geterr_, nullptr);
        pcap_findalldevs_ = std::exchange(other.pcap_findalldevs_, nullptr);
        pcap_freealldevs_ = std::exchange(other.pcap_freealldevs_, nullptr);
        pcap_compile_ = std::exchange(other.pcap_compile_, nullptr);
        pcap_setfilter_ = std::exchange(other.pcap_setfilter_, nullptr);
        pcap_freecode_ = std::exchange(other.pcap_freecode_, nullptr);
#endif
        interface_name_ = std::move(other.interface_name_);
    }
};

} // namespace ar::iec61850::goose
