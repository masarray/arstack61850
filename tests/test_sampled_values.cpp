// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/integrity/crc32.hpp"
#include "ariec61850/mms/utc_time.hpp"
#include "ariec61850/sampled_values/asdu.hpp"
#include "ariec61850/sampled_values/compiled_device_profile.hpp"
#include "../embedded/esp32p4_smv_injector/main/binary_profile_staging.hpp"
#include "../embedded/esp32p4_smv_injector/main/compiled_profile_adapter.hpp"
#include "ariec61850/sampled_values/frame.hpp"
#include "ariec61850/sampled_values/frame_codec.hpp"
#include "ariec61850/sampled_values/payload_inspector.hpp"
#include "ariec61850/sampled_values/pdu_codec.hpp"
#include "ariec61850/sampled_values/quality.hpp"
#include "ariec61850/sampled_values/sample_counter.hpp"
#include "ariec61850/sampled_values/stream_supervisor.hpp"

#include <algorithm>
#include <array>
#include <bit>
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
        std::uint16_t{4000U},
        std::uint16_t{1U},
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
    std::vector<std::uint8_t> encoded_with_layout(encoded.size());
    std::array<SampledValueAsduEncodeLayout, 1> frame_layout{};
    const auto layout_result = SampledValuesFrameCodec::encode_into_with_layout(
        frame, encoded_with_layout, frame_layout);
    CHECK(layout_result.success());
    CHECK(layout_result.bytes_written == encoded.size());
    CHECK(encoded_with_layout == encoded);
    CHECK(frame_layout[0].sample_count.present);
    CHECK(frame_layout[0].sample_count.value_size == 1U);
    CHECK(encoded[frame_layout[0].sample_count.value_offset] == 120U);
    CHECK(frame_layout[0].configuration_revision.present);
    CHECK(frame_layout[0].configuration_revision.value_size == 1U);
    CHECK(encoded[frame_layout[0].configuration_revision.value_offset] == 3U);
    CHECK(frame_layout[0].reference_time.present);
    CHECK(frame_layout[0].reference_time.value_size == 8U);
    CHECK(frame_layout[0].sample_synchronization.present);
    CHECK(frame_layout[0].sample_synchronization.value_size == 1U);
    CHECK(encoded[frame_layout[0].sample_synchronization.value_offset] == 2U);
    CHECK(frame_layout[0].sample_rate.present);
    CHECK(frame_layout[0].sample_payload.present);
    CHECK(frame_layout[0].sample_payload.value_size == 16U);
    CHECK(frame_layout[0].sample_mode.present);

    auto untagged_frame = frame;
    untagged_frame.vlan.reset();
    const auto untagged_size = SampledValuesFrameCodec::encoded_size(untagged_frame);
    CHECK(untagged_size.has_value());
    std::vector<std::uint8_t> untagged_bytes(*untagged_size);
    std::array<SampledValueAsduEncodeLayout, 1> untagged_layout{};
    CHECK(SampledValuesFrameCodec::encode_into_with_layout(
              untagged_frame, untagged_bytes, untagged_layout).success());
    CHECK(frame_layout[0].sample_count.value_offset ==
          untagged_layout[0].sample_count.value_offset + 4U);
    CHECK(frame_layout[0].sample_payload.value_offset ==
          untagged_layout[0].sample_payload.value_offset + 4U);

    CHECK(to_hex(encoded) ==
          "010CCD040001020000000002810080C888BA4001006800000000"
          "605E800101A259305780134D55303146312F4C4C4E30244D535643423031"
          "81144D55303146312F4C4C4E30245068734D65617331820178830103"
          "84086A2BDFE40000000085010286020FA087100000006400000001000000C8"
          "00000003880101");

    SampledValuesFrame decoded;
    CHECK(SampledValuesFrameCodec::try_decode(encoded, decoded));
    CHECK(decoded == frame);

    auto priority_tagged = frame;
    priority_tagged.vlan = ethernet::VlanTag{4U, 0U};
    CHECK(SampledValuesFrameCodec::encoded_size(priority_tagged).has_value());

    auto maximum_vlan = frame;
    maximum_vlan.vlan = ethernet::VlanTag{7U, 4094U};
    CHECK(SampledValuesFrameCodec::encoded_size(maximum_vlan).has_value());

    auto reserved_vlan = frame;
    reserved_vlan.vlan = ethernet::VlanTag{4U, 4095U};
    CHECK(!SampledValuesFrameCodec::encoded_size(reserved_vlan).has_value());
    bool reserved_vlan_rejected = false;
    try {
        static_cast<void>(SampledValuesFrameCodec::encode(reserved_vlan));
    } catch (const std::out_of_range&) {
        reserved_vlan_rejected = true;
    }
    CHECK(reserved_vlan_rejected);

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
    std::vector<std::uint8_t> encoded_with_layout(encoded.size());
    std::array<SampledValueAsduEncodeLayout, 2> layouts{};
    const auto layout_result = SampledValuesPduCodec::encode_into_with_layout(
        pdu, encoded_with_layout, layouts);
    CHECK(layout_result.success());
    CHECK(encoded_with_layout == encoded);
    CHECK(layouts[0].sample_count.present);
    CHECK(layouts[1].sample_count.present);
    CHECK(layouts[0].sample_count.value_offset < layouts[1].sample_count.value_offset);
    CHECK(layouts[0].reference_time.present);
    CHECK(layouts[0].sample_rate.present);
    CHECK(layouts[0].sample_mode.present);
    CHECK(!layouts[1].reference_time.present);
    CHECK(!layouts[1].sample_rate.present);
    CHECK(!layouts[1].sample_mode.present);
    CHECK(layouts[0].sample_payload.present);
    CHECK(layouts[1].sample_payload.present);
    CHECK(layouts[0].sample_payload.value_size == first.sample_payload.size());
    CHECK(layouts[1].sample_payload.value_size == second.sample_payload.size());

    std::array<SampledValueAsduEncodeLayout, 1> insufficient_layout{};
    CHECK(!SampledValuesPduCodec::encode_into_with_layout(
              pdu, encoded_with_layout, insufficient_layout).success());

    auto patched = encoded_with_layout;
    CHECK(layouts[0].sample_count.value_size == 1U);
    CHECK(layouts[1].sample_count.value_size == 1U);
    patched[layouts[0].sample_count.value_offset] = 122U;
    patched[layouts[1].sample_count.value_offset] = 123U;
    SampledValuesPdu patched_decoded;
    CHECK(SampledValuesPduCodec::try_decode(patched, patched_decoded));
    CHECK(patched_decoded.asdus.size() == 2U);
    CHECK(patched_decoded.asdus[0].sample_count == 122U);
    CHECK(patched_decoded.asdus[1].sample_count == 123U);

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

