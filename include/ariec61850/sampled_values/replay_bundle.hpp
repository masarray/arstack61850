// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/sampled_values/deterministic_injector.hpp"
#include "ariec61850/sampled_values/payload_writer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::sampled_values {

inline constexpr std::size_t replay_payload_bytes =
    injector_channel_count * SampledValuesPayloadWriter::int32_quality_pair_bytes;
inline constexpr std::size_t replay_bundle_header_bytes = 32U;
inline constexpr std::array<std::uint8_t, 8U> replay_bundle_magic{
    'A', 'R', 'S', 'V', 'R', 'P', 'L', '1'};

struct ReplayBundleHeader final {
    std::uint32_t version{1U};
    std::uint32_t sample_rate_hz{4'000U};
    std::uint32_t payload_bytes{static_cast<std::uint32_t>(replay_payload_bytes)};
    std::uint32_t channel_count{static_cast<std::uint32_t>(injector_channel_count)};
    std::uint64_t frame_count{};
};

using ReplayPayloadFrame = std::array<std::uint8_t, replay_payload_bytes>;

namespace detail {

inline void write_be32(
    const std::span<std::uint8_t> bytes,
    const std::size_t offset,
    const std::uint32_t value) noexcept {
    bytes[offset] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value & 0xFFU);
}

inline void write_be64(
    const std::span<std::uint8_t> bytes,
    const std::size_t offset,
    const std::uint64_t value) noexcept {
    write_be32(bytes, offset, static_cast<std::uint32_t>(value >> 32U));
    write_be32(bytes, offset + 4U, static_cast<std::uint32_t>(value & 0xFFFF'FFFFULL));
}

[[nodiscard]] inline std::uint32_t read_be32(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset) noexcept {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
        static_cast<std::uint32_t>(bytes[offset + 3U]);
}

[[nodiscard]] inline std::uint64_t read_be64(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset) noexcept {
    return (static_cast<std::uint64_t>(read_be32(bytes, offset)) << 32U) |
        static_cast<std::uint64_t>(read_be32(bytes, offset + 4U));
}

} // namespace detail

[[nodiscard]] inline bool encode_replay_bundle_header(
    const ReplayBundleHeader& header,
    const std::span<std::uint8_t> output) noexcept {
    if (output.size() < replay_bundle_header_bytes ||
        header.version != 1U || header.sample_rate_hz == 0U ||
        header.payload_bytes != replay_payload_bytes ||
        header.channel_count != injector_channel_count) {
        return false;
    }

    for (std::size_t index = 0U; index < replay_bundle_magic.size(); ++index) {
        output[index] = replay_bundle_magic[index];
    }
    detail::write_be32(output, 8U, header.version);
    detail::write_be32(output, 12U, header.sample_rate_hz);
    detail::write_be32(output, 16U, header.payload_bytes);
    detail::write_be32(output, 20U, header.channel_count);
    detail::write_be64(output, 24U, header.frame_count);
    return true;
}

[[nodiscard]] inline bool decode_replay_bundle_header(
    const std::span<const std::uint8_t> input,
    ReplayBundleHeader& header) noexcept {
    if (input.size() < replay_bundle_header_bytes) {
        return false;
    }
    for (std::size_t index = 0U; index < replay_bundle_magic.size(); ++index) {
        if (input[index] != replay_bundle_magic[index]) {
            return false;
        }
    }

    header.version = detail::read_be32(input, 8U);
    header.sample_rate_hz = detail::read_be32(input, 12U);
    header.payload_bytes = detail::read_be32(input, 16U);
    header.channel_count = detail::read_be32(input, 20U);
    header.frame_count = detail::read_be64(input, 24U);

    return header.version == 1U && header.sample_rate_hz > 0U &&
        header.payload_bytes == replay_payload_bytes &&
        header.channel_count == injector_channel_count;
}

[[nodiscard]] inline std::uint64_t replay_bundle_expected_bytes(
    const ReplayBundleHeader& header) noexcept {
    if (header.payload_bytes != replay_payload_bytes) {
        return 0U;
    }
    constexpr auto maximum = static_cast<std::uint64_t>(-1);
    const auto payload = static_cast<std::uint64_t>(header.payload_bytes);
    if (header.frame_count >
        (maximum - static_cast<std::uint64_t>(replay_bundle_header_bytes)) / payload) {
        return 0U;
    }
    return static_cast<std::uint64_t>(replay_bundle_header_bytes) +
        header.frame_count * payload;
}

} // namespace ar::iec61850::sampled_values
