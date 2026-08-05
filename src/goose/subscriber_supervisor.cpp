// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/goose/subscriber_supervisor.hpp"

#include "ariec61850/mms/data_codec.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace ar::iec61850::goose {
namespace {

constexpr std::uint32_t serial_half_range = 0x80000000U;

std::uint32_t next_state_number(const std::uint32_t value) noexcept {
    return value == std::numeric_limits<std::uint32_t>::max() ? 1U : value + 1U;
}

} // namespace

GooseSubscriberSupervisor::GooseSubscriberSupervisor(GooseSubscriberOptions options)
    : options_{std::move(options)} {}

GooseSupervisionResult GooseSubscriberSupervisor::observe(
    const GoosePdu& pdu,
    const clock::time_point arrival_time) {
    if (last_arrival_time_.has_value() && arrival_time < *last_arrival_time_) {
        throw std::invalid_argument("GOOSE arrival time cannot move backwards.");
    }

    GooseSupervisionResult result;
    if (!identity_matches(pdu)) {
        result.status = GooseSequenceStatus::identity_mismatch;
        result.accepted = false;
        statistics_.rejected_identity_count++;
        return result;
    }

    result.expired_before_arrival = expires_at_.has_value() && arrival_time >= *expires_at_;
    static_cast<void>(record_expiry_if_due(arrival_time));
    if (last_arrival_time_.has_value()) {
        result.arrival_gap = std::chrono::duration_cast<std::chrono::milliseconds>(
            arrival_time - *last_arrival_time_);
    }

    std::uint32_t missed_sequence_count = 0U;
    result.status = classify(pdu.state_number, pdu.sequence_number, missed_sequence_count);
    result.missed_sequence_count = missed_sequence_count;
    result.state_change_sequence_not_zero =
        (result.status == GooseSequenceStatus::state_change ||
         result.status == GooseSequenceStatus::state_jump) &&
        pdu.sequence_number != 0U;

    const auto fingerprint = mms::MmsDataCodec::encode_all(pdu.values);
    result.value_changed = last_state_number_.has_value() &&
                           fingerprint != last_values_fingerprint_;
    result.value_changed_without_state_increment =
        result.value_changed && last_state_number_.has_value() &&
        pdu.state_number == *last_state_number_;
    if (result.value_changed_without_state_increment) {
        statistics_.value_change_without_state_increment_count++;
    }

    result.configuration_revision_changed =
        last_configuration_revision_.has_value() &&
        pdu.configuration_revision != *last_configuration_revision_;

    update_statistics(result.status);
    statistics_.accepted_count++;

    last_arrival_time_ = arrival_time;
    last_state_number_ = pdu.state_number;
    last_sequence_number_ = pdu.sequence_number;
    last_configuration_revision_ = pdu.configuration_revision;
    last_values_fingerprint_ = fingerprint;
    last_time_allowed_to_live_milliseconds_ = pdu.time_allowed_to_live_milliseconds;
    expiry_reported_ = false;

    if (pdu.time_allowed_to_live_milliseconds == 0U) {
        expires_at_.reset();
    } else {
        expires_at_ = arrival_time +
            std::chrono::milliseconds{pdu.time_allowed_to_live_milliseconds};
    }
    result.expires_at = expires_at_;
    return result;
}

std::optional<GooseExpiryEvent> GooseSubscriberSupervisor::check_expiry(
    const clock::time_point now) {
    if (!record_expiry_if_due(now)) {
        return std::nullopt;
    }
    return GooseExpiryEvent{
        *expires_at_,
        last_state_number_.value_or(0U),
        last_sequence_number_.value_or(0U),
        last_time_allowed_to_live_milliseconds_};
}

void GooseSubscriberSupervisor::reset() noexcept {
    statistics_ = {};
    last_arrival_time_.reset();
    expires_at_.reset();
    expiry_reported_ = false;
    last_state_number_.reset();
    last_sequence_number_.reset();
    last_configuration_revision_.reset();
    last_values_fingerprint_.clear();
    last_time_allowed_to_live_milliseconds_ = 0U;
}

bool GooseSubscriberSupervisor::identity_matches(const GoosePdu& pdu) const noexcept {
    if (options_.expected_go_cb_ref.has_value() &&
        pdu.go_cb_ref != *options_.expected_go_cb_ref) {
        return false;
    }
    if (options_.expected_data_set_reference.has_value() &&
        pdu.data_set_reference != *options_.expected_data_set_reference) {
        return false;
    }
    return !options_.expected_configuration_revision.has_value() ||
           pdu.configuration_revision == *options_.expected_configuration_revision;
}

GooseSequenceStatus GooseSubscriberSupervisor::classify(
    const std::uint32_t state_number,
    const std::uint32_t sequence_number,
    std::uint32_t& missed_sequence_count) const noexcept {
    if (!last_state_number_.has_value() || !last_sequence_number_.has_value()) {
        return GooseSequenceStatus::first;
    }

    const auto previous_state = *last_state_number_;
    const auto previous_sequence = *last_sequence_number_;

    if (state_number != previous_state) {
        if (state_number == next_state_number(previous_state)) {
            return GooseSequenceStatus::state_change;
        }
        if ((previous_state == std::numeric_limits<std::uint32_t>::max() && state_number > 1U) ||
            (previous_state != std::numeric_limits<std::uint32_t>::max() &&
             state_number > previous_state)) {
            return GooseSequenceStatus::state_jump;
        }
        return GooseSequenceStatus::state_regression;
    }

    const auto delta = sequence_number - previous_sequence;
    if (delta == 0U) {
        return GooseSequenceStatus::duplicate;
    }
    if (delta < serial_half_range) {
        if (delta == 1U) {
            return GooseSequenceStatus::retransmission;
        }
        missed_sequence_count = delta - 1U;
        return GooseSequenceStatus::sequence_gap;
    }
    return GooseSequenceStatus::sequence_regression;
}

bool GooseSubscriberSupervisor::record_expiry_if_due(const clock::time_point now) {
    if (!expires_at_.has_value() || expiry_reported_ || now < *expires_at_) {
        return false;
    }
    expiry_reported_ = true;
    statistics_.expiration_count++;
    return true;
}

void GooseSubscriberSupervisor::update_statistics(const GooseSequenceStatus status) noexcept {
    switch (status) {
    case GooseSequenceStatus::retransmission:
        statistics_.retransmission_count++;
        break;
    case GooseSequenceStatus::state_change:
        statistics_.state_change_count++;
        break;
    case GooseSequenceStatus::duplicate:
        statistics_.duplicate_count++;
        break;
    case GooseSequenceStatus::sequence_gap:
        statistics_.sequence_gap_count++;
        break;
    case GooseSequenceStatus::sequence_regression:
        statistics_.sequence_regression_count++;
        break;
    case GooseSequenceStatus::state_jump:
        statistics_.state_jump_count++;
        break;
    case GooseSequenceStatus::state_regression:
        statistics_.state_regression_count++;
        break;
    default:
        break;
    }
}

} // namespace ar::iec61850::goose
