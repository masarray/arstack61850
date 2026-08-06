// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/comtrade/model.hpp"
#include "ariec61850/scl/model.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ar::iec61850::comtrade {

enum class MappingConfidence {
    none,
    ordered_fallback,
    semantic,
};

struct SclChannelBinding final {
    std::size_t scl_entry_index{};
    std::string signal_reference;
    std::optional<std::size_t> analog_channel_index;
    std::string channel_key;
    MappingConfidence confidence{MappingConfidence::none};
    std::string reason;

    friend bool operator==(const SclChannelBinding&, const SclChannelBinding&) = default;
};

struct SclMappingReport final {
    std::vector<SclChannelBinding> bindings;
    std::vector<std::string> warnings;

    [[nodiscard]] bool complete() const noexcept;
};

class SclMapper final {
public:
    [[nodiscard]] static SclMappingReport map(
        const scl::SclSampledValuesStream& stream,
        const Configuration& configuration);
};

[[nodiscard]] std::string to_string(MappingConfidence confidence);

} // namespace ar::iec61850::comtrade
