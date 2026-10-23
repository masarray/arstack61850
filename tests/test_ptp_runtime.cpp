// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/time_sync/ptp_runtime.hpp"
#include "ariec61850/time_sync/ptp_source_policy.hpp"
#include "ariec61850/time_sync/ptp_external_candidate_policy.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <optional>
#include <span>
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
    options.transport_specific = std::uint8_t{1U};
    options.domain_number = std::uint8_t{0U};
    options.source_mac = {0x02U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U};
    options.clock_identity = ptp_clock_identity_from_mac(options.source_mac);
    options.vlan_id = std::uint16_t{100U};
    options.vlan_priority = std::uint8_t{4U};
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
    invalid.transport_specific = std::uint8_t{16U};
    CHECK(!PtpPublisherRuntime::validate_options(invalid, error));
    CHECK(!error.empty());

    invalid = options;
    invalid.vlan_id = std::uint16_t{4095U};
    CHECK(!PtpPublisherRuntime::validate_options(invalid, error));

    invalid = options;
    invalid.port_number = std::uint16_t{0U};
    CHECK(!PtpPublisherRuntime::validate_options(invalid, error));

    PtpPublisherRuntime runtime{options};
    const auto wall = std::chrono::system_clock::time_point{std::chrono::seconds{100}};
    const auto mono = std::chrono::steady_clock::time_point{std::chrono::seconds{10}};
    CHECK(runtime.start(wall, mono));
    auto changed = options;
    changed.domain_number = std::uint8_t{1U};
    CHECK(!runtime.reconfigure(changed, error));
    runtime.stop();
    CHECK(runtime.reconfigure(changed, error));
    CHECK(runtime.options().domain_number == std::uint8_t{1U});
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

    const auto announce = runtime.prepare_announce(
        PtpTimestamp{std::uint64_t{10U}, std::uint32_t{20U}});
    CHECK(announce.has_value());
    CHECK(announce->sequence_id == std::uint16_t{1U});
    PtpFrame parsed;
    CHECK(PtpCodec::try_parse_ethernet_frame(announce->ethernet_frame, parsed));
    CHECK(parsed.header.message_type == PtpMessageType::announce);
    CHECK(parsed.header.transport_specific == std::uint8_t{1U});
    CHECK(parsed.vlan_id == std::optional<std::uint16_t>{std::uint16_t{100U}});
    // M3.C actual encoded Announce (not synthetic struct) must pass the
    // precise SOURCE passive-watch domain, transport, VLAN and PCP gate.
    const PtpForeignWireProfile profile{0U, 1U, true, 100U, 4U};
    CHECK(profile.matches(parsed, announce->ethernet_frame));
    CHECK((!PtpForeignWireProfile{1U,1U,true,100U,4U}.matches(
        parsed, announce->ethernet_frame)));
    CHECK((!PtpForeignWireProfile{0U,0U,true,100U,4U}.matches(
        parsed, announce->ethernet_frame)));
    CHECK((!PtpForeignWireProfile{0U,1U,true,101U,4U}.matches(
        parsed, announce->ethernet_frame)));
    CHECK((!PtpForeignWireProfile{0U,1U,true,100U,5U}.matches(
        parsed, announce->ethernet_frame)));
    CHECK((!PtpForeignWireProfile{0U,1U,false,0U,0U}.matches(
        parsed, announce->ethernet_frame)));
    auto corrupted_pcp = announce->ethernet_frame;
    CHECK(corrupted_pcp.size() > 16U);
    corrupted_pcp[14] = static_cast<std::uint8_t>(
        (corrupted_pcp[14] & 0x1FU) | 0xA0U); // PCP 5
    CHECK(!profile.matches(parsed, corrupted_pcp));
    const auto& foreign_announce = *parsed.announce;
    PtpAnnounceRank foreign{
        foreign_announce.priority1, foreign_announce.clock_class,
        static_cast<std::uint8_t>(foreign_announce.clock_accuracy),
        foreign_announce.offset_scaled_log_variance, foreign_announce.priority2,
        foreign_announce.grandmaster_identity, foreign_announce.steps_removed,
        parsed.header.source_port_identity.clock_identity,
        parsed.header.source_port_identity.port_number,
    };
    auto local_rank = foreign;
    local_rank.priority1 = 200U;
    local_rank.gm_identity = {0x66U,0,0,0,0,0,0,1U};
    PtpExternalCandidateGate foreign_gate;
    for (std::uint64_t t=1000U; t<=5000U; t+=1000U)
        foreign_gate.observe(foreign, local_rank, t);
    CHECK(foreign_gate.stable(5000U));

    const auto sync = runtime.prepare_sync(PtpTimestamp{});
    CHECK(sync.has_value());
    CHECK(sync->sequence_id == std::uint16_t{1U});
    CHECK(PtpCodec::try_parse_ethernet_frame(sync->ethernet_frame, parsed));
    CHECK(parsed.header.message_type == PtpMessageType::sync);
    CHECK(parsed.header.is_two_step());
    CHECK(parsed.timestamp.has_value());
    CHECK(*parsed.timestamp == PtpTimestamp{});

    const PtpTimestamp tx_timestamp{std::uint64_t{11U}, std::uint32_t{123456U}};
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
    CHECK(status.announce_sent == std::uint64_t{1U});
    CHECK(status.sync_sent == std::uint64_t{1U});
    CHECK(status.follow_up_sent == std::uint64_t{1U});
    CHECK(status.peer_delay_responses_sent == std::uint64_t{0U});
    CHECK(status.last_sent_at.has_value());
}

