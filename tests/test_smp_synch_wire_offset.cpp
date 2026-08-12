// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/sampled_values/wire_field_offsets.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

#define CHECK(condition) do { \
    if (!(condition)) { \
        throw std::runtime_error(std::string{"CHECK failed: "} + #condition + \
                                 " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
    } \
} while (false)

std::vector<std::uint8_t> make_collision_frame() {
    // One-ASDU SV frame. The svID value deliberately contains the exact byte
    // pattern that the old linear scanner mistook for smpSynch:
    //     85 01 44 87
    // The real smpSynch field is the later TLV: 85 01 00.
    return {
        0x01U, 0x0CU, 0xCDU, 0x04U, 0x00U, 0x01U,
        0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U,
        0x88U, 0xBAU,
        0x40U, 0x00U, 0x00U, 0x25U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x60U, 0x1BU,
        0x80U, 0x01U, 0x01U,
        0xA2U, 0x16U,
        0x30U, 0x14U,
        0x80U, 0x06U, 0x41U, 0x85U, 0x01U, 0x44U, 0x87U, 0x42U,
        0x82U, 0x01U, 0x00U,
        0x83U, 0x01U, 0x01U,
        0x85U, 0x01U, 0x00U,
        0x87U, 0x01U, 0x00U,
    };
}

void structured_locator_skips_tag_like_bytes_inside_svid() {
    using ar::iec61850::sampled_values::find_smp_synch_value_offset;

    auto frame = make_collision_frame();
    const auto offset = find_smp_synch_value_offset(frame);
    CHECK(offset.has_value());
    CHECK(*offset == 47U);
    CHECK(frame[36U] == 0x44U); // old scanner's false-positive value byte
    CHECK(frame[*offset] == 0x00U);

    frame[*offset] = 0x02U;
    CHECK(frame[36U] == 0x44U);
    CHECK(frame[47U] == 0x02U);
}

void structured_locator_handles_vlan_without_changing_ber_semantics() {
    using ar::iec61850::sampled_values::find_smp_synch_value_offset;

    auto untagged = make_collision_frame();
    std::vector<std::uint8_t> tagged;
    tagged.reserve(untagged.size() + 4U);
    tagged.insert(tagged.end(), untagged.begin(), untagged.begin() + 12);
    tagged.push_back(0x81U);
    tagged.push_back(0x00U);
    tagged.push_back(0x80U);
    tagged.push_back(0x64U);
    tagged.insert(tagged.end(), untagged.begin() + 12, untagged.end());

    const auto offset = find_smp_synch_value_offset(tagged);
    CHECK(offset.has_value());
    CHECK(*offset == 51U);
    CHECK(tagged[*offset] == 0x00U);
}

void locator_rejects_non_sv_and_malformed_lengths() {
    using ar::iec61850::sampled_values::find_smp_synch_value_offset;

    auto non_sv = make_collision_frame();
    non_sv[12U] = 0x08U;
    non_sv[13U] = 0x00U;
    CHECK(!find_smp_synch_value_offset(non_sv).has_value());

    auto bad_declared_length = make_collision_frame();
    bad_declared_length[16U] = 0xFFU;
    bad_declared_length[17U] = 0xFFU;
    CHECK(!find_smp_synch_value_offset(bad_declared_length).has_value());

    auto truncated = make_collision_frame();
    truncated.pop_back();
    CHECK(!find_smp_synch_value_offset(truncated).has_value());
}

} // namespace

int main() {
    try {
        structured_locator_skips_tag_like_bytes_inside_svid();
        structured_locator_handles_vlan_without_changing_ber_semantics();
        locator_rejects_non_sv_and_malformed_lengths();
        std::cout << "[PASS] structured smpSynch wire offset regression\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
