// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::sampled_values {

// Helpers for the common SV seqOfData shape where an INT32 sample is followed
// by its 32-bit IEC 61850 quality word. The helper only writes caller-owned
// bytes; stream/DataSet semantics and engineering scaling remain application
// configuration responsibilities.
class SampledValuesPayloadWriter final {
public:
    static constexpr std::size_t int32_quality_pair_bytes = 8U;

    [[nodiscard]] static constexpr std::size_t required_bytes_for_pairs(
        const std::size_t pair_count) noexcept {
        return pair_count > (static_cast<std::size_t>(-1) / int32_quality_pair_bytes)
            ? 0U
            : pair_count * int32_quality_pair_bytes;
    }

    [[nodiscard]] static bool write_int32_quality_pair(
        const std::span<std::uint8_t> payload,
        const std::size_t pair_index,
        const std::int32_t value,
        const std::uint32_t quality) noexcept {
        if (pair_index > (static_cast<std::size_t>(-1) / int32_quality_pair_bytes)) {
            return false;
        }
        const auto offset = pair_index * int32_quality_pair_bytes;
        if (offset > payload.size() ||
            payload.size() - offset < int32_quality_pair_bytes) {
            return false;
        }

        write_u32_be(payload.subspan(offset, 4U), static_cast<std::uint32_t>(value));
        write_u32_be(payload.subspan(offset + 4U, 4U), quality);
        return true;
    }

private:
    static void write_u32_be(
        const std::span<std::uint8_t> destination,
        const std::uint32_t value) noexcept {
        destination[0] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
        destination[1] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
        destination[2] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
        destination[3] = static_cast<std::uint8_t>(value & 0xFFU);
    }
};

} // namespace ar::iec61850::sampled_values
