// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/comtrade/model.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ar::iec61850::comtrade {

std::size_t Dataset::sample_count() const noexcept {
    return samples.size();
}

double Dataset::duration_seconds() const noexcept {
    if (samples.size() < 2U) {
        return 0.0;
    }
    return std::max(0.0, samples.back().timestamp_seconds - samples.front().timestamp_seconds);
}

double Dataset::nominal_sample_rate_hz() const noexcept {
    for (const auto& rate : configuration.sample_rates) {
        if (rate.rate_hz > 0.0 && std::isfinite(rate.rate_hz)) {
            return rate.rate_hz;
        }
    }
    const auto duration = duration_seconds();
    if (samples.size() < 2U || duration <= 0.0) {
        return 0.0;
    }
    return static_cast<double>(samples.size() - 1U) / duration;
}

const Sample& Dataset::sample_by_index(const std::int64_t sample_index, const bool loop) const {
    if (samples.empty()) {
        throw std::runtime_error("COMTRADE dataset contains no samples.");
    }

    if (loop) {
        const auto count = static_cast<std::int64_t>(samples.size());
        auto wrapped = sample_index % count;
        if (wrapped < 0) {
            wrapped += count;
        }
        return samples[static_cast<std::size_t>(wrapped)];
    }

    if (sample_index <= 0) {
        return samples.front();
    }
    const auto unsigned_index = static_cast<std::uint64_t>(sample_index);
    if (unsigned_index >= samples.size()) {
        return samples.back();
    }
    return samples[static_cast<std::size_t>(unsigned_index)];
}

std::string to_string(const DataFileType type) {
    switch (type) {
    case DataFileType::ascii:
        return "ASCII";
    case DataFileType::binary:
        return "BINARY";
    case DataFileType::binary32:
        return "BINARY32";
    case DataFileType::float32:
        return "FLOAT32";
    case DataFileType::unknown:
        return "UNKNOWN";
    }
    return "UNKNOWN";
}

} // namespace ar::iec61850::comtrade