void compiled_device_profile_binary_is_canonical_and_integrity_checked() {
    using namespace ar::iec61850::sampled_values;

    constexpr std::array<std::uint8_t, 9> crc_reference{
        '1','2','3','4','5','6','7','8','9'};
    CHECK(ar::iec61850::integrity::crc32(crc_reference) == 0xCBF43926U);

    CompiledSvDeviceProfile profile;
    profile.profile_family = SvProfileFamily::iec61850_9_2;
    profile.transport_mode = SvTransportMode::multicast;
    profile.destination_mac = {0x01U, 0x0CU, 0xCDU, 0x04U, 0x00U, 0x01U};
    profile.app_id = 0x4000U;
    profile.vlan_present = true;
    profile.vlan_id = 100U;
    profile.vlan_priority = 4U;
    profile.configuration_revision = 3U;
    profile.sampling_basis = SvSampleMode::samples_per_second;
    profile.configured_sample_rate = 4000U;
    profile.frame_rate_hz = 4000U;
    profile.sample_counter_modulus = 4000U;
    profile.no_asdu = 1U;
    profile.asdu_options.element_present = true;
    profile.asdu_options.data_set = true;
    profile.asdu_options.sample_synchronized = true;
    profile.payload_size_bytes = 8U;

    constexpr std::string_view sv_id{"SV1"};
    std::copy(sv_id.begin(), sv_id.end(), profile.sv_id.begin());
    profile.sv_id_length = static_cast<std::uint16_t>(sv_id.size());
    constexpr std::string_view data_set{"DS"};
    std::copy(data_set.begin(), data_set.end(), profile.data_set_reference.begin());
    profile.data_set_reference_length =
        static_cast<std::uint16_t>(data_set.size());

    profile.leaf_count = 2U;
    profile.leaves[0] = {SvDeviceWireType::int32, 4U, false, false};
    profile.leaves[1] = {SvDeviceWireType::quality, 4U, true, false};

    const auto size = SvDeviceProfileBinaryCodec::encoded_size(profile);
    CHECK(size == std::optional<std::size_t>{75U});
    ByteVector encoded(*size);
    const auto encoded_result =
        SvDeviceProfileBinaryCodec::encode_into(profile, encoded);
    CHECK(encoded_result.success());
    CHECK(encoded_result.bytes == encoded.size());
    CHECK(to_hex(encoded) ==
          "41525356000100140000004B00000037AAEB211E"
          "0101008B010CCD040001400000640401000100000003"
          "00000FA000000FA00FA000020000000800030002"
          "060000040C0100045356314453");

    CompiledSvDeviceProfile decoded;
    const auto decoded_result = SvDeviceProfileBinaryCodec::decode(encoded, decoded);
    CHECK(decoded_result.success());
    CHECK(decoded_result.bytes == encoded.size());
    CHECK(decoded == profile);

    // No unknown enum may be silently encoded into a valid different mode.
    auto unknown_family = profile;
    unknown_family.profile_family = static_cast<SvProfileFamily>(0xFFU);
    CHECK(!SvDeviceProfileBinaryCodec::encoded_size(unknown_family).has_value());
    CHECK(SvDeviceProfileBinaryCodec::encode_into(unknown_family, encoded).status ==
          SvDeviceProfileCodecStatus::invalid_value);
    auto unknown_transport = profile;
    unknown_transport.transport_mode = static_cast<SvTransportMode>(0xFFU);
    CHECK(!SvDeviceProfileBinaryCodec::encoded_size(unknown_transport).has_value());
    CHECK(SvDeviceProfileBinaryCodec::encode_into(unknown_transport, encoded).status ==
          SvDeviceProfileCodecStatus::invalid_value);
    auto unknown_basis = profile;
    unknown_basis.sampling_basis = static_cast<SvSampleMode>(0xFFU);
    CHECK(!SvDeviceProfileBinaryCodec::encoded_size(unknown_basis).has_value());
    CHECK(SvDeviceProfileBinaryCodec::encode_into(unknown_basis, encoded).status ==
          SvDeviceProfileCodecStatus::invalid_value);

    // Valid-CRC, late-invalid descriptor must never expose a partial output.
    auto late_invalid = encoded;
    constexpr std::size_t first_leaf_flags =
        SvDeviceProfileBinaryCodec::header_bytes +
        SvDeviceProfileBinaryCodec::fixed_payload_bytes + 1U;
    late_invalid[first_leaf_flags] = 0x80U;
    std::fill(late_invalid.begin() + 16U, late_invalid.begin() + 20U,
              std::uint8_t{0U});
    const auto repaired_crc = ar::iec61850::integrity::crc32(late_invalid);
    for (std::size_t i = 0; i < 4U; ++i) {
        late_invalid[16U + i] =
            static_cast<std::uint8_t>(repaired_crc >> ((3U - i) * 8U));
    }
    decoded = profile;
    CHECK(SvDeviceProfileBinaryCodec::decode(late_invalid, decoded).status ==
          SvDeviceProfileCodecStatus::invalid_value);
    CHECK(decoded == CompiledSvDeviceProfile{});

    ByteVector short_buffer(encoded.size() - 1U);
    const auto short_result =
        SvDeviceProfileBinaryCodec::encode_into(profile, short_buffer);
    CHECK(short_result.status == SvDeviceProfileCodecStatus::buffer_too_small);
    CHECK(short_result.bytes == encoded.size());

    auto invalid_width = profile;
    invalid_width.leaves[1].wire_width_bytes = 2U;
    CHECK(!SvDeviceProfileBinaryCodec::encoded_size(invalid_width).has_value());

    auto embedded_nul = profile;
    embedded_nul.sv_id[1] = '\0';
    CHECK(!SvDeviceProfileBinaryCodec::encoded_size(embedded_nul).has_value());

    auto inconsistent_options = profile;
    inconsistent_options.asdu_options.element_present = false;
    CHECK(!SvDeviceProfileBinaryCodec::encoded_size(inconsistent_options).has_value());

    auto corrupted = encoded;
    corrupted.back() ^= 0x01U;
    CHECK(SvDeviceProfileBinaryCodec::decode(corrupted, decoded).status ==
          SvDeviceProfileCodecStatus::checksum_mismatch);

    auto wrong_magic = encoded;
    wrong_magic[0] = 'X';
    CHECK(SvDeviceProfileBinaryCodec::decode(wrong_magic, decoded).status ==
          SvDeviceProfileCodecStatus::invalid_magic);

    auto wrong_version = encoded;
    wrong_version[5] = 2U;
    CHECK(SvDeviceProfileBinaryCodec::decode(wrong_version, decoded).status ==
          SvDeviceProfileCodecStatus::unsupported_version);

    auto wrong_length = encoded;
    wrong_length[11] = static_cast<std::uint8_t>(wrong_length[11] - 1U);
    CHECK(SvDeviceProfileBinaryCodec::decode(wrong_length, decoded).status ==
          SvDeviceProfileCodecStatus::invalid_length);
}