void peer_delay_runtime_preserves_request_identity_and_hw_timestamps() {
    using namespace ar::iec61850::time_sync;
    PtpPublisherRuntime runtime{make_options()};
    CHECK(runtime.start());

    const PtpPortIdentity requester{
        {0x10U, 0x20U, 0x30U, 0x40U, 0x50U, 0x60U, 0x70U, 0x80U},
        std::uint16_t{2U},
    };
    const PtpTimestamp t2{std::uint64_t{100U}, std::uint32_t{222333444U}};
    const PtpTimestamp t3{std::uint64_t{100U}, std::uint32_t{333444555U}};
    constexpr std::uint16_t request_sequence = 0x55U;
    constexpr std::int8_t request_log_interval = -3;

    const auto response = runtime.prepare_pdelay_response(
        requester,
        request_sequence,
        request_log_interval,
        t2);
    CHECK(response.has_value());
    PtpFrame parsed;
    CHECK(PtpCodec::try_parse_ethernet_frame(response->ethernet_frame, parsed));
    CHECK(parsed.peer_delay_multicast);
    CHECK(parsed.header.message_type == PtpMessageType::pdelay_resp);
    CHECK(parsed.header.sequence_id == request_sequence);
    CHECK(parsed.header.log_message_interval == request_log_interval);
    PtpTimestamp parsed_t2;
    const auto response_body = std::span<const std::uint8_t>{parsed.body.data(), parsed.body.size()};
    CHECK(response_body.size() >= 10U);
    CHECK(PtpTimestamp::try_read(response_body.first(10U), parsed_t2));
    CHECK(parsed_t2 == t2);

    const auto follow_up = runtime.prepare_pdelay_response_follow_up(
        requester,
        request_sequence,
        request_log_interval,
        t3);
    CHECK(follow_up.has_value());
    CHECK(PtpCodec::try_parse_ethernet_frame(follow_up->ethernet_frame, parsed));
    CHECK(parsed.header.message_type == PtpMessageType::pdelay_resp_follow_up);
    PtpTimestamp parsed_t3;
    const auto follow_up_body = std::span<const std::uint8_t>{parsed.body.data(), parsed.body.size()};
    CHECK(follow_up_body.size() >= 10U);
    CHECK(PtpTimestamp::try_read(follow_up_body.first(10U), parsed_t3));
    CHECK(parsed_t3 == t3);

    runtime.record_sent(PtpMessageType::pdelay_resp);
    runtime.record_sent(PtpMessageType::pdelay_resp_follow_up);
    CHECK(runtime.status().peer_delay_responses_sent == std::uint64_t{2U});
}

