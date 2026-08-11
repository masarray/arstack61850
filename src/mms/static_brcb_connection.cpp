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

    std::size_t cotp_required{};
    if (!add_size(p_data.bytes_written, 3U, cotp_required)) {
        return make_result(MmsStaticBrcbConnectionStatus::frame_encode_failed, &entry);
    }
    const auto tpdu_limit = connection.negotiated_tpdu_size_bytes();
    if (tpdu_limit == 0U || cotp_required > tpdu_limit) {
        return make_result(MmsStaticBrcbConnectionStatus::peer_limit_exceeded, &entry);
    }

    std::size_t final_required{};
    if (!add_size(cotp_required, osi::TpktSpanCodec::header_length, final_required)) {
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

    auto result = make_result(MmsStaticBrcbConnectionStatus::response_ready, &entry);
    std::copy_n(workspace.begin(), tpkt.bytes_written, response.begin());
    result.bytes_written = tpkt.bytes_written;
    result.required_response_bytes = final_required;
    result.required_workspace_bytes = final_required;
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