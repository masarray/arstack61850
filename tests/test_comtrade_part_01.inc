// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/comtrade/channel_mapper.hpp"
#include "ariec61850/comtrade/reader.hpp"
#include "ariec61850/comtrade/scl_mapper.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

#define CHECK(condition) do { \
    if (!(condition)) { \
        throw std::runtime_error(std::string{"CHECK failed: "} + #condition + \
                                 " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
    } \
} while (false)

void check_close(const double actual, const double expected, const double tolerance = 1.0e-9) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(
            "Expected " + std::to_string(expected) + ", got " + std::to_string(actual) + ".");
    }
}

template <typename Function>
void check_throws(Function&& function) {
    bool thrown{};
    try {
        function();
    } catch (const std::exception&) {
        thrown = true;
    }
    CHECK(thrown);
}

std::string configuration_text(
    const std::string& data_type,
    const std::string& rate_lines = "1\n4000,2",
    const std::string& time_multiplier = "1") {
    return
        "TEST STATION,ARIED,2013\n"
        "4,2A,2D\n"
        "1,VA,A,BUS,V,0.01,0,0,-32768,32767,110000,100,P\n"
        "2,IA,A,FEEDER,A,0.001,0,0,-32768,32767,1000,1,P\n"
        "1,TRIP,A,CB,0\n"
        "2,CLOSE,A,CB,1\n"
        "50\n" + rate_lines + "\n"
        "01/01/2026,00:00:00.000000\n"
        "01/01/2026,00:00:00.001000\n" +
        data_type + "\n" + time_multiplier + "\n"
        "UTC,+0h\n"
        "A,0\n";
}

void append_u16(std::vector<std::uint8_t>& bytes, const std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void append_u32(std::vector<std::uint8_t>& bytes, const std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

void append_i16(std::vector<std::uint8_t>& bytes, const std::int16_t value) {
    append_u16(bytes, std::bit_cast<std::uint16_t>(value));
}

void append_i32(std::vector<std::uint8_t>& bytes, const std::int32_t value) {
    append_u32(bytes, std::bit_cast<std::uint32_t>(value));
}

void append_float(std::vector<std::uint8_t>& bytes, const float value) {
    append_u32(bytes, std::bit_cast<std::uint32_t>(value));
}

void parser_extracts_configuration_metadata() {
    using namespace ar::iec61850::comtrade;
    const auto configuration = Reader{}.parse_configuration(configuration_text("ASCII"), "sample.cfg");
    CHECK(configuration.source_name == "sample.cfg");
    CHECK(configuration.station_name == "TEST STATION");
    CHECK(configuration.device_id == "ARIED");
    CHECK(configuration.revision_year == 2013U);
    CHECK(configuration.total_channel_count == 4U);
    CHECK(configuration.analog_channels.size() == 2U);
    CHECK(configuration.digital_channels.size() == 2U);
    CHECK(configuration.sample_rates.size() == 1U);
    CHECK(configuration.data_file_type == DataFileType::ascii);
    CHECK(configuration.time_code == "UTC");
    CHECK(configuration.local_code == "+0h");
    CHECK(configuration.time_quality == "A");
    CHECK(configuration.leap_second == "0");
    CHECK(configuration.warnings.empty());
}

void reader_loads_ascii_scaling_digital_and_time() {
    using namespace ar::iec61850::comtrade;
    const auto configuration = Reader{}.parse_configuration(configuration_text("ASCII"));
    const auto samples = Reader{}.read_ascii_data(
        "1,0,10000,1000,1,0\n"
        "2,250,9000,-5000,0,1\n",
        configuration);
    CHECK(samples.size() == 2U);
    CHECK(samples[0].number == 1U);
    check_close(samples[0].timestamp_seconds, 0.0);
    check_close(samples[1].timestamp_seconds, 0.00025);
    check_close(samples[0].analog_values[0], 100.0);