void compiled_binary_profile_activates_only_supported_4i4v_device_layout() {
    using namespace ar::iec61850::sampled_values;
    using namespace ar::esp32p4::smv;

    CompiledSvDeviceProfile device{};
    device.profile_family = SvProfileFamily::iec61850_9_2;
    device.transport_mode = SvTransportMode::multicast;
    device.destination_mac = {0x01U, 0x0CU, 0xCDU, 0x04U, 0x00U, 0x01U};
    device.app_id = 0x4001U;
    device.vlan_present = true;
    device.vlan_id = 100U;
    device.vlan_priority = 4U;
    device.configuration_revision = 9U;
    device.sampling_basis = SvSampleMode::samples_per_second;
    device.configured_sample_rate = 4800U;
    device.frame_rate_hz = 4800U;
    device.sample_counter_modulus = 4800U;
    device.no_asdu = 1U;
    device.asdu_options.element_present = true;
    device.asdu_options.sample_synchronized = true;
    device.asdu_options.data_set = true;
    device.asdu_options.sample_rate = true;
    device.payload_size_bytes = 64U;
    device.leaf_count = 16U;
    device.sv_id_length = 3U;
    device.sv_id[0] = 'S'; device.sv_id[1] = 'V'; device.sv_id[2] = '2';
    device.data_set_reference_length = 2U;
    device.data_set_reference[0] = 'D';
    device.data_set_reference[1] = 'S';
    for (std::size_t i = 0U; i < 8U; ++i) {
        device.leaves[2U * i] = {SvDeviceWireType::int32, 4U, false, false};
        device.leaves[2U * i + 1U] = {SvDeviceWireType::quality, 4U, true, false};
    }
    const auto size = SvDeviceProfileBinaryCodec::encoded_size(device);
    CHECK(size.has_value());
    std::vector<std::uint8_t> encoded(*size);
    CHECK(SvDeviceProfileBinaryCodec::encode_into(device, encoded).success());

    BinaryProfileStaging chunks;
    CHECK(chunks.begin(1U, encoded.size()));
    for (std::size_t offset = 0U; offset < encoded.size();) {
        const auto count = std::min(BinaryProfileStaging::max_chunk_bytes,
                                    encoded.size() - offset);
        CHECK(chunks.append(1U, offset, std::span<const std::uint8_t>{encoded}.subspan(offset, count)));
        offset += count;
    }
    CHECK(chunks.complete(1U));
    RuntimePublisherProfile runtime{};
    CHECK(decode_binary_runtime_profile(chunks.complete_record(1U), runtime) ==
          BinaryRuntimeProfileStatus::ok);
    CHECK(runtime.publisher_rate_hz == 4800U);
    CHECK(runtime.sample_counter_modulus == 4800U);
    CHECK(runtime.configuration_revision == 9U);
    CHECK(runtime.app_id == 0x4001U);
    CHECK(runtime.sv_id[0] == 'S' && runtime.sv_id[2] == '2');
    CHECK(runtime.include_data_set && runtime.include_sample_rate);
    CHECK(runtime.data_set_reference[0] == 'D');
    const auto previous_generation = runtime.generation;
    chunks.abort();
    CHECK(!chunks.begin(1U, encoded.size())); // Replayed transaction ID.
    CHECK(chunks.begin(2U, encoded.size()));
    CHECK(!chunks.append(2U, 1U, std::span<const std::uint8_t>{encoded}.first(8U)));
    CHECK(!chunks.active()); // An out-of-order chunk destroys staging.
    CHECK(chunks.begin(3U, encoded.size()));
    CHECK(chunks.append(3U, 0U, std::span<const std::uint8_t>{encoded}.first(8U)));
    CHECK(!chunks.append(3U, 0U, std::span<const std::uint8_t>{encoded}.first(8U)));
    CHECK(!chunks.active()); // Duplicate offset fails closed.
    CHECK(chunks.begin(4U, encoded.size()));
    CHECK(!chunks.complete(4U)); // No partial commit.
    CHECK(chunks.complete_record(4U).empty());
    chunks.abort();
    CHECK(chunks.begin(5U, encoded.size()));
    CHECK(!chunks.append(
        5U, 0U,
        std::span<const std::uint8_t>{encoded}.first(
            BinaryProfileStaging::max_chunk_bytes + 1U)));
    CHECK(!chunks.active()); // Oversized chunks cannot bypass the console bound.
    CHECK(!chunks.begin(5U, encoded.size())); // No replay after abort.
    CHECK(!chunks.begin(6U, BinaryProfileStaging::max_record_bytes + 1U));

    auto corrupted = encoded;
    corrupted.back() ^= 0x01U;
    CHECK(decode_binary_runtime_profile(corrupted, runtime) ==
          BinaryRuntimeProfileStatus::invalid_envelope);
    CHECK(runtime.generation == previous_generation);
    CHECK(runtime.sv_id[2] == '2');

    auto unsupported = device;
    unsupported.profile_family = SvProfileFamily::iec61869_9;
    CHECK(SvDeviceProfileBinaryCodec::encode_into(unsupported, encoded).success());
    CHECK(decode_binary_runtime_profile(encoded, runtime) ==
          BinaryRuntimeProfileStatus::unsupported_device_capability);
    CHECK(runtime.app_id == 0x4001U);

    unsupported = device;
    unsupported.asdu_options.sample_synchronized = false;
    CHECK(SvDeviceProfileBinaryCodec::encode_into(unsupported, encoded).success());
    CHECK(decode_binary_runtime_profile(encoded, runtime) ==
          BinaryRuntimeProfileStatus::unsupported_device_capability);

    unsupported = device;
    unsupported.leaves[0] = {SvDeviceWireType::uint32, 4U, false, false};
    CHECK(SvDeviceProfileBinaryCodec::encode_into(unsupported, encoded).success());
    CHECK(decode_binary_runtime_profile(encoded, runtime) ==
          BinaryRuntimeProfileStatus::unsupported_device_capability);

    unsupported = device;
    unsupported.frame_rate_hz = 4000U; // Cannot silently change 4800 sample cadence.
    CHECK(SvDeviceProfileBinaryCodec::encode_into(unsupported, encoded).success());
    CHECK(decode_binary_runtime_profile(encoded, runtime) ==
          BinaryRuntimeProfileStatus::unsupported_device_capability);
    CHECK(runtime.publisher_rate_hz == 4800U);
}

