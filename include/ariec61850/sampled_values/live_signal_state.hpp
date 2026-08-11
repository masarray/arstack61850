// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace ar::iec61850::sampled_values {

constexpr std::size_t kLiveSignalChannelCount = 8U;
constexpr std::size_t kLiveSineLutSize = 4096U;

struct SvLiveChannelState final {
    // RMS magnitude in raw wire counts. Engineering-unit conversion belongs
    // to the host/profile layer so the realtime engine remains profile-neutral.
    std::int32_t rms_counts{};
    std::int32_t phase_millidegrees{};
    std::uint32_t quality{};
    bool enabled{true};

    friend bool operator==(const SvLiveChannelState&, const SvLiveChannelState&) = default;
};

struct SvCurrentWaveformShape final {
    bool enabled{false};
    std::int32_t dc_offset_permille{0};
    std::uint32_t harmonic_permille{0};
    std::uint8_t harmonic_order{2};
    std::uint32_t clip_permille{1000};

    friend bool operator==(const SvCurrentWaveformShape&, const SvCurrentWaveformShape&) = default;
};

struct SvLiveSignalState final {
    // Fundamental signal frequency in millihertz. This is deliberately
    // independent of the SV publisher event rate.
    std::uint32_t frequency_millihz{50000U};
    std::array<SvLiveChannelState, kLiveSignalChannelCount> channels{};
    SvCurrentWaveformShape current_shape{};
    std::uint64_t generation{};

    friend bool operator==(const SvLiveSignalState&, const SvLiveSignalState&) = default;
};

class SvLiveSignalBank final {
public:
    SvLiveSignalBank() noexcept {
        banks_[0] = default_state();
        banks_[1] = banks_[0];
        banks_[2] = banks_[0];
    }

    // One control-plane writer and any number of serialized realtime snapshots
    // are expected. The reader advertises the bank it is copying before the
    // copy begins. With three banks, the writer can always select a bank that
    // is neither active nor being read, so no non-atomic object is read and
    // written concurrently.
    [[nodiscard]] SvLiveSignalState snapshot() const noexcept {
        while (true) {
            const auto index = active_index_.load(std::memory_order_acquire);
            reader_index_.store(index, std::memory_order_release);

            if (active_index_.load(std::memory_order_acquire) != index) {
                reader_index_.store(kNoReader, std::memory_order_release);
                continue;
            }

            const auto copy = banks_[index];
            reader_index_.store(kNoReader, std::memory_order_release);
            return copy;
        }
    }

    [[nodiscard]] std::uint32_t active_index() const noexcept {
        return active_index_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool publish(const SvLiveSignalState& requested) noexcept {
        if (!validate(requested)) {
            return false;
        }

        const auto current = active_index_.load(std::memory_order_acquire);
        const auto reader = reader_index_.load(std::memory_order_acquire);

        std::uint32_t next = kNoReader;
        for (std::uint32_t candidate = 0U; candidate < banks_.size(); ++candidate) {
            if (candidate != current && candidate != reader) {
                next = candidate;
                break;
            }
        }
        if (next == kNoReader) {
            return false;
        }

        banks_[next] = requested;
        banks_[next].generation = banks_[current].generation + 1U;
        active_index_.store(next, std::memory_order_release);
        return true;
    }

    [[nodiscard]] static bool validate(const SvLiveSignalState& state) noexcept {
        // 0 mHz is the explicit DC mode. AC remains bounded to 1..1000 Hz.
        if (state.frequency_millihz != 0U &&
            (state.frequency_millihz < 1000U || state.frequency_millihz > 1000000U)) {
            return false;
        }
        for (const auto& channel : state.channels) {
            if (channel.rms_counts == std::numeric_limits<std::int32_t>::min()) {
                return false;
            }
            if (channel.phase_millidegrees < -360000000 ||
                channel.phase_millidegrees > 360000000) {
                return false;
            }
        }
        if (state.current_shape.dc_offset_permille < -3000 ||
            state.current_shape.dc_offset_permille > 3000 ||
            state.current_shape.harmonic_permille > 3000U ||
            state.current_shape.harmonic_order < 2U ||
            state.current_shape.harmonic_order > 63U ||
            state.current_shape.clip_permille < 10U ||
            state.current_shape.clip_permille > 10000U) {
            return false;
        }
        return true;
    }

    [[nodiscard]] static SvLiveSignalState default_state() noexcept {
        SvLiveSignalState state;
        state.channels[0] = {1000, 0, 0U, true};
        state.channels[1] = {1000, -120000, 0U, true};
        state.channels[2] = {1000, 120000, 0U, true};
        state.channels[3] = {0, 0, 0U, true};
        state.channels[4] = {5774, 0, 0U, true};
        state.channels[5] = {5774, -120000, 0U, true};
        state.channels[6] = {5774, 120000, 0U, true};
        state.channels[7] = {0, 0, 0U, true};
        return state;
    }

private:
    static constexpr std::uint32_t kNoReader = 3U;
    std::array<SvLiveSignalState, 3> banks_{};
    mutable std::atomic<std::uint32_t> reader_index_{kNoReader};
    std::atomic<std::uint32_t> active_index_{0U};
};

class SvFixedPointSineEngine final {
public:
    using SampleRow = std::array<std::int32_t, kLiveSignalChannelCount>;

    SvFixedPointSineEngine() noexcept {
        build_lut();
    }

    void reset_phase(const std::uint32_t phase = 0U) noexcept {
        phase_accumulator_ = phase;
    }

