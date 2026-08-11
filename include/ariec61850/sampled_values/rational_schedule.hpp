// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <stdexcept>

namespace ar::iec61850::sampled_values {

class RationalTickSchedule final {
public:
    RationalTickSchedule(
        const std::uint64_t timer_frequency_hz,
        const std::uint32_t event_rate_hz)
        : timer_frequency_hz_(timer_frequency_hz),
          event_rate_hz_(event_rate_hz) {
        if (timer_frequency_hz_ == 0U || event_rate_hz_ == 0U) {
            throw std::invalid_argument(
                "Timer frequency and event rate must both be non-zero.");
        }
        if (timer_frequency_hz_ < event_rate_hz_) {
            throw std::invalid_argument(
                "Timer frequency must be at least as high as the event rate.");
        }

        base_interval_ticks_ = timer_frequency_hz_ / event_rate_hz_;
        remainder_ticks_numerator_ = static_cast<std::uint32_t>(
            timer_frequency_hz_ % event_rate_hz_);
    }

    [[nodiscard]] std::uint64_t next_interval_ticks() noexcept {
        auto interval = base_interval_ticks_;
        const auto accumulated =
            static_cast<std::uint64_t>(remainder_accumulator_) +
            remainder_ticks_numerator_;
        if (accumulated >= event_rate_hz_) {
            ++interval;
            remainder_accumulator_ = static_cast<std::uint32_t>(
                accumulated - event_rate_hz_);
        } else {
            remainder_accumulator_ = static_cast<std::uint32_t>(accumulated);
        }
        return interval;
    }

    void reset() noexcept {
        remainder_accumulator_ = 0U;
    }

    [[nodiscard]] std::uint64_t timer_frequency_hz() const noexcept {
        return timer_frequency_hz_;
    }
    [[nodiscard]] std::uint32_t event_rate_hz() const noexcept {
        return event_rate_hz_;
    }
    [[nodiscard]] std::uint64_t base_interval_ticks() const noexcept {
        return base_interval_ticks_;
    }
    [[nodiscard]] std::uint32_t remainder_ticks_numerator() const noexcept {
        return remainder_ticks_numerator_;
    }

private:
    std::uint64_t timer_frequency_hz_{};
    std::uint32_t event_rate_hz_{};
    std::uint64_t base_interval_ticks_{};
    std::uint32_t remainder_ticks_numerator_{};
    std::uint32_t remainder_accumulator_{};
};

} // namespace ar::iec61850::sampled_values
