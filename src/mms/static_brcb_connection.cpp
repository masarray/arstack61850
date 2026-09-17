// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_brcb_connection.hpp"

#include "ariec61850/osi/cotp_span.hpp"
#include "ariec61850/osi/cotp_tpkt_stream.hpp"
#include "ariec61850/osi/presentation_span.hpp"
#include "ariec61850/osi/tpkt_span.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace ar::iec61850::mms {
namespace {

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

[[nodiscard]] bool same_owner(
    const std::span<const std::uint8_t> left,
    const std::span<const std::uint8_t> right) noexcept {
    return left.size() == right.size() && !left.empty() &&
        std::equal(left.begin(), left.end(), right.begin());
}

} // namespace

MmsStaticBrcbConnectionResult MmsStaticBrcbConnection::poll(
    const MmsStaticConnectionRuntime& connection,
    MmsStaticBrcbControl& control,
    MmsStaticBrcbRuntime& reports,
    const std::uint64_t now_ms,
    const std::span<std::uint8_t> response,
    const std::span<std::uint8_t> workspace) noexcept {
    if (connection.state() != MmsStaticConnectionState::established ||
        connection.mms_presentation_context_id() == 0U) {
        return make_result(MmsStaticBrcbConnectionStatus::not_established);
    }

    const auto control_state = control.state(now_ms);
    if (!control_state.report_enabled) {
        return make_result(MmsStaticBrcbConnectionStatus::reporting_disabled);
    }

    const auto access = connection.access_context();
    if (!control.valid() || !control_state.reserved || !control_state.owner_connected ||
        control_state.association_id == 0U ||
        access.association_id != control_state.association_id ||
        !same_owner(access.owner, control_state.owner)) {
        return make_result(MmsStaticBrcbConnectionStatus::access_denied);
    }

    MmsStaticBrcbEntryView entry;
    if (!reports.front(entry)) {
        return make_result(MmsStaticBrcbConnectionStatus::no_report_available);
    }

    const auto mms_limit = connection.negotiated_mms_pdu_size();
    if (mms_limit == 0U || entry.mms_pdu.size() > mms_limit) {
        return make_result(MmsStaticBrcbConnectionStatus::peer_limit_exceeded, &entry);
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
        osi::CotpTpktDataStreamPlan plan;
        if (!osi::CotpTpktDataStreamSpanCodec::try_plan(
                p_data.required_bytes, connection.negotiated_tpdu_size_bytes(), plan)) {
            return make_result(MmsStaticBrcbConnectionStatus::peer_limit_exceeded, &entry);
        }
        auto result = make_result(
            MmsStaticBrcbConnectionStatus::workspace_too_small, &entry);
        result.required_workspace_bytes = p_data.required_bytes;
        result.required_response_bytes = plan.required_bytes;
        return result;
    }

    const auto framed = osi::CotpTpktDataStreamSpanCodec::encode_into(
        workspace.first(p_data.bytes_written),
        connection.negotiated_tpdu_size_bytes(),
        response);
    if (!framed.success()) {
        if (framed.status == wire::EncodeStatus::buffer_too_small) {
            auto result = make_result(
                MmsStaticBrcbConnectionStatus::response_buffer_too_small, &entry);
            result.required_response_bytes = framed.required_bytes;
            return result;
        }
        return make_result(MmsStaticBrcbConnectionStatus::peer_limit_exceeded, &entry);
    }

    auto result = make_result(MmsStaticBrcbConnectionStatus::response_ready, &entry);
    result.bytes_written = framed.bytes_written;
    result.required_response_bytes = framed.required_bytes;
    result.required_workspace_bytes = p_data.bytes_written;
    return result;
}

bool MmsStaticBrcbConnection::commit_sent(
    const MmsStaticConnectionRuntime& connection,
    MmsStaticBrcbControl& control,
    MmsStaticBrcbRuntime& reports,
    const std::uint64_t now_ms,
    const MmsStaticBrcbConnectionResult& staged) noexcept {
    if (!staged.response_ready() ||
        connection.state() != MmsStaticConnectionState::established ||
        connection.mms_presentation_context_id() == 0U || !control.valid()) {
        return false;
    }

    const auto control_state = control.state(now_ms);
    if (!control_state.report_enabled || !control_state.reserved ||
        !control_state.owner_connected || control_state.association_id == 0U) {
        return false;
    }

    const auto access = connection.access_context();
    if (access.association_id != control_state.association_id ||
        !same_owner(access.owner, control_state.owner)) {
        return false;
    }

    return reports.commit_delivery(staged.entry_id) == MmsStaticBrcbStatus::ok;
}

} // namespace ar::iec61850::mms