void fixed_deadline_cadence_survives_scheduler_latency() {
    using namespace ar::iec61850::time_sync;
    auto opts = make_options();
    opts.announce_interval = std::chrono::milliseconds{1000};
    opts.sync_interval = std::chrono::milliseconds{1000};
    PtpPublisherRuntime source{opts};
    const auto start = std::chrono::steady_clock::time_point{};
    CHECK(source.start(std::chrono::system_clock::time_point{}, start));
    auto due = source.poll_due(start);
    CHECK(due.announce && due.sync);
    // A late poll must not permanently shift the next 1s deadline by 7ms.
    due = source.poll_due(start + std::chrono::milliseconds{1007});
    CHECK(due.announce && due.sync);
    due = source.poll_due(start + std::chrono::milliseconds{1999});
    CHECK(!due.announce && !due.sync);
    due = source.poll_due(start + std::chrono::milliseconds{2001});
    CHECK(due.announce && due.sync);

    // After a long pause, send at most ONE pair: never generate bursts.
    due = source.poll_due(start + std::chrono::milliseconds{10250});
    CHECK(due.announce && due.sync);
    due = source.poll_due(start + std::chrono::milliseconds{10251});
    CHECK(!due.announce && !due.sync);
    due = source.poll_due(start + std::chrono::milliseconds{10999});
    CHECK(!due.announce && !due.sync);
    due = source.poll_due(start + std::chrono::milliseconds{11001});
    CHECK(due.announce && due.sync);
    source.stop();

    // Even with different intervals the periods must remain independent.
    opts.sync_interval = std::chrono::milliseconds{250};
    PtpPublisherRuntime subsecond{opts};
    CHECK(subsecond.start(std::chrono::system_clock::time_point{}, start));
    due = subsecond.poll_due(start);
    CHECK(due.announce && due.sync);
    due = subsecond.poll_due(start + std::chrono::milliseconds{261});
    CHECK(!due.announce && due.sync);
    due = subsecond.poll_due(start + std::chrono::milliseconds{499});
    CHECK(!due.announce && !due.sync);
    due = subsecond.poll_due(start + std::chrono::milliseconds{501});
    CHECK(!due.announce && due.sync);
}

void one_step_policy_suppresses_follow_up() {
    using namespace ar::iec61850::time_sync;
    auto options = make_options();
    options.two_step_clock = false;
    PtpPublisherRuntime runtime{options};
    CHECK(runtime.start());
    const auto sync = runtime.prepare_sync(
        PtpTimestamp{std::uint64_t{1U}, std::uint32_t{2U}});
    CHECK(sync.has_value());
    PtpFrame parsed;
    CHECK(PtpCodec::try_parse_ethernet_frame(sync->ethernet_frame, parsed));
    CHECK(!parsed.header.is_two_step());
    CHECK(!runtime.prepare_follow_up(
        sync->sequence_id,
        PtpTimestamp{std::uint64_t{1U}, std::uint32_t{3U}}).has_value());
}


