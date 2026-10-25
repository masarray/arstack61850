// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/sampled_values/m4_three_sv_roster.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ar::iec61850::sampled_values {

// Distinct from the existing single-publisher 'ARSV' V1 codec.
// The V2 envelope nests exactly THREE complete, byte-bounded ARSV V1 records.
// No C++ object-layout serialization, XML, dynamic allocation, or optional
// unbounded stream count on the device receiving path.
inline constexpr std::uint16_t m4_three_sv_binary_version = 2U;

enum class M4ThreeSvBinaryStatus : std::uint8_t {
    ok,
    buffer_too_small,
    invalid_magic,
    unsupported_version,
    invalid_length,
    checksum_mismatch,
    invalid_profile,
};

struct M4ThreeSvBinaryResult final {
    M4ThreeSvBinaryStatus status{M4ThreeSvBinaryStatus::invalid_profile};
    std::size_t bytes{};
    [[nodiscard]] bool success() const noexcept {
        return status == M4ThreeSvBinaryStatus::ok;
    }
};

class M4ThreeSvBinaryCodec final {
public:
    static constexpr std::size_t header_bytes = 28U;

    // The caller owns and bounds every buffer. encode_into writes exactly the
    // required number of bytes; decode commits the destination only on success.
    [[nodiscard]] static std::optional<std::size_t> encoded_size(
        const std::array<CompiledSvDeviceProfile, m4_three_sv_capacity>& profiles) noexcept;

    [[nodiscard]] static M4ThreeSvBinaryResult encode_into(
        const std::array<CompiledSvDeviceProfile, m4_three_sv_capacity>& profiles,
        std::span<std::uint8_t> destination) noexcept;

    [[nodiscard]] static M4ThreeSvBinaryResult decode(
        std::span<const std::uint8_t> source,
        std::array<CompiledSvDeviceProfile, m4_three_sv_capacity>& destination) noexcept;
};

} // namespace ar::iec61850::sampled_values
