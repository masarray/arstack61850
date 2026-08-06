// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/comtrade/reader.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <string_view>

namespace {

std::string json_escape(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            result.push_back(character);
            break;
        }
    }
    return result;
}

void print_json(const ar::iec61850::comtrade::Dataset& dataset) {
    const auto& configuration = dataset.configuration;
    std::cout << "{\n"
              << "  \"station\": \"" << json_escape(configuration.station_name) << "\",\n"
              << "  \"deviceId\": \"" << json_escape(configuration.device_id) << "\",\n"
              << "  \"revisionYear\": " << configuration.revision_year << ",\n"
              << "  \"dataFileType\": \""
              << json_escape(ar::iec61850::comtrade::to_string(configuration.data_file_type)) << "\",\n"
              << "  \"analogChannels\": " << configuration.analog_channels.size() << ",\n"
              << "  \"digitalChannels\": " << configuration.digital_channels.size() << ",\n"
              << "  \"samples\": " << dataset.sample_count() << ",\n"
              << "  \"durationSeconds\": " << dataset.duration_seconds() << ",\n"
              << "  \"nominalSampleRateHz\": " << dataset.nominal_sample_rate_hz() << ",\n"
              << "  \"defaultChannelMap\": {";
    bool first = true;
    for (const auto& [key, index] : dataset.default_channel_map) {
        std::cout << (first ? "\n" : ",\n")
                  << "    \"" << json_escape(key) << "\": " << index;
        first = false;
    }
    if (!first) {
        std::cout << '\n';
    }
    std::cout << "  },\n  \"warnings\": [";
    for (std::size_t index = 0U; index < configuration.warnings.size(); ++index) {
        std::cout << (index == 0U ? "\n" : ",\n")
                  << "    \"" << json_escape(configuration.warnings[index]) << "\"";
    }
    if (!configuration.warnings.empty()) {
        std::cout << '\n';
    }
    std::cout << "  ]\n}\n";
}

void print_text(const ar::iec61850::comtrade::Dataset& dataset) {
    const auto& configuration = dataset.configuration;
    std::cout << "Station: " << configuration.station_name << '\n'
              << "Device: " << configuration.device_id << '\n'
              << "Revision: " << configuration.revision_year << '\n'
              << "DAT type: " << ar::iec61850::comtrade::to_string(configuration.data_file_type) << '\n'
              << "Channels: " << configuration.analog_channels.size() << " analog, "
              << configuration.digital_channels.size() << " digital\n"
              << "Samples: " << dataset.sample_count() << '\n'
              << "Duration: " << dataset.duration_seconds() << " s\n"
              << "Nominal sample rate: " << dataset.nominal_sample_rate_hz() << " Hz\n";
    if (!dataset.default_channel_map.empty()) {
        std::cout << "Default channel map:\n";
        for (const auto& [key, index] : dataset.default_channel_map) {
            std::cout << "  " << key << " -> analog[" << index << "]\n";
        }
    }
    for (const auto& warning : configuration.warnings) {
        std::cout << "Warning: " << warning << '\n';
    }
}

} // namespace

int main(const int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: ariec61850_comtrade_inspect <record.cfg> [--json]\n";
        return 2;
    }
    const bool json = argc == 3 && std::string_view{argv[2]} == "--json";
    if (argc == 3 && !json) {
        std::cerr << "Unknown option: " << argv[2] << '\n';
        return 2;
    }
    try {
        const auto dataset = ar::iec61850::comtrade::Reader{}.load(argv[1]);
        if (json) {
            print_json(dataset);
        } else {
            print_text(dataset);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "COMTRADE inspection failed: " << error.what() << '\n';
        return 1;
    }
}
