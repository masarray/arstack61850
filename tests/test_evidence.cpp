// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/capture/pcap.hpp"
#include "ariec61850/evidence/pcap_equivalence.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <fstream>
#include <iterator>
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
    for (std::size_t index = 0U; index < text.size(); index += 2U) {
        result.push_back(static_cast<std::uint8_t>(
            std::stoul(text.substr(index, 2U), nullptr, 16)));
    }
    return result;
}


ByteVector read_binary_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Unable to read fixture: " + path.string());
    }
    const std::string bytes{
        std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    ByteVector result;
    result.reserve(bytes.size());
    for (const char value : bytes) {
        result.push_back(static_cast<std::uint8_t>(
            static_cast<unsigned char>(value)));
    }
    return result;
}

bool pcap_read_throws(const ByteVector& bytes) {
    const std::string input{
        reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    std::istringstream stream{input, std::ios::in | std::ios::binary};
    try {
        static_cast<void>(ar::iec61850::capture::PcapReader::read_all(stream));
        return false;
    } catch (const ar::iec61850::capture::PcapFormatError&) {
        return true;
    }
}

ByteVector goose_frame() {
    return from_hex(
        "010CCD0100010200000000018100806488B81001005E00000000615480104C44302F4C4C4E30"
        "24474F2467636231810203E8820C4C44302F4C4C4E30244453318306474F4F53453184086A2B"
        "77254000000A8501038601098701008801028901018A0103AB0A8301018501FD8A024F4B");
}

ByteVector sampled_values_frame() {
    return from_hex(
        "010CCD040001020000000002810080C888BA4001006800000000"
        "605E800101A259305780134D55303146312F4C4C4E30244D535643423031"
        "81144D55303146312F4C4C4E30245068734D65617331820178830103"
        "84086A2BDFE40000000085010286020FA087100000006400000001000000C8"
        "00000003880101");
}

std::vector<ar::iec61850::capture::PcapPacket> reference_packets() {
    using Clock = std::chrono::system_clock;
    return {
        {Clock::time_point{std::chrono::seconds{1'781'233'445}} +
             std::chrono::microseconds{250'000}, goose_frame()},
        {Clock::time_point{std::chrono::seconds{1'781'260'260}} +
             std::chrono::microseconds{125'000}, sampled_values_frame()}};
}

void pcap_reader_writer_preserves_canonical_process_bus_evidence() {
    using namespace ar::iec61850;
    const auto packets = reference_packets();
    std::stringstream stream{std::ios::in | std::ios::out | std::ios::binary};
    capture::PcapWriter writer{stream};
    for (const auto& packet : packets) {
        writer.write_packet(packet.timestamp, packet.frame);
    }
    stream.seekg(0, std::ios::beg);
    const auto decoded = capture::PcapReader::read_all(stream);
    CHECK(decoded.size() == packets.size());
    CHECK(decoded[0].frame == packets[0].frame);
    CHECK(decoded[1].frame == packets[1].frame);
    CHECK(evidence::PcapEquivalenceAnalyzer::canonical_pcap_round_trip_matches(decoded));
}

void pcap_analyzer_reports_exact_goose_and_sv_round_trips() {
    using namespace ar::iec61850;
    const auto report = evidence::PcapEquivalenceAnalyzer::analyze(reference_packets());
    CHECK(report.passed());
    CHECK(report.packet_count == 2U);
    CHECK(report.process_bus_packet_count == 2U);
    CHECK(report.goose_packet_count == 1U);
    CHECK(report.sampled_values_packet_count == 1U);
    CHECK(report.malformed_packet_count == 0U);
    CHECK(report.reencode_mismatch_count == 0U);
    CHECK(report.exact_reencode_match_count == 2U);
    CHECK(report.canonical_pcap_round_trip_match);
    CHECK(report.packets[0].stream_identifier == "LD0/LLN0$GO$gcb1");
    CHECK(report.packets[1].stream_identifier == "MU01F1/LLN0$MSVCB01");

    const auto json = evidence::PcapEquivalenceAnalyzer::to_json(report);
    CHECK(json.find("\"passed\": true") != std::string::npos);
    CHECK(json.find("GOOSE") != std::string::npos);
    CHECK(json.find("Sampled Values") != std::string::npos);
}

void pcap_analyzer_rejects_malformed_process_bus_packets() {
    using namespace ar::iec61850;
    auto packets = reference_packets();
    packets[0].frame[17] = 0xBAU;
    const auto report = evidence::PcapEquivalenceAnalyzer::analyze(packets);
    CHECK(!report.passed());
    CHECK(report.sampled_values_packet_count == 2U);
    CHECK(report.malformed_packet_count == 1U);
    CHECK(!report.packets[0].decoded);
    CHECK(!report.packets[0].diagnostics.empty());
}


void pcap_reader_rejects_unbounded_allocation_lengths() {
    const auto fixture_path = std::filesystem::path{ARIEC61850_SOURCE_DIR} /
        "tests/fixtures/process_bus_csharp_vectors.pcap";
    const auto fixture = read_binary_file(fixture_path);
    CHECK(fixture.size() > 40U);

    auto excessive_snap_length = fixture;
    excessive_snap_length[16] = 0xFFU;
    excessive_snap_length[17] = 0xFFU;
    excessive_snap_length[18] = 0xFFU;
    excessive_snap_length[19] = 0x7FU;
    CHECK(pcap_read_throws(excessive_snap_length));

    auto excessive_packet_length = fixture;
    excessive_packet_length[32] = 0x00U;
    excessive_packet_length[33] = 0x00U;
    excessive_packet_length[34] = 0x01U;
    excessive_packet_length[35] = 0x00U;
    CHECK(pcap_read_throws(excessive_packet_length));
}

void pcap_analyzer_requires_at_least_one_process_bus_packet() {
    using namespace ar::iec61850;
    capture::PcapPacket packet;
    packet.timestamp = std::chrono::system_clock::time_point{};
    packet.frame = from_hex("FFFFFFFFFFFF020000000003080001020304");
    const auto report = evidence::PcapEquivalenceAnalyzer::analyze(
        std::vector<capture::PcapPacket>{packet});
    CHECK(!report.passed());
    CHECK(report.other_ethernet_packet_count == 1U);
    CHECK(report.process_bus_packet_count == 0U);
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"PCAP canonical round trip", pcap_reader_writer_preserves_canonical_process_bus_evidence},
        {"GOOSE and SV PCAP equivalence", pcap_analyzer_reports_exact_goose_and_sv_round_trips},
        {"Malformed process-bus evidence", pcap_analyzer_rejects_malformed_process_bus_packets},
        {"PCAP allocation bounds", pcap_reader_rejects_unbounded_allocation_lengths},
        {"Require process-bus evidence", pcap_analyzer_requires_at_least_one_process_bus_packet}};

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
    std::cout << "Passed " << passed << '/' << tests.size()
              << " evidence tests.\n";
    return 0;
}
