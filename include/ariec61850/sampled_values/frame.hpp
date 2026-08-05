// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/sampled_values/asdu.hpp"

#include <cstdint>
#include <optional>

namespace ar::iec61850::sampled_values {

struct SampledValuesFrame final {
    ethernet::MacAddress destination;
    ethernet::MacAddress source;
    std::optional<ethernet::VlanTag> vlan;
    std::uint16_t app_id{};
    std::uint16_t reserved1{};
    std::uint16_t reserved2{};
    SampledValuesPdu pdu;

    friend bool operator==(const SampledValuesFrame&, const SampledValuesFrame&) = default;
};

} // namespace ar::iec61850::sampled_values
