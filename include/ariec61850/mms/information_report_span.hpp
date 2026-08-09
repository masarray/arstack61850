// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/services_span.hpp"
#include "ariec61850/wire/encode_result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {

struct MmsInformationReportReferenceInput final {
    std::string_view domain;
    std::string_view item;
};

struct MmsInformationReportSnapshotInput final {
    std::string_view report_id;
    std::span<const std::uint8_t> optional_fields;
    std::uint32_t sequence_number{};
    std::span<const std::uint8_t> report_time;
    MmsInformationReportReferenceInput data_set_reference{};
    bool buffered{};
    bool buffer_overflow{};
    std::span<const std::uint8_t> entry_id;
    std::uint32_t conf_revision{};
    std::span<const MmsInformationReportReferenceInput> member_references;
    std::span<const MmsReadAccessResultInput> member_results;
    std::uint8_t reason_for_inclusion{};
};

struct MmsInformationReportView final {
    MmsObjectNameView variable_list_name{};
    std::span<const std::uint8_t> access_result_list{};
    std::size_t item_count{};

    [[nodiscard]] bool try_item(
        std::size_t index,
        MmsReadAccessResultView& result) const noexcept;
};

class MmsInformationReportSpanCodec final {
public:
    static constexpr std::size_t maximum_members = MmsServiceSpanCodec::maximum_variables;
    static constexpr std::size_t maximum_reference_bytes = 129U;
    static constexpr std::size_t maximum_report_id_bytes = 129U;
    static constexpr std::size_t optional_field_bytes = 2U;
    static constexpr std::size_t binary_time_bytes = 6U;
    static constexpr std::size_t entry_id_bytes = 8U;
    static constexpr std::size_t maximum_access_results =
        (3U * maximum_members) + 16U;

    [[nodiscard]] static bool try_decode_information_report(
        std::span<const std::uint8_t> mms_pdu,
        MmsInformationReportView& report) noexcept;

    [[nodiscard]] static wire::EncodeResult encode_snapshot_into(
        const MmsInformationReportSnapshotInput& report,
        std::span<std::uint8_t> destination) noexcept;
};

} // namespace ar::iec61850::mms
