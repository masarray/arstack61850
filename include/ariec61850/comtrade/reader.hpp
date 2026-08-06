// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/comtrade/model.hpp"

#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace ar::iec61850::comtrade {

class Reader final {
public:
    static constexpr std::size_t maximum_configuration_bytes = 16U * 1024U * 1024U;
    static constexpr std::size_t maximum_data_bytes = 512U * 1024U * 1024U;
    static constexpr std::uint32_t maximum_channels = 100000U;
    static constexpr std::size_t maximum_samples = 10000000U;

    [[nodiscard]] Configuration parse_configuration(
        std::string_view text,
        std::string source_name = {}) const;

    [[nodiscard]] std::vector<Sample> read_ascii_data(
        std::string_view text,
        const Configuration& configuration) const;

    [[nodiscard]] std::vector<Sample> read_binary_data(
        std::span<const std::uint8_t> bytes,
        const Configuration& configuration) const;

    [[nodiscard]] Dataset load(const std::filesystem::path& configuration_path) const;
};

} // namespace ar::iec61850::comtrade
