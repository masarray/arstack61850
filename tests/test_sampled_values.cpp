// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/mms/utc_time.hpp"
#include "ariec61850/sampled_values/asdu.hpp"
#include "ariec61850/sampled_values/frame.hpp"
#include "ariec61850/sampled_values/frame_codec.hpp"
#include "ariec61850/sampled_values/payload_inspector.hpp"
#include "ariec61850/sampled_values/pdu_codec.hpp"
#include "ariec61850/sampled_values/quality.hpp"
#include "ariec61850/sampled_values/sample_counter.hpp"
#include "ariec61850/sampled_values/stream_supervisor.hpp"

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

std::string to_hex(const ByteVector& bytes) {
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setfill('0');
    for (const auto byte : bytes) {
        stream << std::setw(2) << static_cast<unsigned>(byte);
    }
    return stream.str();
}

ar::iec61850::sampled_values::SampledValueAsdu make_reference_asdu() {
    using namespace ar::iec61850;
    return {
        "MU01F1/LLN0$MSVCB01",
        "MU01F1/LLN0$PhsMeas1",
        120U,
        3U,
        mms::Iec61850UtcTime{
            std::chrono::system_clock::time_point{std::chrono::seconds{1'781'260'260}},
            0U},
        2U,
        4000U,
        1U,
        from_hex("0000006400000001000000C800000003")};
}

void sampled_values_pdu_matches_csharp_golden_vector() {
    using namespace ar::iec61850::sampled_values;

    const SampledValuesPdu pdu{{make_reference_asdu()}};
    const auto encoded = SampledValuesPduCodec::encode(pdu);
    CHECK(to_hex(encoded) ==
          "605E800101A259305780134D55303146312F4C4C4E30244D535643423031"
          "81144D55303146312F4C4C4E30245068734D65617331820178830103"
          "84086A2BDFE40000000085010286020FA087100000006400000001000000C8"
          "00000003880101");

    SampledValuesPdu decoded;
    CHECK(SampledValuesPduCodec::try_decode(encoded, decoded));
    CHECK(decoded == pdu);
}

void sampled_values_frame_round_trips_vlan_process_bus_header() {
    using namespace ar::iec61850;
    using namespace ar::iec61850::sampled_values;

    const SampledValuesFrame frame{
        ethernet::MacAddress::parse("01:0C:CD:04:00:01"),
        ethernet::MacAddress::parse("02:00:00:00:00:02"),
        ethernet::VlanTag{4U, 200U},
        0x4001U,
        0U,
        0U,
        SampledValuesPdu{{make_reference_asdu()}}};

    const auto encoded = SampledValuesFrameCodec::encode(frame);
    CHECK(to_hex(encoded) ==
          "010CCD040001020000000002810080C888BA4001006800000000"
          "605E800101A259305780134D55303146312F4C4C4E30244D535643423031"
          "81144D55303146312F4C4C4E30245068734D65617331820178830103"
          "84086A2BDFE40000000085010286020FA087100000006400000001000000C8"
          "00000003880101");

    SampledValuesFrame decoded;
    CHECK(SampledValuesFrameCodec::try_decode(encoded, decoded));
    CHECK(decoded == frame);

    auto wrong_ethertype = encoded;
    wrong_ethertype[16] = 0x88U;
    wrong_ethertype[17] = 0xB8U;
    CHECK(!SampledValuesFrameCodec::try_decode(wrong_ethertype, decoded));

    auto impossible_length = encoded;
    impossible_length[20] = 0xFFU;
    impossible_length[21] = 0xFFU;
    CHECK(!SampledValuesFrameCodec::try_decode(impossible_length, decoded));
}

void sampled_values_codec_handles_multiple_asdus_and_rejects_malformed_input() {
    using namespace ar::iec61850::sampled_values;

    auto first = make_reference_asdu();
    auto second = first;
    second.sample_count = 121U;
    second.reference_time.reset();
    second.sample_rate.reset();
    second.sample_mode.reset();
    second.data_set_reference.clear();
    second.sample_payload = {0xAAU};

    const SampledValuesPdu pdu{{first, second}};
    const auto encoded = SampledValuesPduCodec::encode(pdu);
    SampledValuesPdu decoded;
    CHECK(SampledValuesPduCodec::try_decode(encoded, decoded));
    CHECK(decoded == pdu);

    auto count_mismatch = encoded;
    std::size_t count_offset = count_mismatch.size();
    for (std::size_t index = 0U; index + 2U < count_mismatch.size(); ++index) {
        if (count_mismatch[index] == 0x80U &&
            count_mismatch[index + 1U] == 0x01U &&
            count_mismatch[index + 2U] == 0x02U) {
            count_offset = index;
            break;
        }
    }
    CHECK(count_offset < count_mismatch.size());
    count_mismatch[count_offset + 2U] = 0x03U;
    CHECK(!SampledValuesPduCodec::try_decode(count_mismatch, decoded));

    auto wrong_outer = encoded;
    wrong_outer[0] = 0x61U;
    CHECK(!SampledValuesPduCodec::try_decode(wrong_outer, decoded));

    auto trailing = encoded;
    trailing.push_back(0x00U);
    CHECK(!SampledValuesPduCodec::try_decode(trailing, decoded));

    auto wrong_sequence = encoded;
    const auto sequence = std::find(wrong_sequence.begin(), wrong_sequence.end(), 0x30U);
    CHECK(sequence != wrong_sequence.end());
    *sequence = 0x31U;
    CHECK(!SampledValuesPduCodec::try_decode(wrong_sequence, decoded));

    auto bad_timestamp = SampledValuesPduCodec::encode(SampledValuesPdu{{make_reference_asdu()}});
    std::size_t timestamp_offset = bad_timestamp.size();
    for (std::size_t index = 0U; index + 1U < bad_timestamp.size(); ++index) {
        if (bad_timestamp[index] == 0x84U && bad_timestamp[index + 1U] == 0x08U) {
            timestamp_offset = index;
            break;
        }
    }
    CHECK(timestamp_offset < bad_timestamp.size());
    bad_timestamp[timestamp_offset + 1U] = 0x07U;
    CHECK(!SampledValuesPduCodec::try_decode(bad_timestamp, decoded));
}

void sample_counter_tracker_distinguishes_wrap_gap_duplicate_and_order() {
    using namespace ar::iec61850::sampled_values;

    SampleCounterTracker tracker;
    CHECK(tracker.observe(3998U, 4000U).kind == SampleCounterTransitionKind::initial);
    CHECK(tracker.observe(3999U, 4000U).kind == SampleCounterTransitionKind::continuous);
    CHECK(tracker.observe(0U, 4000U).kind == SampleCounterTransitionKind::normal_wrap);

    const auto gap = tracker.observe(3U, 4000U);
    CHECK(gap.kind == SampleCounterTransitionKind::gap);
    CHECK(gap.missing_samples == 2U);
    CHECK(gap.is_anomaly());

    CHECK(tracker.observe(3U, 4000U).kind == SampleCounterTransitionKind::duplicate);
    CHECK(tracker.observe(2U, 4000U).kind == SampleCounterTransitionKind::out_of_order);
    CHECK(tracker.observe(100U, 4000U, true).kind == SampleCounterTransitionKind::restart);

    const auto timestamp = std::chrono::system_clock::time_point{
        std::chrono::seconds{100}} + std::chrono::milliseconds{250};
    CHECK(SampleCounterPolicy::initial_sample_count(
              timestamp, 4000.0, 4000U, SampleCounterMode::second_aligned) == 1000U);
    CHECK(SampleCounterPolicy::initial_sample_count(
              timestamp, 4000.0, 4000U, SampleCounterMode::free_run) == 0U);
    CHECK(SampleCounterPolicy::increment(3999U, 4000U) == 0U);
    CHECK(SampleCounterPolicy::increment(65'535U, std::nullopt) == 0U);
}

void stream_supervisor_tracks_identity_configuration_and_statistics() {
    using namespace ar::iec61850::sampled_values;

    SampledValuesStreamSupervisor supervisor(StreamExpectation{
        std::string{"MU01"},
        std::string{"MU01/LLN0$Dataset1"},
        3U,
        4000U});

    SampledValueAsdu asdu;
    asdu.sv_id = "MU01";
    asdu.data_set_reference = "MU01/LLN0$Dataset1";
    asdu.configuration_revision = 3U;
    asdu.sample_count = 10U;

    CHECK(supervisor.observe(asdu).counter.kind == SampleCounterTransitionKind::initial);
    asdu.sample_count = 11U;
    CHECK(supervisor.observe(asdu).counter.kind == SampleCounterTransitionKind::continuous);
    asdu.sample_count = 13U;
    CHECK(supervisor.observe(asdu).counter.kind == SampleCounterTransitionKind::gap);
    CHECK(supervisor.observe(asdu).counter.kind == SampleCounterTransitionKind::duplicate);
    asdu.sample_count = 12U;
    CHECK(supervisor.observe(asdu).counter.kind == SampleCounterTransitionKind::out_of_order);

    asdu.configuration_revision = 4U;
    asdu.sample_count = 0U;
    const auto restart = supervisor.observe(asdu);
    CHECK(restart.configuration_changed);
    CHECK(restart.counter.kind == SampleCounterTransitionKind::restart);
    CHECK(!restart.identity_matches);
    CHECK(!restart.diagnostics.empty());

    const auto& statistics = supervisor.statistics();
    CHECK(statistics.observations == 6U);
    CHECK(statistics.continuous == 1U);
    CHECK(statistics.gaps == 1U);
    CHECK(statistics.duplicates == 1U);
    CHECK(statistics.out_of_order == 1U);
    CHECK(statistics.restarts == 1U);
    CHECK(statistics.missing_samples == 1U);
    CHECK(statistics.configuration_changes == 1U);
    CHECK(statistics.identity_mismatches == 1U);
}

void quality_and_generic_payload_diagnostics_preserve_wire_evidence() {
    using namespace ar::iec61850::sampled_values;

    const SampledValueQuality quality{
        SampledValueValidity::questionable,
        true,
        false,
        false,
        false,
        true,
        true,
        false,
        false,
        true,
        true};
    CHECK(quality.to_uint32() == 0x000018C7U);
    CHECK(to_hex(quality.to_bytes()) == "000018C7");
    CHECK(SampledValueQuality::from_uint32(quality.to_uint32()) == quality);
    CHECK(SampledValueQuality::from_bytes(quality.to_bytes()) == quality);

    const auto inspection = GenericPayloadInspector::inspect(
        from_hex("000003E8FFFFFC18"));
    CHECK(inspection.payload_length == 8U);
    CHECK(inspection.complete_word_count == 2U);
    CHECK(inspection.has_eight_byte_group_shape);
    CHECK(inspection.words[0].signed_int32 == 1000);
    CHECK(inspection.words[1].signed_int32 == -1000);
    CHECK(inspection.words[0].structural_role ==
          GenericPayloadWordRole::first_word_in_eight_byte_group);
    CHECK(inspection.words[1].structural_role ==
          GenericPayloadWordRole::second_word_in_eight_byte_group);
    CHECK(inspection.diagnostics.size() >= 2U);

    const auto trailing = GenericPayloadInspector::inspect(
        from_hex("00000001AABB"));
    CHECK(!trailing.is_four_byte_aligned);
    CHECK(trailing.complete_word_count == 1U);
    CHECK(trailing.trailing_bytes == ByteVector({0xAAU, 0xBBU}));
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"SV PDU golden vector", sampled_values_pdu_matches_csharp_golden_vector},
        {"SV Ethernet frame", sampled_values_frame_round_trips_vlan_process_bus_header},
        {"SV malformed input", sampled_values_codec_handles_multiple_asdus_and_rejects_malformed_input},
        {"SV sample counter", sample_counter_tracker_distinguishes_wrap_gap_duplicate_and_order},
        {"SV stream supervisor", stream_supervisor_tracks_identity_configuration_and_statistics},
        {"SV quality and payload diagnostics", quality_and_generic_payload_diagnostics_preserve_wire_evidence}};

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
              << " Sampled Values tests.\n";
    return 0;
}
