// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/time_sync/ptp.hpp"
#include "ariec61850/time_sync/ptp_monitor.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using ByteVector = std::vector<std::uint8_t>;
#define CHECK(condition) do { if (!(condition)) { throw std::runtime_error( \
    std::string{"CHECK failed: "} + #condition + " at " + __FILE__ + ":" + \
    std::to_string(__LINE__)); } } while (false)

std::string to_hex(const ByteVector& bytes) {
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setfill('0');
    for (const auto byte : bytes) {
        stream << std::setw(2) << static_cast<unsigned>(byte);
    }
    return stream.str();
}

ar::iec61850::time_sync::PtpBuildOptions make_options() {
    using namespace ar::iec61850::time_sync;
    PtpBuildOptions options;
    options.domain_number = 0U;
    options.source_port_identity.clock_identity = {
        0x02U, 0x00U, 0x00U, 0xFFU, 0xFEU, 0x00U, 0x00U, 0x01U};
    options.source_port_identity.port_number = 1U;
    options.sequence_id = 0x1234U;
    options.log_message_interval = -2;
    options.two_step = true;
    options.timestamp = {0x010203040506ULL, 123456789U};
    return options;
}

void sync_matches_csharp_oracle_and_roundtrips() {
    using namespace ar::iec61850::time_sync;
    const auto encoded = PtpCodec::build_sync(make_options());
    CHECK(to_hex(encoded) ==
        "0002002C00000200000000000000000000000000020000FFFE0000010001123400FE"
        "010203040506075BCD15");

    PtpFrame decoded;
    CHECK(PtpCodec::try_parse_message(encoded, decoded));
    CHECK(decoded.header.message_type == PtpMessageType::sync);
    CHECK(decoded.header.version == 2U);
    CHECK(decoded.header.message_length == 44U);
    CHECK(decoded.header.domain_number == 0U);
    CHECK(decoded.header.is_two_step());
    CHECK(decoded.header.sequence_id == 0x1234U);
    CHECK(decoded.header.log_message_interval == -2);
    CHECK(decoded.timestamp.has_value());
    CHECK(decoded.timestamp->seconds == 0x010203040506ULL);
    CHECK(decoded.timestamp->nanoseconds == 123456789U);
}

void announce_matches_csharp_oracle_and_roundtrips() {
    using namespace ar::iec61850::time_sync;
    auto options = make_options();
    options.sequence_id = 7U;
    options.log_message_interval = 0;
    options.two_step = false;
    options.timestamp = {};
    const auto encoded = PtpCodec::build_announce(options, 37);
    CHECK(to_hex(encoded) ==
        "0B02004000000000000000000000000000000000020000FFFE000001000100070500"
        "0000000000000000000000250080F8FEFFFF80020000FFFE0000010000A0");

    PtpFrame decoded;
    CHECK(PtpCodec::try_parse_message(encoded, decoded));
    CHECK(decoded.header.message_type == PtpMessageType::announce);
    CHECK(decoded.announce.has_value());
    CHECK(decoded.announce->current_utc_offset == 37);
    CHECK(decoded.announce->priority1 == 128U);
    CHECK(decoded.announce->clock_class == 248U);
    CHECK(decoded.announce->clock_accuracy == PtpClockAccuracy::unknown);
    CHECK(decoded.announce->time_source == PtpTimeSource::internal_oscillator);
    CHECK(decoded.announce->grandmaster_identity == options.source_port_identity.clock_identity);
}

void pdelay_response_preserves_request_identity_and_hardware_timestamp_fields() {
    using namespace ar::iec61850::time_sync;
    auto options = make_options();
    options.sequence_id = 0x0055U;
    options.log_message_interval = -3;
    options.two_step = true;
    options.timestamp = {0x000102030405ULL, 222333444U};

    const PtpPortIdentity requesting_port{
        {0x10U, 0x20U, 0x30U, 0x40U, 0x50U, 0x60U, 0x70U, 0x80U},
        2U,
    };

    const auto response = PtpCodec::build_pdelay_resp(options, requesting_port);
    CHECK(response.size() == 54U);

    PtpFrame decoded;
    CHECK(PtpCodec::try_parse_message(response, decoded));
    CHECK(decoded.header.message_type == PtpMessageType::pdelay_resp);
    CHECK(decoded.header.sequence_id == 0x0055U);
    CHECK(decoded.header.log_message_interval == -3);
    CHECK(decoded.header.is_two_step());
    CHECK(decoded.body.size() == 20U);

    PtpTimestamp request_receipt_timestamp;
    CHECK(PtpTimestamp::try_read(
        std::span<const std::uint8_t>{decoded.body}.first(10U),
        request_receipt_timestamp));
    CHECK(request_receipt_timestamp == options.timestamp);
    CHECK(std::equal(
        requesting_port.clock_identity.begin(),
        requesting_port.clock_identity.end(),
        decoded.body.begin() + 10));
    CHECK(decoded.body[18] == 0x00U);
    CHECK(decoded.body[19] == 0x02U);

    auto follow_up_options = options;
    follow_up_options.two_step = false;
    follow_up_options.timestamp = {0x000102030406ULL, 555666777U};
    const auto follow_up = PtpCodec::build_pdelay_resp_follow_up(
        follow_up_options,
        requesting_port);
    CHECK(follow_up.size() == 54U);
    CHECK(PtpCodec::try_parse_message(follow_up, decoded));
    CHECK(decoded.header.message_type == PtpMessageType::pdelay_resp_follow_up);
    CHECK(decoded.header.sequence_id == 0x0055U);
    CHECK(!decoded.header.is_two_step());

    PtpTimestamp response_origin_timestamp;
    CHECK(PtpTimestamp::try_read(
        std::span<const std::uint8_t>{decoded.body}.first(10U),
        response_origin_timestamp));
    CHECK(response_origin_timestamp == follow_up_options.timestamp);
    CHECK(std::equal(
        requesting_port.clock_identity.begin(),
        requesting_port.clock_identity.end(),
        decoded.body.begin() + 10));
}

