// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/sampled_values/compiled_device_profile.hpp"

#include "ariec61850/integrity/crc32.hpp"
#include "ariec61850/sampled_values/esp32p4_profile_support.hpp"
#include "ariec61850/sampled_values/profile_family.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <string_view>

namespace ar::iec61850::sampled_values {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'A', 'R', 'S', 'V'};
constexpr std::size_t kCrcOffset = 16U;
constexpr std::uint16_t kFlagVlan = 1U << 0U;
constexpr std::uint16_t kFlagDataSet = 1U << 1U;
constexpr std::uint16_t kFlagSampleRate = 1U << 2U;
constexpr std::uint16_t kFlagSampleSynchronized = 1U << 3U;
constexpr std::uint16_t kFlagRefreshTime = 1U << 4U;
constexpr std::uint16_t kFlagSecurity = 1U << 5U;
constexpr std::uint16_t kFlagSynchSourceId = 1U << 6U;
constexpr std::uint16_t kFlagSmvOptsPresent = 1U << 7U;
constexpr std::uint16_t kKnownFlags =
    kFlagVlan | kFlagDataSet | kFlagSampleRate | kFlagSampleSynchronized |
    kFlagRefreshTime | kFlagSecurity | kFlagSynchSourceId | kFlagSmvOptsPresent;

void write_u16(
    const std::span<std::uint8_t> out,
    const std::size_t offset,
    const std::uint16_t value) noexcept {
    out[offset] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    out[offset + 1U] = static_cast<std::uint8_t>(value & 0xFFU);
}

void write_u32(
    const std::span<std::uint8_t> out,
    const std::size_t offset,
    const std::uint32_t value) noexcept {
    for (std::size_t index = 0U; index < 4U; ++index) {
        const auto shift = static_cast<unsigned>((3U - index) * 8U);
        out[offset + index] =
            static_cast<std::uint8_t>((value >> shift) & 0xFFU);
    }
}

[[nodiscard]] std::uint16_t read_u16(
    const std::span<const std::uint8_t> in,
    const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(in[offset]) << 8U) |
        static_cast<std::uint16_t>(in[offset + 1U]));
}

[[nodiscard]] std::uint32_t read_u32(
    const std::span<const std::uint8_t> in,
    const std::size_t offset) noexcept {
    std::uint32_t result{};
    for (std::size_t index = 0U; index < 4U; ++index) {
        result = static_cast<std::uint32_t>(
            (result << 8U) | static_cast<std::uint32_t>(in[offset + index]));
    }
    return result;
}

[[nodiscard]] std::uint16_t flags_for(
    const CompiledSvDeviceProfile& profile) noexcept {
    std::uint16_t flags{};
    if (profile.vlan_present) flags |= kFlagVlan;
    if (profile.asdu_options.data_set) flags |= kFlagDataSet;
    if (profile.asdu_options.sample_rate) flags |= kFlagSampleRate;
    if (profile.asdu_options.sample_synchronized) flags |= kFlagSampleSynchronized;
    if (profile.asdu_options.refresh_time) flags |= kFlagRefreshTime;
    if (profile.asdu_options.security) flags |= kFlagSecurity;
    if (profile.asdu_options.synch_source_id) flags |= kFlagSynchSourceId;
    if (profile.asdu_options.element_present) flags |= kFlagSmvOptsPresent;
    return flags;
}

[[nodiscard]] bool contains_nul(const std::string_view text) noexcept {
    return text.find('\0') != std::string_view::npos;
}

[[nodiscard]] bool valid_wire_type(const SvDeviceWireType type) noexcept {
    const auto raw = static_cast<std::uint8_t>(type);
    return raw >= static_cast<std::uint8_t>(SvDeviceWireType::boolean) &&
           raw <= static_cast<std::uint8_t>(SvDeviceWireType::timestamp);
}

