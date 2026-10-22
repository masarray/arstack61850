// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/time_sync/ptp_power_profile.hpp"
#include "ariec61850/time_sync/ptp.hpp"
#include "ariec61850/time_sync/ptp_tai_source.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace ar::iec61850::time_sync;

#define REQUIRE(value) do { if (!(value)) { \
    throw std::runtime_error(std::string{"Check failed: "} + #value + \
        " line " + std::to_string(__LINE__)); \
} } while (false)

namespace {

PtpPublisherOptions make_source() {
    PtpPublisherOptions options;
    options.source_mac = {0x02U, 0x31U, 0x51U, 0x71U, 0x91U, 0xB1U};
    options.clock_identity = ptp_clock_identity_from_mac(options.source_mac);
    options.clock_class = 248U;
    options.clock_accuracy = PtpClockAccuracy::unknown;
    options.time_source = PtpTimeSource::internal_oscillator;
    options.respond_to_peer_delay = true;
    return options;
}

void check_defaults_and_reject_nonstandard_cadence() {
    const auto studioProfile = inspect_ptp_power_profile_cadence(1000U, 1000U, true);
    REQUIRE(studioProfile.compatible());
    REQUIRE(!inspect_ptp_power_profile_cadence(1000U, 1000U, false).compatible());
    REQUIRE(!inspect_ptp_power_profile_cadence(2000U, 1000U, true).compatible());
    REQUIRE(!inspect_ptp_power_profile_cadence(1000U, 250U, true).compatible());
    REQUIRE(!inspect_ptp_power_profile_cadence(0U, 1000U, true).compatible());
    auto profile = make_source();
    REQUIRE(inspect_ptp_power_profile_cadence(profile).compatible());
    profile.sync_interval = std::chrono::milliseconds{250};
    REQUIRE(!inspect_ptp_power_profile_cadence(profile).compatible());
    profile.sync_interval = std::chrono::milliseconds{1000};
    profile.announce_interval = std::chrono::milliseconds{2000};
    REQUIRE(!inspect_ptp_power_profile_cadence(profile).compatible());
    profile.announce_interval = std::chrono::milliseconds{1000};
    profile.respond_to_peer_delay = false;
    REQUIRE(!inspect_ptp_power_profile_cadence(profile).compatible());
}

void check_source_readiness_requires_fresh_wire_proof() {
    PtpSourceReadinessGate gate;
    REQUIRE(!gate.ready());
    gate.observe_peer_delay(true);
    REQUIRE(!gate.ready()); // Pdelay response is never Sync evidence.
    gate.observe_announce(true);
    REQUIRE(!gate.ready());
    gate.observe_sync_pair(true);
    REQUIRE(gate.ready());

    gate.observe_sync_pair(false); // e.g., Follow_Up failed after Sync TX
    REQUIRE(!gate.ready());
    gate.observe_peer_delay(true);
    REQUIRE(!gate.ready()); // previously sent Announce/Sync must not count
    gate.observe_sync_pair(true);
    REQUIRE(!gate.ready()); // need a *fresh* Announce too
    gate.observe_announce(true);
    REQUIRE(gate.ready());

    gate.observe_peer_delay(false);
    REQUIRE(!gate.ready());
    gate.observe_announce(true);
    REQUIRE(!gate.ready());
    gate.observe_sync_pair(true);
    REQUIRE(gate.ready());

    // A failed Announce revokes readiness even if the Sync pair was fine.
    gate.observe_announce(false);
    REQUIRE(!gate.ready());
    gate.observe_announce(true);
    REQUIRE(!gate.ready());
    gate.observe_sync_pair(true);
    REQUIRE(gate.ready());
    gate.invalidate(); // SOURCE STOP / teardown
    REQUIRE(!gate.ready());
}

void check_source_heartbeat_freshness_and_time_provenance() {
    // A late task or absent Announce must revoke READY without waiting for
    // another SOURCE loop. A clock going backward is never healthy.
    using Fresh = PtpSourceEvidenceFreshness;
    REQUIRE(!Fresh::fresh(5000000, 0, 4700000, 3000000));
    REQUIRE(!Fresh::fresh(5000000, 4700000, 0, 3000000));
    REQUIRE(Fresh::fresh(5000000, 3000001, 4200000, 3000000));
    REQUIRE(!Fresh::fresh(5000000, 2000000, 4200000, 3000000));
    REQUIRE(!Fresh::fresh(5000000, 4200000, 1999999, 3000000));
    REQUIRE(!Fresh::fresh(5000000, 5100000, 4200000, 3000000));
    REQUIRE(!Fresh::fresh(5000000, 4200000, 5100000, 3000000));
    REQUIRE(!Fresh::fresh(5000000, 4200000, 4200000, 0));
    REQUIRE(Fresh::fresh(8000000, 6000000, 7000000, 3000000));
    REQUIRE(!Fresh::fresh(10000001, 6000000, 7000000, 3000000));

    const auto unverified = conservative_lab_time_properties(true, false);
    REQUIRE(unverified.flags() == 0U);
    REQUIRE(!unverified.ptp_timescale);
    const auto unseeded = conservative_lab_time_properties(false, true);
    REQUIRE(unseeded.flags() == 0U);
    const auto verified = conservative_lab_time_properties(true, true);
    REQUIRE(verified.flags() == 0x000CU);
    REQUIRE(!verified.time_traceable && !verified.frequency_traceable);
    // Test actual wire bytes separately from the code's own PTP decoder.
    auto options = make_source();
    options.time_properties = unverified;
    PtpPublisherRuntime source{options};
    REQUIRE(source.start());
    const auto first = source.prepare_announce(PtpTimestamp{1700000037U, 0U});
    REQUIRE(first.has_value());
    REQUIRE(first->ethernet_frame[20] == 0x00U);
    REQUIRE(first->ethernet_frame[21] == 0x00U);
    source.stop();

    options.time_properties = verified;
    PtpPublisherRuntime explicit_offset{options};
    REQUIRE(explicit_offset.start());
    const auto second = explicit_offset.prepare_announce(PtpTimestamp{1700000037U, 0U});
    REQUIRE(second.has_value());
    REQUIRE(second->ethernet_frame[20] == 0x00U);
    REQUIRE(second->ethernet_frame[21] == 0x0CU);
    explicit_offset.stop();
}

void check_priority_tagged_power_profile_wire() {
    auto options = make_source();
    options.vlan_id = 0U;
    options.vlan_priority = 4U;
    std::string error;
    REQUIRE(PtpPublisherRuntime::validate_options(options, error));
    PtpPublisherRuntime source{options};
    REQUIRE(source.start());
    const auto announce = source.prepare_announce(PtpTimestamp{1000U, 0U});
    REQUIRE(announce.has_value());
    const auto& raw = announce->ethernet_frame;
    REQUIRE(raw.size() > 18U);
    REQUIRE(raw[12] == 0x81U && raw[13] == 0x00U);
    REQUIRE(raw[14] == 0x80U && raw[15] == 0x00U); // PCP4, VID0
    REQUIRE(raw[16] == 0x88U && raw[17] == 0xF7U);
    PtpFrame parsed{};
    REQUIRE(PtpCodec::try_parse_ethernet_frame(raw, parsed));
    REQUIRE(parsed.vlan_id == std::optional<std::uint16_t>{0U});
    REQUIRE(parsed.header.message_type == PtpMessageType::announce);
    source.stop();
    options.vlan_id = 4095U;
    REQUIRE(!PtpPublisherRuntime::validate_options(options, error));
}

void check_ptp_wire_sequence_and_identity() {
    auto options = make_source();
    constexpr std::uint8_t domain = 93U; // supported alternate network domain
    options.domain_number = domain;
    PtpPublisherRuntime source{options};
    const auto start_time = std::chrono::steady_clock::time_point{};
    REQUIRE(source.start(std::chrono::system_clock::time_point{}, start_time));
    auto due = source.poll_due(start_time);
    REQUIRE(due.announce && due.sync);
    due = source.poll_due(start_time + std::chrono::milliseconds{999});
    REQUIRE(!due.announce && !due.sync);
    due = source.poll_due(start_time + std::chrono::milliseconds{1000});
    REQUIRE(due.announce && due.sync);

    const auto announce = source.prepare_announce(PtpTimestamp{1000U, 0U});
    REQUIRE(announce.has_value());
    PtpFrame frame;
    REQUIRE(PtpCodec::try_parse_ethernet_frame(announce->ethernet_frame, frame));
    REQUIRE(frame.header.message_type == PtpMessageType::announce);
    REQUIRE(frame.header.version == ptp_version);
    REQUIRE(frame.header.domain_number == domain);
    REQUIRE(frame.header.log_message_interval == 0);
    REQUIRE(frame.announce.has_value());
    REQUIRE(frame.announce->clock_class == 248U);
    REQUIRE(frame.announce->clock_accuracy == PtpClockAccuracy::unknown);
    REQUIRE(frame.announce->time_source == PtpTimeSource::internal_oscillator);
    REQUIRE(frame.announce->grandmaster_identity == options.clock_identity);
    REQUIRE(frame.announce->steps_removed == 0U);
    REQUIRE((frame.header.flags & 0x0030U) == 0U); // no false traceability
    REQUIRE(std::equal(ptp_general_multicast_mac.begin(),
                       ptp_general_multicast_mac.end(),
                       announce->ethernet_frame.begin()));
    REQUIRE(announce->ethernet_frame[12U] == 0x88U);
    REQUIRE(announce->ethernet_frame[13U] == 0xF7U);

    const auto sync = source.prepare_sync(PtpTimestamp{});
    REQUIRE(sync.has_value());
    REQUIRE(PtpCodec::try_parse_ethernet_frame(sync->ethernet_frame, frame));
    REQUIRE(frame.header.message_type == PtpMessageType::sync);
    REQUIRE(frame.header.log_message_interval == 0);
    REQUIRE(frame.header.is_two_step());
    REQUIRE(frame.header.domain_number == domain);

    const PtpTimestamp captured_egress_time{1001U, 345678901U};
    const auto follow = source.prepare_follow_up(sync->sequence_id, captured_egress_time);
    REQUIRE(follow.has_value());
    REQUIRE(PtpCodec::try_parse_ethernet_frame(follow->ethernet_frame, frame));
    REQUIRE(frame.header.message_type == PtpMessageType::follow_up);
    REQUIRE(frame.header.sequence_id == sync->sequence_id);
    REQUIRE(frame.header.log_message_interval == 0);
    REQUIRE(frame.timestamp == captured_egress_time);

    const PtpPortIdentity requesting_port{
        {0x02U, 0x00U, 0x00U, 0xFFU, 0xFEU, 0x00U, 0x10U, 0x20U}, 1U};
    const auto reply = source.prepare_pdelay_response(
        requesting_port, 33U, 0, PtpTimestamp{1001U, 12000U});
    REQUIRE(reply.has_value());
    REQUIRE(PtpCodec::try_parse_ethernet_frame(reply->ethernet_frame, frame));
    REQUIRE(frame.header.message_type == PtpMessageType::pdelay_resp);
    REQUIRE(frame.header.sequence_id == 33U);
    REQUIRE(frame.header.log_message_interval == 0);
    REQUIRE(frame.peer_delay_multicast);
    REQUIRE(std::equal(ptp_peer_delay_multicast_mac.begin(),
                       ptp_peer_delay_multicast_mac.end(),
                       reply->ethernet_frame.begin()));

    const auto reply_follow = source.prepare_pdelay_response_follow_up(
        requesting_port, 33U, 0, PtpTimestamp{1001U, 13000U});
    REQUIRE(reply_follow.has_value());
    REQUIRE(PtpCodec::try_parse_ethernet_frame(reply_follow->ethernet_frame, frame));
    REQUIRE(frame.header.message_type == PtpMessageType::pdelay_resp_follow_up);
    REQUIRE(frame.header.sequence_id == 33U);

    source.stop();
}

void check_tai_seed_and_truthful_time_properties() {
    // The physical clock uses unsigned 32-bit seconds; reject unavailable
    // system time, stale RTC and arithmetic overflow rather than wrapping.
    REQUIRE(!try_seed_ptp_tai_seconds(0, 37).has_value());
    REQUIRE(!try_seed_ptp_tai_seconds(1577836799LL, 37).has_value());
    REQUIRE(!try_seed_ptp_tai_seconds(1700000000LL, -1).has_value());
    REQUIRE(!try_seed_ptp_tai_seconds(1700000000LL, 201).has_value());
    REQUIRE(!try_seed_ptp_tai_seconds(4294967290LL, 37).has_value());
    REQUIRE(try_seed_ptp_tai_seconds(1700000000LL, 37) ==
            std::optional<std::uint32_t>{1700000037U});
    REQUIRE(try_seed_ptp_tai_seconds(4294967258LL, 37) ==
            std::optional<std::uint32_t>{4294967295U});

    auto options = make_source();
    options.time_properties.ptp_timescale = true;
    // Local RTC-derived TAI is not UTC/GNSS traceable, even with a TAI epoch.
    PtpPublisherRuntime local_clock{options};
    REQUIRE(local_clock.start());
    const auto announce = local_clock.prepare_announce(PtpTimestamp{1700000037U, 0U});
    REQUIRE(announce.has_value());
    PtpFrame frame;
    REQUIRE(PtpCodec::try_parse_ethernet_frame(announce->ethernet_frame, frame));
    REQUIRE(frame.header.flags == 0x0008U);
    // Independent raw-octet check, so encoder/parser cannot agree on
    // an identical but incorrectly located flagField.
    REQUIRE(announce->ethernet_frame[20U] == 0x00U);
    REQUIRE(announce->ethernet_frame[21U] == 0x08U);
    REQUIRE(frame.announce.has_value());
    REQUIRE(frame.announce->current_utc_offset == 37);
    REQUIRE(frame.announce->clock_class == 248U);

    const auto sync = local_clock.prepare_sync(PtpTimestamp{});
    REQUIRE(sync.has_value());
    REQUIRE(PtpCodec::try_parse_ethernet_frame(sync->ethernet_frame, frame));
    REQUIRE(frame.header.flags == 0x0208U); // two-step + PTP timescale
    REQUIRE(sync->ethernet_frame[20U] == 0x02U);
    REQUIRE(sync->ethernet_frame[21U] == 0x08U);
    const auto follow_up = local_clock.prepare_follow_up(
        sync->sequence_id, PtpTimestamp{1700000038U, 123456U});
    REQUIRE(follow_up.has_value());
    REQUIRE(PtpCodec::try_parse_ethernet_frame(follow_up->ethernet_frame, frame));
    REQUIRE(frame.header.flags == 0x0008U);
    local_clock.stop();

    // An explicitly verified TAI-UTC offset is a separate assertion;
    // it does not implicitly enable timeTraceable/frequencyTraceable.
    options.time_properties.current_utc_offset_valid = true;
    PtpPublisherRuntime manually_verified{options};
    REQUIRE(manually_verified.start());
    const auto verified_announce = manually_verified.prepare_announce(
        PtpTimestamp{1700000037U, 0U});
    REQUIRE(verified_announce.has_value());
    REQUIRE(PtpCodec::try_parse_ethernet_frame(
        verified_announce->ethernet_frame, frame));
    REQUIRE(frame.header.flags == 0x000CU);
    REQUIRE(verified_announce->ethernet_frame[20U] == 0x00U);
    REQUIRE(verified_announce->ethernet_frame[21U] == 0x0CU);
    REQUIRE((frame.header.flags & 0x0030U) == 0U);
    manually_verified.stop();

    // Time-properties must never be inferred from PTP packet visibility.
    PtpPublisherRuntime unseeded{make_source()};
    REQUIRE(unseeded.start());
    const auto unseeded_announce = unseeded.prepare_announce(
        PtpTimestamp{1U, 0U});
    REQUIRE(unseeded_announce.has_value());
    REQUIRE(PtpCodec::try_parse_ethernet_frame(
        unseeded_announce->ethernet_frame, frame));
    REQUIRE(frame.header.flags == 0U);
    unseeded.stop();

    auto inconsistent = make_source();
    inconsistent.time_properties.current_utc_offset_valid = true;
    std::string error;
    REQUIRE(!PtpPublisherRuntime::validate_options(inconsistent, error));
    REQUIRE(!error.empty());
}

void check_vlan_encoding_remains_configurable() {
    auto options = make_source();
    options.vlan_id = std::uint16_t{100U};
    options.vlan_priority = std::uint8_t{4U};
    PtpPublisherRuntime source{options};
    REQUIRE(source.start());
    const auto announce = source.prepare_announce(PtpTimestamp{1000U, 0U});
    REQUIRE(announce.has_value());
    PtpFrame parsed;
    REQUIRE(PtpCodec::try_parse_ethernet_frame(announce->ethernet_frame, parsed));
    REQUIRE(parsed.vlan_id == 100U);
    REQUIRE(announce->ethernet_frame[12U] == 0x81U);
    REQUIRE(announce->ethernet_frame[13U] == 0x00U);
    source.stop();
}

} // namespace

int main() {
    try {
        check_defaults_and_reject_nonstandard_cadence();
        check_source_readiness_requires_fresh_wire_proof();
        check_source_heartbeat_freshness_and_time_provenance();
        check_ptp_wire_sequence_and_identity();
        check_priority_tagged_power_profile_wire();
        check_vlan_encoding_remains_configurable();
        check_tai_seed_and_truthful_time_properties();
        std::cout << "PTP Power Profile wire and configuration checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
