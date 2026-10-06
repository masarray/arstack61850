// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>

namespace ar::iec61850::sampled_values {

enum class SvSampleMode : std::uint8_t {
    unknown,
    samples_per_period,
    samples_per_second,
    seconds_per_sample,
};

enum class SvTimingResolution : std::uint8_t {
    resolved,
    needs_nominal_frequency,
    invalid_sample_rate,
    invalid_asdu_count,
    unsupported_sample_mode,
    invalid_nominal_frequency,
    arithmetic_overflow,
};

struct SvRationalRate final {
    std::uint64_t numerator{};
    std::uint64_t denominator{1U};

    [[nodiscard]] static std::optional<SvRationalRate> make(
        const std::uint64_t numerator_value,
        const std::uint64_t denominator_value) noexcept {
        if (numerator_value == 0U || denominator_value == 0U) {
            return std::nullopt;
        }
        const auto divisor = std::gcd(numerator_value, denominator_value);
        return SvRationalRate{
            numerator_value / divisor,
            denominator_value / divisor,
        };
    }

    [[nodiscard]] std::optional<std::uint32_t> exact_hz() const noexcept {
        if (denominator == 0U || numerator == 0U ||
            (numerator % denominator) != 0U) {
            return std::nullopt;
        }
        const auto value = numerator / denominator;
        if (value > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(value);
    }

    friend bool operator==(const SvRationalRate&, const SvRationalRate&) = default;
};

struct SvPublicationTiming final {
    SvSampleMode sampling_basis{SvSampleMode::unknown};
    std::uint32_t configured_sample_rate{};
    std::uint16_t asdus_per_frame{1U};
    std::optional<std::uint32_t> nominal_frequency_millihz;
    std::optional<SvRationalRate> samples_per_second;
    std::optional<SvRationalRate> frames_per_second;
    SvTimingResolution resolution{SvTimingResolution::unsupported_sample_mode};

    [[nodiscard]] bool resolved() const noexcept {
        return resolution == SvTimingResolution::resolved;
    }

    [[nodiscard]] std::optional<std::uint32_t> exact_sample_rate_hz() const noexcept {
        return samples_per_second ? samples_per_second->exact_hz() : std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint32_t> exact_frame_rate_hz() const noexcept {
        return frames_per_second ? frames_per_second->exact_hz() : std::nullopt;
    }

    friend bool operator==(const SvPublicationTiming&, const SvPublicationTiming&) = default;
};

[[nodiscard]] inline SvPublicationTiming resolve_sv_publication_timing(
    const SvSampleMode sampling_basis,
    const std::uint32_t configured_sample_rate,
    const std::uint16_t asdus_per_frame,
    const std::optional<std::uint32_t> nominal_frequency_millihz = std::nullopt) noexcept {
    SvPublicationTiming timing;
    timing.sampling_basis = sampling_basis;
    timing.configured_sample_rate = configured_sample_rate;
    timing.asdus_per_frame = asdus_per_frame;
    timing.nominal_frequency_millihz = nominal_frequency_millihz;

    if (configured_sample_rate == 0U) {
        timing.resolution = SvTimingResolution::invalid_sample_rate;
        return timing;
    }
    if (asdus_per_frame == 0U) {
        timing.resolution = SvTimingResolution::invalid_asdu_count;
        return timing;
    }

    std::optional<SvRationalRate> samples_per_second;
    switch (sampling_basis) {
    case SvSampleMode::samples_per_second:
        samples_per_second = SvRationalRate::make(configured_sample_rate, 1U);
        break;
    case SvSampleMode::samples_per_period:
        if (!nominal_frequency_millihz.has_value()) {
            timing.resolution = SvTimingResolution::needs_nominal_frequency;
            return timing;
        }
        if (*nominal_frequency_millihz == 0U) {
            timing.resolution = SvTimingResolution::invalid_nominal_frequency;
            return timing;
        }
        if (configured_sample_rate >
            std::numeric_limits<std::uint64_t>::max() /
                static_cast<std::uint64_t>(*nominal_frequency_millihz)) {
            timing.resolution = SvTimingResolution::arithmetic_overflow;
            return timing;
        }
        samples_per_second = SvRationalRate::make(
            static_cast<std::uint64_t>(configured_sample_rate) *
                static_cast<std::uint64_t>(*nominal_frequency_millihz),
            1000U);
        break;
    case SvSampleMode::seconds_per_sample:
        samples_per_second = SvRationalRate::make(1U, configured_sample_rate);
        break;
    case SvSampleMode::unknown:
        timing.resolution = SvTimingResolution::unsupported_sample_mode;
        return timing;
    }

    if (!samples_per_second.has_value()) {
        timing.resolution = SvTimingResolution::arithmetic_overflow;
        return timing;
    }
    if (samples_per_second->denominator >
        std::numeric_limits<std::uint64_t>::max() /
            static_cast<std::uint64_t>(asdus_per_frame)) {
        timing.resolution = SvTimingResolution::arithmetic_overflow;
        return timing;
    }

    const auto frames_per_second = SvRationalRate::make(
        samples_per_second->numerator,
        samples_per_second->denominator * static_cast<std::uint64_t>(asdus_per_frame));
    if (!frames_per_second.has_value()) {
        timing.resolution = SvTimingResolution::arithmetic_overflow;
        return timing;
    }

    timing.samples_per_second = *samples_per_second;
    timing.frames_per_second = *frames_per_second;
    timing.resolution = SvTimingResolution::resolved;
    return timing;
}

} // namespace ar::iec61850::sampled_values
