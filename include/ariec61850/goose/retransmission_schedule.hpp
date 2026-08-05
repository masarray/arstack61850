// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace ar::iec61850::goose {

class RetransmissionSchedule final {
public:
    RetransmissionSchedule(std::uint32_t min_time_milliseconds,
                           std::uint32_t max_time_milliseconds);

    [[nodiscard]] int min_time_milliseconds() const noexcept { return min_time_milliseconds_; }
    [[nodiscard]] int max_time_milliseconds() const noexcept { return max_time_milliseconds_; }

    int next_delay_milliseconds() noexcept;
    void reset() noexcept;

private:
    static int normalize_min_time(std::uint32_t value) noexcept;
    static int normalize_max_time(std::uint32_t value, int min_time_milliseconds) noexcept;

    int min_time_milliseconds_{};
    int max_time_milliseconds_{};
    int next_delay_milliseconds_{};
};

} // namespace ar::iec61850::goose
