// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_brcb_connection.hpp"

#include "ariec61850/osi/cotp_span.hpp"
#include "ariec61850/osi/presentation_span.hpp"
#include "ariec61850/osi/tpkt_span.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace ar::iec61850::mms {
namespace {

[[nodiscard]] bool add_size(
    const std::size_t base,
    const std::size_t extra,
    std::size_t& total) noexcept {
    if (extra > std::numeric_limits<std::size_t>::max() - base) {
        total = 0U;
        return false;
    }
    total = base + extra;
    return true;
}

[[nodiscard]] MmsStaticBrcbConnectionResult make_result(
    const MmsStaticBrcbConnectionStatus status,
    const MmsStaticBrcbEntryView* entry = nullptr) noexcept {
    MmsStaticBrcbConnectionResult result;
    result.status = status;
    if (entry != nullptr) {
        if (entry->entry_id.size() == result.entry_id.size()) {
            std::copy(entry->entry_id.begin(), entry->entry_id.end(), result.entry_id.begin());
        }
        result.sequence_number = entry->sequence_number;
        result.buffer_overflow = entry->buffer_overflow;
    }
    return result;
}

} // namespace

MmsStaticBrcbConnectionResult MmsStaticBrcbConnection::poll(
    const MmsStaticConnectionRuntime& connection,
    MmsStaticBrcbRuntime& reports,
    const std::span<std::uint8_t> response,
    const std::span<std::uint8_t> workspace) noexcept {
    if (connection.state() != MmsStaticConnectionState::established ||
        connection.mms_presentation_context_id() == 0U) {
        return make_result(MmsStaticBrcbConnectionStatus::not_established);
    }

    MmsStaticBrcbEntryView entry;
    if (!reports.front(entry)) {
        return make_result(MmsStaticBrcbConnectionStatus::no_report_available);
    }

    const auto p_data = osi::PresentationSpanCodec::encode_p_data_into(
        entry.mms_pdu,
        workspace,
        connection.mms_presentation_context_id(),
        true);
    if (!p_data.success()) {
        if (p_data.status != wire::EncodeStatus::buffer_too_small) {
            return make_result(MmsStaticBrcbConnectionStatus::frame_encode_failed, &entry);
        }
        std::size_t final_required{};
        if (!add_size(p_data.required_bytes, 7U, final_required)) {
            return make_result(MmsStaticBrcbConnectionStatus::frame_encode_failed, &entry);
        }
        auto result = make_result(
            MmsStaticBrcbConnectionStatus::workspace_too_small, &entry);
        result.required_workspace_bytes = final_required;
        result.required_response_bytes = final_required;
        return result;
    }

    std::size_t final_required{};
    if (!add_size(p_data.bytes_written, 7U, final_required)) {
        return make_result(MmsStaticBrcbConnectionStatus::frame_encode_failed, &entry);
    }
    if (response.size() < final_required) {
        auto result = make_result(
            MmsStaticBrcbConnectionStatus::response_buffer_too_small, &entry);
        result.required_response_bytes = final_required;
        return result;
    }
    if (workspace.size() < final_required) {
        auto result = make_result(
            MmsStaticBrcbConnectionStatus::workspace_too_small, &entry);
        result.required_response_bytes = final_required;
        result.required_workspace_bytes = final_required;
        return result;
    }

    const auto cotp = osi::CotpSpanCodec::encode_data_into(
        workspace.first(p_data.bytes_written),
        response.first(final_required - osi::TpktSpanCodec::header_length));
    if (!cotp.success()) {
        return make_result(MmsStaticBrcbConnectionStatus::frame_encode_failed, &entry);
    }

    const auto tpkt = osi::TpktSpanCodec::encode_into(
        response.first(cotp.bytes_written),
        workspace.first(final_required));
    if (!tpkt.success() || tpkt.bytes_written != final_required) {
        return make_result(MmsStaticBrcbConnectionStatus::frame_encode_failed, &entry);
    }

    const auto committed = reports.commit_delivery(entry.entry_id);
    if (committed != MmsStaticBrcbStatus::ok) {
        return make_result(MmsStaticBrcbConnectionStatus::stale_entry, &entry);
    }

    std::copy_n(workspace.begin(), tpkt.bytes_written, response.begin());
    auto result = make_result(MmsStaticBrcbConnectionStatus::response_ready, &entry);
    result.bytes_written = tpkt.bytes_written;
    result.required_response_bytes = final_required;
    result.required_workspace_bytes = final_required;
    return result;
}

} // namespace ar::iec61850::mms
