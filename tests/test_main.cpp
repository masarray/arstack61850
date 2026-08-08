// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/asn1/ber.hpp"
#include "ariec61850/capture/pcap.hpp"
#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/goose/retransmission_schedule.hpp"
#include "ariec61850/mms/live_model.hpp"

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

#define CHECK(condition) do { \
    if (!(condition)) { \
        throw std::runtime_error(std::string{"CHECK failed: "} + #condition + \
                                 " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
    } \
} while (false)

ByteVector from_hex(const std::string& text) {
    if ((text.size() % 2U) != 0U) {
        throw std::invalid_argument("Hex text must contain an even number of characters.");
    }
    ByteVector result;
    result.reserve(text.size() / 2U);
    for (std::size_t index = 0; index < text.size(); index += 2U) {
        result.push_back(static_cast<std::uint8_t>(std::stoul(text.substr(index, 2U), nullptr, 16)));
    }
    return result;
}

std::string to_hex(const ByteVector& bytes) {
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setfill('0');
    for (const auto byte : bytes) {
        stream << std::setw(2) << static_cast<unsigned>(byte);
    }
    return stream.str();
}

void ber_long_form_length_round_trip() {
    using namespace ar::iec61850::asn1;
    ByteVector value(130U);
    for (std::size_t index = 0; index < value.size(); ++index) {
        value[index] = static_cast<std::uint8_t>(index);
    }

    const auto encoded = BerWriter::encode_tlv(BerClass::context_specific, false, 3, value);
    CHECK(encoded[0] == 0x83U);
    CHECK(encoded[1] == 0x81U);
    CHECK(encoded[2] == 130U);

    std::size_t offset = 0;
    BerTlv tlv;
    CHECK(BerReader::try_read_tlv(encoded, offset, tlv));
    CHECK(tlv.tag_class == BerClass::context_specific);
    CHECK(tlv.tag_number == 3);
    CHECK(tlv.value == value);
    CHECK(offset == encoded.size());
}

void ber_signed_integer_encoding_is_minimal() {
    using namespace ar::iec61850::asn1;
    const std::vector<std::pair<std::int64_t, std::string>> cases{
        {0, "00"}, {127, "7F"}, {128, "0080"}, {-1, "FF"}, {-129, "FF7F"}};

    for (const auto& [value, expected] : cases) {
        const auto encoded = BerWriter::encode_signed_integer(value);
        CHECK(to_hex(encoded) == expected);
        const auto wrapped = BerWriter::encode_tlv(0x85U, encoded);
        std::size_t offset = 0;
        BerTlv tlv;
        CHECK(BerReader::try_read_tlv(wrapped, offset, tlv));
        CHECK(BerReader::read_signed_integer(tlv).value() == value);
    }
}

void ber_context_string_round_trip() {
    using namespace ar::iec61850::asn1;
    const auto text = std::string{"IED1LD0/LLN0$GO$gcb01"};
    const auto encoded = BerWriter::encode_tlv(0x80U, BerWriter::encode_ascii(text));
    std::size_t offset = 0;
    BerTlv tlv;
    CHECK(BerReader::try_read_tlv(encoded, offset, tlv));
    CHECK(tlv.tag_class == BerClass::context_specific);
    CHECK(!tlv.constructed);
    CHECK(tlv.tag_number == 0);
    CHECK(BerReader::read_ascii_string(tlv) == text);
}

void ber_high_tag_round_trip_and_malformed_rejection() {
    using namespace ar::iec61850::asn1;
    const ByteVector value{0xAAU, 0x55U};
    const auto encoded = BerWriter::encode_tlv(BerClass::application, true, 201, value);
    std::size_t offset = 0;
    BerTlv tlv;
    CHECK(BerReader::try_read_tlv(encoded, offset, tlv));
    CHECK(tlv.tag_number == 201);
    CHECK(tlv.constructed);
    CHECK(tlv.value == value);

    const ByteVector malformed{0x9FU, 0x81U};
    offset = 0;
    CHECK(!BerReader::try_read_tlv(malformed, offset, tlv));
}