[[nodiscard]] std::optional<std::uint16_t> canonical_wire_width(
    const SvDeviceWireType type) noexcept {
    switch (type) {
    case SvDeviceWireType::boolean:
    case SvDeviceWireType::int8:
    case SvDeviceWireType::uint8:
        return static_cast<std::uint16_t>(1U);
    case SvDeviceWireType::int16:
    case SvDeviceWireType::uint16:
        return static_cast<std::uint16_t>(2U);
    case SvDeviceWireType::int32:
    case SvDeviceWireType::uint32:
    case SvDeviceWireType::float32:
    case SvDeviceWireType::quality:
        return static_cast<std::uint16_t>(4U);
    case SvDeviceWireType::int64:
    case SvDeviceWireType::uint64:
    case SvDeviceWireType::float64:
    case SvDeviceWireType::timestamp:
        return static_cast<std::uint16_t>(8U);
    }
    return std::nullopt;
}

[[nodiscard]] bool valid_profile_shape(
    const CompiledSvDeviceProfile& profile) noexcept {
    if (profile.schema_version != compiled_sv_device_profile_version ||
        profile.profile_family == SvProfileFamily::unspecified ||
        profile.app_id == 0U ||
        profile.frame_rate_hz == 0U ||
        profile.sample_counter_modulus == 0U ||
        profile.no_asdu == 0U ||
        profile.sv_id_length == 0U ||
        profile.sv_id_length > compiled_sv_device_profile_max_sv_id_bytes ||
        profile.data_set_reference_length >
            compiled_sv_device_profile_max_dataset_bytes ||
        profile.leaf_count == 0U ||
        profile.leaf_count > compiled_sv_device_profile_max_leaves) {
        return false;
    }
    if (profile.asdu_options.data_set &&
        profile.data_set_reference_length == 0U) {
        return false;
    }
    if (!profile.asdu_options.element_present &&
        (profile.asdu_options.refresh_time ||
         profile.asdu_options.sample_synchronized ||
         profile.asdu_options.sample_rate ||
         profile.asdu_options.data_set ||
         profile.asdu_options.security ||
         profile.asdu_options.synch_source_id)) {
        return false;
    }
    if (!profile.vlan_present &&
        (profile.vlan_id != 0U || profile.vlan_priority != 0U)) {
        return false;
    }
    const std::string_view sv_id{
        profile.sv_id.data(), profile.sv_id_length};
    const std::string_view data_set{
        profile.data_set_reference.data(), profile.data_set_reference_length};
    if (contains_nul(sv_id) || contains_nul(data_set)) {
        return false;
    }
    if (!is_valid_sv_app_id(profile.app_id)) return false;
    if (profile.transport_mode == SvTransportMode::multicast) {
        if (!is_iec_sv_multicast_mac(profile.destination_mac)) return false;
    } else if (!is_valid_sv_unicast_mac(profile.destination_mac)) {
        return false;
    }
    if (profile.vlan_present &&
        (!ethernet::is_valid_vlan_id(profile.vlan_id) ||
         !ethernet::is_valid_vlan_priority(profile.vlan_priority))) {
        return false;
    }
    if (profile.sampling_basis == SvSampleMode::unknown ||
        profile.configured_sample_rate == 0U) {
        return false;
    }

    std::uint64_t payload{};
    for (std::size_t index = 0U; index < profile.leaf_count; ++index) {
        const auto& leaf = profile.leaves[index];
        const auto expected_width = canonical_wire_width(leaf.wire_type);
        if (!expected_width.has_value() ||
            leaf.wire_width_bytes != *expected_width ||
            leaf.quality != (leaf.wire_type == SvDeviceWireType::quality) ||
            leaf.timestamp != (leaf.wire_type == SvDeviceWireType::timestamp)) {
            return false;
        }
        payload += leaf.wire_width_bytes;
        if (payload > std::numeric_limits<std::uint32_t>::max()) return false;
    }
    return payload == profile.payload_size_bytes;
}

[[nodiscard]] std::uint32_t record_crc(
    const std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() < SvDeviceProfileBinaryCodec::header_bytes) return 0U;
    std::uint32_t state = integrity::crc32_initial_state;
    state = integrity::crc32_update(state, bytes.first(kCrcOffset));
    constexpr std::array<std::uint8_t, 4> zero_crc{};
    state = integrity::crc32_update(state, zero_crc);
    state = integrity::crc32_update(
        state, bytes.subspan(kCrcOffset + zero_crc.size()));
    return integrity::crc32_finalize(state);
}

