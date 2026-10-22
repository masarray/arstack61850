// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdint>

namespace ar::esp32p4::smv {

struct RuntimePublisherProfile final {
    std::uint32_t schema_version{1U};
    std::array<std::uint8_t, 6> destination_mac{0x01U, 0x0CU, 0xCDU, 0x04U, 0x00U, 0x01U};
    std::uint16_t app_id{0x4000U};
    bool vlan_present{true};
    std::uint16_t vlan_id{0U};
    std::uint8_t vlan_priority{4U};
    std::uint32_t configuration_revision{1U};
    std::uint32_t publisher_rate_hz{4000U};
    std::uint16_t sample_counter_modulus{4000U};
    std::uint16_t no_asdu{1U};
    bool include_data_set{false};
    bool include_sample_rate{false};
    // Ed2.1 optional wire values (zero is legacy ASCII auto-rate).
    std::uint16_t wire_sample_rate{0U};
    std::uint16_t wire_sample_mode{1U};
    std::array<char, 96> sv_id{};
    std::array<char, 160> data_set_reference{};
    // Zero for factory/default or legacy ASCII commits. A valid value is set
    // only after an entire canonical PROFILE BINCOMMIT validates successfully.
    std::uint64_t binary_record_identity_hash{0U};
    std::uint32_t binary_record_bytes{0U};
    std::uint64_t generation{1U};
};

void runtime_profile_initialize() noexcept;
[[nodiscard]] RuntimePublisherProfile runtime_profile_snapshot() noexcept;
[[nodiscard]] bool runtime_profile_commit(const RuntimePublisherProfile& requested) noexcept;
[[nodiscard]] bool runtime_profile_validate(const RuntimePublisherProfile& profile) noexcept;

} // namespace ar::esp32p4::smv
