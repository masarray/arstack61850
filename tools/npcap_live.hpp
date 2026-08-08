// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/embedded/io.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <bit>
#include <windows.h>
#endif

namespace ar::iec61850::tools {

struct LiveEthernetAdapter final {
    std::string name;
    std::string description;
};

class NpcapLivePort final {
public:
    NpcapLivePort() noexcept {
#ifdef _WIN32
        load();
#else
        error_ = "Npcap live mode is available only on Windows.";
#endif
    }

    ~NpcapLivePort() { close(); unload(); }

    NpcapLivePort(const NpcapLivePort&) = delete;
    NpcapLivePort& operator=(const NpcapLivePort&) = delete;
    NpcapLivePort(NpcapLivePort&&) = delete;
    NpcapLivePort& operator=(NpcapLivePort&&) = delete;

    [[nodiscard]] bool runtime_available() const noexcept {
#ifdef _WIN32
        return module_ != nullptr && find_all_devs_ != nullptr &&
            free_all_devs_ != nullptr && open_live_ != nullptr &&
            send_packet_ != nullptr && get_error_ != nullptr && close_ != nullptr;
#else
        return false;
#endif
    }

    [[nodiscard]] bool opened() const noexcept {
#ifdef _WIN32
        return handle_ != nullptr;
#else
        return false;
#endif
    }

    [[nodiscard]] const std::string& error() const noexcept { return error_; }
    [[nodiscard]] const std::string& opened_name() const noexcept { return opened_name_; }
    [[nodiscard]] const std::string& opened_description() const noexcept {
        return opened_description_;
    }

    [[nodiscard]] bool list_adapters(std::vector<LiveEthernetAdapter>& adapters) {
        adapters.clear();
#ifdef _WIN32
        if (!runtime_available()) {
            return false;
        }

        char error_buffer[kErrorBufferSize]{};
        PcapIf* devices = nullptr;
        if (find_all_devs_(&devices, error_buffer) != 0) {
            error_ = error_buffer[0] == '\0'
                ? "Npcap could not enumerate adapters."
                : std::string{error_buffer};
            return false;
        }

        for (auto* current = devices; current != nullptr; current = current->next) {
            if (current->name == nullptr) {
                continue;
            }
            adapters.push_back({
                current->name,
                current->description == nullptr ? std::string{} : current->description});
        }
        free_all_devs_(devices);
        error_.clear();
        return true;
#else
        return false;
#endif
    }

