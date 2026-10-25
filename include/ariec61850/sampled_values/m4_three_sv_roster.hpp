// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/sampled_values/compiled_device_profile.hpp"
#include "ariec61850/scl/model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ar::iec61850::sampled_values {

// M4.0 intentionally fixes the number of source-backed virtual MUs at three.
// A firmware capability change, rather than an unbounded vector, raises this limit.
inline constexpr std::size_t m4_three_sv_capacity = 3U;
inline constexpr std::uint32_t m4_three_sv_rate_hz = 4000U;

struct M4CompiledSvStream final {
    std::string ied_name;
    std::string control_block_reference;
    CompiledSvDeviceProfile device_profile;
};

struct M4ThreeSvRoster final {
    std::array<M4CompiledSvStream, m4_three_sv_capacity> streams{};
    std::uint32_t common_sample_rate_hz{m4_three_sv_rate_hz};
    std::uint32_t total_ethernet_frames_per_second{
        static_cast<std::uint32_t>(m4_three_sv_capacity) * m4_three_sv_rate_hz};
};

struct M4ThreeSvCompileResult final {
    std::optional<M4ThreeSvRoster> roster;
    std::vector<std::string> errors;
    [[nodiscard]] bool ok() const noexcept { return roster.has_value() && errors.empty(); }
};

// Pure host-side compiler. No hardware/clock/USB dependency. Never parses XML
// twice: the existing SclParser creates SclDocument and the existing one-stream
// compiler/codec remains the only authority for each stream's wire fields.
// The caller EXPLICITLY selects the 9-2LE family and counter policy.
class M4ThreeSvRosterCompiler final {
public:
    [[nodiscard]] static M4ThreeSvCompileResult compile(
        const scl::SclDocument& document,
        const SvPublisherProfileCompileContext& context);
};

} // namespace ar::iec61850::sampled_values