[[nodiscard]] std::optional<SvDeviceWireType> wire_type_for(
    const SvPublisherChannel& channel) {
    if (channel.is_quality) return SvDeviceWireType::quality;
    if (channel.is_timestamp) return SvDeviceWireType::timestamp;

    std::string type;
    type.reserve(channel.basic_type.size());
    for (const char ch : channel.basic_type) {
        type.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))));
    }
    if (type == "boolean") return SvDeviceWireType::boolean;
    if (type == "int8") return SvDeviceWireType::int8;
    if (type == "int8u") return SvDeviceWireType::uint8;
    if (type == "int16") return SvDeviceWireType::int16;
    if (type == "int16u") return SvDeviceWireType::uint16;
    if (type == "int32") return SvDeviceWireType::int32;
    if (type == "int32u") return SvDeviceWireType::uint32;
    if (type == "int64") return SvDeviceWireType::int64;
    if (type == "int64u") return SvDeviceWireType::uint64;
    if (type == "float32") return SvDeviceWireType::float32;
    if (type == "float64") return SvDeviceWireType::float64;
    return std::nullopt;
}

template <std::size_t N>
bool copy_text(
    const std::string_view source,
    std::array<char, N>& destination,
    std::uint16_t& length) noexcept {
    if (source.size() >= N ||
        source.size() > std::numeric_limits<std::uint16_t>::max() ||
        contains_nul(source)) {
        return false;
    }
    destination.fill('\0');
    std::copy(source.begin(), source.end(), destination.begin());
    length = static_cast<std::uint16_t>(source.size());
    return true;
}

[[nodiscard]] bool decode_profile_family(
    const std::uint8_t raw,
    SvProfileFamily& value) noexcept {
    switch (raw) {
    case 1U: value = SvProfileFamily::iec61850_9_2; return true;
    case 2U: value = SvProfileFamily::legacy_9_2le; return true;
    case 3U: value = SvProfileFamily::iec61869_9; return true;
    default: return false;
    }
}

[[nodiscard]] std::uint8_t encode_profile_family(
    const SvProfileFamily value) noexcept {
    switch (value) {
    case SvProfileFamily::iec61850_9_2: return 1U;
    case SvProfileFamily::legacy_9_2le: return 2U;
    case SvProfileFamily::iec61869_9: return 3U;
    case SvProfileFamily::unspecified: return 0U;
    }
    return 0U;
}

[[nodiscard]] bool decode_sampling_basis(
    const std::uint8_t raw,
    SvSampleMode& value) noexcept {
    switch (raw) {
    case 1U: value = SvSampleMode::samples_per_second; return true;
    case 2U: value = SvSampleMode::samples_per_period; return true;
    case 3U: value = SvSampleMode::seconds_per_sample; return true;
    default: return false;
    }
}

[[nodiscard]] std::uint8_t encode_sampling_basis(
    const SvSampleMode value) noexcept {
    switch (value) {
    case SvSampleMode::samples_per_second: return 1U;
    case SvSampleMode::samples_per_period: return 2U;
    case SvSampleMode::seconds_per_sample: return 3U;
    case SvSampleMode::unknown: return 0U;
    }
    return 0U;
}

} // namespace

