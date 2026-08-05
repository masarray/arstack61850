// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/goose/frame.hpp"
#include "ariec61850/goose/retransmission_schedule.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ar::iec61850::goose {

struct GoosePublication final {
    GooseFrame frame;
    std::vector<std::uint8_t> ethernet_bytes;
};

class GoosePublisherSession final {
public:
    explicit GoosePublisherSession(
        GooseFrame frame_template,
        std::uint32_t initial_state_number = 1U,
        std::uint32_t initial_sequence_number = 0U);

    [[nodiscard]] GoosePublication publish_initial(
        std::span<const mms::MmsDataValue> values,
        mms::Iec61850UtcTime timestamp,
        bool test = false,
        bool needs_commissioning = false);

    [[nodiscard]] GoosePublication publish_state_change(
        std::span<const mms::MmsDataValue> values,
        mms::Iec61850UtcTime timestamp,
        bool test = false,
        bool needs_commissioning = false);

    [[nodiscard]] GoosePublication publish_retransmission();

    [[nodiscard]] bool has_state() const noexcept { return has_state_; }
    [[nodiscard]] std::uint32_t state_number() const noexcept { return state_number_; }
    [[nodiscard]] std::uint32_t next_sequence_number() const noexcept {
        return sequence_number_;
    }

private:
    [[nodiscard]] GoosePublication emit();
    static std::uint32_t increment_state_number(std::uint32_t current) noexcept;
    static std::uint32_t increment_sequence_number(std::uint32_t current) noexcept;

    GooseFrame frame_template_;
    std::vector<mms::MmsDataValue> values_;
    mms::Iec61850UtcTime state_timestamp_{};
    std::uint32_t state_number_{1U};
    std::uint32_t sequence_number_{};
    bool test_{};
    bool needs_commissioning_{};
    bool has_state_{};
};

class GoosePublisherRuntime final {
public:
    using clock = std::chrono::steady_clock;

    GoosePublisherRuntime(
        GoosePublisherSession session,
        std::uint32_t min_time_milliseconds,
        std::uint32_t max_time_milliseconds);

    [[nodiscard]] GoosePublication start(
        std::span<const mms::MmsDataValue> values,
        mms::Iec61850UtcTime timestamp,
        clock::time_point now,
        bool test = false,
        bool needs_commissioning = false);

    [[nodiscard]] GoosePublication state_change(
        std::span<const mms::MmsDataValue> values,
        mms::Iec61850UtcTime timestamp,
        clock::time_point now,
        bool test = false,
        bool needs_commissioning = false);

    [[nodiscard]] std::optional<GoosePublication> poll(clock::time_point now);

    void stop() noexcept;

    [[nodiscard]] bool running() const noexcept { return running_; }
    [[nodiscard]] std::optional<clock::time_point> next_due() const noexcept {
        return next_due_;
    }
    [[nodiscard]] const GoosePublisherSession& session() const noexcept { return session_; }

private:
    void schedule_next(clock::time_point now) noexcept;

    GoosePublisherSession session_;
    RetransmissionSchedule schedule_;
    std::optional<clock::time_point> next_due_;
    bool running_{};
};

} // namespace ar::iec61850::goose
