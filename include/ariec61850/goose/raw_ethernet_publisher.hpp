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
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#endif

namespace ar::iec61850::goose {

// Minimal RAII Layer-2 sender for IEC 61850 GOOSE. The caller must bind an
// explicit interface; this class never guesses or falls back to another link.
// Linux uses AF_PACKET. Windows dynamically loads Npcap (wpcap.dll) and the
// IP Helper adapter query, avoiding hard link dependencies while still failing
// closed when Npcap, the requested adapter, or its source MAC is unavailable.
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

    [[nodiscard]] bool open(
        std::string_view interface_name,
        std::string& error,
        std::optional<ethernet::MacAddress> configured_source_mac = std::nullopt) {
        close();
        error.clear();
        if (interface_name.empty()) {
            error = "GOOSE publication requires an explicit Ethernet interface.";
            return false;
        }
#if defined(__linux__)
        (void)configured_source_mac;
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
#elif defined(_WIN32)
        const std::string requested_name{interface_name};
        wpcap_module_ = ::LoadLibraryA("wpcap.dll");
        if (wpcap_module_ == nullptr) {
            error = "Npcap wpcap.dll was not found. Install Npcap to publish Layer-2 GOOSE on Windows.";
            return false;
        }
        pcap_open_live_ = reinterpret_cast<PcapOpenLive>(
            ::GetProcAddress(wpcap_module_, "pcap_open_live"));
        pcap_sendpacket_ = reinterpret_cast<PcapSendPacket>(
            ::GetProcAddress(wpcap_module_, "pcap_sendpacket"));
        pcap_close_ = reinterpret_cast<PcapClose>(
            ::GetProcAddress(wpcap_module_, "pcap_close"));
        pcap_geterr_ = reinterpret_cast<PcapGetErr>(
            ::GetProcAddress(wpcap_module_, "pcap_geterr"));
        pcap_findalldevs_ = reinterpret_cast<PcapFindAllDevs>(
            ::GetProcAddress(wpcap_module_, "pcap_findalldevs"));
        pcap_freealldevs_ = reinterpret_cast<PcapFreeAllDevs>(
            ::GetProcAddress(wpcap_module_, "pcap_freealldevs"));
        if (pcap_open_live_ == nullptr || pcap_sendpacket_ == nullptr || pcap_close_ == nullptr ||
            pcap_findalldevs_ == nullptr || pcap_freealldevs_ == nullptr) {
            error = "Npcap is present but required packet-transmit/adapter exports are unavailable.";
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
        std::string adapter_token;
        for (auto* device = devices; device != nullptr; device = device->next) {
            const std::string device_name = device->name == nullptr ? std::string{} : device->name;
            const std::string description =
                device->description == nullptr ? std::string{} : device->description;
            if (!windows_interface_matches(requested_name, device_name, description)) continue;
            pcap_name = device_name;
            adapter_token = adapter_token_from_pcap_name(device_name);
            break;
        }
        pcap_freealldevs_(devices);
        devices = nullptr;
        if (pcap_name.empty()) {
            error = "Npcap could not resolve the selected Ethernet interface: " + requested_name;
            close();
            return false;
        }

        std::array<std::uint8_t, 6> resolved_mac{};
        if (configured_source_mac.has_value()) {
            resolved_mac = configured_source_mac->bytes();
        } else if (!resolve_windows_source_mac(adapter_token, resolved_mac, error)) {
            close();
            return false;
        }

        errbuf.fill('\0');
        pcap_handle_ = pcap_open_live_(pcap_name.c_str(), 65'535, 0, 1, errbuf.data());
        if (pcap_handle_ == nullptr) {
            error = "Npcap could not open adapter " + requested_name + ": " + std::string{errbuf.data()};
            close();
            return false;
        }
        interface_name_ = requested_name;
        source_mac_ = resolved_mac;
        return true;
#else
        (void)interface_name;
        (void)configured_source_mac;
        error = "Raw Ethernet GOOSE publication is unavailable on this platform.";
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
#elif defined(_WIN32)
        if (pcap_handle_ == nullptr || pcap_sendpacket_ == nullptr) {
            error = "GOOSE Npcap publisher is not open.";
            return false;
        }
        if (ethernet_frame.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            error = "GOOSE frame is too large for Npcap.";
            return false;
        }
        const int result = pcap_sendpacket_(
            pcap_handle_,
            reinterpret_cast<const unsigned char*>(ethernet_frame.data()),
            static_cast<int>(ethernet_frame.size()));
        if (result != 0) {
            const char* detail = pcap_geterr_ != nullptr ? pcap_geterr_(pcap_handle_) : nullptr;
            error = "GOOSE Npcap transmit failed on " + interface_name_;
            if (detail != nullptr && *detail != '\0') error += ": " + std::string{detail};
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
#elif defined(_WIN32)
        if (pcap_handle_ != nullptr && pcap_close_ != nullptr) pcap_close_(pcap_handle_);
        pcap_handle_ = nullptr;
        pcap_open_live_ = nullptr;
        pcap_sendpacket_ = nullptr;
        pcap_close_ = nullptr;
        pcap_geterr_ = nullptr;
        pcap_findalldevs_ = nullptr;
        pcap_freealldevs_ = nullptr;
        if (wpcap_module_ != nullptr) ::FreeLibrary(wpcap_module_);
        wpcap_module_ = nullptr;
#endif
        interface_name_.clear();
        source_mac_.fill(0U);
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
    [[nodiscard]] ethernet::MacAddress source_mac() const {
        return ethernet::MacAddress{std::span<const std::uint8_t>{source_mac_}};
    }

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
    using PcapHandle = pcap;
    using PcapOpenLive = PcapHandle* (__cdecl *)(const char*, int, int, int, char*);
    using PcapSendPacket = int (__cdecl *)(PcapHandle*, const unsigned char*, int);
    using PcapClose = void (__cdecl *)(PcapHandle*);
    using PcapGetErr = char* (__cdecl *)(PcapHandle*);
    using PcapFindAllDevs = int (__cdecl *)(PcapIf**, char*);
    using PcapFreeAllDevs = void (__cdecl *)(PcapIf*);
    using GetAdaptersAddressesFn = ULONG (WINAPI *)(
        ULONG, ULONG, PVOID, PIP_ADAPTER_ADDRESSES, PULONG);

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

    [[nodiscard]] static std::string adapter_token_from_pcap_name(const std::string& value) {
        constexpr std::string_view prefix{"\\\\Device\\NPF_"};
        if (value.rfind(prefix.data(), 0U) == 0U) return value.substr(prefix.size());
        const auto brace = value.find('{');
        return brace == std::string::npos ? value : value.substr(brace);
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
        if (brace != std::string::npos && wanted.find(device.substr(brace)) != std::string::npos) {
            return true;
        }
        return false;
    }

    [[nodiscard]] static bool resolve_windows_source_mac(
        const std::string& adapter_token,
        std::array<std::uint8_t, 6>& mac,
        std::string& error) {
        HMODULE module = ::LoadLibraryA("iphlpapi.dll");
        if (module == nullptr) {
            error = "Windows IP Helper API is unavailable; source MAC cannot be resolved.";
            return false;
        }
        const auto get_adapters = reinterpret_cast<GetAdaptersAddressesFn>(
            ::GetProcAddress(module, "GetAdaptersAddresses"));
        if (get_adapters == nullptr) {
            error = "Windows IP Helper API does not expose GetAdaptersAddresses.";
            ::FreeLibrary(module);
            return false;
        }

        ULONG bytes{};
        ULONG status = get_adapters(AF_UNSPEC, 0U, nullptr, nullptr, &bytes);
        if (status != ERROR_BUFFER_OVERFLOW || bytes == 0U) {
            error = "Windows adapter inventory size query failed.";
            ::FreeLibrary(module);
            return false;
        }
        std::vector<std::uint8_t> storage(bytes);
        auto* addresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(storage.data());
        status = get_adapters(AF_UNSPEC, 0U, nullptr, addresses, &bytes);
        if (status != NO_ERROR) {
            error = "Windows adapter inventory query failed with code " + std::to_string(status) + ".";
            ::FreeLibrary(module);
            return false;
        }

        const auto wanted = normalized_windows_interface(adapter_token);
        bool found{};
        for (auto* adapter = addresses; adapter != nullptr; adapter = adapter->Next) {
            if (adapter->AdapterName == nullptr) continue;
            if (normalized_windows_interface(adapter->AdapterName) != wanted) continue;
            if (adapter->PhysicalAddressLength != mac.size()) {
                error = "Selected Windows adapter does not expose a 6-byte Ethernet MAC address.";
                ::FreeLibrary(module);
                return false;
            }
            std::copy_n(adapter->PhysicalAddress, mac.size(), mac.begin());
            found = true;
            break;
        }
        ::FreeLibrary(module);
        if (!found) {
            error = "Could not resolve source MAC for selected Windows/Npcap adapter: " + adapter_token;
            return false;
        }
        return true;
    }
#endif

    void move_from(RawEthernetPublisher& other) noexcept {
#if defined(__linux__)
        socket_fd_ = other.socket_fd_;
        interface_index_ = other.interface_index_;
        other.socket_fd_ = -1;
        other.interface_index_ = 0;
#elif defined(_WIN32)
        wpcap_module_ = other.wpcap_module_;
        pcap_handle_ = other.pcap_handle_;
        pcap_open_live_ = other.pcap_open_live_;
        pcap_sendpacket_ = other.pcap_sendpacket_;
        pcap_close_ = other.pcap_close_;
        pcap_geterr_ = other.pcap_geterr_;
        pcap_findalldevs_ = other.pcap_findalldevs_;
        pcap_freealldevs_ = other.pcap_freealldevs_;
        other.wpcap_module_ = nullptr;
        other.pcap_handle_ = nullptr;
        other.pcap_open_live_ = nullptr;
        other.pcap_sendpacket_ = nullptr;
        other.pcap_close_ = nullptr;
        other.pcap_geterr_ = nullptr;
        other.pcap_findalldevs_ = nullptr;
        other.pcap_freealldevs_ = nullptr;
#endif
        interface_name_ = std::move(other.interface_name_);
        source_mac_ = other.source_mac_;
        other.interface_name_.clear();
        other.source_mac_.fill(0U);
    }

#if defined(__linux__)
    int socket_fd_{-1};
    int interface_index_{};
#elif defined(_WIN32)
    HMODULE wpcap_module_{};
    PcapHandle* pcap_handle_{};
    PcapOpenLive pcap_open_live_{};
    PcapSendPacket pcap_sendpacket_{};
    PcapClose pcap_close_{};
    PcapGetErr pcap_geterr_{};
    PcapFindAllDevs pcap_findalldevs_{};
    PcapFreeAllDevs pcap_freealldevs_{};
#endif
    std::string interface_name_;
    std::array<std::uint8_t, 6> source_mac_{};
};

} // namespace ar::iec61850::goose