void mac_address_value_semantics() {
    using ar::iec61850::ethernet::MacAddress;
    const auto colon = MacAddress::parse("01:0C:CD:04:00:02");
    const auto hyphen = MacAddress::parse("01-0c-cd-04-00-02");
    const auto other = MacAddress::parse("01:0C:CD:04:00:01");
    CHECK(colon == hyphen);
    CHECK(colon != other);
    CHECK(colon.to_string() == "01:0C:CD:04:00:02");
}

void ethernet_vlan_and_process_bus_round_trip() {
    using namespace ar::iec61850::ethernet;
    const auto destination = MacAddress::parse("01:0C:CD:04:00:02");
    const auto source = MacAddress::parse("02:00:00:00:00:01");
    const ByteVector apdu{0x61U, 0x02U, 0x80U, 0x00U};
    const auto ethernet = ProcessBusFrameCodec::encode_ethernet_frame(
        destination, source, goose_ethertype, 0x1001U, apdu, VlanTag{4U, false, 100U});
    const auto bytes = EthernetFrameCodec::encode(ethernet);

    EthernetFrame decoded_ethernet;
    CHECK(EthernetFrameCodec::try_decode(bytes, decoded_ethernet));
    CHECK(decoded_ethernet.destination == destination);
    CHECK(decoded_ethernet.source == source);
    CHECK(decoded_ethernet.ether_type == goose_ethertype);
    CHECK(decoded_ethernet.vlan.has_value());
    CHECK(decoded_ethernet.vlan->priority_code_point == 4U);
    CHECK(decoded_ethernet.vlan->vlan_id == 100U);

    ProcessBusFrame decoded_process_bus;
    CHECK(ProcessBusFrameCodec::try_decode(decoded_ethernet, decoded_process_bus));
    CHECK(decoded_process_bus.app_id == 0x1001U);
    CHECK(decoded_process_bus.apdu == apdu);
}