void explicit_ptp_source_authority_failover_is_exclusive_and_second_rearmed() {
    using namespace ar::iec61850::time_sync;
    PtpSourcePolicyMachine internal;
    PtpSourcePolicyOptions internal_opts{};
    CHECK(internal.configure(internal_opts));
    PtpSourceEvidence event{};
    event.link_up = true;
    CHECK(internal.tick(0U, event).action == PtpSourceAction::start_internal);
    event.source_worker_active = true;
    event.internal_gm_tx_ready = true;
    CHECK(internal.tick(1000U, event).state == PtpSourceState::internal_ready);

    PtpSourcePolicyMachine external_only;
    PtpSourcePolicyOptions external_opts{};
    external_opts.intent = PtpSourceIntent::external_follower;
    external_opts.external_target_selected = true;
    CHECK(external_only.configure(external_opts));
    event = {};
    event.link_up = true;
    CHECK(external_only.tick(0U, event).action == PtpSourceAction::start_external);
    event.receiver_worker_active = true;
    CHECK(external_only.tick(6000U, event).action == PtpSourceAction::none);
    CHECK(external_only.state() == PtpSourceState::acquiring_external);
    event.selected_external_servo_locked = true;
    CHECK(external_only.tick(7000U, event).state == PtpSourceState::external_locked);
    event.selected_external_servo_locked = false;
    CHECK(external_only.tick(7100U, event).action ==
          PtpSourceAction::revoke_sv_and_pause);
    CHECK(external_only.tick(20000U, event).action == PtpSourceAction::none);
    CHECK(external_only.state() == PtpSourceState::external_loss);

    PtpSourcePolicyMachine auto_source;
    PtpSourcePolicyOptions auto_options{};
    auto_options.intent = PtpSourceIntent::auto_failover;
    auto_options.external_target_selected = true;
    auto_options.fallback_authorized = true;
    CHECK(auto_source.configure(auto_options));
    event = {};
    event.link_up = true;
    CHECK(auto_source.tick(0U, event).action == PtpSourceAction::start_external);
    event.receiver_worker_active = true;
    CHECK(auto_source.tick(5000U, event).action ==
          PtpSourceAction::revoke_sv_and_pause);
    CHECK(auto_source.generation() == 1U);
    CHECK(auto_source.tick(5100U, event).action == PtpSourceAction::none);
    event.sv_transmission_paused = true;
    CHECK(auto_source.tick(5200U, event).action ==
          PtpSourceAction::stop_current_ptp);
    CHECK(auto_source.tick(5300U, event).action == PtpSourceAction::none);
    event.receiver_worker_active = false;
    event.receiver_cleanup_pending = true;
    CHECK(auto_source.tick(5400U, event).action == PtpSourceAction::none);
    event.receiver_cleanup_pending = false;
    CHECK(auto_source.tick(5500U, event).action == PtpSourceAction::none);
    event.other_grandmaster_present = true;
    CHECK(auto_source.tick(5600U, event).action ==
          PtpSourceAction::refuse_competing_gm);
    event.other_grandmaster_present = false;
    CHECK(auto_source.tick(5700U, event).action ==
          PtpSourceAction::start_internal);
    CHECK(auto_source.generation() == 2U);
    event.source_worker_active = true;
    event.internal_gm_tx_ready = true;
    CHECK(auto_source.tick(5800U, event).state ==
          PtpSourceState::waiting_second_boundary);
    event.at_ptp_second_boundary = true;
    event.sv_gm_identity_matches_active_ptp = false;
    CHECK(auto_source.tick(6000U, event).action == PtpSourceAction::none);
    event.sv_gm_identity_matches_active_ptp = true;
    CHECK(auto_source.tick(7000U, event).action ==
          PtpSourceAction::rearm_sv_at_second);
    CHECK(auto_source.state() == PtpSourceState::internal_ready);

    PtpSourcePolicyMachine auto_no_external;
    auto_options.external_target_selected = false;
    CHECK(auto_no_external.configure(auto_options));
    event = {};
    event.link_up = true;
    CHECK(auto_no_external.tick(0U, event).action ==
          PtpSourceAction::start_internal); // zero artificial network wait

    PtpSourcePolicyMachine reject;
    auto_options.intent = PtpSourceIntent::external_follower;
    auto_options.fallback_authorized = false;
    CHECK(!reject.configure(auto_options)); // no external target is illegal
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"PTP ClockIdentity helpers", clock_identity_helpers_match_ariec61850_behavior},
        {"PTP runtime option validation", options_are_fail_safe_and_reconfiguration_requires_stop},
        {"PTP runtime scheduler/status", scheduler_sequence_and_status_match_runtime_contract},
        {"PTP deterministic cadence under delayed polling", fixed_deadline_cadence_survives_scheduler_latency},
        {"PTP runtime hardware Pdelay preparation", peer_delay_runtime_preserves_request_identity_and_hw_timestamps},
        {"PTP runtime one-step policy", one_step_policy_suppresses_follow_up},
        {"PTP source policy exclusive failover", explicit_ptp_source_authority_failover_is_exclusive_and_second_rearmed},
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
