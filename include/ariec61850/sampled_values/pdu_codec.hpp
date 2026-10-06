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

struct SampledValuesEncodedFieldRegion final {
    std::size_t value_offset{};
    std::size_t value_size{};
    bool present{};

    friend bool operator==(const SampledValuesEncodedFieldRegion&,
                           const SampledValuesEncodedFieldRegion&) = default;
};

// Offsets are PDU-relative when produced by SampledValuesPduCodec and become
// Ethernet-frame absolute when produced by SampledValuesFrameCodec.
struct SampledValueAsduEncodeLayout final {
    SampledValuesEncodedFieldRegion sample_count;
    SampledValuesEncodedFieldRegion configuration_revision;
    SampledValuesEncodedFieldRegion reference_time;
    SampledValuesEncodedFieldRegion sample_synchronization;
    SampledValuesEncodedFieldRegion sample_rate;
    SampledValuesEncodedFieldRegion sample_payload;
    SampledValuesEncodedFieldRegion sample_mode;

    friend bool operator==(const SampledValueAsduEncodeLayout&,
                           const SampledValueAsduEncodeLayout&) = default;
};

class SampledValuesPduCodec final {
public:
    [[nodiscard]] static std::optional<std::size_t> encoded_size(
        const SampledValuesPdu& pdu) noexcept;

    [[nodiscard]] static wire::EncodeResult encode_into(
        const SampledValuesPdu& pdu,
        std::span<std::uint8_t> destination) noexcept;

    // Encodes once and records field value regions at the moment bytes are
    // written. An empty layout span disables metadata collection; otherwise it
    // must provide at least one slot per ASDU. No BER rescan is performed.
    [[nodiscard]] static wire::EncodeResult encode_into_with_layout(
        const SampledValuesPdu& pdu,
        std::span<std::uint8_t> destination,
        std::span<SampledValueAsduEncodeLayout> layouts) noexcept;

    // Host convenience wrapper. Embedded steady-state publishers should use
    // encode_into with caller-owned storage to avoid per-frame allocation.
    [[nodiscard]] static std::vector<std::uint8_t> encode(const SampledValuesPdu& pdu);

    [[nodiscard]] static bool try_decode(
        std::span<const std::uint8_t> apdu, SampledValuesPdu& pdu) noexcept;
};

} // namespace ar::iec61850::sampled_values
