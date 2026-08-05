// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/goose/publisher_runtime.hpp"
#include "ariec61850/goose/subscriber_supervisor.hpp"
#include "ariec61850/mms/data_value.hpp"
#include "ariec61850/mms/utc_time.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

#define CHECK(condition) do { if (!(condition)) { throw std::runtime_error( \
    std::string{"CHECK failed: "} + #condition + " at " + __FILE__ + ":" + \
    std::to_string(__LINE__)); } } while (false)

using namespace std::chrono_literals;

ar::iec61850::mms::Iec61850UtcTime utc_at(const std::int64_t seconds) {
    return {
        std::chrono::system_clock::time_point{std::chrono::seconds{seconds}},
        0U};
}

std::vector<ar::iec61850::mms::MmsDataValue> values(const bool state) {
    using ar::iec61850::mms::MmsDataValue;
    return {
        MmsDataValue::boolean(state),
        MmsDataValue::unsigned_integer(state ? 2U : 1U)};
}

ar::iec61850::goose::GoosePdu pdu(
    const std::uint32_t state_number,
    const std::uint32_t sequence_number,
    const bool data_state = false,
    const std::uint32_t ttl = 1000U) {
    ar::iec61850::goose::GoosePdu result;
    result.go_cb_ref = "LD0/LLN0$GO$gcb1";
    result.time_allowed_to_live_milliseconds = ttl;
    result.data_set_reference = "LD0/LLN0$DS1";
    result.go_id = "GOOSE1";
    result.timestamp = utc_at(1'781'233'445);
    result.state_number = state_number;
    result.sequence_number = sequence_number;
    result.configuration_revision = 2U;
    result.values = values(data_state);
    return result;
}

ar::iec61850::goose::GooseFrame frame_template() {
    using namespace ar::iec61850;
    goose::GooseFrame frame;
    frame.destination = ethernet::MacAddress::parse("01:0C:CD:01:00:01");
    frame.source = ethernet::MacAddress::parse("02:00:00:00:00:01");
    frame.vlan = ethernet::VlanTag{4U, 100U};
    frame.app_id = 0x1001U;
    frame.pdu = pdu(1U, 0U);
    frame.pdu.values.clear();
    return frame;
}

void subscriber_classifies_sequence_and_state_transitions() {
    using namespace ar::iec61850::goose;
    GooseSubscriberSupervisor supervisor;
    const auto t0 = GooseSubscriberSupervisor::clock::time_point{};

    const auto first = supervisor.observe(pdu(1U, 0U), t0);
    CHECK(first.status == GooseSequenceStatus::first);
    CHECK(first.accepted);
    CHECK(first.expires_at == t0 + 1000ms);

    const auto retransmission = supervisor.observe(pdu(1U, 1U), t0 + 4ms);
    CHECK(retransmission.status == GooseSequenceStatus::retransmission);
    CHECK(retransmission.arrival_gap == 4ms);

    const auto duplicate = supervisor.observe(pdu(1U, 1U), t0 + 5ms);
    CHECK(duplicate.status == GooseSequenceStatus::duplicate);

    const auto gap = supervisor.observe(pdu(1U, 4U), t0 + 6ms);
    CHECK(gap.status == GooseSequenceStatus::sequence_gap);
    CHECK(gap.missed_sequence_count == 2U);

    const auto regression = supervisor.observe(pdu(1U, 2U), t0 + 7ms);
    CHECK(regression.status == GooseSequenceStatus::sequence_regression);

    const auto changed = supervisor.observe(pdu(2U, 0U, true), t0 + 8ms);
    CHECK(changed.status == GooseSequenceStatus::state_change);
    CHECK(changed.value_changed);
    CHECK(!changed.value_changed_without_state_increment);

    const auto illegal_value_change = supervisor.observe(pdu(2U, 1U, false), t0 + 9ms);
    CHECK(illegal_value_change.status == GooseSequenceStatus::retransmission);
    CHECK(illegal_value_change.value_changed_without_state_increment);

    const auto state_jump = supervisor.observe(pdu(4U, 0U), t0 + 10ms);
    CHECK(state_jump.status == GooseSequenceStatus::state_jump);

    const auto state_regression = supervisor.observe(pdu(3U, 0U), t0 + 11ms);
    CHECK(state_regression.status == GooseSequenceStatus::state_regression);

    const auto& statistics = supervisor.statistics();
    CHECK(statistics.accepted_count == 9U);
    CHECK(statistics.retransmission_count == 2U);
    CHECK(statistics.duplicate_count == 1U);
    CHECK(statistics.sequence_gap_count == 1U);
    CHECK(statistics.sequence_regression_count == 1U);
    CHECK(statistics.state_change_count == 1U);
    CHECK(statistics.state_jump_count == 1U);
    CHECK(statistics.state_regression_count == 1U);
    CHECK(statistics.value_change_without_state_increment_count == 1U);
}

void subscriber_supervises_ttl_and_identity() {
    using namespace ar::iec61850::goose;
    GooseSubscriberOptions options;
    options.expected_go_cb_ref = "LD0/LLN0$GO$gcb1";
    options.expected_data_set_reference = "LD0/LLN0$DS1";
    options.expected_configuration_revision = 2U;
    GooseSubscriberSupervisor supervisor{options};
    const auto t0 = GooseSubscriberSupervisor::clock::time_point{};

    CHECK(supervisor.observe(pdu(1U, 0U, false, 10U), t0).accepted);
    CHECK(!supervisor.check_expiry(t0 + 9ms).has_value());
    const auto expiry = supervisor.check_expiry(t0 + 10ms);
    CHECK(expiry.has_value());
    CHECK(expiry->state_number == 1U);
    CHECK(expiry->sequence_number == 0U);
    CHECK(expiry->time_allowed_to_live_milliseconds == 10U);
    CHECK(!supervisor.check_expiry(t0 + 11ms).has_value());

    const auto late = supervisor.observe(pdu(1U, 1U, false, 10U), t0 + 12ms);
    CHECK(late.expired_before_arrival);

    auto wrong = pdu(1U, 2U);
    wrong.go_cb_ref = "OTHER/LLN0$GO$gcb1";
    const auto rejected = supervisor.observe(wrong, t0 + 13ms);
    CHECK(!rejected.accepted);
    CHECK(rejected.status == GooseSequenceStatus::identity_mismatch);
    CHECK(supervisor.statistics().expiration_count == 1U);
    CHECK(supervisor.statistics().rejected_identity_count == 1U);
}

void subscriber_handles_counter_wraparound() {
    using namespace ar::iec61850::goose;
    GooseSubscriberSupervisor sequence_supervisor;
    const auto t0 = GooseSubscriberSupervisor::clock::time_point{};
    static_cast<void>(sequence_supervisor.observe(
        pdu(7U, std::numeric_limits<std::uint32_t>::max()), t0));
    const auto sequence_wrap = sequence_supervisor.observe(pdu(7U, 0U), t0 + 1ms);
    CHECK(sequence_wrap.status == GooseSequenceStatus::retransmission);

    GooseSubscriberSupervisor state_supervisor;
    static_cast<void>(state_supervisor.observe(
        pdu(std::numeric_limits<std::uint32_t>::max(), 8U), t0));
    const auto state_wrap = state_supervisor.observe(pdu(1U, 0U), t0 + 1ms);
    CHECK(state_wrap.status == GooseSequenceStatus::state_change);
}

void publisher_session_matches_csharp_sequence_semantics() {
    using namespace ar::iec61850::goose;
    GoosePublisherSession session{frame_template()};
    const auto initial_values = values(false);
    const auto changed_values = values(true);
    const auto first_timestamp = utc_at(1'781'233'445);
    const auto changed_timestamp = utc_at(1'781'233'446);

    const auto first = session.publish_initial(initial_values, first_timestamp);
    const auto second = session.publish_retransmission();
    const auto changed = session.publish_state_change(changed_values, changed_timestamp);

    CHECK(first.frame.pdu.state_number == 1U);
    CHECK(first.frame.pdu.sequence_number == 0U);
    CHECK(second.frame.pdu.state_number == 1U);
    CHECK(second.frame.pdu.sequence_number == 1U);
    CHECK(second.frame.pdu.timestamp == first_timestamp);
    CHECK(changed.frame.pdu.state_number == 2U);
    CHECK(changed.frame.pdu.sequence_number == 0U);
    CHECK(changed.frame.pdu.timestamp == changed_timestamp);
    CHECK(session.state_number() == 2U);
    CHECK(session.next_sequence_number() == 1U);
    CHECK(!first.ethernet_bytes.empty());
}

void publisher_runtime_applies_retransmission_schedule() {
    using namespace ar::iec61850::goose;
    GoosePublisherRuntime runtime{
        GoosePublisherSession{frame_template()}, 4U, 16U};
    const auto t0 = GoosePublisherRuntime::clock::time_point{};
    const auto first_timestamp = utc_at(1'781'233'445);
    const auto changed_timestamp = utc_at(1'781'233'450);

    const auto first = runtime.start(values(false), first_timestamp, t0);
    CHECK(first.frame.pdu.sequence_number == 0U);
    CHECK(runtime.next_due() == t0 + 4ms);
    CHECK(!runtime.poll(t0 + 3ms).has_value());

    const auto retransmission1 = runtime.poll(t0 + 4ms);
    CHECK(retransmission1.has_value());
    CHECK(retransmission1->frame.pdu.sequence_number == 1U);
    CHECK(retransmission1->frame.pdu.timestamp == first_timestamp);
    CHECK(runtime.next_due() == t0 + 12ms);

    const auto changed = runtime.state_change(
        values(true), changed_timestamp, t0 + 5ms, true, true);
    CHECK(changed.frame.pdu.state_number == 2U);
    CHECK(changed.frame.pdu.sequence_number == 0U);
    CHECK(changed.frame.pdu.test);
    CHECK(changed.frame.pdu.needs_commissioning);
    CHECK(runtime.next_due() == t0 + 9ms);

    const auto retransmission2 = runtime.poll(t0 + 9ms);
    CHECK(retransmission2.has_value());
    CHECK(retransmission2->frame.pdu.state_number == 2U);
    CHECK(retransmission2->frame.pdu.sequence_number == 1U);
    CHECK(retransmission2->frame.pdu.timestamp == changed_timestamp);
    CHECK(runtime.next_due() == t0 + 17ms);

    runtime.stop();
    CHECK(!runtime.running());
    CHECK(!runtime.next_due().has_value());
    CHECK(!runtime.poll(t0 + 100ms).has_value());
}

void publisher_wraps_state_and_sequence_counters() {
    using namespace ar::iec61850::goose;
    GoosePublisherSession sequence_session{
        frame_template(), 5U, std::numeric_limits<std::uint32_t>::max()};
    const auto first = sequence_session.publish_initial(values(false), utc_at(1));
    const auto wrapped_sequence = sequence_session.publish_retransmission();
    CHECK(first.frame.pdu.sequence_number == std::numeric_limits<std::uint32_t>::max());
    CHECK(wrapped_sequence.frame.pdu.sequence_number == 0U);

    GoosePublisherSession state_session{
        frame_template(), std::numeric_limits<std::uint32_t>::max(), 0U};
    static_cast<void>(state_session.publish_initial(values(false), utc_at(1)));
    const auto wrapped_state = state_session.publish_state_change(values(true), utc_at(2));
    CHECK(wrapped_state.frame.pdu.state_number == 1U);
    CHECK(wrapped_state.frame.pdu.sequence_number == 0U);
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"GOOSE subscriber sequence supervision", subscriber_classifies_sequence_and_state_transitions},
        {"GOOSE subscriber TTL and identity", subscriber_supervises_ttl_and_identity},
        {"GOOSE subscriber counter wrap", subscriber_handles_counter_wraparound},
        {"GOOSE publisher session", publisher_session_matches_csharp_sequence_semantics},
        {"GOOSE publisher runtime schedule", publisher_runtime_applies_retransmission_schedule},
        {"GOOSE publisher counter wrap", publisher_wraps_state_and_sequence_counters}};

    std::size_t passed = 0U;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << "Passed " << passed << "/" << tests.size()
              << " GOOSE runtime tests.\n";
    return 0;
}
