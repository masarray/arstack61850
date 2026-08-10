// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/information_report_span.hpp"
#include "ariec61850/wire/encode_result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {

// Event-report form of InformationReport. The inclusion bit-string keeps the
// original DataSet cardinality while only the selected member values are
// encoded. This is the shape required by change-driven IEC 61850 reports.
struct MmsSelectiveInformationReportSnapshotInput final {
    std::string_view report_id;
    std::span<const std::uint8_t> optional_fields;
    std::uint32_t sequence_number{};
    std::span<const std::uint8_t> report_time;
    MmsInformationReportReferenceInput data_set_reference{};
    std::uint32_t conf_revision{};

    std::size_t data_set_member_count{};
    std::span<const std::size_t> included_member_indices;
    std::span<const MmsInformationReportReferenceInput> included_member_references;
    std::span<const MmsReadAccessResultInput> included_member_results;
    std::span<const std::uint8_t> included_reason_for_inclusion;
};

class MmsSelectiveInformationReportSpanCodec final {
public:
    [[nodiscard]] static wire::EncodeResult encode_snapshot_into(
        const MmsSelectiveInformationReportSnapshotInput& report,
        std::span<std::uint8_t> destination) noexcept;
};

} // namespace ar::iec61850::mms
