// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/time_sync/ptp_runtime.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

#define CHECK(condition) do { if (!(condition)) { throw std::runtime_error( \
    std::string{"CHECK failed: "} + #condition + " at " + __FILE__ + ":" + \
    std::to_string(__LINE__)); } } while (false)

ar::iec61850::time_sync::PtpPublisherOptions make_options() {
    using namespace ar::iec61850::time_sync;
    PtpPublisherOptions options;
    options.transport_specific = 1U;
    options.domain_number = 0U;
    options.source_mac = {0x02U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U};
    options.clock_identity = ptp_clock_identity_from_mac(options.source_mac);
    options.vlan_id = std::uint16_t{100U};
    options.vlan_priority = 4U;
    options.announce_interval = std::chrono::milliseconds{1000};
    options.sync_interval = std::chrono::milliseconds{250};
    return options;
}

void clock_identity_helpers_match_ariec61850_behavior() {
    using namespace ar::iec61850::time_sync;
    PtpClockIdentity identity{};
    CHECK(try_parse_ptp_clock_identity("02:00:00:FF:FE:00:00:01", identity));
    CHECK(format_ptp_clock_identity(identity) == "02:00:00:FF:FE:00:00:01");

    PtpClockIdentity dashed{};
    CHECK(try_parse_ptp_clock_identity(" 02-00-00-FF-FE-00-00-01 ", dashed));
    CHECK(dashed == identity);
    CHECK(!try_parse_ptp_clock_identity("02:00:00:FF:FE:00:00", dashed));
    CHECK(!try_parse_ptp_clock_identity("02:00:00:FF:FE:00:00:GG", dashed));

    const std::array<std::uint8_t, 6> mac{0x02U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U};
    CHECK(format_ptp_clock_identity(ptp_clock_identity_from_mac(mac)) ==
          "02:11:22:FF:FE:33:44:55");
}

void options_are_fail_safe_and_reconfiguration_requires_stop() {
    using namespace ar::iec61850::time_sync;
    auto options = make_options();
    std::string error;
    CHECK(PtpPublisherRuntime::validate_options(options, error));
    CHECK(error.empty());

    auto invalid = options;
    invalid.transport_specific = 16U;
    CHECK(!PtpPublisherRuntime::validate_options(invalid, error));
    CHECK(!error.empty());

    invalid = options;
    invalid.vlan_id = std::uint16_t{4095U};
    CHECK(!PtpPublisherRuntime::validate_options(invalid, error));

    invalid = options;
    invalid.port_number = 0U;
    CHECK(!PtpPublisherRuntime::validate_options(invalid, error));

    PtpPublisherRuntime runtime{options};
    const auto wall = std::chrono::system_clock::time_point{std::chrono::seconds{100}};
    const auto mono = std::chrono::steady_clock::time_point{std::chrono::seconds{10}};
    CHECK(runtime.start(wall, mono));
    auto changed = options;
    changed.domain_number = 1U;
    CHECK(!runtime.reconfigure(changed, error));
    runtime.stop();
    CHECK(runtime.reconfigure(changed, error));
    CHECK(runtime.options().domain_number == 1U);
}

void scheduler_sequence_and_status_match_runtime_contract() {
    using namespace ar::iec61850::time_sync;
    PtpPublisherRuntime runtime{make_options()};
    const auto wall = std::chrono::system_clock::time_point{std::chrono::seconds{1000}};
    const auto mono = std::chrono::steady_clock::time_point{std::chrono::seconds{50}};
    CHECK(runtime.start(wall, mono));

    auto due = runtime.poll_due(mono);
    CHECK(due.announce);
    CHECK(due.sync);
    due = runtime.poll_due(mono + std::chrono::milliseconds{100});
    CHECK(!due.announce);
    CHECK(!due.sync);
    due = runtime.poll_due(mono + std::chrono::milliseconds{250});
    CHECK(!due.announce);
    CHECK(due.sync);
    due = runtime.poll_due(mono + std::chrono::milliseconds{1000});
    CHECK(due.announce);
    CHECK(due.sync);

    const auto announce = runtime.prepare_announce(PtpTimestamp{10U, 20U});
    CHECK(announce.has_value());
    CHECK(announce->sequence_id == std::uint16_t{1U});
    PtpFrame parsed;
    CHECK(PtpCodec::try_parse_ethernet_frame(announce->ethernet_frame, parsed));
    CHECK(parsed.header.message_type == PtpMessageType::announce);
    CHECK(parsed.header.transport_specific == std::uint8_t{1U});
    CHECK(parsed.vlan_id == std::optional<std::uint16_t>{100U});

    const auto sync = runtime.prepare_sync(PtpTimestamp{});
    CHECK(sync.has_value());
    CHECK(sync->sequence_id == std::uint16_t{1U});
    CHECK(PtpCodec::try_parse_ethernet_frame(sync->ethernet_frame, parsed));
    CHECK(parsed.header.message_type == PtpMessageType::sync);
    CHECK(parsed.header.is_two_step());
    CHECK(parsed.timestamp.has_value());
    CHECK(*parsed.timestamp == PtpTimestamp{});

    const PtpTimestamp tx_timestamp{11U, 123456U};
    const auto follow_up = runtime.prepare_follow_up(sync->sequence_id, tx_timestamp);
    CHECK(follow_up.has_value());
    CHECK(follow_up->sequence_id == sync->sequence_id);
    CHECK(PtpCodec::try_parse_ethernet_frame(follow_up->ethernet_frame, parsed));
    CHECK(parsed.header.message_type == PtpMessageType::follow_up);
    CHECK(!parsed.header.is_two_step());
    CHECK(parsed.timestamp == std::optional<PtpTimestamp>{tx_timestamp});

    runtime.record_sent(PtpMessageType::announce, wall + std::chrono::seconds{1});
    runtime.record_sent(PtpMessageType::sync, wall + std::chrono::seconds{2});
    runtime.record_sent(PtpMessageType::follow_up, wall + std::chrono::seconds{2});
    const auto status = runtime.status();
    CHECK(status.is_running);
    CHECK(status.started_at == std::optional<std::chrono::system_clock::time_point>{wall});
    CHECK(status.announce_sent == 1U);
    CHECK(status.sync_sent == 1U);
    CHECK(status.follow_up_sent == 1U);
    CHECK(status.peer_delay_responses_sent == 0U);
    CHECK(status.last_sent_at.has_value());
}

