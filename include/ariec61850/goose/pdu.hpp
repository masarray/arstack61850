// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/mms/data_value.hpp"
#include "ariec61850/mms/utc_time.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ar::iec61850::goose {

struct GoosePdu final {
    std::string go_cb_ref;
    std::uint32_t time_allowed_to_live_milliseconds{};
    std::string data_set_reference;
    std::string go_id;
    mms::Iec61850UtcTime timestamp{};
    std::uint32_t state_number{1U};
    std::uint32_t sequence_number{};
    bool test{};
    std::uint32_t configuration_revision{1U};
    bool needs_commissioning{};
    std::vector<mms::MmsDataValue> values;
};

} // namespace ar::iec61850::goose
