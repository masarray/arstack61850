// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace ar::iec61850::sampled_values {

inline constexpr std::size_t injector_channel_count = 8U;
inline constexpr std::uint32_t injector_gain_one = 10'000U;

struct InjectorSignalProfile final {
    bool enabled{true};
    std::int32_t rms_counts{};
    std::int32_t dc_counts{};
    std::int32_t phase_millidegrees{};
    std::uint32_t frequency_millihz{50'000U};
    std::uint16_t harmonic_permyriad{};
    std::uint8_t harmonic_order{2U};
    std::uint16_t clip_permyriad{};
    std::uint32_t quality{};
};

enum class InjectorSegmentTransition : std::uint8_t {
    step,
    linear_from_previous,
};

struct InjectorScenarioSegment final {
    // Zero means hold this segment indefinitely.
    std::uint32_t duration_samples{};
    InjectorSegmentTransition transition{InjectorSegmentTransition::step};
    std::array<InjectorSignalProfile, injector_channel_count> channels{};
};

struct InjectorSample final {
    std::uint64_t sample_index{};
    std::size_t segment_index{};
    std::array<std::int32_t, injector_channel_count> values{};
    std::array<std::uint32_t, injector_channel_count> qualities{};
};

class DeterministicSvInjector final {
public:
    DeterministicSvInjector(
        const std::span<const InjectorScenarioSegment> segments,
        const std::uint32_t sample_rate_hz,
        const bool loop) noexcept
        : segments_(segments), sample_rate_hz_(sample_rate_hz), loop_(loop) {
        valid_ = validate_configuration();
        reset();
    }

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] bool finished() const noexcept { return finished_; }
    [[nodiscard]] std::uint64_t sample_index() const noexcept { return sample_index_; }
    [[nodiscard]] std::size_t segment_index() const noexcept { return segment_index_; }

    void reset() noexcept {
        sample_index_ = 0U;
        segment_index_ = 0U;
        sample_in_segment_ = 0U;
        finished_ = !valid_;
        phase_remainder_.fill(0U);
        phase_.fill(0U);
        if (!valid_) {
            return;
        }
        for (std::size_t channel = 0U; channel < injector_channel_count; ++channel) {
            phase_[channel] = phase_from_millidegrees(
                segments_.front().channels[channel].phase_millidegrees);
        }
    }

    [[nodiscard]] bool step(InjectorSample& output) noexcept {
        if (!valid_ || finished_) {
            return false;
        }

        output.sample_index = sample_index_;
        output.segment_index = segment_index_;

        for (std::size_t channel = 0U; channel < injector_channel_count; ++channel) {
            const auto profile = effective_profile(channel);
            output.values[channel] = evaluate_channel(profile, channel);
            output.qualities[channel] = profile.quality;
            advance_phase(profile.frequency_millihz, channel);
        }

        ++sample_index_;
        advance_segment();
        return true;
    }

private:
    static constexpr std::uint64_t phase_turn = 0x1'0000'0000ULL;
    static constexpr std::int64_t sqrt_two_q30 = 1'518'500'250LL;
    static constexpr std::array<std::int16_t, 256U> sine_q15_table{
        0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739, 9512, 10278, 11039, 11793,
        12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530, 18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
        23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
        30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
        32767, 32757, 32728, 32678, 32609, 32521, 32412, 32285, 32137, 31971, 31785, 31580, 31356, 31113, 30852, 30571,
        30273, 29956, 29621, 29268, 28898, 28510, 28105, 27683, 27245, 26790, 26319, 25832, 25329, 24811, 24279, 23731,
        23170, 22594, 22005, 21403, 20787, 20159, 19519, 18868, 18204, 17530, 16846, 16151, 15446, 14732, 14010, 13279,
        12539, 11793, 11039, 10278, 9512, 8739, 7962, 7179, 6393, 5602, 4808, 4011, 3212, 2410, 1608, 804,
        0, -804, -1608, -2410, -3212, -4011, -4808, -5602, -6393, -7179, -7962, -8739, -9512, -10278, -11039, -11793,
        -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530, -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
        -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790, -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
        -30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971, -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
        -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285, -32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
        -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683, -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
        -23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868, -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
        -12539, -11793, -11039, -10278, -9512, -8739, -7962, -7179, -6393, -5602, -4808, -4011, -3212, -2410, -1608, -804,
    };

