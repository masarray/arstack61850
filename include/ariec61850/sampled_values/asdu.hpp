// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/mms/utc_time.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ar::iec61850::sampled_values {

struct SampledValueAsdu final {
    std::string sv_id;
    std::string data_set_reference;
    std::uint16_t sample_count{};
    std::uint32_t configuration_revision{1U};
    std::optional<mms::Iec61850UtcTime> reference_time;
    std::uint8_t sample_synchronization{2U};
    std::optional<std::uint16_t> sample_rate;
    std::optional<std::uint16_t> sample_mode;
    std::vector<std::uint8_t> sample_payload;

    friend bool operator==(const SampledValueAsdu&, const SampledValueAsdu&) = default;
};

struct SampledValuesPdu final {
    std::vector<SampledValueAsdu> asdus;

    friend bool operator==(const SampledValuesPdu&, const SampledValuesPdu&) = default;
};

} // namespace ar::iec61850::sampled_values
