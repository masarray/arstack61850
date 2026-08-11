// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_report_connection.hpp"

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

[[nodiscard]] MmsStaticReportConnectionResult make_result(
    const MmsStaticReportConnectionStatus status,
    const MmsStaticUrcbStatus urcb_status,
    const MmsStaticUrcbEmissionPlan& plan = {}) noexcept {
    MmsStaticReportConnectionResult result;
    result.status = status;
    result.urcb_status = urcb_status;
    result.control_block_index = plan.index;
    result.sequence_number = plan.sequence_number;
    result.reason = plan.reason;
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

[[nodiscard]] MmsStaticReportConnectionResult response_capacity(
    const MmsStaticUrcbEmissionPlan& plan,
    const MmsStaticUrcbStatus urcb_status,
    const std::size_t required) noexcept {
    auto result = make_result(
        MmsStaticReportConnectionStatus::response_buffer_too_small,
        urcb_status,
        plan);
    result.required_response_bytes = required;
    return result;
}

[[nodiscard]] MmsStaticReportConnectionResult workspace_capacity(
    const MmsStaticUrcbEmissionPlan& plan,
    const MmsStaticUrcbStatus urcb_status,
    const std::size_t required) noexcept {
    auto result = make_result(
        MmsStaticReportConnectionStatus::workspace_too_small,
        urcb_status,
        plan);
    result.required_workspace_bytes = required;
    return result;
}

} // namespace

MmsStaticReportConnectionResult MmsStaticReportConnection::poll(
    const MmsStaticConnectionRuntime& connection,
    MmsStaticUrcbRuntime& reports,
    const std::uint64_t now_ms,
    const std::span<const std::uint8_t> report_time,
    const std::span<std::uint8_t> response,
    const std::span<std::uint8_t> workspace) noexcept {
    if (connection.state() != MmsStaticConnectionState::established ||
        connection.mms_presentation_context_id() == 0U) {
        return make_result(
            MmsStaticReportConnectionStatus::not_established,
            MmsStaticUrcbStatus::temporarily_unavailable);
    }

    MmsStaticUrcbEmissionPlan plan;
    if (!reports.next_due(now_ms, plan)) {
        return make_result(
            MmsStaticReportConnectionStatus::no_report_due,
            reports.valid()
                ? MmsStaticUrcbStatus::no_report_due
                : MmsStaticUrcbStatus::invalid_runtime);
    }

    // Raw MMS report is written into response. The report encoder may use the
    // caller workspace transiently for DataSet member values. Only after the
    // raw PDU succeeds is workspace reused for P-DATA and final TPKT staging.
    const auto encoded = reports.encode(plan, report_time, response, workspace);
    if (!encoded.success()) {
        if (encoded.status == MmsStaticUrcbStatus::response_buffer_too_small) {
            std::size_t final_required{};
            if (!final_frame_size(
                    connection.mms_presentation_context_id(),
                    encoded.required_bytes,
                    final_required)) {
                return make_result(
                    MmsStaticReportConnectionStatus::report_encode_failed,
                    MmsStaticUrcbStatus::report_encode_failed,
                    plan);
            }
            return response_capacity(plan, encoded.status, final_required);
        }
        if (encoded.status == MmsStaticUrcbStatus::workspace_too_small) {
            return workspace_capacity(plan, encoded.status, encoded.required_bytes);
        }
        return make_result(
            MmsStaticReportConnectionStatus::report_encode_failed,
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
                    MmsStaticReportConnectionStatus::report_encode_failed,
                    MmsStaticUrcbStatus::report_encode_failed,
                    plan);
            }
            auto result = workspace_capacity(
                plan, MmsStaticUrcbStatus::workspace_too_small, final_required);
            result.required_response_bytes = final_required;
            return result;
        }
        return make_result(
            MmsStaticReportConnectionStatus::report_encode_failed,
            MmsStaticUrcbStatus::report_encode_failed,
            plan);
    }

    std::size_t final_required{};
    if (!add_size(p_data.bytes_written, 7U, final_required)) {
        return make_result(
            MmsStaticReportConnectionStatus::report_encode_failed,
            MmsStaticUrcbStatus::report_encode_failed,
            plan);
    }
    if (response.size() < final_required) {
        return response_capacity(
            plan, MmsStaticUrcbStatus::response_buffer_too_small, final_required);
    }
    if (workspace.size() < final_required) {
        auto result = workspace_capacity(
            plan, MmsStaticUrcbStatus::workspace_too_small, final_required);
        result.required_response_bytes = final_required;
        return result;
    }

    // P-DATA currently resides in workspace. COTP consumes it into response;
    // after that source is no longer needed, so workspace can safely become
    // the TPKT staging destination.
    const auto cotp = osi::CotpSpanCodec::encode_data_into(
        workspace.first(p_data.bytes_written),
        response.first(final_required - osi::TpktSpanCodec::header_length));
    if (!cotp.success()) {
        return make_result(
            MmsStaticReportConnectionStatus::report_encode_failed,
            MmsStaticUrcbStatus::report_encode_failed,
            plan);
    }

    const auto tpkt = osi::TpktSpanCodec::encode_into(
        response.first(cotp.bytes_written),
        workspace.first(final_required));
    if (!tpkt.success() || tpkt.bytes_written != final_required) {
        return make_result(
            MmsStaticReportConnectionStatus::report_encode_failed,
            MmsStaticUrcbStatus::report_encode_failed,
            plan);
    }

    // Commit only after the complete TPKT image exists. If capacity failed at
    // any earlier stage, the same plan/SqNum can be retried without a gap.
    const auto committed = reports.commit(plan, now_ms);
    if (committed != MmsStaticUrcbStatus::ok) {
        return make_result(
            MmsStaticReportConnectionStatus::stale_plan,
            committed,
            plan);
    }

    std::copy_n(workspace.begin(), tpkt.bytes_written, response.begin());
    auto result = make_result(
        MmsStaticReportConnectionStatus::response_ready,
        MmsStaticUrcbStatus::ok,
        plan);
    result.bytes_written = tpkt.bytes_written;
    result.required_response_bytes = final_required;
    result.required_workspace_bytes = final_required;
    return result;
}

} // namespace ar::iec61850::mms