    [[nodiscard]] bool open(const std::string_view selector) {
#ifdef _WIN32
        close();
        if (!runtime_available()) {
            return false;
        }
        if (selector.empty()) {
            error_ = "Live mode requires --interface NAME.";
            return false;
        }

        std::vector<LiveEthernetAdapter> adapters;
        if (!list_adapters(adapters)) {
            return false;
        }

        const auto selector_lower = lower(selector);
        std::vector<const LiveEthernetAdapter*> partial_matches;
        const LiveEthernetAdapter* selected = nullptr;
        for (const auto& adapter : adapters) {
            if (adapter.name == selector) {
                selected = &adapter;
                break;
            }
            const auto name_lower = lower(adapter.name);
            const auto description_lower = lower(adapter.description);
            if (name_lower.find(selector_lower) != std::string::npos ||
                description_lower.find(selector_lower) != std::string::npos) {
                partial_matches.push_back(&adapter);
            }
        }

        if (selected == nullptr) {
            if (partial_matches.size() == 1U) {
                selected = partial_matches.front();
            } else if (partial_matches.empty()) {
                error_ = "No Npcap adapter matched --interface '" +
                    std::string{selector} + "'. Use --list-interfaces.";
                return false;
            } else {
                error_ = "The --interface selector '" + std::string{selector} +
                    "' is ambiguous. Use the exact Npcap device name from --list-interfaces.";
                return false;
            }
        }

        char error_buffer[kErrorBufferSize]{};
        handle_ = open_live_(selected->name.c_str(), 65'536, 1, 1, error_buffer);
        if (handle_ == nullptr) {
            error_ = error_buffer[0] == '\0'
                ? "Npcap could not open the selected adapter."
                : std::string{error_buffer};
            return false;
        }

        opened_name_ = selected->name;
        opened_description_ = selected->description;
        error_.clear();
        return true;
#else
        (void)selector;
        return false;
#endif
    }

    void close() noexcept {
#ifdef _WIN32
        if (handle_ != nullptr && close_ != nullptr) {
            close_(handle_);
        }
        handle_ = nullptr;
#endif
        opened_name_.clear();
        opened_description_.clear();
    }

    [[nodiscard]] embedded::RawEthernetPort raw_port() noexcept {
        return {this, &transmit_callback};
    }

private:
#ifdef _WIN32
    struct Pcap;
    struct PcapAddr;
    struct PcapIf final {
        PcapIf* next;
        char* name;
        char* description;
        PcapAddr* addresses;
        unsigned int flags;
    };

    static constexpr std::size_t kErrorBufferSize = 256U;

    using FindAllDevs = int(__cdecl*)(PcapIf**, char*);
    using FreeAllDevs = void(__cdecl*)(PcapIf*);
    using OpenLive = Pcap*(__cdecl*)(const char*, int, int, int, char*);
    using SendPacket = int(__cdecl*)(Pcap*, const unsigned char*, int);
    using GetError = const char*(__cdecl*)(Pcap*);
    using Close = void(__cdecl*)(Pcap*);

    template <typename Function>
    [[nodiscard]] Function symbol(const char* name) noexcept {
        const auto raw = GetProcAddress(module_, name);
        return std::bit_cast<Function>(raw);
    }

    void load() noexcept {
        module_ = LoadLibraryA("wpcap.dll");
        if (module_ == nullptr) {
            error_ = "wpcap.dll was not found. Install Npcap, then retry.";
            return;
        }

        find_all_devs_ = symbol<FindAllDevs>("pcap_findalldevs");
        free_all_devs_ = symbol<FreeAllDevs>("pcap_freealldevs");
        open_live_ = symbol<OpenLive>("pcap_open_live");
        send_packet_ = symbol<SendPacket>("pcap_sendpacket");
        get_error_ = symbol<GetError>("pcap_geterr");
        close_ = symbol<Close>("pcap_close");

        if (!runtime_available()) {
            error_ = "wpcap.dll is present but required Npcap symbols are missing.";
            unload();
            return;
        }
        error_.clear();
    }

    void unload() noexcept {
        if (module_ != nullptr) {
            FreeLibrary(module_);
        }
        module_ = nullptr;
        find_all_devs_ = nullptr;
        free_all_devs_ = nullptr;
        open_live_ = nullptr;
        send_packet_ = nullptr;
        get_error_ = nullptr;
        close_ = nullptr;
    }

    [[nodiscard]] static std::string lower(const std::string_view value) {
        std::string result{value};
        std::transform(
            result.begin(), result.end(), result.begin(),
            [](const char character) {
                return static_cast<char>(
                    std::tolower(static_cast<unsigned char>(character)));
            });
        return result;
    }
#endif

    static embedded::IoResult transmit_callback(
        void* context,
        const std::span<const std::uint8_t> bytes) noexcept {
        auto* self = static_cast<NpcapLivePort*>(context);
        if (self == nullptr || bytes.empty()) {
            return {embedded::IoStatus::invalid_argument, 0U};
        }
#ifdef _WIN32
        if (!self->opened() || self->send_packet_ == nullptr ||
            bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return {embedded::IoStatus::io_error, 0U};
        }

        const auto result = self->send_packet_(
            self->handle_, bytes.data(), static_cast<int>(bytes.size()));
        if (result != 0) {
            const auto* message = self->get_error_ == nullptr
                ? nullptr
                : self->get_error_(self->handle_);
            self->error_ = message == nullptr || message[0] == '\0'
                ? "Npcap pcap_sendpacket failed."
                : std::string{message};
            return {embedded::IoStatus::io_error, 0U};
        }
        return {embedded::IoStatus::ok, bytes.size()};
#else
        return {embedded::IoStatus::io_error, 0U};
#endif
    }

    std::string error_;
    std::string opened_name_;
    std::string opened_description_;

#ifdef _WIN32
    HMODULE module_{};
    Pcap* handle_{};
    FindAllDevs find_all_devs_{};
    FreeAllDevs free_all_devs_{};
    OpenLive open_live_{};
    SendPacket send_packet_{};
    GetError get_error_{};
    Close close_{};
#endif
};

} // namespace ar::iec61850::tools
