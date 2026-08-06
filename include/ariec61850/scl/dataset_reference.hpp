// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/scl/model.hpp"

#include <span>
#include <string>

namespace ar::iec61850::scl {

struct SclDataSetBindingResolution final {
    SclDataSetBindingStatus status{SclDataSetBindingStatus::not_specified};
    const SclDataSet* data_set{};
    std::string canonical_reference;
    std::string local_name;
};

class SclDataSetReferenceResolver final {
public:
    [[nodiscard]] static SclDataSetBindingResolution resolve(
        std::span<const SclDataSet> data_sets,
        const std::string& ied_name,
        const std::string& ld_inst,
        const std::string& logical_node_path,
        const std::string& raw_reference);
};

} // namespace ar::iec61850::scl
