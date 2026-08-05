// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/sampled_values/asdu.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ar::iec61850::sampled_values {

class SampledValuesPduCodec final {
public:
    [[nodiscard]] static std::vector<std::uint8_t> encode(const SampledValuesPdu& pdu);
    [[nodiscard]] static bool try_decode(
        std::span<const std::uint8_t> apdu, SampledValuesPdu& pdu) noexcept;
};

} // namespace ar::iec61850::sampled_values
