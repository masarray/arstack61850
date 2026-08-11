// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/connection_runtime.hpp"
#include "ariec61850/mms/static_brcb_control.hpp"
#include "ariec61850/mms/static_brcb_runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::mms {

enum class MmsStaticBrcbConnectionStatus : std::uint8_t {
    no_report_available,
    reporting_disabled,
    response_ready,
    not_established,
    access_denied,
    response_buffer_too_small,
    workspace_too_small,
    frame_encode_failed,
    stale_entry,
};

struct MmsStaticBrcbConnectionResult final {
    MmsStaticBrcbConnectionStatus status{
        MmsStaticBrcbConnectionStatus::no_report_available};
    std::size_t bytes_written{};
    std::size_t required_response_bytes{};
    std::size_t required_workspace_bytes{};
    std::array<std::uint8_t, MmsInformationReportSpanCodec::entry_id_bytes> entry_id{};
    std::uint8_t sequence_number{};
    bool buffer_overflow{};

    [[nodiscard]] constexpr bool response_ready() const noexcept {
        return status == MmsStaticBrcbConnectionStatus::response_ready;
    }
};

class MmsStaticBrcbConnection final {
public:
    // Stage at most one already-buffered report for the live BRCB owner.
    // Reporting must be enabled through `control`, and the established MMS
    // connection's association/Owner identity must exactly match the current
    // live reservation. Foreign associations are denied without exposing or
    // advancing retained report state.
    //
    // A successful poll only stages a complete TPKT image. It deliberately does
    // NOT advance the BRCB delivery cursor. The transport adapter must call
    // commit_sent() only after the complete staged frame has been accepted by
    // the transport. Capacity errors, denied access, disabled reporting,
    // disconnected sessions and send failures therefore preserve the same
    // EntryID for retry/reconnect.
    [[nodiscard]] static MmsStaticBrcbConnectionResult poll(
        const MmsStaticConnectionRuntime& connection,
        MmsStaticBrcbControl& control,
        MmsStaticBrcbRuntime& reports,
        std::uint64_t now_ms,
        std::span<std::uint8_t> response,
        std::span<std::uint8_t> workspace) noexcept;

    // Second phase of report delivery. Call only after every byte of the staged
    // response was accepted by the transport. The staged EntryID acts as the
    // bounded delivery token; stale/replayed tokens are rejected by the runtime.
    [[nodiscard]] static MmsStaticBrcbStatus commit_sent(
        MmsStaticBrcbRuntime& reports,
        const MmsStaticBrcbConnectionResult& staged) noexcept;
};

} // namespace ar::iec61850::mms