SvDeviceProfileCompileResult compile_esp32p4_device_profile(
    const SvPublisherProfile& source) {
    SvDeviceProfileCompileResult result;
    if (classify_esp32p4_sv_profile(source) != Esp32P4SvProfileSupport::ready) {
        result.errors.emplace_back(
            "SV profile is not deployable on the current ESP32-P4 capability contract.");
        return result;
    }

    const auto frame_rate = source.timing.exact_frame_rate_hz();
    if (!frame_rate.has_value() || *frame_rate == 0U) {
        result.errors.emplace_back(
            "ESP32-P4 device profile requires a resolved integral frame cadence.");
        return result;
    }
    if (source.sample_counter_policy != SvSampleCounterPolicy::explicit_modulus ||
        !source.sample_counter_modulus.has_value() ||
        *source.sample_counter_modulus == 0U) {
        result.errors.emplace_back(
            "ESP32-P4 device profile requires an explicit validated sample-counter modulus.");
        return result;
    }
    if (source.channels.size() > compiled_sv_device_profile_max_leaves ||
        source.payload_size_bytes > std::numeric_limits<std::uint32_t>::max()) {
        result.errors.emplace_back(
            "SV profile exceeds the bounded device-profile layout capacity.");
        return result;
    }

    CompiledSvDeviceProfile profile;
    profile.profile_family = source.profile_family;
    profile.transport_mode = source.transport_mode;
    profile.destination_mac = source.destination_mac;
    profile.app_id = source.app_id;
    profile.vlan_present = source.vlan_present;
    profile.vlan_id = source.vlan_id;
    profile.vlan_priority = source.vlan_priority;
    profile.configuration_revision = source.configuration_revision;
    profile.sampling_basis = source.timing.sampling_basis;
    profile.configured_sample_rate = source.timing.configured_sample_rate;
    profile.frame_rate_hz = *frame_rate;
    profile.sample_counter_modulus = *source.sample_counter_modulus;
    profile.no_asdu = source.timing.asdus_per_frame;
    profile.asdu_options = source.asdu_options;
    profile.payload_size_bytes =
        static_cast<std::uint32_t>(source.payload_size_bytes);

    if (!copy_text(source.sv_id, profile.sv_id, profile.sv_id_length) ||
        !copy_text(
            source.data_set_reference,
            profile.data_set_reference,
            profile.data_set_reference_length)) {
        result.errors.emplace_back(
            "SV identifiers exceed the bounded device-profile string capacity or contain NUL.");
        return result;
    }

    profile.leaf_count = static_cast<std::uint16_t>(source.channels.size());
    for (std::size_t index = 0U; index < source.channels.size(); ++index) {
        const auto type = wire_type_for(source.channels[index]);
        if (!type.has_value()) {
            result.errors.push_back(
                "SV device profile cannot encode leaf type '" +
                source.channels[index].basic_type + "'.");
            return result;
        }
        profile.leaves[index] = {
            *type,
            source.channels[index].wire_width_bytes,
            source.channels[index].is_quality,
            source.channels[index].is_timestamp,
        };
    }

    if (!valid_profile_shape(profile)) {
        result.errors.emplace_back(
            "Compiled ESP32-P4 profile failed the canonical binary-profile invariant.");
        return result;
    }
    result.profile = profile;
    return result;
}

std::optional<std::size_t> SvDeviceProfileBinaryCodec::encoded_size(
    const CompiledSvDeviceProfile& profile) noexcept {
    if (!valid_profile_shape(profile)) return std::nullopt;

    const std::size_t leaf_bytes =
        static_cast<std::size_t>(profile.leaf_count) * leaf_descriptor_bytes;
    const std::size_t payload =
        fixed_payload_bytes + leaf_bytes +
        profile.sv_id_length + profile.data_set_reference_length;
    if (payload > std::numeric_limits<std::uint32_t>::max() ||
        payload > std::numeric_limits<std::size_t>::max() - header_bytes) {
        return std::nullopt;
    }
    return header_bytes + payload;
}

