// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ar::iec61850::ethernet {

inline constexpr std::uint16_t vlan_tag_ethertype = 0x8100;
inline constexpr std::uint16_t goose_ethertype = 0x88B8;
inline constexpr std::uint16_t sampled_values_ethertype = 0x88BA;
inline constexpr std::uint16_t ptp_ethertype = 0x88F7;

class MacAddress final {
public:
    MacAddress() = default;

    // Exact-size construction is the preferred embedded path: the size is
    // encoded in the type, so there is nothing to validate or throw at runtime.
    explicit constexpr MacAddress(const std::array<std::uint8_t, 6>& bytes) noexcept
        : bytes_(bytes) {}

    // Retained for host/source compatibility when the caller only has a span.
    explicit MacAddress(std::span<const std::uint8_t> bytes);

    [[nodiscard]] static MacAddress parse(const std::string& text);
    [[nodiscard]] static bool try_parse(const std::string& text, MacAddress& address) noexcept;

    [[nodiscard]] const std::array<std::uint8_t, 6>& bytes() const noexcept { return bytes_; }
    void copy_to(std::span<std::uint8_t> destination) const;
    [[nodiscard]] std::string to_string() const;

    friend bool operator==(const MacAddress&, const MacAddress&) = default;

private:
    std::array<std::uint8_t, 6> bytes_{};
};

struct VlanTag final {
    std::uint8_t priority_code_point{};
    bool drop_eligible{};
    std::uint16_t vlan_id{};

    VlanTag() = default;
    VlanTag(std::uint8_t priority, std::uint16_t vlan)
        : priority_code_point(priority), drop_eligible(false), vlan_id(vlan) {}
    VlanTag(std::uint8_t priority, bool drop, std::uint16_t vlan)
        : priority_code_point(priority), drop_eligible(drop), vlan_id(vlan) {}

    [[nodiscard]] std::uint16_t to_tag_control_information() const;
    [[nodiscard]] static VlanTag from_tag_control_information(std::uint16_t tci) noexcept;

    friend bool operator==(const VlanTag&, const VlanTag&) = default;
};

struct EthernetFrame final {
    MacAddress destination;
    MacAddress source;
    std::uint16_t ether_type{};
    std::optional<VlanTag> vlan;
    std::vector<std::uint8_t> payload;
};

class EthernetFrameCodec final {
public:
    [[nodiscard]] static std::vector<std::uint8_t> encode(const EthernetFrame& frame);
    [[nodiscard]] static bool try_decode(std::span<const std::uint8_t> bytes, EthernetFrame& frame) noexcept;
};

struct ProcessBusFrame final {
    EthernetFrame ethernet;
    std::uint16_t app_id{};
    std::uint16_t declared_length{};
    std::uint16_t reserved1{};
    std::uint16_t reserved2{};
    std::vector<std::uint8_t> apdu;
};

class ProcessBusFrameCodec final {
public:
    static constexpr std::size_t header_length = 8U;

    [[nodiscard]] static std::vector<std::uint8_t> encode_payload(
        std::uint16_t app_id,
        std::span<const std::uint8_t> apdu,
        std::uint16_t reserved1 = 0,
        std::uint16_t reserved2 = 0);

    [[nodiscard]] static EthernetFrame encode_ethernet_frame(
        const MacAddress& destination,
        const MacAddress& source,
        std::uint16_t ether_type,
        std::uint16_t app_id,
        std::span<const std::uint8_t> apdu,
        std::optional<VlanTag> vlan = std::nullopt,
        std::uint16_t reserved1 = 0,
        std::uint16_t reserved2 = 0);

    [[nodiscard]] static bool try_decode(const EthernetFrame& ethernet, ProcessBusFrame& frame) noexcept;
};

} // namespace ar::iec61850::ethernet