void peer_delay_runtime_preserves_request_identity_and_hw_timestamps() {
    using namespace ar::iec61850::time_sync;
    PtpPublisherRuntime runtime{make_options()};
    CHECK(runtime.start());

    const PtpPortIdentity requester{
        {0x10U, 0x20U, 0x30U, 0x40U, 0x50U, 0x60U, 0x70U, 0x80U},
        2U,
    };
    const PtpTimestamp t2{100U, 222333444U};
    const PtpTimestamp t3{100U, 333444555U};

    const auto response = runtime.prepare_pdelay_response(requester, 0x55U, -3, t2);
    CHECK(response.has_value());
    PtpFrame parsed;
    CHECK(PtpCodec::try_parse_ethernet_frame(response->ethernet_frame, parsed));
    CHECK(parsed.peer_delay_multicast);
    CHECK(parsed.header.message_type == PtpMessageType::pdelay_resp);
    CHECK(parsed.header.sequence_id == std::uint16_t{0x55U});
    CHECK(parsed.header.log_message_interval == -3);
    PtpTimestamp parsed_t2;
    CHECK(PtpTimestamp::try_read(std::span<const std::uint8_t>{parsed.body}.first(10U), parsed_t2));
    CHECK(parsed_t2 == t2);

    const auto follow_up = runtime.prepare_pdelay_response_follow_up(requester, 0x55U, -3, t3);
    CHECK(follow_up.has_value());
    CHECK(PtpCodec::try_parse_ethernet_frame(follow_up->ethernet_frame, parsed));
    CHECK(parsed.header.message_type == PtpMessageType::pdelay_resp_follow_up);
    PtpTimestamp parsed_t3;
    CHECK(PtpTimestamp::try_read(std::span<const std::uint8_t>{parsed.body}.first(10U), parsed_t3));
    CHECK(parsed_t3 == t3);

    runtime.record_sent(PtpMessageType::pdelay_resp);
    runtime.record_sent(PtpMessageType::pdelay_resp_follow_up);
    CHECK(runtime.status().peer_delay_responses_sent == 2U);
}

void one_step_policy_suppresses_follow_up() {
    using namespace ar::iec61850::time_sync;
    auto options = make_options();
    options.two_step_clock = false;
    PtpPublisherRuntime runtime{options};
    CHECK(runtime.start());
    const auto sync = runtime.prepare_sync(PtpTimestamp{1U, 2U});
    CHECK(sync.has_value());
    PtpFrame parsed;
    CHECK(PtpCodec::try_parse_ethernet_frame(sync->ethernet_frame, parsed));
    CHECK(!parsed.header.is_two_step());
    CHECK(!runtime.prepare_follow_up(sync->sequence_id, PtpTimestamp{1U, 3U}).has_value());
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"PTP ClockIdentity helpers", clock_identity_helpers_match_ariec61850_behavior},
        {"PTP runtime option validation", options_are_fail_safe_and_reconfiguration_requires_stop},
        {"PTP runtime scheduler/status", scheduler_sequence_and_status_match_runtime_contract},
        {"PTP runtime hardware Pdelay preparation", peer_delay_runtime_preserves_request_identity_and_hw_timestamps},
        {"PTP runtime one-step policy", one_step_policy_suppresses_follow_up},
    };

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
    std::cout << "Passed " << passed << "/" << tests.size() << " PTP runtime tests.\n";
    return 0;
}
