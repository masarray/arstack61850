// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_urcb_event_connection.hpp"

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

[[nodiscard]] MmsStaticUrcbEventConnectionResult make_result(
    const MmsStaticUrcbEventConnectionStatus status,
    const MmsStaticUrcbEventStatus event_status,
    const MmsStaticUrcbEventPlan& plan = {}) noexcept {
    MmsStaticUrcbEventConnectionResult result;
    result.status = status;
    result.event_status = event_status;
    result.control_block_index = plan.index;
    result.sequence_number = plan.sequence_number;
    return result;
}

[[nodiscard]] bool final_frame_size(
    const std::uint32_t presentation_context_id,
    const std::size_t mms_bytes,
    std::size_t& required) noexcept {
    const auto fully_encoded = osi::PresentationSpanCodec::fully_encoded_data_size(
        presentation_context_id, mms_bytes);
    return fully_encoded && add_size(*fully_encoded, 11U, required);
}

[[nodiscard]] MmsStaticUrcbEventConnectionResult response_capacity(
    const MmsStaticUrcbEventPlan& plan,
    const MmsStaticUrcbEventStatus event_status,
    const std::size_t required) noexcept {
    auto result = make_result(
        MmsStaticUrcbEventConnectionStatus::response_buffer_too_small,
        event_status,
        plan);
    result.required_response_bytes = required;
    return result;
}

[[nodiscard]] MmsStaticUrcbEventConnectionResult workspace_capacity(
    const MmsStaticUrcbEventPlan& plan,
    const MmsStaticUrcbEventStatus event_status,
    const std::size_t required) noexcept {
    auto result = make_result(
        MmsStaticUrcbEventConnectionStatus::workspace_too_small,
        event_status,
        plan);
    result.required_workspace_bytes = required;
    return result;
}

} // namespace

MmsStaticUrcbEventConnectionResult MmsStaticUrcbEventConnection::poll(
    const MmsStaticConnectionRuntime& connection,
    MmsStaticUrcbEventRuntime& events,
    const std::uint64_t now_ms,
    const std::span<const std::uint8_t> report_time,
    const std::span<std::uint8_t> response,
    const std::span<std::uint8_t> workspace) noexcept {
    if (connection.state() != MmsStaticConnectionState::established ||
        connection.mms_presentation_context_id() == 0U) {
        return make_result(
            MmsStaticUrcbEventConnectionStatus::not_established,
            MmsStaticUrcbEventStatus::temporarily_unavailable);
    }

    MmsStaticUrcbEventPlan plan;
    if (!events.next_due(now_ms, plan)) {
        return make_result(
            MmsStaticUrcbEventConnectionStatus::no_report_due,
            events.valid()
                ? MmsStaticUrcbEventStatus::ok
                : MmsStaticUrcbEventStatus::invalid_runtime);
    }

    const auto encoded = events.encode(plan, report_time, response, workspace);
    if (!encoded.success()) {
        if (encoded.status == MmsStaticUrcbEventStatus::response_buffer_too_small) {
            std::size_t final_required{};
            if (!final_frame_size(
                    connection.mms_presentation_context_id(),
                    encoded.required_bytes,
                    final_required)) {
                return make_result(
                    MmsStaticUrcbEventConnectionStatus::report_encode_failed,
                    MmsStaticUrcbEventStatus::report_encode_failed,
                    plan);
            }
            return response_capacity(plan, encoded.status, final_required);
        }
        if (encoded.status == MmsStaticUrcbEventStatus::workspace_too_small) {
            return workspace_capacity(plan, encoded.status, encoded.required_bytes);
        }
        if (encoded.status == MmsStaticUrcbEventStatus::stale_plan) {
            return make_result(
                MmsStaticUrcbEventConnectionStatus::stale_plan,
                encoded.status,
                plan);
        }
        return make_result(
            MmsStaticUrcbEventConnectionStatus::report_encode_failed,
            encoded.status,
            plan);
    }

    const auto p_data = osi::PresentationSpanCodec::encode_p_data_into(
        response.first(encoded.bytes_written),
        workspace,
        connection.mms_presentation_context_id(),
        true);
    if (!p_data.success()) {
        if (p_data.status == wire::EncodeStatus::buffer_too_small) {
            std::size_t final_required{};
            if (!add_size(p_data.required_bytes, 7U, final_required)) {
                return make_result(
                    MmsStaticUrcbEventConnectionStatus::report_encode_failed,
                    MmsStaticUrcbEventStatus::report_encode_failed,
                    plan);
            }
            auto result = workspace_capacity(
                plan, MmsStaticUrcbEventStatus::workspace_too_small, final_required);
            result.required_response_bytes = final_required;
            return result;
        }
        return make_result(
            MmsStaticUrcbEventConnectionStatus::report_encode_failed,
            MmsStaticUrcbEventStatus::report_encode_failed,
            plan);
    }

    std::size_t final_required{};
    if (!add_size(p_data.bytes_written, 7U, final_required)) {
        return make_result(
            MmsStaticUrcbEventConnectionStatus::report_encode_failed,
            MmsStaticUrcbEventStatus::report_encode_failed,
            plan);
    }
    if (response.size() < final_required) {
        return response_capacity(
            plan, MmsStaticUrcbEventStatus::response_buffer_too_small, final_required);
    }
    if (workspace.size() < final_required) {
        auto result = workspace_capacity(
            plan, MmsStaticUrcbEventStatus::workspace_too_small, final_required);
        result.required_response_bytes = final_required;
        return result;
    }

    const auto cotp = osi::CotpSpanCodec::encode_data_into(
        workspace.first(p_data.bytes_written),
        response.first(final_required - osi::TpktSpanCodec::header_length));
    if (!cotp.success()) {
        return make_result(
            MmsStaticUrcbEventConnectionStatus::report_encode_failed,
            MmsStaticUrcbEventStatus::report_encode_failed,
            plan);
    }

    const auto tpkt = osi::TpktSpanCodec::encode_into(
        response.first(cotp.bytes_written),
        workspace.first(final_required));
    if (!tpkt.success() || tpkt.bytes_written != final_required) {
        return make_result(
            MmsStaticUrcbEventConnectionStatus::report_encode_failed,
            MmsStaticUrcbEventStatus::report_encode_failed,
            plan);
    }

    const auto committed = events.commit(plan);
    if (committed != MmsStaticUrcbEventStatus::ok) {
        return make_result(
            MmsStaticUrcbEventConnectionStatus::stale_plan,
            committed,
            plan);
    }

    std::copy_n(workspace.begin(), tpkt.bytes_written, response.begin());
    auto result = make_result(
        MmsStaticUrcbEventConnectionStatus::response_ready,
        MmsStaticUrcbEventStatus::ok,
        plan);
    result.bytes_written = tpkt.bytes_written;
    result.required_response_bytes = final_required;
    result.required_workspace_bytes = final_required;
    return result;
}

} // namespace ar::iec61850::mms
