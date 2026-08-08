// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/comtrade/reader.hpp"
#include "ariec61850/sampled_values/payload_writer.hpp"
#include "ariec61850/sampled_values/replay_bundle.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace ar::iec61850;

constexpr std::uint32_t kTargetSampleRateHz = 4'000U;
constexpr std::array<std::string_view, sampled_values::injector_channel_count>
    kChannelNames{"Ia", "Ib", "Ic", "In", "Va", "Vb", "Vc", "Vn"};

[[nodiscard]] double unit_to_base_factor(const std::string_view unit) {
    if (unit == "A" || unit == "V") {
        return 1.0;
    }
    if (unit == "kA" || unit == "kV") {
        return 1'000.0;
    }
    if (unit == "mA" || unit == "mV") {
        return 0.001;
    }
    throw std::runtime_error(
        "Mapped analog channel uses unsupported engineering unit '" +
        std::string{unit} + "'. Expected A, kA, mA, V, kV, or mV.");
}

[[nodiscard]] std::int32_t to_replay_count(
    const double engineering_value,
    const std::string_view unit) {
    if (!std::isfinite(engineering_value)) {
        throw std::runtime_error("Record contains a non-finite analog value.");
    }
    const auto base_value = engineering_value * unit_to_base_factor(unit);
    if (!std::isfinite(base_value) ||
        base_value < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
        base_value > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
        throw std::runtime_error("Mapped analog value exceeds INT32 replay range.");
    }
    return static_cast<std::int32_t>(std::llround(base_value));
}

[[nodiscard]] sampled_values::ReplayPayloadFrame make_frame(
    const comtrade::Dataset& dataset,
    const comtrade::Sample& sample) {
    sampled_values::ReplayPayloadFrame frame{};
    const auto bytes = std::span<std::uint8_t>{frame.data(), frame.size()};

    for (std::size_t output = 0U; output < kChannelNames.size(); ++output) {
        std::int32_t value{};
        const auto found = dataset.default_channel_map.find(kChannelNames[output]);
        if (found != dataset.default_channel_map.end()) {
            const auto analog_index = found->second;
            if (analog_index >= sample.analog_values.size() ||
                analog_index >= dataset.configuration.analog_channels.size()) {
                throw std::runtime_error("Default channel map points outside analog data.");
            }
            value = to_replay_count(
                sample.analog_values[analog_index],
                dataset.configuration.analog_channels[analog_index].unit);
        }

        if (!sampled_values::SampledValuesPayloadWriter::write_int32_quality_pair(
                bytes, output, value, 0U)) {
            throw std::runtime_error("Could not encode normalized replay payload.");
        }
    }
    return frame;
}

void require_supported_sample_rate(const comtrade::Dataset& dataset) {
    const auto rate = dataset.nominal_sample_rate_hz();
    if (!std::isfinite(rate) ||
        std::abs(rate - static_cast<double>(kTargetSampleRateHz)) > 1.0e-6) {
        throw std::runtime_error(
            "Record sample rate must be exactly 4000 Hz for replay bundle v1. "
            "Resampling is intentionally not implicit.");
    }
}

void write_bundle(
    const comtrade::Dataset& dataset,
    const std::filesystem::path& output_path) {
    require_supported_sample_rate(dataset);
    if (dataset.samples.empty()) {
        throw std::runtime_error("Record contains no samples.");
    }

    sampled_values::ReplayBundleHeader header{};
    header.sample_rate_hz = kTargetSampleRateHz;
    header.frame_count = static_cast<std::uint64_t>(dataset.samples.size());

    std::array<std::uint8_t, sampled_values::replay_bundle_header_bytes> encoded_header{};
    if (!sampled_values::encode_replay_bundle_header(header, encoded_header)) {
        throw std::runtime_error("Could not encode replay bundle header.");
    }

    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Could not open replay bundle output.");
    }
    output.write(
        reinterpret_cast<const char*>(encoded_header.data()),
        static_cast<std::streamsize>(encoded_header.size()));

    for (const auto& sample : dataset.samples) {
        const auto frame = make_frame(dataset, sample);
        output.write(
            reinterpret_cast<const char*>(frame.data()),
            static_cast<std::streamsize>(frame.size()));
        if (!output) {
            throw std::runtime_error("Failed while writing replay bundle payload.");
        }
    }
    output.flush();
    if (!output) {
        throw std::runtime_error("Failed to flush replay bundle output.");
    }
}

void print_mapping(const comtrade::Dataset& dataset) {
    std::cout << "Channel map       :";
    for (const auto name : kChannelNames) {
        const auto found = dataset.default_channel_map.find(name);
        if (found == dataset.default_channel_map.end()) {
            std::cout << ' ' << name << "=0";
        } else {
            std::cout << ' ' << name << "=analog[" << found->second << ']';
        }
    }
    std::cout << '\n';
}

} // namespace

int main(const int argc, char** argv) {
    if (argc != 3) {
        std::cerr
            << "Usage: ariec61850_sv_replay_prepare <record.cfg> <output.arsvr>\n"
            << "Creates a normalized 4000-sample/s 8-channel SV replay bundle.\n";
        return 2;
    }

    try {
        const auto dataset = comtrade::Reader{}.load(argv[1]);
        write_bundle(dataset, argv[2]);
        const sampled_values::ReplayBundleHeader header{
            1U,
            kTargetSampleRateHz,
            static_cast<std::uint32_t>(sampled_values::replay_payload_bytes),
            static_cast<std::uint32_t>(sampled_values::injector_channel_count),
            static_cast<std::uint64_t>(dataset.samples.size())};

        std::cout
            << "ARStack61850 recorded-waveform replay preparation\n\n"
            << "Input             : " << argv[1] << '\n'
            << "Output            : " << argv[2] << '\n'
            << "Source samples    : " << dataset.samples.size() << '\n'
            << "Sample rate       : " << kTargetSampleRateHz << " Hz\n"
            << "Payload/frame     : " << sampled_values::replay_payload_bytes << " bytes\n"
            << "Bundle bytes      : "
            << sampled_values::replay_bundle_expected_bytes(header) << '\n';
        print_mapping(dataset);
        std::cout << "RESULT            : REPLAY-BUNDLE/PREPARED\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ariec61850_sv_replay_prepare: " << error.what() << '\n';
        return 1;
    }
}
