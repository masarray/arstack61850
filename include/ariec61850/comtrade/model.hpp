// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace ar::iec61850::comtrade {

enum class DataFileType {
    ascii,
    binary,
    binary32,
    float32,
    unknown,
};

struct AnalogChannel final {
    std::uint32_t index{};
    std::string name;
    std::string phase;
    std::string circuit_component;
    std::string unit;
    double multiplier{1.0};
    double offset{};
    double skew_microseconds{};
    double minimum{};
    double maximum{};
    double primary{1.0};
    double secondary{1.0};
    std::string scaling_identifier;

    friend bool operator==(const AnalogChannel&, const AnalogChannel&) = default;
};

struct DigitalChannel final {
    std::uint32_t index{};
    std::string name;
    std::string phase;
    std::string circuit_component;
    bool normal_state{};

    friend bool operator==(const DigitalChannel&, const DigitalChannel&) = default;
};

struct SampleRate final {
    double rate_hz{};
    std::uint32_t last_sample_number{};

    friend bool operator==(const SampleRate&, const SampleRate&) = default;
};

struct Configuration final {
    std::string source_name;
    std::string station_name;
    std::string device_id;
    std::uint32_t revision_year{1999U};
    std::uint32_t total_channel_count{};
    std::uint32_t analog_channel_count{};
    std::uint32_t digital_channel_count{};
    double line_frequency_hz{};
    std::vector<AnalogChannel> analog_channels;
    std::vector<DigitalChannel> digital_channels;
    std::vector<SampleRate> sample_rates;
    std::string start_timestamp_text;
    std::string trigger_timestamp_text;
    DataFileType data_file_type{DataFileType::ascii};
    std::string data_file_type_text{"ASCII"};
    double time_multiplier{1.0};
    std::string time_code;
    std::string local_code;
    std::string time_quality;
    std::string leap_second;
    std::vector<std::string> warnings;

    friend bool operator==(const Configuration&, const Configuration&) = default;
};

struct Sample final {
    std::uint32_t number{};
    double timestamp_seconds{};
    std::vector<double> analog_values;
    std::vector<std::uint8_t> digital_values;

    friend bool operator==(const Sample&, const Sample&) = default;
};

struct Dataset final {
    std::filesystem::path configuration_path;
    std::filesystem::path data_path;
    Configuration configuration;
    std::vector<Sample> samples;
    std::map<std::string, std::size_t, std::less<>> default_channel_map;

    [[nodiscard]] std::size_t sample_count() const noexcept;
    [[nodiscard]] double duration_seconds() const noexcept;
    [[nodiscard]] double nominal_sample_rate_hz() const noexcept;
    [[nodiscard]] const Sample& sample_by_index(std::int64_t sample_index, bool loop) const;
};

[[nodiscard]] std::string to_string(DataFileType type);

} // namespace ar::iec61850::comtrade
