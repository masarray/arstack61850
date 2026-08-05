// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/goose/retransmission_schedule.hpp"

#include <algorithm>
#include <limits>

namespace ar::iec61850::goose {

RetransmissionSchedule::RetransmissionSchedule(
    const std::uint32_t min_time_milliseconds,
    const std::uint32_t max_time_milliseconds)
    : min_time_milliseconds_(normalize_min_time(min_time_milliseconds)),
      max_time_milliseconds_(normalize_max_time(max_time_milliseconds, min_time_milliseconds_)) {
    reset();
}

int RetransmissionSchedule::next_delay_milliseconds() noexcept {
    const auto delay = next_delay_milliseconds_;
    next_delay_milliseconds_ = next_delay_milliseconds_ >= max_time_milliseconds_ / 2
        ? max_time_milliseconds_
        : std::min(max_time_milliseconds_, next_delay_milliseconds_ * 2);
    return delay;
}

void RetransmissionSchedule::reset() noexcept {
    next_delay_milliseconds_ = min_time_milliseconds_;
}

int RetransmissionSchedule::normalize_min_time(const std::uint32_t value) noexcept {
    return value == 0U
        ? 4
        : static_cast<int>(std::min<std::uint32_t>(value, static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
}

int RetransmissionSchedule::normalize_max_time(const std::uint32_t value,
                                               const int min_time_milliseconds) noexcept {
    const auto maximum = value == 0U
        ? 1000
        : static_cast<int>(std::min<std::uint32_t>(value, static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
    return std::max(min_time_milliseconds, maximum);
}

} // namespace ar::iec61850::goose
