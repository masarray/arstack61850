// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/sampled_values/asdu.hpp"
#include "ariec61850/wire/encode_result.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#if !defined(ARIEC61850_NO_EXCEPTIONS)
#include <vector>
#endif

namespace ar::iec61850::sampled_values {

class SampledValuesPduCodec final {
public:
    [[nodiscard]] static std::optional<std::size_t> encoded_size(
        const SampledValuesPdu& pdu) noexcept;

    [[nodiscard]] static wire::EncodeResult encode_into(
        const SampledValuesPdu& pdu,
        std::span<std::uint8_t> destination) noexcept;

#if !defined(ARIEC61850_NO_EXCEPTIONS)
    // Host/soft-profile convenience wrapper. Hard embedded publishers use
    // encode_into with caller-owned storage.
    [[nodiscard]] static std::vector<std::uint8_t> encode(const SampledValuesPdu& pdu);

    // The current dynamic decoder is retained outside the hard no-exception
    // profile until its BER traversal/storage path is fully status-based.
    [[nodiscard]] static bool try_decode(
        std::span<const std::uint8_t> apdu, SampledValuesPdu& pdu) noexcept;
#endif
};

} // namespace ar::iec61850::sampled_values