    void advance(
        const SvLiveSignalState& state,
        const std::uint32_t publisher_rate_hz,
        const std::uint32_t slots) noexcept {
        const std::uint64_t delta =
            static_cast<std::uint64_t>(phase_step_for(state.frequency_millihz, publisher_rate_hz)) *
            static_cast<std::uint64_t>(slots);
        phase_accumulator_ += static_cast<std::uint32_t>(delta);
    }

    [[nodiscard]] std::uint32_t phase_accumulator() const noexcept {
        return phase_accumulator_;
    }

    [[nodiscard]] SampleRow next(
        const SvLiveSignalState& state,
        const std::uint32_t publisher_rate_hz) noexcept {
        SampleRow row{};
        if (publisher_rate_hz == 0U) {
            return row;
        }

        const auto phase_step = phase_step_for(state.frequency_millihz, publisher_rate_hz);
        const auto base_phase = phase_accumulator_;

        for (std::size_t index = 0U; index < row.size(); ++index) {
            const auto& channel = state.channels[index];
            if (!channel.enabled || channel.rms_counts == 0) {
                row[index] = 0;
                continue;
            }

            if (state.frequency_millihz == 0U) {
                // In DC mode the engineering magnitude is an instantaneous
                // signed value. Phase is intentionally ignored.
                row[index] = channel.rms_counts;
                continue;
            }

            const auto offset = phase_from_millidegrees(channel.phase_millidegrees);
            const std::uint32_t phase = base_phase + offset;
            const auto lut_index = static_cast<std::size_t>(phase >> (32U - 12U));
            std::int64_t sine_q30 = sine_lut_[lut_index & (kLiveSineLutSize - 1U)];
            if (index < 4U && state.current_shape.enabled) {
                const auto harmonic_phase = phase * state.current_shape.harmonic_order;
                const auto harmonic_index = static_cast<std::size_t>(harmonic_phase >> (32U - 12U));
                const std::int64_t harmonic_q30 = sine_lut_[harmonic_index & (kLiveSineLutSize - 1U)];
                constexpr std::int64_t q30 = std::int64_t{1} << 30U;
                sine_q30 += (harmonic_q30 * state.current_shape.harmonic_permille) / std::int64_t{1000};
                sine_q30 += (q30 * state.current_shape.dc_offset_permille) / std::int64_t{1000};
                const std::int64_t clip_q30 =
                    (q30 * state.current_shape.clip_permille) / std::int64_t{1000};
                sine_q30 = std::clamp(sine_q30, -clip_q30, clip_q30);
            }
            const std::int64_t peak_counts = rms_to_peak_counts(channel.rms_counts);
            const std::int64_t value = (sine_q30 * peak_counts) >> 30U;
            row[index] = clamp_i32(value);
        }

        phase_accumulator_ += phase_step;
        return row;
    }

    [[nodiscard]] static std::uint32_t phase_step_for(
        const std::uint32_t frequency_millihz,
        const std::uint32_t publisher_rate_hz) noexcept {
        if (publisher_rate_hz == 0U) {
            return 0U;
        }
        constexpr std::uint64_t full_turn = (std::uint64_t{1} << 32U);
        const std::uint64_t denominator =
            static_cast<std::uint64_t>(publisher_rate_hz) * 1000ULL;
        const std::uint64_t numerator =
            full_turn * static_cast<std::uint64_t>(frequency_millihz);
        return static_cast<std::uint32_t>((numerator + denominator / 2ULL) / denominator);
    }

    [[nodiscard]] static std::uint32_t phase_from_millidegrees(
        const std::int32_t millidegrees) noexcept {
        constexpr std::int64_t full_turn_md = 360000LL;
        constexpr std::uint64_t full_turn = (std::uint64_t{1} << 32U);
        std::int64_t normalized = static_cast<std::int64_t>(millidegrees) % full_turn_md;
        if (normalized < 0) {
            normalized += full_turn_md;
        }
        return static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(normalized) * full_turn) /
            static_cast<std::uint64_t>(full_turn_md));
    }

private:
    std::array<std::int32_t, kLiveSineLutSize> sine_lut_{};
    std::uint32_t phase_accumulator_{};

    void build_lut() noexcept {
        constexpr double pi = 3.141592653589793238462643383279502884;
        constexpr double q30 = static_cast<double>(std::uint64_t{1} << 30U);
        for (std::size_t index = 0U; index < sine_lut_.size(); ++index) {
            const double angle = 2.0 * pi * static_cast<double>(index) /
                                 static_cast<double>(sine_lut_.size());
            sine_lut_[index] = static_cast<std::int32_t>(std::llround(std::sin(angle) * q30));
        }
    }

    [[nodiscard]] static std::int64_t rms_to_peak_counts(const std::int32_t rms) noexcept {
        constexpr std::int64_t sqrt2_q30 = 1518500250LL;
        return (static_cast<std::int64_t>(rms) * sqrt2_q30) >> 30U;
    }

    [[nodiscard]] static std::int32_t clamp_i32(const std::int64_t value) noexcept {
        if (value > std::numeric_limits<std::int32_t>::max()) {
            return std::numeric_limits<std::int32_t>::max();
        }
        if (value < std::numeric_limits<std::int32_t>::min()) {
            return std::numeric_limits<std::int32_t>::min();
        }
        return static_cast<std::int32_t>(value);
    }
};

[[nodiscard]] inline std::optional<std::size_t> live_channel_index(
    const std::string_view name) noexcept {
    constexpr std::array<std::string_view, kLiveSignalChannelCount> names{
        "IA", "IB", "IC", "IN", "UA", "UB", "UC", "UN"};
    for (std::size_t index = 0U; index < names.size(); ++index) {
        if (name == names[index]) {
            return index;
        }
    }
    return std::nullopt;
}

} // namespace ar::iec61850::sampled_values
