// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "runtime_profile.hpp"
#include "ariec61850/sampled_values/compiled_device_profile.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::esp32p4::smv {

enum class BinaryRuntimeProfileStatus : std::uint8_t {
    ok,
    invalid_envelope,
    unsupported_device_capability,
};

// Device capability is narrower than host SCL validity. Do not reinterpret
// unsupported wire fields or invent an alternative active profile model.
[[nodiscard]] inline BinaryRuntimeProfileStatus decode_binary_runtime_profile(
    const std::span<const std::uint8_t> bytes,
    RuntimePublisherProfile& output) noexcept {
    using namespace ar::iec61850::sampled_values;
    CompiledSvDeviceProfile decoded{};
    if (!SvDeviceProfileBinaryCodec::decode(bytes, decoded).success()) {
        return BinaryRuntimeProfileStatus::invalid_envelope;
    }
    if ((decoded.profile_family != SvProfileFamily::iec61850_9_2 &&
         decoded.profile_family != SvProfileFamily::legacy_9_2le) ||
        decoded.sampling_basis != SvSampleMode::samples_per_second ||
        decoded.no_asdu != 1U ||
        decoded.frame_rate_hz != decoded.configured_sample_rate ||
        decoded.frame_rate_hz == 0U || decoded.frame_rate_hz > 65535U ||
        decoded.leaf_count != 16U || decoded.payload_size_bytes != 64U ||
        !decoded.asdu_options.sample_synchronized ||
        decoded.asdu_options.refresh_time ||
        decoded.asdu_options.security ||
        decoded.asdu_options.synch_source_id) {
        return BinaryRuntimeProfileStatus::unsupported_device_capability;
    }

    // The current fixed 4I+4V TX template writes eight ordered INT32+Quality
    // pairs. Refuse every other valid IEC layout instead of altering its wire.
    for (std::size_t i = 0U; i < 8U; ++i) {
        const auto& value = decoded.leaves[2U * i];
        const auto& quality = decoded.leaves[2U * i + 1U];
        if (value.wire_type != SvDeviceWireType::int32 ||
            value.wire_width_bytes != 4U || value.quality || value.timestamp ||
            quality.wire_type != SvDeviceWireType::quality ||
            quality.wire_width_bytes != 4U || !quality.quality ||
            quality.timestamp) {
            return BinaryRuntimeProfileStatus::unsupported_device_capability;
        }
    }

    RuntimePublisherProfile candidate{};
    candidate.schema_version = 1U;
    candidate.destination_mac = decoded.destination_mac;
    candidate.app_id = decoded.app_id;
    candidate.vlan_present = decoded.vlan_present;
    candidate.vlan_id = decoded.vlan_id;
    candidate.vlan_priority = decoded.vlan_priority;
    candidate.configuration_revision = decoded.configuration_revision;
    candidate.publisher_rate_hz = decoded.frame_rate_hz;
    candidate.sample_counter_modulus = decoded.sample_counter_modulus;
    candidate.no_asdu = decoded.no_asdu;
    candidate.include_data_set = decoded.asdu_options.data_set;
    candidate.include_sample_rate = decoded.asdu_options.sample_rate;
    std::copy_n(decoded.sv_id.begin(), decoded.sv_id_length,
                candidate.sv_id.begin());
    std::copy_n(decoded.data_set_reference.begin(),
                decoded.data_set_reference_length,
                candidate.data_set_reference.begin());
    // Generation is assigned exclusively by runtime_profile_commit().
    output = candidate;
    return BinaryRuntimeProfileStatus::ok;
}

} // namespace ar::esp32p4::smv