void ethernet_vlan_and_qinq_are_parsed_for_analyzer_use() {
    using namespace ar::iec61850::time_sync;
    auto options = make_options();
    const auto message = PtpCodec::build_sync(options);
    const std::array<std::uint8_t, 6> source{0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U};
    const auto vlan_frame = PtpCodec::build_ethernet_frame(
        ptp_general_multicast_mac, source, message, 100U, 4U);
    CHECK(!vlan_frame.empty());

    PtpFrame decoded;
    CHECK(PtpCodec::try_parse_ethernet_frame(vlan_frame, decoded));
    CHECK(decoded.vlan_id == 100U);
    CHECK(!decoded.outer_vlan_id.has_value());
    CHECK(!decoded.peer_delay_multicast);

    ByteVector qinq;
    qinq.reserve(vlan_frame.size() + 4U);
    qinq.insert(qinq.end(), vlan_frame.begin(), vlan_frame.begin() + 12);
    qinq.push_back(0x88U);
    qinq.push_back(0xA8U);
    qinq.push_back(0xA0U); // PCP 5, VID 10
    qinq.push_back(0x0AU);
    qinq.insert(qinq.end(), vlan_frame.begin() + 12, vlan_frame.end());

    CHECK(PtpCodec::try_parse_ethernet_frame(qinq, decoded));
    CHECK(decoded.outer_vlan_id == 10U);
    CHECK(decoded.vlan_id == 100U);
    CHECK(decoded.header.sequence_id == 0x1234U);
}

void passive_monitor_health_drives_conservative_smp_synch_policy() {
    using namespace ar::iec61850::time_sync;
    const auto observed_at = std::chrono::system_clock::time_point{std::chrono::seconds{1000}};
    const std::array<std::uint8_t, 6> source{0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U};

    auto options = make_options();
    options.timestamp = {};
    options.log_message_interval = 0;
    options.sequence_id = 1U;
    options.two_step = false;
    const auto announce = PtpCodec::build_ethernet_frame(
        ptp_general_multicast_mac, source, PtpCodec::build_announce(options));

    options.two_step = true;
    const auto sync = PtpCodec::build_ethernet_frame(
        ptp_general_multicast_mac, source, PtpCodec::build_sync(options));
    options.two_step = false;
    const auto follow_up = PtpCodec::build_ethernet_frame(
        ptp_general_multicast_mac, source, PtpCodec::build_follow_up(options));

    PtpPassiveMonitor monitor;
    CHECK(monitor.observe_ethernet_frame(announce, observed_at));
    CHECK(monitor.observe_ethernet_frame(sync, observed_at + std::chrono::milliseconds{10}));
    CHECK(monitor.observe_ethernet_frame(follow_up, observed_at + std::chrono::milliseconds{20}));

    const auto snapshot = monitor.snapshot(observed_at + std::chrono::milliseconds{100});
    PtpTimingHealthOptions health_options;
    health_options.expected_domain_number = 0U;
    const auto report = PtpTimingHealthValidator::evaluate(snapshot, health_options);
    CHECK(report.is_healthy());
    CHECK(report.selected_source.has_value());
    CHECK(resolve_smp_synch(report) == SmpSynchValue::global_synchronized);

    PtpPassiveMonitor incomplete_monitor;
    CHECK(incomplete_monitor.observe_ethernet_frame(sync, observed_at));
    const auto incomplete_report = PtpTimingHealthValidator::evaluate(
        incomplete_monitor.snapshot(observed_at + std::chrono::milliseconds{100}),
        health_options);
    CHECK(!incomplete_report.is_healthy());
    CHECK(resolve_smp_synch(incomplete_report, true) == SmpSynchValue::local_synchronized);
    CHECK(resolve_smp_synch(incomplete_report, false) == SmpSynchValue::not_synchronized);
}

void malformed_or_non_ptp_frames_are_rejected() {
    using namespace ar::iec61850::time_sync;
    PtpFrame frame;
    CHECK(!PtpCodec::try_parse_message(ByteVector(33U, 0U), frame));

    auto message = PtpCodec::build_sync(make_options());
    message[1] = 0x03U;
    CHECK(!PtpCodec::try_parse_message(message, frame));

    std::array<std::uint8_t, 60> ethernet{};
    ethernet[12] = 0x88U;
    ethernet[13] = 0xBAU;
    CHECK(!PtpCodec::try_parse_ethernet_frame(ethernet, frame));
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"PTP Sync C# oracle", sync_matches_csharp_oracle_and_roundtrips},
        {"PTP Announce C# oracle", announce_matches_csharp_oracle_and_roundtrips},
        {"PTP Pdelay response wire fields", pdelay_response_preserves_request_identity_and_hardware_timestamp_fields},
        {"PTP VLAN/QinQ analyzer parser", ethernet_vlan_and_qinq_are_parsed_for_analyzer_use},
        {"PTP monitor health and smpSynch", passive_monitor_health_drives_conservative_smp_synch_policy},
        {"PTP malformed input", malformed_or_non_ptp_frames_are_rejected},
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
    std::cout << "Passed " << passed << "/" << tests.size() << " PTP tests.\n";
    return 0;
}
