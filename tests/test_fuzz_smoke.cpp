// SPDX-License-Identifier: GPL-3.0-or-later

#include "fuzz_targets.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using ByteVector = std::vector<std::uint8_t>;
using Exercise = std::function<void(const ByteVector&)>;

ByteVector from_hex(const std::string& text) {
    ByteVector result;
    result.reserve(text.size() / 2U);
    for (std::size_t index = 0U; index + 1U < text.size(); index += 2U) {
        result.push_back(static_cast<std::uint8_t>(
            std::stoul(text.substr(index, 2U), nullptr, 16)));
    }
    return result;
}

ByteVector read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Unable to read fuzz seed: " + path.string());
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

std::size_t mutation_case_count(const ByteVector& seed) {
    return 2U + (3U * std::min<std::size_t>(seed.size(), 64U));
}

std::size_t exercise_mutations(const ByteVector& seed, const Exercise& exercise) {
    std::size_t cases = 0U;
    exercise(seed);
    ++cases;
    exercise({});
    ++cases;

    const auto mutation_limit = std::min<std::size_t>(seed.size(), 64U);
    for (std::size_t index = 0U; index < mutation_limit; ++index) {
        auto flipped = seed;
        flipped[index] ^= 0xFFU;
        exercise(flipped);
        ++cases;

        auto zeroed = seed;
        zeroed[index] = 0U;
        exercise(zeroed);
        ++cases;

        exercise(ByteVector{seed.begin(), seed.begin() + static_cast<std::ptrdiff_t>(index)});
        ++cases;
    }
    return cases;
}

void deterministic_decoder_mutation_smoke() {
    const auto ber = from_hex(
        "8301018501FD86012A870508414800008A024F4B8C041234567891086A2B77254000000A");
    const auto goose = from_hex(
        "010CCD0100010200000000018100806488B81001005E00000000615480104C44302F4C4C4E30"
        "24474F2467636231810203E8820C4C44302F4C4C4E30244453318306474F4F53453184086A2B"
        "77254000000A8501038601098701008801028901018A0103AB0A8301018501FD8A024F4B");
    const auto sampled_values = from_hex(
        "010CCD040001020000000002810080C888BA4001006800000000"
        "605E800101A259305780134D55303146312F4C4C4E30244D535643423031"
        "81144D55303146312F4C4C4E30245068734D65617331820178830103"
        "84086A2BDFE40000000085010286020FA087100000006400000001000000C8"
        "00000003880101");
    const auto pcap = read_file(
        std::filesystem::path{ARIEC61850_SOURCE_DIR} /
        "tests/fixtures/process_bus_csharp_vectors.pcap");
    const auto scl = read_file(
        std::filesystem::path{ARIEC61850_SOURCE_DIR} /
        "tests/fixtures/scl/minimal-station.scd");
    const auto comtrade = read_file(
        std::filesystem::path{ARIEC61850_SOURCE_DIR} /
        "tests/fixtures/comtrade/simple_fault.cfg");
    const auto transport = from_hex(
        "03000015011E0000000100C0010AC1020001C2020001");
    const auto association = read_file(
        std::filesystem::path{ARIEC61850_SOURCE_DIR} /
        "fuzz/corpus/association/balanced_aarq");
    const auto mms_services = read_file(
        std::filesystem::path{ARIEC61850_SOURCE_DIR} /
        "fuzz/corpus/mms_services/initiate_request");
    const auto reporting = read_file(
        std::filesystem::path{ARIEC61850_SOURCE_DIR} /
        "fuzz/corpus/reporting/realistic_information_report");

    const auto expected_cases =
        mutation_case_count(ber) +
        mutation_case_count(goose) +
        mutation_case_count(sampled_values) +
        mutation_case_count(pcap) +
        mutation_case_count(scl) +
        mutation_case_count(comtrade) +
        mutation_case_count(transport) +
        mutation_case_count(association) +
        mutation_case_count(mms_services) +
        mutation_case_count(reporting) +
        1U;

    std::size_t cases = 0U;
    cases += exercise_mutations(ber, [](const ByteVector& bytes) {
        ar::iec61850::fuzzing::exercise_ber(bytes);
    });
    cases += exercise_mutations(goose, [](const ByteVector& bytes) {
        ar::iec61850::fuzzing::exercise_goose(bytes);
    });

    // Regress libFuzzer crash 798ea9d3: VLAN TCI 0xFFFF decodes to reserved
    // VID 4095. try_decode must reject it instead of returning an object whose
    // subsequent encode throws std::out_of_range.
    auto reserved_vlan_goose = goose;
    if (reserved_vlan_goose.size() < 16U) {
        throw std::runtime_error("GOOSE seed is unexpectedly short.");
    }
    reserved_vlan_goose[14] = 0xFFU;
    reserved_vlan_goose[15] = 0xFFU;
    ar::iec61850::fuzzing::exercise_goose(reserved_vlan_goose);
    ++cases;

    cases += exercise_mutations(sampled_values, [](const ByteVector& bytes) {
        ar::iec61850::fuzzing::exercise_sampled_values(bytes);
    });
    cases += exercise_mutations(pcap, [](const ByteVector& bytes) {
        ar::iec61850::fuzzing::exercise_pcap(bytes);
    });
    cases += exercise_mutations(scl, [](const ByteVector& bytes) {
        ar::iec61850::fuzzing::exercise_scl(bytes);
    });
    cases += exercise_mutations(comtrade, [](const ByteVector& bytes) {
        ar::iec61850::fuzzing::exercise_comtrade(bytes);
    });
    cases += exercise_mutations(transport, [](const ByteVector& bytes) {
        ar::iec61850::fuzzing::exercise_transport(bytes);
    });
    cases += exercise_mutations(association, [](const ByteVector& bytes) {
        ar::iec61850::fuzzing::exercise_association(bytes);
    });
    cases += exercise_mutations(mms_services, [](const ByteVector& bytes) {
        ar::iec61850::fuzzing::exercise_mms_services(bytes);
    });
    cases += exercise_mutations(reporting, [](const ByteVector& bytes) {
        ar::iec61850::fuzzing::exercise_reporting(bytes);
    });

    if (cases != expected_cases) {
        throw std::runtime_error(
            "Mutation smoke case-count mismatch: expected " +
            std::to_string(expected_cases) + ", got " + std::to_string(cases) + ".");
    }
    std::cout << "Exercised " << cases
              << " deterministic decoder mutation cases.\n";
}

} // namespace

int main() {
    try {
        deterministic_decoder_mutation_smoke();
        std::cout << "[PASS] deterministic decoder mutation smoke\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] deterministic decoder mutation smoke: "
                  << error.what() << '\n';
        return 1;
    }
}