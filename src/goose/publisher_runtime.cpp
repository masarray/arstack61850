// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/goose/publisher_runtime.hpp"

#include "ariec61850/goose/frame_codec.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace ar::iec61850::goose {

GoosePublisherSession::GoosePublisherSession(
    GooseFrame frame_template,
    const std::uint32_t initial_state_number,
    const std::uint32_t initial_sequence_number)
    : frame_template_{std::move(frame_template)},
      state_number_{initial_state_number == 0U ? 1U : initial_state_number},
      sequence_number_{initial_sequence_number} {}

GoosePublication GoosePublisherSession::publish_initial(
    const std::span<const mms::MmsDataValue> values,
    const mms::Iec61850UtcTime timestamp,
    const bool test,
    const bool needs_commissioning) {
    if (has_state_) {
        throw std::logic_error("GOOSE publisher session has already been started.");
    }
    values_ = {values.begin(), values.end()};
    state_timestamp_ = timestamp;
    test_ = test;
    needs_commissioning_ = needs_commissioning;
    has_state_ = true;
    return emit();
}

GoosePublication GoosePublisherSession::publish_state_change(
    const std::span<const mms::MmsDataValue> values,
    const mms::Iec61850UtcTime timestamp,
    const bool test,
    const bool needs_commissioning) {
    if (!has_state_) {
        return publish_initial(values, timestamp, test, needs_commissioning);
    }
    state_number_ = increment_state_number(state_number_);
    sequence_number_ = 0U;
    values_ = {values.begin(), values.end()};
    state_timestamp_ = timestamp;
    test_ = test;
    needs_commissioning_ = needs_commissioning;
    return emit();
}

GoosePublication GoosePublisherSession::publish_retransmission() {
    if (!has_state_) {
        throw std::logic_error("GOOSE publisher session must be started before retransmission.");
    }
    return emit();
}

GoosePublication GoosePublisherSession::emit() {
    auto frame = frame_template_;
    frame.pdu.values = values_;
    frame.pdu.timestamp = state_timestamp_;
    frame.pdu.state_number = state_number_;
    frame.pdu.sequence_number = sequence_number_;
    frame.pdu.test = test_;
    frame.pdu.needs_commissioning = needs_commissioning_;

    auto bytes = GooseFrameCodec::encode(frame);
    sequence_number_ = increment_sequence_number(sequence_number_);
    return GoosePublication{std::move(frame), std::move(bytes)};
}

std::uint32_t GoosePublisherSession::increment_state_number(
    const std::uint32_t current) noexcept {
    return current == std::numeric_limits<std::uint32_t>::max() ? 1U : current + 1U;
}

std::uint32_t GoosePublisherSession::increment_sequence_number(
    const std::uint32_t current) noexcept {
    return current == std::numeric_limits<std::uint32_t>::max() ? 0U : current + 1U;
}

GoosePublisherRuntime::GoosePublisherRuntime(
    GoosePublisherSession session,
    const std::uint32_t min_time_milliseconds,
    const std::uint32_t max_time_milliseconds)
    : session_{std::move(session)},
      schedule_{min_time_milliseconds, max_time_milliseconds} {}

GoosePublication GoosePublisherRuntime::start(
    const std::span<const mms::MmsDataValue> values,
    const mms::Iec61850UtcTime timestamp,
    const clock::time_point now,
    const bool test,
    const bool needs_commissioning) {
    if (running_) {
        throw std::logic_error("GOOSE publisher runtime is already running.");
    }
    schedule_.reset();
    auto publication = session_.publish_initial(values, timestamp, test, needs_commissioning);
    running_ = true;
    schedule_next(now);
    return publication;
}

GoosePublication GoosePublisherRuntime::state_change(
    const std::span<const mms::MmsDataValue> values,
    const mms::Iec61850UtcTime timestamp,
    const clock::time_point now,
    const bool test,
    const bool needs_commissioning) {
    schedule_.reset();
    auto publication = session_.publish_state_change(
        values, timestamp, test, needs_commissioning);
    running_ = true;
    schedule_next(now);
    return publication;
}

std::optional<GoosePublication> GoosePublisherRuntime::poll(const clock::time_point now) {
    if (!running_ || !next_due_.has_value() || now < *next_due_) {
        return std::nullopt;
    }
    auto publication = session_.publish_retransmission();
    schedule_next(now);
    return publication;
}

void GoosePublisherRuntime::stop() noexcept {
    running_ = false;
    next_due_.reset();
}

void GoosePublisherRuntime::schedule_next(const clock::time_point now) noexcept {
    next_due_ = now + std::chrono::milliseconds{schedule_.next_delay_milliseconds()};
}

} // namespace ar::iec61850::goose
