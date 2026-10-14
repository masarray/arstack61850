// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/time_sync/ptp_power_profile.hpp"
#include "ariec61850/time_sync/ptp.hpp"

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

void check_vlan_encoding_remains_configurable() {
    auto options = make_source();
    options.vlan_id = 100U;
    options.vlan_priority = 4U;
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
        check_ptp_wire_sequence_and_identity();
        check_vlan_encoding_remains_configurable();
        std::cout << "PTP Power Profile wire and configuration checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
