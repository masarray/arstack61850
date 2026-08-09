// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/connection_runtime.hpp"
#include "ariec61850/mms/static_urcb_event_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::mms {

enum class MmsStaticUrcbEventConnectionStatus : std::uint8_t {
    no_report_due,
    response_ready,
    not_established,
    response_buffer_too_small,
    workspace_too_small,
    report_encode_failed,
    stale_plan,
};

struct MmsStaticUrcbEventConnectionResult final {
    MmsStaticUrcbEventConnectionStatus status{
        MmsStaticUrcbEventConnectionStatus::no_report_due};
    MmsStaticUrcbEventStatus event_status{MmsStaticUrcbEventStatus::ok};
    std::size_t bytes_written{};
    std::size_t required_response_bytes{};
    std::size_t required_workspace_bytes{};
    std::size_t control_block_index{MmsStaticUrcbEventPlan::invalid_index};
    std::uint8_t sequence_number{};

    [[nodiscard]] constexpr bool response_ready() const noexcept {
        return status == MmsStaticUrcbEventConnectionStatus::response_ready;
    }
};

class MmsStaticUrcbEventConnection final {
public:
    // Poll exactly one due event-driven URCB report on an established MMS
    // association. The event plan is committed only after the complete TPKT
    // frame exists, preserving pending membership/reasons and SqNum across
    // response/workspace capacity retries.
    [[nodiscard]] static MmsStaticUrcbEventConnectionResult poll(
        const MmsStaticConnectionRuntime& connection,
        MmsStaticUrcbEventRuntime& events,
        std::uint64_t now_ms,
        std::span<const std::uint8_t> report_time,
        std::span<std::uint8_t> response,
        std::span<std::uint8_t> workspace) noexcept;
};

} // namespace ar::iec61850::mms
