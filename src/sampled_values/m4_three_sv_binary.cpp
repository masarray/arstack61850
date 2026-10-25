// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/sampled_values/m4_three_sv_binary.hpp"
#include "ariec61850/integrity/crc32.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace ar::iec61850::sampled_values {
namespace {
constexpr std::array<std::uint8_t, 4> kMagic{'A', 'R', 'M', '3'};
constexpr std::size_t kCrcOffset = 12U;
constexpr std::size_t kLengthOffset = 20U;

void write_u16(const std::span<std::uint8_t> data,
               const std::size_t pos, const std::uint16_t value) noexcept {
    data[pos] = static_cast<std::uint8_t>(value >> 8U);
    data[pos + 1U] = static_cast<std::uint8_t>(value);
}
void write_u32(const std::span<std::uint8_t> data,
               const std::size_t pos, const std::uint32_t value) noexcept {
    data[pos] = static_cast<std::uint8_t>(value >> 24U);
    data[pos + 1U] = static_cast<std::uint8_t>(value >> 16U);
    data[pos + 2U] = static_cast<std::uint8_t>(value >> 8U);
    data[pos + 3U] = static_cast<std::uint8_t>(value);
}
[[nodiscard]] std::uint16_t read_u16(const std::span<const std::uint8_t> data,
                                     const std::size_t pos) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[pos]) << 8U) |
                                      static_cast<std::uint16_t>(data[pos + 1U]));
}
[[nodiscard]] std::uint32_t read_u32(const std::span<const std::uint8_t> data,
                                     const std::size_t pos) noexcept {
    return (static_cast<std::uint32_t>(data[pos]) << 24U) |
           (static_cast<std::uint32_t>(data[pos + 1U]) << 16U) |
           (static_cast<std::uint32_t>(data[pos + 2U]) << 8U) |
           static_cast<std::uint32_t>(data[pos + 3U]);
}
[[nodiscard]] std::uint32_t envelope_crc(
    const std::span<const std::uint8_t> data) noexcept {
    std::uint32_t state = integrity::crc32_initial_state;
    state = integrity::crc32_update(state, data.first(kCrcOffset));
    constexpr std::array<std::uint8_t, 4> zero{};
    state = integrity::crc32_update(state, zero);
    state = integrity::crc32_update(state, data.subspan(kCrcOffset + zero.size()));
    return integrity::crc32_finalize(state);
}

[[nodiscard]] bool compatible(
    const CompiledSvDeviceProfile& p) noexcept {
    return (p.profile_family == SvProfileFamily::legacy_9_2le ||
            p.profile_family == SvProfileFamily::iec61850_9_2) &&
           p.frame_rate_hz == m4_three_sv_rate_hz &&
           p.configured_sample_rate == m4_three_sv_rate_hz &&
           p.sampling_basis == SvSampleMode::samples_per_second &&
           p.sample_counter_modulus == m4_three_sv_rate_hz &&
           p.no_asdu == 1U &&
           p.leaf_count == 16U &&
           p.payload_size_bytes == 64U &&
           p.asdu_options.sample_synchronized &&
           !p.asdu_options.refresh_time &&
           !p.asdu_options.security &&
           !p.asdu_options.synch_source_id &&
           [&p]() noexcept {
               // The ESP32-P4 runtime patches eight exactly ordered
               // INT32+Quality pairs; 16 arbitrary four-byte leaves are NOT
               // an interchangeable 4I+4V waveform payload.
               for (std::size_t channel = 0U; channel < 8U; ++channel) {
                   const auto& value = p.leaves[channel * 2U];
                   const auto& quality = p.leaves[channel * 2U + 1U];
                   if (value.wire_type != SvDeviceWireType::int32 ||
                       value.wire_width_bytes != 4U || value.quality ||
                       value.timestamp ||
                       quality.wire_type != SvDeviceWireType::quality ||
                       quality.wire_width_bytes != 4U || !quality.quality ||
                       quality.timestamp) {
                       return false;
                   }
               }
               return true;
           }();
}

[[nodiscard]] bool distinct_identity(
    const CompiledSvDeviceProfile& lhs,
    const CompiledSvDeviceProfile& rhs) noexcept {
    const auto name = [](const CompiledSvDeviceProfile& p) {
        return std::string_view{p.sv_id.data(), p.sv_id_length};
    };
    return !(lhs.app_id == rhs.app_id &&
        lhs.destination_mac == rhs.destination_mac &&
        lhs.transport_mode == rhs.transport_mode &&
        lhs.vlan_present == rhs.vlan_present &&
        (!lhs.vlan_present ||
         (lhs.vlan_id == rhs.vlan_id &&
          lhs.vlan_priority == rhs.vlan_priority)) &&
        name(lhs) == name(rhs));
}

[[nodiscard]] bool valid_roster(
    const std::array<CompiledSvDeviceProfile, m4_three_sv_capacity>& p) noexcept {
    for (std::size_t i = 0U; i < p.size(); ++i) {
        if (!compatible(p[i])) return false;
        for (std::size_t j = 0U; j < i; ++j) {
            if (!distinct_identity(p[i], p[j])) return false;
        }
    }
    return true;
}
} // namespace