SvDeviceProfileCodecResult SvDeviceProfileBinaryCodec::encode_into(
    const CompiledSvDeviceProfile& profile,
    const std::span<std::uint8_t> destination) noexcept {
    const auto required = encoded_size(profile);
    if (!required.has_value()) {
        return {SvDeviceProfileCodecStatus::invalid_value, 0U};
    }
    if (destination.size() < *required) {
        return {SvDeviceProfileCodecStatus::buffer_too_small, *required};
    }

    auto out = destination.first(*required);
    std::fill(out.begin(), out.end(), std::uint8_t{0U});
    std::copy(kMagic.begin(), kMagic.end(), out.begin());
    write_u16(out, 4U, compiled_sv_device_profile_version);
    write_u16(out, 6U, static_cast<std::uint16_t>(header_bytes));
    write_u32(out, 8U, static_cast<std::uint32_t>(*required));
    write_u32(
        out, 12U,
        static_cast<std::uint32_t>(*required - header_bytes));
    write_u32(out, kCrcOffset, 0U);

    const std::size_t base = header_bytes;
    out[base + 0U] = encode_profile_family(profile.profile_family);
    out[base + 1U] = profile.transport_mode == SvTransportMode::multicast ? 1U : 2U;
    write_u16(out, base + 2U, flags_for(profile));
    std::copy(
        profile.destination_mac.begin(), profile.destination_mac.end(),
        out.begin() + static_cast<std::ptrdiff_t>(base + 4U));
    write_u16(out, base + 10U, profile.app_id);
    write_u16(out, base + 12U, profile.vlan_id);
    out[base + 14U] = profile.vlan_priority;
    out[base + 15U] = encode_sampling_basis(profile.sampling_basis);
    write_u16(out, base + 16U, profile.no_asdu);
    write_u32(out, base + 18U, profile.configuration_revision);
    write_u32(out, base + 22U, profile.configured_sample_rate);
    write_u32(out, base + 26U, profile.frame_rate_hz);
    write_u16(out, base + 30U, profile.sample_counter_modulus);
    write_u16(out, base + 32U, profile.leaf_count);
    write_u32(out, base + 34U, profile.payload_size_bytes);
    write_u16(out, base + 38U, profile.sv_id_length);
    write_u16(out, base + 40U, profile.data_set_reference_length);

    std::size_t offset = base + fixed_payload_bytes;
    for (std::size_t index = 0U; index < profile.leaf_count; ++index) {
        const auto& leaf = profile.leaves[index];
        out[offset] = static_cast<std::uint8_t>(leaf.wire_type);
        out[offset + 1U] =
            static_cast<std::uint8_t>((leaf.quality ? 1U : 0U) |
                                      (leaf.timestamp ? 2U : 0U));
        write_u16(out, offset + 2U, leaf.wire_width_bytes);
        offset += leaf_descriptor_bytes;
    }
    std::copy_n(
        reinterpret_cast<const std::uint8_t*>(profile.sv_id.data()),
        profile.sv_id_length,
        out.begin() + static_cast<std::ptrdiff_t>(offset));
    offset += profile.sv_id_length;
    std::copy_n(
        reinterpret_cast<const std::uint8_t*>(profile.data_set_reference.data()),
        profile.data_set_reference_length,
        out.begin() + static_cast<std::ptrdiff_t>(offset));

    write_u32(out, kCrcOffset, record_crc(out));
    return {SvDeviceProfileCodecStatus::ok, *required};
}

