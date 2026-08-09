// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/connection_runtime.hpp"
#include "ariec61850/mms/static_brcb_runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::mms {

enum class MmsStaticBrcbConnectionStatus : std::uint8_t {
    no_report_available,
    response_ready,
    not_established,
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
    // Deliver at most one already-buffered report. The queue front is removed
    // only after a complete TPKT image is staged successfully. Capacity errors
    // and disconnected sessions preserve the exact same EntryID for retry.
    [[nodiscard]] static MmsStaticBrcbConnectionResult poll(
        const MmsStaticConnectionRuntime& connection,
        MmsStaticBrcbRuntime& reports,
        std::span<std::uint8_t> response,
        std::span<std::uint8_t> workspace) noexcept;
};

} // namespace ar::iec61850::mms
