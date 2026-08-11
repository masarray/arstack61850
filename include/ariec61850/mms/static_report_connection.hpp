// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/connection_runtime.hpp"
#include "ariec61850/mms/static_urcb_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::mms {

enum class MmsStaticReportConnectionStatus : std::uint8_t {
    no_report_due,
    response_ready,
    not_established,
    response_buffer_too_small,
    workspace_too_small,
    report_encode_failed,
    stale_plan,
};

struct MmsStaticReportConnectionResult final {
    MmsStaticReportConnectionStatus status{MmsStaticReportConnectionStatus::no_report_due};
    MmsStaticUrcbStatus urcb_status{MmsStaticUrcbStatus::no_report_due};
    std::size_t bytes_written{};
    std::size_t required_response_bytes{};
    std::size_t required_workspace_bytes{};
    std::size_t control_block_index{MmsStaticUrcbEmissionPlan::invalid_index};
    std::uint8_t sequence_number{};
    MmsStaticUrcbReportReason reason{MmsStaticUrcbReportReason::none};

    [[nodiscard]] constexpr bool response_ready() const noexcept {
        return status == MmsStaticReportConnectionStatus::response_ready;
    }
};

class MmsStaticReportConnection final {
public:
    // Poll one due URCB report for an already-established MMS connection.
    // The URCB plan is committed only after a complete TPKT frame is encoded,
    // so capacity retries cannot consume GI/integrity state or skip SqNum.
    [[nodiscard]] static MmsStaticReportConnectionResult poll(
        const MmsStaticConnectionRuntime& connection,
        MmsStaticUrcbRuntime& reports,
        std::uint64_t now_ms,
        std::span<const std::uint8_t> report_time,
        std::span<std::uint8_t> response,
        std::span<std::uint8_t> workspace) noexcept;
};

} // namespace ar::iec61850::mms