SvDeviceProfileCodecResult SvDeviceProfileBinaryCodec::decode(
    const std::span<const std::uint8_t> source,
    CompiledSvDeviceProfile& profile) noexcept {
    profile = {};
    if (source.size() < header_bytes) {
        return {SvDeviceProfileCodecStatus::invalid_length, 0U};
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), source.begin())) {
        return {SvDeviceProfileCodecStatus::invalid_magic, 0U};
    }
    if (read_u16(source, 4U) != compiled_sv_device_profile_version) {
        return {SvDeviceProfileCodecStatus::unsupported_version, 0U};
    }
    if (read_u16(source, 6U) != header_bytes) {
        return {SvDeviceProfileCodecStatus::invalid_length, 0U};
    }
    const auto total_length = read_u32(source, 8U);
    const auto payload_length = read_u32(source, 12U);
    if (total_length != source.size() ||
        payload_length != total_length - header_bytes ||
        payload_length < fixed_payload_bytes) {
        return {SvDeviceProfileCodecStatus::invalid_length, 0U};
    }
    if (record_crc(source) != read_u32(source, kCrcOffset)) {
        return {SvDeviceProfileCodecStatus::checksum_mismatch, 0U};
    }

    const std::size_t base = header_bytes;
    if (!decode_profile_family(source[base], profile.profile_family)) {
        return {SvDeviceProfileCodecStatus::invalid_value, 0U};
    }
    if (source[base + 1U] == 1U) {
        profile.transport_mode = SvTransportMode::multicast;
    } else if (source[base + 1U] == 2U) {
        profile.transport_mode = SvTransportMode::unicast;
    } else {
        return {SvDeviceProfileCodecStatus::invalid_value, 0U};
    }
    const auto flags = read_u16(source, base + 2U);
    if ((flags & static_cast<std::uint16_t>(~kKnownFlags)) != 0U) {
        return {SvDeviceProfileCodecStatus::invalid_value, 0U};
    }
    profile.vlan_present = (flags & kFlagVlan) != 0U;
    profile.asdu_options.data_set = (flags & kFlagDataSet) != 0U;
    profile.asdu_options.sample_rate = (flags & kFlagSampleRate) != 0U;
    profile.asdu_options.sample_synchronized =
        (flags & kFlagSampleSynchronized) != 0U;
    profile.asdu_options.refresh_time = (flags & kFlagRefreshTime) != 0U;
    profile.asdu_options.security = (flags & kFlagSecurity) != 0U;
    profile.asdu_options.synch_source_id = (flags & kFlagSynchSourceId) != 0U;
    profile.asdu_options.element_present = (flags & kFlagSmvOptsPresent) != 0U;

    std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(base + 4U),
                profile.destination_mac.size(), profile.destination_mac.begin());
    profile.app_id = read_u16(source, base + 10U);
    profile.vlan_id = read_u16(source, base + 12U);
    profile.vlan_priority = source[base + 14U];
    if (!decode_sampling_basis(source[base + 15U], profile.sampling_basis)) {
        return {SvDeviceProfileCodecStatus::invalid_value, 0U};
    }
    profile.no_asdu = read_u16(source, base + 16U);
    profile.configuration_revision = read_u32(source, base + 18U);
    profile.configured_sample_rate = read_u32(source, base + 22U);
    profile.frame_rate_hz = read_u32(source, base + 26U);
    profile.sample_counter_modulus = read_u16(source, base + 30U);
    profile.leaf_count = read_u16(source, base + 32U);
    profile.payload_size_bytes = read_u32(source, base + 34U);
    profile.sv_id_length = read_u16(source, base + 38U);
    profile.data_set_reference_length = read_u16(source, base + 40U);

    if (profile.leaf_count > compiled_sv_device_profile_max_leaves ||
        profile.sv_id_length > compiled_sv_device_profile_max_sv_id_bytes ||
        profile.data_set_reference_length >
            compiled_sv_device_profile_max_dataset_bytes) {
        return {SvDeviceProfileCodecStatus::invalid_length, 0U};
    }

    const std::size_t descriptor_bytes =
        static_cast<std::size_t>(profile.leaf_count) * leaf_descriptor_bytes;
    const std::size_t expected_payload =
        fixed_payload_bytes + descriptor_bytes +
        profile.sv_id_length + profile.data_set_reference_length;
    if (expected_payload != payload_length) {
        return {SvDeviceProfileCodecStatus::invalid_length, 0U};
    }

    std::size_t offset = base + fixed_payload_bytes;
    for (std::size_t index = 0U; index < profile.leaf_count; ++index) {
        const auto type = static_cast<SvDeviceWireType>(source[offset]);
        const auto leaf_flags = source[offset + 1U];
        if (!valid_wire_type(type) || (leaf_flags & 0xFCU) != 0U) {
            return {SvDeviceProfileCodecStatus::invalid_value, 0U};
        }
        profile.leaves[index] = {
            type,
            read_u16(source, offset + 2U),
            (leaf_flags & 1U) != 0U,
            (leaf_flags & 2U) != 0U,
        };
        offset += leaf_descriptor_bytes;
    }

    std::copy_n(
        source.begin() + static_cast<std::ptrdiff_t>(offset),
        profile.sv_id_length,
        reinterpret_cast<std::uint8_t*>(profile.sv_id.data()));
    offset += profile.sv_id_length;
    std::copy_n(
        source.begin() + static_cast<std::ptrdiff_t>(offset),
        profile.data_set_reference_length,
        reinterpret_cast<std::uint8_t*>(profile.data_set_reference.data()));

    if (!valid_profile_shape(profile)) {
        profile = {};
        return {SvDeviceProfileCodecStatus::invalid_value, 0U};
    }
    return {SvDeviceProfileCodecStatus::ok, source.size()};
}

} // namespace ar::iec61850::sampled_values
