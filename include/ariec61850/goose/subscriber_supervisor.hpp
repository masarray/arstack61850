// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/goose/pdu.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ar::iec61850::goose {

enum class GooseSequenceStatus : std::uint8_t {
    first,
    retransmission,
    state_change,
    duplicate,
    sequence_gap,
    sequence_regression,
    state_jump,
    state_regression,
    identity_mismatch
};

struct GooseSubscriberOptions final {
    std::optional<std::string> expected_go_cb_ref;
    std::optional<std::string> expected_data_set_reference;
    std::optional<std::uint32_t> expected_configuration_revision;
};

struct GooseSupervisionResult final {
    GooseSequenceStatus status{GooseSequenceStatus::first};
    bool accepted{true};
    bool expired_before_arrival{};
    bool value_changed{};
    bool value_changed_without_state_increment{};
    bool state_change_sequence_not_zero{};
    bool configuration_revision_changed{};
    std::uint32_t missed_sequence_count{};
    std::chrono::milliseconds arrival_gap{};
    std::optional<std::chrono::steady_clock::time_point> expires_at;
};

struct GooseExpiryEvent final {
    std::chrono::steady_clock::time_point deadline{};
    std::uint32_t state_number{};
    std::uint32_t sequence_number{};
    std::uint32_t time_allowed_to_live_milliseconds{};
};

struct GooseSubscriberStatistics final {
    std::uint64_t accepted_count{};
    std::uint64_t rejected_identity_count{};
    std::uint64_t retransmission_count{};
    std::uint64_t state_change_count{};
    std::uint64_t duplicate_count{};
    std::uint64_t sequence_gap_count{};
    std::uint64_t sequence_regression_count{};
    std::uint64_t state_jump_count{};
    std::uint64_t state_regression_count{};
    std::uint64_t expiration_count{};
    std::uint64_t value_change_without_state_increment_count{};
};

class GooseSubscriberSupervisor final {
public:
    using clock = std::chrono::steady_clock;

    explicit GooseSubscriberSupervisor(GooseSubscriberOptions options = {});

    [[nodiscard]] GooseSupervisionResult observe(
        const GoosePdu& pdu,
        clock::time_point arrival_time);

    [[nodiscard]] std::optional<GooseExpiryEvent> check_expiry(clock::time_point now);

    void reset() noexcept;

    [[nodiscard]] const GooseSubscriberStatistics& statistics() const noexcept {
        return statistics_;
    }
    [[nodiscard]] std::optional<clock::time_point> expires_at() const noexcept {
        return expires_at_;
    }
    [[nodiscard]] std::optional<std::uint32_t> last_state_number() const noexcept {
        return last_state_number_;
    }
    [[nodiscard]] std::optional<std::uint32_t> last_sequence_number() const noexcept {
        return last_sequence_number_;
    }

private:
    [[nodiscard]] bool identity_matches(const GoosePdu& pdu) const noexcept;
    [[nodiscard]] GooseSequenceStatus classify(
        std::uint32_t state_number,
        std::uint32_t sequence_number,
        std::uint32_t& missed_sequence_count) const noexcept;
    [[nodiscard]] bool record_expiry_if_due(clock::time_point now);
    void update_statistics(GooseSequenceStatus status) noexcept;

    GooseSubscriberOptions options_;
    GooseSubscriberStatistics statistics_;
    std::optional<clock::time_point> last_arrival_time_;
    std::optional<clock::time_point> expires_at_;
    bool expiry_reported_{};
    std::optional<std::uint32_t> last_state_number_;
    std::optional<std::uint32_t> last_sequence_number_;
    std::optional<std::uint32_t> last_configuration_revision_;
    std::vector<std::uint8_t> last_values_fingerprint_;
    std::uint32_t last_time_allowed_to_live_milliseconds_{};
};

} // namespace ar::iec61850::goose