void sample_counter_tracker_distinguishes_wrap_gap_duplicate_and_order() {
    using namespace ar::iec61850::sampled_values;

    SampleCounterTracker tracker;
    const auto wrap = std::optional<std::uint16_t>{std::uint16_t{4000U}};
    CHECK(tracker.observe(3998U, wrap).kind == SampleCounterTransitionKind::initial);
    CHECK(tracker.observe(3999U, wrap).kind == SampleCounterTransitionKind::continuous);
    CHECK(tracker.observe(0U, wrap).kind == SampleCounterTransitionKind::normal_wrap);

    const auto gap = tracker.observe(3U, wrap);
    CHECK(gap.kind == SampleCounterTransitionKind::gap);
    CHECK(gap.missing_samples == 2U);
    CHECK(gap.is_anomaly());

    CHECK(tracker.observe(3U, wrap).kind == SampleCounterTransitionKind::duplicate);
    CHECK(tracker.observe(2U, wrap).kind == SampleCounterTransitionKind::out_of_order);
    CHECK(tracker.observe(100U, wrap, true).kind == SampleCounterTransitionKind::restart);

    const auto timestamp = std::chrono::system_clock::time_point{
        std::chrono::seconds{100}} + std::chrono::milliseconds{250};
    CHECK(SampleCounterPolicy::initial_sample_count(
              timestamp, 4000.0, wrap, SampleCounterMode::second_aligned) == 1000U);
    CHECK(SampleCounterPolicy::initial_sample_count(
              timestamp, 4000.0, wrap, SampleCounterMode::free_run) == 0U);
    CHECK(SampleCounterPolicy::increment(3999U, wrap) == 0U);
    CHECK(SampleCounterPolicy::increment(65'535U, std::nullopt) == 0U);
}

void stream_supervisor_tracks_identity_configuration_and_statistics() {
    using namespace ar::iec61850::sampled_values;

    SampledValuesStreamSupervisor supervisor(StreamExpectation{
        std::string{"MU01"},
        std::string{"MU01/LLN0$Dataset1"},
        3U,
        std::uint16_t{4000U}});

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
        {"SV compiled device profile binary", compiled_device_profile_binary_is_canonical_and_integrity_checked},
        {"SV binary ESP32-P4 activation gate", compiled_binary_profile_activates_only_supported_4i4v_device_layout},
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
