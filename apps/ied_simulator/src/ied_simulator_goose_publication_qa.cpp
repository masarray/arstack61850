// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/capture/pcap.hpp"
#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/goose/frame_codec.hpp"
#include "ariec61850/goose/publisher_runtime.hpp"
#include "ariec61850/goose/raw_ethernet_publisher.hpp"
#include "ariec61850/mms/data_value.hpp"
#include "ariec61850/scl/parser.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace ar::iec61850;

#define REQUIRE(condition, message) do { if (!(condition)) throw std::runtime_error(message); } while (false)

mms::MmsDataValue sample_value(const scl::SclDataSetEntry& entry, const std::size_t index) {
    if (entry.is_quality) {
        const std::uint8_t quality[]{0U, 0U};
        return mms::MmsDataValue::bit_string(3U, quality);
    }
    if (entry.is_timestamp) {
        return mms::MmsDataValue::utc_time(mms::Iec61850UtcTime{
            std::chrono::system_clock::time_point{std::chrono::seconds{1'700'000'000}}, 0U});
    }
    std::string type = entry.basic_type;
    std::transform(type.begin(), type.end(), type.begin(), [](const unsigned char value) {
        return static_cast<char>(std::toupper(value));
    });
    if (type.find("BOOLEAN") != std::string::npos || type == "BOOL") {
        return mms::MmsDataValue::boolean((index & 1U) != 0U);
    }
    if (type.find("FLOAT") != std::string::npos || type.find("DOUBLE") != std::string::npos) {
        return mms::MmsDataValue::floating_point(static_cast<double>(index) + 0.5);
    }
    if (type.find("UINT") != std::string::npos || type.find("UNSIGNED") != std::string::npos) {
        return mms::MmsDataValue::unsigned_integer(static_cast<std::uint64_t>(index));
    }
    if (type.find("INT") != std::string::npos) {
        return mms::MmsDataValue::integer(static_cast<std::int64_t>(index));
    }
    return mms::MmsDataValue::visible_string("qa-" + std::to_string(index));
}

std::uint32_t doubled_ttl(const std::uint32_t delay) {
    return delay > std::numeric_limits<std::uint32_t>::max() / 2U
        ? std::numeric_limits<std::uint32_t>::max()
        : std::max<std::uint32_t>(1U, delay * 2U);
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) {
            std::cerr << "usage: ied_simulator_goose_publication_qa <scl> <pcap-output>\n";
            return 2;
        }
        const auto document = scl::SclParser{}.load(std::filesystem::path{argv[1]});
        const auto found = std::find_if(
            document.goose_streams.begin(), document.goose_streams.end(),
            [](const scl::SclGooseStream& stream) {
                return stream.address.app_id.has_value() &&
                    stream.address.destination_mac.has_value() &&
                    !stream.entries.empty() && stream.min_time_milliseconds > 0U &&
                    stream.max_time_milliseconds >= stream.min_time_milliseconds;
            });
        REQUIRE(found != document.goose_streams.end(), "fixture has no addressed GOOSE stream");
        const auto& stream = *found;

        goose::GooseFrame frame;
        frame.destination = ethernet::MacAddress{
            std::span<const std::uint8_t>{stream.address.destination_mac->data(),
                                          stream.address.destination_mac->size()}};
        frame.source = ethernet::MacAddress::parse("02:00:00:00:00:4D");
        frame.app_id = *stream.address.app_id;
        if (stream.address.vlan_id.has_value()) {
            REQUIRE(stream.address.vlan_priority.has_value(), "fixture VLAN priority missing");
            frame.vlan = ethernet::VlanTag{
                *stream.address.vlan_priority, *stream.address.vlan_id};
        }
        frame.pdu.go_cb_ref = stream.control_block_reference;
        frame.pdu.data_set_reference = stream.data_set_reference;
        frame.pdu.go_id = stream.go_id;
        frame.pdu.configuration_revision = stream.configuration_revision;

        std::vector<mms::MmsDataValue> values;
        values.reserve(stream.entries.size());
        for (std::size_t index = 0; index < stream.entries.size(); ++index) {
            values.push_back(sample_value(stream.entries[index], index));
        }

        goose::GoosePublisherRuntime runtime{
            goose::GoosePublisherSession{std::move(frame)},
            stream.min_time_milliseconds,
            stream.max_time_milliseconds};
        const auto base = goose::GoosePublisherRuntime::clock::time_point{} + std::chrono::seconds{1};
        const auto timestamp = mms::Iec61850UtcTime{
            std::chrono::system_clock::time_point{std::chrono::seconds{1'700'000'000}}, 0U};
        const auto initial = runtime.start(values, timestamp, base);

        goose::GooseFrame decodedInitial;
        REQUIRE(goose::GooseFrameCodec::try_decode(initial.ethernet_bytes, decodedInitial),
                "initial GOOSE frame did not decode");
        REQUIRE(decodedInitial.destination == frame.destination, "destination MAC mismatch");
        REQUIRE(decodedInitial.source == ethernet::MacAddress::parse("02:00:00:00:00:4D"),
                "source MAC mismatch");
        REQUIRE(decodedInitial.app_id == *stream.address.app_id, "APPID mismatch");
        REQUIRE(decodedInitial.pdu.go_cb_ref == stream.control_block_reference, "gocbRef mismatch");
        REQUIRE(decodedInitial.pdu.data_set_reference == stream.data_set_reference, "DataSet mismatch");
        REQUIRE(decodedInitial.pdu.configuration_revision == stream.configuration_revision,
                "ConfRev mismatch");
        REQUIRE(decodedInitial.pdu.values.size() == stream.entries.size(), "allData member count mismatch");
        REQUIRE(decodedInitial.pdu.state_number == 1U && decodedInitial.pdu.sequence_number == 0U,
                "initial stNum/sqNum mismatch");
        REQUIRE(decodedInitial.pdu.time_allowed_to_live_milliseconds ==
                    doubled_ttl(stream.min_time_milliseconds),
                "initial timeAllowedToLive does not match next retransmission");

        const auto retransmission = runtime.poll(
            base + std::chrono::milliseconds{stream.min_time_milliseconds});
        REQUIRE(retransmission.has_value(), "first retransmission was not due at MinTime");
        goose::GooseFrame decodedRetransmission;
        REQUIRE(goose::GooseFrameCodec::try_decode(
                    retransmission->ethernet_bytes, decodedRetransmission),
                "retransmission did not decode");
        REQUIRE(decodedRetransmission.pdu.state_number == 1U &&
                    decodedRetransmission.pdu.sequence_number == 1U,
                "retransmission stNum/sqNum mismatch");
        const auto secondDelay = std::min<std::uint64_t>(
            static_cast<std::uint64_t>(stream.max_time_milliseconds),
            static_cast<std::uint64_t>(stream.min_time_milliseconds) * 2ULL);
        REQUIRE(decodedRetransmission.pdu.time_allowed_to_live_milliseconds ==
                    doubled_ttl(static_cast<std::uint32_t>(secondDelay)),
                "retransmission timeAllowedToLive mismatch");

        auto changedValues = values;
        changedValues[0] = mms::MmsDataValue::visible_string("state-change");
        const auto changed = runtime.state_change(
            changedValues,
            timestamp,
            base + std::chrono::milliseconds{stream.min_time_milliseconds + 1U});
        goose::GooseFrame decodedChanged;
        REQUIRE(goose::GooseFrameCodec::try_decode(changed.ethernet_bytes, decodedChanged),
                "state-change frame did not decode");
        REQUIRE(decodedChanged.pdu.state_number == 2U && decodedChanged.pdu.sequence_number == 0U,
                "state change did not increment stNum/reset sqNum");
        REQUIRE(decodedChanged.pdu.time_allowed_to_live_milliseconds ==
                    doubled_ttl(stream.min_time_milliseconds),
                "state-change TTL did not restart at MinTime");

        capture::PcapWriter::write_all(
            std::filesystem::path{argv[2]},
            std::vector<capture::PcapPacket>{
                {std::chrono::system_clock::time_point{std::chrono::seconds{1}}, initial.ethernet_bytes},
                {std::chrono::system_clock::time_point{std::chrono::seconds{1}} +
                     std::chrono::milliseconds{stream.min_time_milliseconds},
                 retransmission->ethernet_bytes},
                {std::chrono::system_clock::time_point{std::chrono::seconds{1}} +
                     std::chrono::milliseconds{stream.min_time_milliseconds + 1U},
                 changed.ethernet_bytes}});

        goose::RawEthernetPublisher raw;
        std::string error;
        REQUIRE(!raw.open({}, error) && !error.empty(), "empty interface did not fail closed");
        error.clear();
        REQUIRE(!raw.open("arstack-no-such-interface-61850", error) && !error.empty(),
                "nonexistent interface did not fail closed");

        std::cout << "GOOSE_WIRE_INTEROP_PASS appid=" << *stream.address.app_id
                  << " vlan=" << (stream.address.vlan_id.has_value()
                                      ? std::to_string(*stream.address.vlan_id) : "untagged")
                  << " members=" << stream.entries.size()
                  << " st_initial=1 sq_retx=1 st_change=2 ttl_initial="
                  << decodedInitial.pdu.time_allowed_to_live_milliseconds
                  << " ttl_retx=" << decodedRetransmission.pdu.time_allowed_to_live_milliseconds
                  << " pcap_packets=3\n";
        std::cout << "GOOSE_PUBLICATION_NEGATIVE_PASS empty_interface=rejected "
                     "nonexistent_interface=rejected explicit_binding=required\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "GOOSE_PUBLICATION_QA_FAIL " << error.what() << '\n';
        return 1;
    }
}
