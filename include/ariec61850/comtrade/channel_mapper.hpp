// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/comtrade/model.hpp"

#include <map>
#include <string>

namespace ar::iec61850::comtrade {

class ChannelMapper final {
public:
    [[nodiscard]] static std::map<std::string, std::size_t, std::less<>> create_default_map(
        const std::vector<AnalogChannel>& channels);
};

} // namespace ar::iec61850::comtrade