std::optional<std::size_t> M4ThreeSvBinaryCodec::encoded_size(
    const std::array<CompiledSvDeviceProfile, m4_three_sv_capacity>& p) noexcept {
    if (!valid_roster(p)) return std::nullopt;
    std::size_t total = header_bytes;
    for (const auto& profile : p) {
        const auto size = SvDeviceProfileBinaryCodec::encoded_size(profile);
        if (!size.has_value() ||
            *size == 0U || *size > std::numeric_limits<std::uint16_t>::max() ||
            total > std::numeric_limits<std::uint32_t>::max() - *size) {
            return std::nullopt;
        }
        total += *size;
    }
    return total;
}

M4ThreeSvBinaryResult M4ThreeSvBinaryCodec::encode_into(
    const std::array<CompiledSvDeviceProfile, m4_three_sv_capacity>& p,
    const std::span<std::uint8_t> destination) noexcept {
    const auto total = encoded_size(p);
    if (!total.has_value()) return {M4ThreeSvBinaryStatus::invalid_profile, 0U};
    if (destination.size() < *total)
        return {M4ThreeSvBinaryStatus::buffer_too_small, *total};
    const auto out = destination.first(*total);
    for (std::size_t i = 0U; i < kMagic.size(); ++i) out[i] = kMagic[i];
    write_u16(out, 4U, m4_three_sv_binary_version);
    out[6U] = static_cast<std::uint8_t>(m4_three_sv_capacity);
    out[7U] = 0U; // reserved
    write_u32(out, 8U, static_cast<std::uint32_t>(*total));
    write_u32(out, kCrcOffset, 0U);
    write_u32(out, 16U, m4_three_sv_rate_hz);
    out[26U] = 0U;
    out[27U] = 0U;
    std::size_t offset = header_bytes;
    for (std::size_t i = 0U; i < p.size(); ++i) {
        const auto size = SvDeviceProfileBinaryCodec::encoded_size(p[i]);
        if (!size.has_value()) return {M4ThreeSvBinaryStatus::invalid_profile, 0U};
        write_u16(out, kLengthOffset + 2U * i, static_cast<std::uint16_t>(*size));
        const auto encoded = SvDeviceProfileBinaryCodec::encode_into(
            p[i], out.subspan(offset, *size));
        if (!encoded.success() || encoded.bytes != *size)
            return {M4ThreeSvBinaryStatus::invalid_profile, 0U};
        offset += *size;
    }
    write_u32(out, kCrcOffset, envelope_crc(out));
    return {M4ThreeSvBinaryStatus::ok, *total};
}

M4ThreeSvBinaryResult M4ThreeSvBinaryCodec::decode(
    const std::span<const std::uint8_t> source,
    std::array<CompiledSvDeviceProfile, m4_three_sv_capacity>& destination) noexcept {
    if (source.size() < header_bytes)
        return {M4ThreeSvBinaryStatus::invalid_length, 0U};
    for (std::size_t i = 0U; i < kMagic.size(); ++i)
        if (source[i] != kMagic[i])
            return {M4ThreeSvBinaryStatus::invalid_magic, 0U};
    if (read_u16(source, 4U) != m4_three_sv_binary_version)
        return {M4ThreeSvBinaryStatus::unsupported_version, 0U};
    if (source[6U] != m4_three_sv_capacity || source[7U] != 0U ||
        read_u32(source, 8U) != source.size() ||
        read_u32(source, 16U) != m4_three_sv_rate_hz ||
        source[26U] != 0U || source[27U] != 0U) {
        return {M4ThreeSvBinaryStatus::invalid_length, 0U};
    }
    std::size_t sum = header_bytes;
    for (std::size_t i = 0U; i < m4_three_sv_capacity; ++i) {
        const std::size_t record_length = read_u16(source, kLengthOffset + 2U * i);
        if (record_length == 0U ||
            record_length > source.size() - sum) {
            return {M4ThreeSvBinaryStatus::invalid_length, 0U};
        }
        sum += record_length;
    }
    if (sum != source.size())
        return {M4ThreeSvBinaryStatus::invalid_length, 0U};
    if (read_u32(source, kCrcOffset) != envelope_crc(source))
        return {M4ThreeSvBinaryStatus::checksum_mismatch, 0U};

    // Validate all nested V1 records and identities before publishing any
    // externally-visible state. Failed decode never edits the caller's roster.
    std::array<CompiledSvDeviceProfile, m4_three_sv_capacity> candidate{};
    std::size_t offset = header_bytes;
    for (std::size_t i = 0U; i < candidate.size(); ++i) {
        const auto length = read_u16(source, kLengthOffset + 2U * i);
        const auto inner = SvDeviceProfileBinaryCodec::decode(
            source.subspan(offset, length), candidate[i]);
        if (!inner.success() || inner.bytes != length)
            return {M4ThreeSvBinaryStatus::invalid_profile, 0U};
        offset += length;
    }
    if (!valid_roster(candidate))
        return {M4ThreeSvBinaryStatus::invalid_profile, 0U};

    destination = candidate;
    return {M4ThreeSvBinaryStatus::ok, source.size()};
}

} // namespace ar::iec61850::sampled_values
