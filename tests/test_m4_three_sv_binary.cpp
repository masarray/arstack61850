// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/sampled_values/m4_three_sv_binary.hpp"
#include "ariec61850/scl/parser.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
#define CHECK(condition) do { if (!(condition)) { \
    throw std::runtime_error(std::string{"M4 binary CHECK failed: "} + #condition + \
        " at " + std::to_string(__LINE__)); \
} } while (false)

using namespace ar::iec61850::sampled_values;
using Profiles = std::array<CompiledSvDeviceProfile, m4_three_sv_capacity>;

Profiles reference_profiles() {
    const auto path = std::filesystem::path{ARIEC61850_SOURCE_DIR} /
        "tests/fixtures/scl/m4-three-9-2le-reference.scd";
    const auto document = ar::iec61850::scl::SclParser{}.load(path);
    SvPublisherProfileCompileContext context;
    context.profile_family = SvProfileFamily::legacy_9_2le;
    context.sample_counter_modulus = static_cast<std::uint16_t>(4000U);
    const auto compiled = M4ThreeSvRosterCompiler::compile(document, context);
    CHECK(compiled.ok());
    return {compiled.roster->streams[0].device_profile,
            compiled.roster->streams[1].device_profile,
            compiled.roster->streams[2].device_profile};
}

void three_v1_profiles_inside_v2_envelope() {
    const auto profiles = reference_profiles();
    const auto required = M4ThreeSvBinaryCodec::encoded_size(profiles);
    CHECK(required.has_value());
    CHECK(*required > M4ThreeSvBinaryCodec::header_bytes);

    std::vector<std::uint8_t> too_small(*required - 1U);
    const auto shortage = M4ThreeSvBinaryCodec::encode_into(profiles, too_small);
    CHECK(shortage.status == M4ThreeSvBinaryStatus::buffer_too_small);
    CHECK(shortage.bytes == *required);

    std::vector<std::uint8_t> bytes(*required);
    const auto encoded = M4ThreeSvBinaryCodec::encode_into(profiles, bytes);
    CHECK(encoded.success());
    CHECK(encoded.bytes == *required);
    CHECK(bytes[0] == 'A' && bytes[1] == 'R' &&
          bytes[2] == 'M' && bytes[3] == '3');
    CHECK(bytes[6] == 3U);
    CHECK(bytes[16] == 0U && bytes[17] == 0U &&
          bytes[18] == 0x0FU && bytes[19] == 0xA0U); // 4000 big endian

    Profiles decoded{};
    CHECK(M4ThreeSvBinaryCodec::decode(bytes, decoded).success());
    CHECK(decoded == profiles);

    // Successful decode is repeatable and byte-stable.
    std::vector<std::uint8_t> reencoded(*required);
    CHECK(M4ThreeSvBinaryCodec::encode_into(decoded, reencoded).success());
    CHECK(reencoded == bytes);

    // No partial mutation even when the last nested V1 record is damaged.
    const auto original = decoded;
    bytes.back() ^= 1U;
    CHECK(M4ThreeSvBinaryCodec::decode(bytes, decoded).status ==
          M4ThreeSvBinaryStatus::checksum_mismatch);
    CHECK(decoded == original);
    bytes.back() ^= 1U;

    bytes[5] = 3U; // version 2 -> 3
    CHECK(M4ThreeSvBinaryCodec::decode(bytes, decoded).status ==
          M4ThreeSvBinaryStatus::unsupported_version);
    CHECK(decoded == original);
    bytes[5] = 2U;

    bytes[23] ^= 1U; // length of second nested record
    CHECK(!M4ThreeSvBinaryCodec::decode(bytes, decoded).success());
    CHECK(decoded == original);
    bytes[23] ^= 1U;

    // Repeated APPID is allowed but complete duplicate wire identity is not.
    auto duplicate = profiles;
    duplicate[2].destination_mac = duplicate[0].destination_mac;
    duplicate[2].sv_id = duplicate[0].sv_id;
    duplicate[2].sv_id_length = duplicate[0].sv_id_length;
    CHECK(!M4ThreeSvBinaryCodec::encoded_size(duplicate).has_value());

    auto wrong_rate = profiles;
    wrong_rate[1].frame_rate_hz = 4800U;
    CHECK(!M4ThreeSvBinaryCodec::encoded_size(wrong_rate).has_value());
    // A 64-byte, 16-leaf DataSet is not sufficient: MU waveform output needs
    // eight exact INT32/Quality pairs in the expected positional order.
    auto wrong_leaf = profiles;
    wrong_leaf[1].leaves[0].wire_type = SvDeviceWireType::uint32;
    CHECK(!M4ThreeSvBinaryCodec::encoded_size(wrong_leaf).has_value());
    auto wrong_quality = profiles;
    wrong_quality[2].leaves[1].wire_type = SvDeviceWireType::uint32;
    wrong_quality[2].leaves[1].quality = false;
    CHECK(!M4ThreeSvBinaryCodec::encoded_size(wrong_quality).has_value());


    std::cout << "PASS: V2 three-stream envelope: CRC32, all-or-none "
                 "readback, V1 nested profiles, replay-safe byte format\n";
}
} // namespace

int main() {
    try {
        three_v1_profiles_inside_v2_envelope();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