    [[nodiscard]] bool validate_configuration() const noexcept {
        if (segments_.empty() || sample_rate_hz_ == 0U || sample_rate_hz_ > 1'000'000U) {
            return false;
        }
        const auto maximum_frequency_millihz =
            static_cast<std::uint64_t>(sample_rate_hz_) * 500ULL;
        for (const auto& segment : segments_) {
            for (const auto& channel : segment.channels) {
                if (static_cast<std::uint64_t>(channel.frequency_millihz) >
                        maximum_frequency_millihz ||
                    channel.harmonic_permyriad > injector_gain_one ||
                    channel.harmonic_order < 2U ||
                    channel.harmonic_order > 63U ||
                    channel.clip_permyriad > injector_gain_one) {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] static std::uint32_t phase_from_millidegrees(
        const std::int32_t millidegrees) noexcept {
        constexpr std::int64_t full_circle = 360'000LL;
        auto normalized = static_cast<std::int64_t>(millidegrees) % full_circle;
        if (normalized < 0LL) {
            normalized += full_circle;
        }
        const auto numerator = static_cast<std::uint64_t>(normalized) * phase_turn;
        return static_cast<std::uint32_t>(
            numerator / static_cast<std::uint64_t>(full_circle));
    }

    [[nodiscard]] static std::int32_t sine_q15(const std::uint32_t phase) noexcept {
        const auto index = static_cast<std::uint8_t>(phase >> 24U);
        const auto next = static_cast<std::uint8_t>(
            index + static_cast<std::uint8_t>(1U));
        const auto fraction = static_cast<std::uint32_t>((phase >> 8U) & 0xFFFFU);
        const auto left = static_cast<std::int32_t>(sine_q15_table[index]);
        const auto right = static_cast<std::int32_t>(sine_q15_table[next]);
        const auto delta = static_cast<std::int64_t>(right - left);
        const auto interpolated = static_cast<std::int64_t>(left) +
            (delta * static_cast<std::int64_t>(fraction)) / 65'536LL;
        return static_cast<std::int32_t>(interpolated);
    }

    [[nodiscard]] static std::int32_t interpolate_i32(
        const std::int32_t start,
        const std::int32_t finish,
        const std::uint32_t position,
        const std::uint32_t denominator) noexcept {
        if (denominator == 0U || start == finish) {
            return finish;
        }
        const auto delta = static_cast<std::int64_t>(finish) -
            static_cast<std::int64_t>(start);
        const auto value = static_cast<std::int64_t>(start) +
            (delta * static_cast<std::int64_t>(position)) /
                static_cast<std::int64_t>(denominator);
        return clamp_i32(value);
    }

    [[nodiscard]] static std::uint32_t interpolate_u32(
        const std::uint32_t start,
        const std::uint32_t finish,
        const std::uint32_t position,
        const std::uint32_t denominator) noexcept {
        if (denominator == 0U || start == finish) {
            return finish;
        }
        const auto delta = static_cast<std::int64_t>(finish) -
            static_cast<std::int64_t>(start);
        const auto value = static_cast<std::int64_t>(start) +
            (delta * static_cast<std::int64_t>(position)) /
                static_cast<std::int64_t>(denominator);
        return value <= 0LL ? 0U : static_cast<std::uint32_t>(value);
    }

    [[nodiscard]] InjectorSignalProfile effective_profile(
        const std::size_t channel) const noexcept {
        auto profile = segments_[segment_index_].channels[channel];
        const auto& segment = segments_[segment_index_];
        if (segment.transition != InjectorSegmentTransition::linear_from_previous ||
            segment_index_ == 0U || segment.duration_samples <= 1U) {
            return profile;
        }

        const auto& previous = segments_[segment_index_ - 1U].channels[channel];
        const auto denominator = segment.duration_samples - 1U;
        profile.rms_counts = interpolate_i32(
            previous.rms_counts, profile.rms_counts, sample_in_segment_, denominator);
        profile.dc_counts = interpolate_i32(
            previous.dc_counts, profile.dc_counts, sample_in_segment_, denominator);
        profile.frequency_millihz = interpolate_u32(
            previous.frequency_millihz,
            profile.frequency_millihz,
            sample_in_segment_,
            denominator);
        profile.harmonic_permyriad = static_cast<std::uint16_t>(interpolate_u32(
            previous.harmonic_permyriad,
            profile.harmonic_permyriad,
            sample_in_segment_,
            denominator));
        profile.clip_permyriad = static_cast<std::uint16_t>(interpolate_u32(
            previous.clip_permyriad,
            profile.clip_permyriad,
            sample_in_segment_,
            denominator));
        return profile;
    }

    [[nodiscard]] static std::int32_t clamp_i32(const std::int64_t value) noexcept {
        if (value > static_cast<std::int64_t>(
                        std::numeric_limits<std::int32_t>::max())) {
            return std::numeric_limits<std::int32_t>::max();
        }
        if (value < static_cast<std::int64_t>(
                        std::numeric_limits<std::int32_t>::min())) {
            return std::numeric_limits<std::int32_t>::min();
        }
        return static_cast<std::int32_t>(value);
    }

    [[nodiscard]] std::int32_t evaluate_channel(
        const InjectorSignalProfile& profile,
        const std::size_t channel) const noexcept {
        if (!profile.enabled || profile.rms_counts == 0) {
            return profile.dc_counts;
        }

        const auto peak =
            (static_cast<std::int64_t>(profile.rms_counts) * sqrt_two_q30) >> 30U;
        const auto fundamental_q15 = sine_q15(phase_[channel]);
        auto sample =
            (peak * static_cast<std::int64_t>(fundamental_q15)) / 32'767LL;

        if (profile.harmonic_permyriad > 0U) {
            const auto harmonic_phase = static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(phase_[channel]) *
                static_cast<std::uint64_t>(profile.harmonic_order));
            const auto harmonic_q15 = sine_q15(harmonic_phase);
            const auto harmonic_peak =
                (peak * static_cast<std::int64_t>(profile.harmonic_permyriad)) /
                static_cast<std::int64_t>(injector_gain_one);
            sample +=
                (harmonic_peak * static_cast<std::int64_t>(harmonic_q15)) / 32'767LL;
        }

        sample += static_cast<std::int64_t>(profile.dc_counts);

        if (profile.clip_permyriad > 0U) {
            const auto absolute_peak = peak < 0LL ? -peak : peak;
            const auto limit =
                (absolute_peak * static_cast<std::int64_t>(profile.clip_permyriad)) /
                static_cast<std::int64_t>(injector_gain_one);
            if (sample > limit) {
                sample = limit;
            } else if (sample < -limit) {
                sample = -limit;
            }
        }

        return clamp_i32(sample);
    }

    void advance_phase(
        const std::uint32_t frequency_millihz,
        const std::size_t channel) noexcept {
        const auto denominator =
            static_cast<std::uint64_t>(sample_rate_hz_) * 1'000ULL;
        const auto numerator =
            static_cast<std::uint64_t>(frequency_millihz) * phase_turn +
            phase_remainder_[channel];
        const auto increment = numerator / denominator;
        phase_remainder_[channel] = numerator % denominator;
        phase_[channel] += static_cast<std::uint32_t>(increment);
    }

    void adjust_phase_offsets(
        const InjectorScenarioSegment& previous,
        const InjectorScenarioSegment& next) noexcept {
        for (std::size_t channel = 0U; channel < injector_channel_count; ++channel) {
            const auto delta =
                static_cast<std::int64_t>(next.channels[channel].phase_millidegrees) -
                static_cast<std::int64_t>(previous.channels[channel].phase_millidegrees);
            auto normalized = delta % 360'000LL;
            if (normalized < 0LL) {
                normalized += 360'000LL;
            }
            const auto delta_phase = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(normalized) * phase_turn) / 360'000ULL);
            phase_[channel] += delta_phase;
        }
    }

    void advance_segment() noexcept {
        const auto duration = segments_[segment_index_].duration_samples;
        if (duration == 0U) {
            ++sample_in_segment_;
            return;
        }

        ++sample_in_segment_;
        if (sample_in_segment_ < duration) {
            return;
        }

        const auto previous_index = segment_index_;
        sample_in_segment_ = 0U;
        if (segment_index_ + 1U < segments_.size()) {
            ++segment_index_;
            adjust_phase_offsets(
                segments_[previous_index], segments_[segment_index_]);
            return;
        }

        if (loop_) {
            segment_index_ = 0U;
            adjust_phase_offsets(
                segments_[previous_index], segments_[segment_index_]);
            return;
        }

        finished_ = true;
    }

    std::span<const InjectorScenarioSegment> segments_{};
    std::uint32_t sample_rate_hz_{};
    bool loop_{};
    bool valid_{};
    bool finished_{};
    std::uint64_t sample_index_{};
    std::size_t segment_index_{};
    std::uint32_t sample_in_segment_{};
    std::array<std::uint32_t, injector_channel_count> phase_{};
    std::array<std::uint64_t, injector_channel_count> phase_remainder_{};
};

} // namespace ar::iec61850::sampled_values
