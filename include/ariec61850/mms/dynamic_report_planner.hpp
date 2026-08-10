// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/live_discovery.hpp"

#include <cstddef>
#include <vector>

namespace ar::iec61850::mms {

struct MmsDynamicReportMemberSelection final {
    std::size_t successful_type_probes{};
    std::size_t scalar_leaf_candidates{};
    std::vector<MmsObjectName> members;
};

// Projects scalar ST/MX leaves from live GetVariableAccessAttributes evidence.
// Discovery normally probes one type tree per Logical Node, so this selector
// walks nested FC/DO/DA structures instead of requiring one probe per leaf.
class MmsDynamicReportMemberSelector final {
public:
    [[nodiscard]] static MmsDynamicReportMemberSelection select(
        const MmsLiveDiscoveryResult& discovery,
        const MmsReportControlCandidate& report_control,
        std::size_t requested_members);
};

} // namespace ar::iec61850::mms