void pcap_writer_produces_classic_ethernet_pcap_and_round_trips() {
    using namespace ar::iec61850::capture;
    const auto first_frame = from_hex("010CCD01000102000000000188B80000");
    const auto second_frame = from_hex("010CCD04000102000000000188BA0001");
    const auto first_timestamp = std::chrono::system_clock::time_point{
        std::chrono::seconds{1'781'270'400}} + std::chrono::milliseconds{123};
    const auto second_timestamp = first_timestamp + std::chrono::milliseconds{1};

    std::ostringstream output(std::ios::binary);
    PcapWriter writer(output);
    writer.write_packet(first_timestamp, first_frame);
    writer.write_packet(second_timestamp, second_frame);
    const auto data = output.str();
    CHECK(data.size() > 24U);
    CHECK(static_cast<unsigned char>(data[0]) == 0xD4U);
    CHECK(static_cast<unsigned char>(data[1]) == 0xC3U);
    CHECK(static_cast<unsigned char>(data[2]) == 0xB2U);
    CHECK(static_cast<unsigned char>(data[3]) == 0xA1U);
    CHECK(static_cast<unsigned char>(data[20]) == 0x01U);

    std::istringstream input(data, std::ios::binary);
    const auto packets = PcapReader::read_all(input);
    CHECK(packets.size() == 2U);
    CHECK(packets[0].timestamp == first_timestamp);
    CHECK(packets[0].frame == first_frame);
    CHECK(packets[1].timestamp == second_timestamp);
    CHECK(packets[1].frame == second_frame);
}

void goose_retransmission_schedule_matches_csharp_behavior() {
    using ar::iec61850::goose::RetransmissionSchedule;
    RetransmissionSchedule schedule{4U, 1000U};
    const std::vector<int> expected{4, 8, 16, 32, 64, 128, 256, 512, 1000, 1000};
    for (const auto value : expected) {
        CHECK(schedule.next_delay_milliseconds() == value);
    }

    RetransmissionSchedule defaults{0U, 0U};
    CHECK(defaults.min_time_milliseconds() == 4);
    CHECK(defaults.max_time_milliseconds() == 1000);
    CHECK(defaults.next_delay_milliseconds() == 4);
    defaults.reset();
    CHECK(defaults.next_delay_milliseconds() == 4);
}

void live_type_templates_and_variable_type_projection_match_csharp_shape() {
    using namespace ar::iec61850::mms;

    MmsLiveDiscoveryResult discovery;
    discovery.endpoint = {"127.0.0.1", 102U};
    discovery.names.domain_variables["TESTIEDLD0"] = {
        "LLN0$ST$Mod$stVal",
        "LLN0$ST$Mod$q",
        "LLN0$ST$Mod$t"};
    discovery.report_inventory = MmsReportInventoryBuilder::build(discovery.names);

    MmsVariableTypeEvidence type_evidence;
    type_evidence.variable = MmsObjectName::domain_specific(
        "TESTIEDLD0", "LLN0$ST$Mod$stVal");
    MmsVariableAccessAttributesResponse attributes;
    attributes.mms_deletable = false;
    attributes.type.kind = MmsTypeKind::boolean;
    type_evidence.attributes = attributes;
    discovery.variable_types.push_back(type_evidence);

    const auto model = MmsLiveModelBuilder::build(discovery);
    CHECK(model.logical_devices.size() == 1U);
    CHECK(model.logical_devices[0].logical_nodes.size() == 1U);
    const auto& ln = model.logical_devices[0].logical_nodes[0];
    CHECK(ln.name == "LLN0");
    CHECK(ln.proposed_type_id == "LN_LLN0_LLN0");
    CHECK(ln.data_objects.size() == 1U);
    const auto& object = ln.data_objects[0];
    CHECK(object.name == "Mod");
    CHECK(object.proposed_do_type_id == "DO_INC_LLN0_Mod");

    CHECK(model.type_templates.size() == 2U);
    CHECK(model.type_templates[0].template_kind == "LNodeType");
    CHECK(model.type_templates[0].id == "LN_LLN0_LLN0");
    CHECK(model.type_templates[0].members.size() == 1U);
    CHECK(model.type_templates[0].members[0] == "Mod");
    CHECK(model.type_templates[1].template_kind == "DOType");
    CHECK(model.type_templates[1].id == "DO_INC_LLN0_Mod");
    CHECK(!model.type_templates[1].members.empty());

    CHECK(model.variable_type_discoveries.size() == 1U);
    const auto& type = model.variable_type_discoveries[0];
    CHECK(type.success);
    CHECK(type.domain == "TESTIEDLD0");
    CHECK(type.mms_item_name == "LLN0$ST$Mod$stVal");
    CHECK(type.functional_constraint == "ST");
    CHECK(!type.mms_type.empty());
    CHECK(type.scl_basic_type == "BOOLEAN");
    CHECK(type.mms_deletable.has_value());
    CHECK(!*type.mms_deletable);

    const auto json = model.to_json();
    CHECK(json.find("\"proposedLnTypeId\":\"LN_LLN0_LLN0\"") != std::string::npos);
    CHECK(json.find("\"proposedDoTypeId\":\"DO_INC_LLN0_Mod\"") != std::string::npos);
    CHECK(json.find("\"typeTemplates\":[") != std::string::npos);
    CHECK(json.find("\"variableTypeDiscoveries\":[") != std::string::npos);
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"BER long-form length", ber_long_form_length_round_trip},
        {"BER signed integer", ber_signed_integer_encoding_is_minimal},
        {"BER context string", ber_context_string_round_trip},
        {"BER high tag", ber_high_tag_round_trip_and_malformed_rejection},
        {"MAC value semantics", mac_address_value_semantics},
        {"Ethernet/VLAN/process bus", ethernet_vlan_and_process_bus_round_trip},
        {"PCAP round trip", pcap_writer_produces_classic_ethernet_pcap_and_round_trips},
        {"GOOSE retransmission schedule", goose_retransmission_schedule_matches_csharp_behavior},
        {"C# live type projections", live_type_templates_and_variable_type_projection_match_csharp_shape}};

    std::size_t passed = 0;
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

    std::cout << "Passed " << passed << "/" << tests.size() << " tests.\n";
    return 0;
}