// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/sampled_values/m4_three_sv_roster.hpp"
#include "ariec61850/sampled_values/compiled_device_profile.hpp"
#include "ariec61850/scl/parser.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
#define CHECK(condition) do { if (!(condition)) { \
    throw std::runtime_error(std::string{"M4 CHECK failed: "} + #condition + \
        " at " + std::to_string(__LINE__)); \
} } while (false)

using namespace ar::iec61850::sampled_values;

std::string_view sv_id(const CompiledSvDeviceProfile& p) {
    return {p.sv_id.data(), p.sv_id_length};
}

SvPublisherProfileCompileContext reference_context() {
    SvPublisherProfileCompileContext c;
    c.profile_family = SvProfileFamily::legacy_9_2le;
    c.sample_counter_modulus = static_cast<std::uint16_t>(4000U);
    return c;
}

void exact_three_stream_compilation_and_codec() {
    const auto path = std::filesystem::path{ARIEC61850_SOURCE_DIR} /
        "tests/fixtures/scl/m4-three-9-2le-reference.scd";
    auto document = ar::iec61850::scl::SclParser{}.load(path);
    CHECK(document.sampled_values_streams.size() == 3U);
    // This document has same APPID on three different L2 destinations.
    // The existing SCL conflict list must not arbitrarily block this roster.
    const auto compile = M4ThreeSvRosterCompiler::compile(document, reference_context());
    if (!compile.ok()) {
        for (const auto& e : compile.errors) std::cerr << e << '\n';
    }
    CHECK(compile.ok());
    const auto& roster = *compile.roster;
    CHECK(roster.common_sample_rate_hz == 4000U);
    CHECK(roster.total_ethernet_frames_per_second == 12000U);

    for (std::size_t i = 0U; i < 3U; ++i) {
        const auto& stream = roster.streams[i];
        const auto& profile = stream.device_profile;
        CHECK(stream.ied_name == "MU01_4I_4V_" + std::to_string(i + 1U));
        CHECK(stream.control_block_reference.find("MSVCB01") != std::string::npos);
        CHECK(sv_id(profile) == "MU01_SV" + std::to_string(i + 1U));
        CHECK(profile.destination_mac == (std::array<std::uint8_t, 6>{
            0x01U, 0x0CU, 0xCDU, 0x04U, 0x00U,
            static_cast<std::uint8_t>(i + 1U)}));
        CHECK(profile.app_id == 0x4000U);
        CHECK(profile.vlan_present);
        CHECK(profile.vlan_id == 0U);
        CHECK(profile.vlan_priority == 4U);
        CHECK(profile.frame_rate_hz == 4000U);
        CHECK(profile.sample_counter_modulus == 4000U);
        CHECK(profile.payload_size_bytes == 64U);
        CHECK(profile.no_asdu == 1U);
        CHECK(profile.leaf_count == 16U);
        CHECK(profile.asdu_options.sample_synchronized);

        const auto bytes = SvDeviceProfileBinaryCodec::encoded_size(profile);
        CHECK(bytes.has_value() && *bytes > 0U);
        std::vector<std::uint8_t> encoded(*bytes);
        CHECK(SvDeviceProfileBinaryCodec::encode_into(profile, encoded).success());
        CompiledSvDeviceProfile decoded;
        CHECK(SvDeviceProfileBinaryCodec::decode(encoded, decoded).success());
        CHECK(decoded == profile);
    }

    // Reject a fourth stream, not merely truncate it.
    auto invalid = document;
    invalid.sampled_values_streams.push_back(invalid.sampled_values_streams.back());
    CHECK(!M4ThreeSvRosterCompiler::compile(invalid, reference_context()).ok());

    // No implicit family or rate-sized counter guess.
    CHECK(!M4ThreeSvRosterCompiler::compile(document, {}).ok());
    auto wrong_counter = reference_context();
    wrong_counter.sample_counter_modulus = static_cast<std::uint16_t>(4800U);
    CHECK(!M4ThreeSvRosterCompiler::compile(document, wrong_counter).ok());

    // One bad stream must invalidate the entire atomic roster, not two-thirds.
    invalid = document;
    invalid.sampled_values_streams[1].sample_rate = 4800U;
    CHECK(!M4ThreeSvRosterCompiler::compile(invalid, reference_context()).ok());

    // Same APPID with different MAC is allowed; identical complete wire
    // identity (MAC+APPID+VLAN+svID) is rejected.
    invalid = document;
    invalid.sampled_values_streams[2].address.destination_mac =
        invalid.sampled_values_streams[0].address.destination_mac;
    invalid.sampled_values_streams[2].sv_id =
        invalid.sampled_values_streams[0].sv_id;
    invalid.sampled_values_streams[2].smv_id =
        invalid.sampled_values_streams[0].smv_id;
    CHECK(!M4ThreeSvRosterCompiler::compile(invalid, reference_context()).ok());

    std::cout << "PASS: 3 SCL MUs, 3 exact L2 SV profiles, binary V1 round-trip, "
                 "shared 4000 Hz grid, 12000 fps, atomic rejection\n";
}
} // namespace

int main() {
    try {
        exact_three_stream_compilation_and_codec();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
