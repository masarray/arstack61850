// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

namespace ar::iec61850::sampled_values {

// Compatibility identity for a validated *canonical encoded binary record*.
// Not authentication or a cryptographic signature. Always pair with boot-
// and generation-bound PROFILE SHOW readback before permitting reuse.
[[nodiscard]] constexpr std::uint64_t sv_binary_profile_identity_hash(
    const std::span<const std::uint8_t> record) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto octet : record) {
        hash ^= static_cast<std::uint64_t>(octet);
        hash *= 1099511628211ULL;
    }
    hash ^= static_cast<std::uint64_t>(record.size());
    hash *= 1099511628211ULL;
    return hash == 0U ? 1U : hash;
}

} // namespace ar::iec61850::sampled_values
