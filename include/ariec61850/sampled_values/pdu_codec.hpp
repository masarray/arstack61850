// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/sampled_values/asdu.hpp"
#include "ariec61850/wire/encode_result.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ar::iec61850::sampled_values {

class SampledValuesPduCodec final {
public:
    [[nodiscard]] static std::optional<std::size_t> encoded_size(
        const SampledValuesPdu& pdu) noexcept;

    [[nodiscard]] static wire::EncodeResult encode_into(
        const SampledValuesPdu& pdu,
        std::span<std::uint8_t> destination) noexcept;

    // Host convenience wrapper. Embedded steady-state publishers should use
    // encode_into with caller-owned storage to avoid per-frame allocation.
    [[nodiscard]] static std::vector<std::uint8_t> encode(const SampledValuesPdu& pdu);

    [[nodiscard]] static bool try_decode(
        std::span<const std::uint8_t> apdu, SampledValuesPdu& pdu) noexcept;
};

} // namespace ar::iec61850::sampled_values
