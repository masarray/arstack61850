// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/sampled_values/publisher_profile.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ar::iec61850::sampled_values {

inline constexpr std::uint16_t compiled_sv_device_profile_version = 1U;
inline constexpr std::size_t compiled_sv_device_profile_max_sv_id_bytes = 95U;
inline constexpr std::size_t compiled_sv_device_profile_max_dataset_bytes = 159U;
inline constexpr std::size_t compiled_sv_device_profile_max_leaves = 64U;

enum class SvDeviceWireType : std::uint8_t {
    boolean = 1U,
    int8 = 2U,
    uint8 = 3U,
    int16 = 4U,
    uint16 = 5U,
    int32 = 6U,
    uint32 = 7U,
    int64 = 8U,
    uint64 = 9U,
    float32 = 10U,
    float64 = 11U,
    quality = 12U,
    timestamp = 13U,
};

struct SvDeviceLeafDescriptor final {
    SvDeviceWireType wire_type{SvDeviceWireType::int32};
    std::uint16_t wire_width_bytes{};
    bool quality{};
    bool timestamp{};

    friend bool operator==(const SvDeviceLeafDescriptor&,
                           const SvDeviceLeafDescriptor&) = default;
};

struct CompiledSvDeviceProfile final {
    std::uint16_t schema_version{compiled_sv_device_profile_version};
    SvProfileFamily profile_family{SvProfileFamily::unspecified};
    SvTransportMode transport_mode{SvTransportMode::multicast};

    std::array<std::uint8_t, 6> destination_mac{};
    std::uint16_t app_id{};
    bool vlan_present{};
    std::uint16_t vlan_id{};
    std::uint8_t vlan_priority{};

    std::uint32_t configuration_revision{};
    SvSampleMode sampling_basis{SvSampleMode::unknown};
    std::uint32_t configured_sample_rate{};
    std::uint32_t frame_rate_hz{};
    std::uint16_t sample_counter_modulus{};
    std::uint16_t no_asdu{};

    SvAsduOptions asdu_options{};
    std::uint32_t payload_size_bytes{};

    std::uint16_t sv_id_length{};
    std::array<char, compiled_sv_device_profile_max_sv_id_bytes + 1U> sv_id{};
    std::uint16_t data_set_reference_length{};
    std::array<char, compiled_sv_device_profile_max_dataset_bytes + 1U>
        data_set_reference{};

    std::uint16_t leaf_count{};
    std::array<SvDeviceLeafDescriptor, compiled_sv_device_profile_max_leaves> leaves{};

    friend bool operator==(const CompiledSvDeviceProfile&,
                           const CompiledSvDeviceProfile&) = default;
};

struct SvDeviceProfileCompileResult final {
    std::optional<CompiledSvDeviceProfile> profile;
    std::vector<std::string> errors;

    [[nodiscard]] bool ok() const noexcept {
        return profile.has_value() && errors.empty();
    }
};

[[nodiscard]] SvDeviceProfileCompileResult
compile_esp32p4_device_profile(const SvPublisherProfile& profile);

enum class SvDeviceProfileCodecStatus : std::uint8_t {
    ok,
    buffer_too_small,
    invalid_value,
    invalid_magic,
    unsupported_version,
    invalid_length,
    checksum_mismatch,
};

struct SvDeviceProfileCodecResult final {
    SvDeviceProfileCodecStatus status{SvDeviceProfileCodecStatus::invalid_value};
    std::size_t bytes{};

    [[nodiscard]] bool success() const noexcept {
        return status == SvDeviceProfileCodecStatus::ok;
    }
};

class SvDeviceProfileBinaryCodec final {
public:
    static constexpr std::size_t header_bytes = 20U;
    static constexpr std::size_t fixed_payload_bytes = 42U;
    static constexpr std::size_t leaf_descriptor_bytes = 4U;

    [[nodiscard]] static std::optional<std::size_t> encoded_size(
        const CompiledSvDeviceProfile& profile) noexcept;

    [[nodiscard]] static SvDeviceProfileCodecResult encode_into(
        const CompiledSvDeviceProfile& profile,
        std::span<std::uint8_t> destination) noexcept;

    [[nodiscard]] static SvDeviceProfileCodecResult decode(
        std::span<const std::uint8_t> source,
        CompiledSvDeviceProfile& profile) noexcept;
};

} // namespace ar::iec61850::sampled_values
