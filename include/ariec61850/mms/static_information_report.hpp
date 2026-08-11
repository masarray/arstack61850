// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/information_report_span.hpp"
#include "ariec61850/mms/static_data_set_table.hpp"
#include "ariec61850/mms/static_object_table.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {

enum class MmsStaticInformationReportStatus : std::uint8_t {
    response_ready,
    invalid_object_table,
    invalid_data_set_table,
    data_set_not_found,
    object_not_found,
    workspace_too_small,
    backend_failure,
    invalid_report,
    response_buffer_too_small,
};

struct MmsStaticInformationReportInput final {
    std::string_view report_id;
    std::span<const std::uint8_t> optional_fields;
    std::uint32_t sequence_number{};
    std::span<const std::uint8_t> report_time;
    bool buffered{};
    bool buffer_overflow{};
    std::span<const std::uint8_t> entry_id;
    std::uint32_t conf_revision{};
    std::uint8_t reason_for_inclusion{};
};

struct MmsStaticInformationReportEncodeResult final {
    MmsStaticInformationReportStatus status{
        MmsStaticInformationReportStatus::invalid_report};
    std::size_t bytes_written{};
    std::size_t required_bytes{};
    std::size_t member_count{};

    [[nodiscard]] constexpr bool success() const noexcept {
        return status == MmsStaticInformationReportStatus::response_ready;
    }
};

class MmsStaticInformationReportEncoder final {
public:
    [[nodiscard]] static MmsStaticInformationReportEncodeResult
        encode_data_set_snapshot_into(
            const MmsStaticObjectTable& objects,
            const MmsStaticDataSetTable& data_sets,
            const MmsObjectNameView& data_set_name,
            const MmsStaticInformationReportInput& report,
            std::span<std::uint8_t> destination,
            std::span<std::uint8_t> workspace) noexcept;
};

} // namespace ar::iec61850::mms
