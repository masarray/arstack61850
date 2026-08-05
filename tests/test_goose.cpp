// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/goose/frame_codec.hpp"
#include "ariec61850/goose/pdu_codec.hpp"
#include "ariec61850/mms/data_value.hpp"
#include "ariec61850/mms/utc_time.hpp"

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
#include <variant>
#include <vector>

namespace {
using ByteVector = std::vector<std::uint8_t>;
#define CHECK(condition) do { if (!(condition)) { throw std::runtime_error( \
    std::string{"CHECK failed: "} + #condition + " at " + __FILE__ + ":" + \
    std::to_string(__LINE__)); } } while (false)

ByteVector from_hex(const std::string& text) {
    if ((text.size() % 2U) != 0U) throw std::invalid_argument("Hex length must be even.");
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
    for (const auto byte : bytes) stream << std::setw(2) << static_cast<unsigned>(byte);
    return stream.str();
}

ar::iec61850::goose::GoosePdu make_pdu() {
    using namespace ar::iec61850;
    goose::GoosePdu pdu;
    pdu.go_cb_ref = "LD0/LLN0$GO$gcb1";
    pdu.time_allowed_to_live_milliseconds = 1000U;
    pdu.data_set_reference = "LD0/LLN0$DS1";
    pdu.go_id = "GOOSE1";
    pdu.timestamp = mms::Iec61850UtcTime{
        std::chrono::system_clock::time_point{std::chrono::seconds{1'781'233'445}} +
            std::chrono::milliseconds{250}, 0x0AU};
    pdu.state_number = 3U;
    pdu.sequence_number = 9U;
    pdu.test = false;
    pdu.configuration_revision = 2U;
    pdu.needs_commissioning = true;
    pdu.values = {
        mms::MmsDataValue::boolean(true),
        mms::MmsDataValue::integer(-3),
        mms::MmsDataValue::visible_string("OK")};
    return pdu;
}

void pdu_matches_csharp_vector_and_roundtrips() {
    using namespace ar::iec61850;
    const auto encoded = goose::GoosePduCodec::encode(make_pdu());
    CHECK(to_hex(encoded) ==
        "615480104C44302F4C4C4E3024474F2467636231810203E8820C4C44302F4C4C4E3024445331"
        "8306474F4F53453184086A2B77254000000A8501038601098701008801028901018A0103AB0A"
        "8301018501FD8A024F4B");

    goose::GoosePdu decoded;
    CHECK(goose::GoosePduCodec::try_decode(encoded, decoded));
    CHECK(decoded.go_cb_ref == "LD0/LLN0$GO$gcb1");
    CHECK(decoded.time_allowed_to_live_milliseconds == 1000U);
    CHECK(decoded.data_set_reference == "LD0/LLN0$DS1");
    CHECK(decoded.go_id == "GOOSE1");
    CHECK(decoded.state_number == 3U);
    CHECK(decoded.sequence_number == 9U);
    CHECK(!decoded.test);
    CHECK(decoded.configuration_revision == 2U);
    CHECK(decoded.needs_commissioning);
    CHECK(decoded.values.size() == 3U);
    CHECK(std::get<bool>(decoded.values[0].value()));
    CHECK(std::get<std::int64_t>(decoded.values[1].value()) == -3);
    CHECK(std::get<std::string>(decoded.values[2].value()) == "OK");
}

void ethernet_frame_matches_vector_and_roundtrips() {
    using namespace ar::iec61850;
    goose::GooseFrame frame;
    frame.destination = ethernet::MacAddress::parse("01:0C:CD:01:00:01");
    frame.source = ethernet::MacAddress::parse("02:00:00:00:00:01");
    frame.vlan = ethernet::VlanTag{4U, 100U};
    frame.app_id = 0x1001U;
    frame.pdu = make_pdu();

    const auto encoded = goose::GooseFrameCodec::encode(frame);
    CHECK(to_hex(encoded) ==
        "010CCD0100010200000000018100806488B81001005E00000000615480104C44302F4C4C4E30"
        "24474F2467636231810203E8820C4C44302F4C4C4E30244453318306474F4F53453184086A2B"
        "77254000000A8501038601098701008801028901018A0103AB0A8301018501FD8A024F4B");

    goose::GooseFrame decoded;
    CHECK(goose::GooseFrameCodec::try_decode(encoded, decoded));
    CHECK(decoded.destination == frame.destination);
    CHECK(decoded.source == frame.source);
    CHECK(decoded.vlan.has_value());
    CHECK(decoded.vlan.value() == frame.vlan.value());
    CHECK(decoded.app_id == 0x1001U);
    CHECK(decoded.pdu.sequence_number == 9U);
    CHECK(decoded.pdu.values.size() == 3U);
}

void mismatched_data_set_count_is_rejected() {
    using namespace ar::iec61850;
    auto encoded = goose::GoosePduCodec::encode(make_pdu());
    const ByteVector marker{0x8AU, 0x01U, 0x03U};
    const auto position = std::search(encoded.begin(), encoded.end(), marker.begin(), marker.end());
    CHECK(position != encoded.end());
    *(position + 2) = 0x04U;
    goose::GoosePdu decoded;
    CHECK(!goose::GoosePduCodec::try_decode(encoded, decoded));
}

void malformed_pdu_and_wrong_ethertype_are_rejected() {
    using namespace ar::iec61850;
    goose::GoosePdu decoded_pdu;
    CHECK(!goose::GoosePduCodec::try_decode(from_hex("610584036A2B77"), decoded_pdu));
    CHECK(!goose::GoosePduCodec::try_decode(from_hex("3000"), decoded_pdu));

    goose::GooseFrame frame;
    frame.destination = ethernet::MacAddress::parse("01:0C:CD:01:00:01");
    frame.source = ethernet::MacAddress::parse("02:00:00:00:00:01");
    frame.vlan = ethernet::VlanTag{4U, 100U};
    frame.app_id = 0x1001U;
    frame.pdu = make_pdu();
    auto bytes = goose::GooseFrameCodec::encode(frame);
    bytes[16] = 0x88U;
    bytes[17] = 0xBAU;
    goose::GooseFrame decoded_frame;
    CHECK(!goose::GooseFrameCodec::try_decode(bytes, decoded_frame));
}
}

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"GOOSE PDU golden vector", pdu_matches_csharp_vector_and_roundtrips},
        {"GOOSE Ethernet golden vector", ethernet_frame_matches_vector_and_roundtrips},
        {"GOOSE dataset count validation", mismatched_data_set_count_is_rejected},
        {"GOOSE malformed input", malformed_pdu_and_wrong_ethertype_are_rejected}};
    std::size_t passed = 0U;
    for (const auto& [name, test] : tests) {
        try { test(); ++passed; std::cout << "[PASS] " << name << '\n'; }
        catch (const std::exception& error) {
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n'; return 1;
        }
    }
    std::cout << "Passed " << passed << "/" << tests.size() << " GOOSE tests.\n";
    return 0;
}
