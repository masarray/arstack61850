from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text()


def write(path: str, text: str) -> None:
    Path(path).write_text(text)


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}: {old[:120]!r}")
    write(path, text.replace(old, new, 1))


def replace_between(path: str, start: str, end: str, new: str) -> None:
    text = read(path)
    a = text.find(start)
    if a < 0:
        raise SystemExit(f"{path}: start marker not found: {start!r}")
    b = text.find(end, a + len(start))
    if b < 0:
        raise SystemExit(f"{path}: end marker not found: {end!r}")
    if text.find(start, a + 1) >= 0:
        raise SystemExit(f"{path}: start marker not unique: {start!r}")
    write(path, text[:a] + new + text[b:])


# Shared, allocation-free COTP DT segmentation + TPKT framing. One source of
# truth is used by association/confirmed responses and unsolicited URCB/BRCB.
stream_header = r'''// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/osi/cotp_span.hpp"
#include "ariec61850/osi/tpkt_span.hpp"
#include "ariec61850/wire/encode_result.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace ar::iec61850::osi {

struct CotpTpktDataStreamPlan final {
    std::size_t maximum_user_data{};
    std::size_t segment_count{};
    std::size_t required_bytes{};
};

class CotpTpktDataStreamSpanCodec final {
public:
    [[nodiscard]] static bool try_plan(
        const std::size_t user_data_bytes,
        const std::size_t negotiated_tpdu_size_bytes,
        CotpTpktDataStreamPlan& plan) noexcept {
        plan = {};
        if (negotiated_tpdu_size_bytes <= 3U ||
            negotiated_tpdu_size_bytes > TpktSpanCodec::maximum_payload_bytes) {
            return false;
        }

        plan.maximum_user_data = negotiated_tpdu_size_bytes - 3U;
        plan.segment_count = user_data_bytes == 0U
            ? 1U
            : 1U + ((user_data_bytes - 1U) / plan.maximum_user_data);
        constexpr std::size_t per_segment_overhead =
            TpktSpanCodec::header_length + 3U;
        if (plan.segment_count >
            (std::numeric_limits<std::size_t>::max() - user_data_bytes) /
                per_segment_overhead) {
            plan = {};
            return false;
        }
        plan.required_bytes = user_data_bytes +
            plan.segment_count * per_segment_overhead;
        return true;
    }

    // Encode one TSDU as one or more complete TPKT frames. Each frame contains
    // one COTP Data TPDU whose total TPDU size never exceeds the negotiated C0
    // value. EOT is clear on intermediate segments and set only on the final
    // segment. No heap allocation or hidden queue is used.
    [[nodiscard]] static wire::EncodeResult encode_into(
        const std::span<const std::uint8_t> user_data,
        const std::size_t negotiated_tpdu_size_bytes,
        const std::span<std::uint8_t> destination) noexcept {
        CotpTpktDataStreamPlan plan;
        if (!try_plan(user_data.size(), negotiated_tpdu_size_bytes, plan)) {
            return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
        }
        if (destination.size() < plan.required_bytes) {
            return {wire::EncodeStatus::buffer_too_small, 0U, plan.required_bytes};
        }

        std::size_t input_offset{};
        std::size_t output_offset{};
        for (std::size_t segment = 0U; segment < plan.segment_count; ++segment) {
            const auto remaining = user_data.size() - input_offset;
            const auto chunk = std::min(remaining, plan.maximum_user_data);
            const bool final_segment = segment + 1U == plan.segment_count;
            const auto frame_bytes = TpktSpanCodec::header_length + 3U + chunk;
            if (frame_bytes > TpktSpanCodec::maximum_frame_bytes) {
                return {wire::EncodeStatus::value_out_of_range, 0U, plan.required_bytes};
            }

            auto frame = destination.subspan(output_offset, frame_bytes);
            const auto cotp = CotpSpanCodec::encode_data_into(
                user_data.subspan(input_offset, chunk),
                frame.subspan(TpktSpanCodec::header_length),
                final_segment,
                0U);
            if (!cotp.success() || cotp.bytes_written != chunk + 3U) {
                return {wire::EncodeStatus::value_out_of_range, 0U, plan.required_bytes};
            }

            frame[0] = TpktSpanCodec::supported_version;
            frame[1] = 0x00U;
            frame[2] = static_cast<std::uint8_t>((frame_bytes >> 8U) & 0xFFU);
            frame[3] = static_cast<std::uint8_t>(frame_bytes & 0xFFU);
            input_offset += chunk;
            output_offset += frame_bytes;
        }

        if (input_offset != user_data.size() || output_offset != plan.required_bytes) {
            return {wire::EncodeStatus::value_out_of_range, 0U, plan.required_bytes};
        }
        return {wire::EncodeStatus::ok, plan.required_bytes, plan.required_bytes};
    }
};

} // namespace ar::iec61850::osi
'''
stream_path = Path("include/ariec61850/osi/cotp_tpkt_stream.hpp")
if stream_path.exists():
    raise SystemExit(f"{stream_path}: already exists")
stream_path.write_text(stream_header)


# Core connection runtime: replace single-TPDU rejection with the shared stream
# encoder and make capacity probes segmentation-aware.
replace_once(
    "src/mms/connection_runtime.cpp",
    '#include "ariec61850/osi/cotp_span.hpp"\n',
    '#include "ariec61850/osi/cotp_span.hpp"\n#include "ariec61850/osi/cotp_tpkt_stream.hpp"\n')

core_wrap = r'''[[nodiscard]] MmsStaticConnectionResult wrap_cotp_data_response(
    const std::span<const std::uint8_t> session_or_presentation,
    const std::size_t consumed,
    const MmsStaticConnectionState state,
    const std::size_t negotiated_tpdu_size_bytes,
    const std::span<std::uint8_t> response) noexcept {
    const auto encoded = osi::CotpTpktDataStreamSpanCodec::encode_into(
        session_or_presentation, negotiated_tpdu_size_bytes, response);
    if (!encoded.success()) {
        if (encoded.status == wire::EncodeStatus::buffer_too_small) {
            return make_response_capacity(state, encoded.required_bytes);
        }
        return make_result(
            negotiated_tpdu_size_bytes == 0U
                ? MmsStaticConnectionStatus::peer_limit_exceeded
                : MmsStaticConnectionStatus::backend_failure,
            state,
            consumed);
    }
    return make_result(
        MmsStaticConnectionStatus::response_ready,
        state,
        consumed,
        encoded.bytes_written);
}

[[nodiscard]] bool framed_mms_response_size(
    const std::uint32_t presentation_context_id,
    const std::size_t mms_bytes,
    const std::size_t negotiated_tpdu_size_bytes,
    std::size_t& required) noexcept {
    required = 0U;
    const auto fully_encoded = osi::PresentationSpanCodec::fully_encoded_data_size(
        presentation_context_id, mms_bytes);
    osi::CotpTpktDataStreamPlan plan;
    if (!fully_encoded ||
        !osi::CotpTpktDataStreamSpanCodec::try_plan(
            *fully_encoded, negotiated_tpdu_size_bytes, plan)) {
        return false;
    }
    required = plan.required_bytes;
    return true;
}

'''
replace_between(
    "src/mms/connection_runtime.cpp",
    "[[nodiscard]] MmsStaticConnectionResult wrap_cotp_data_response(",
    "[[nodiscard]] MmsConfirmedRequestRejectReason reject_reason_for(",
    core_wrap)

core_capacity = r'''[[nodiscard]] bool write_outer_capacity(
    const MmsConfirmedPduView& confirmed,
    const MmsStaticApplicationDispatcher& dispatcher,
    const std::uint32_t presentation_context_id,
    const std::size_t negotiated_tpdu_size_bytes,
    std::size_t& mms_required,
    std::size_t& frame_required) noexcept {
    mms_required = 0U;
    frame_required = 0U;
    if (confirmed.service() != MmsWireConfirmedService::write) {
        return false;
    }

    MmsWriteRequestView write;
    if (!MmsServiceSpanCodec::try_decode_write_request(confirmed, write) ||
        write.variable_count == 0U ||
        write.variable_count > dispatcher.policy().maximum_write_variables) {
        return false;
    }

    std::array<MmsWriteAccessResultInput, MmsServiceSpanCodec::maximum_variables> worst{};
    for (std::size_t index = 0U; index < write.variable_count; ++index) {
        worst[index] = MmsWriteAccessResultInput{
            false,
            std::numeric_limits<std::uint32_t>::max()};
    }
    const auto probe = MmsServiceSpanCodec::encode_write_response_into(
        confirmed.invoke_id,
        std::span<const MmsWriteAccessResultInput>{worst}.first(write.variable_count),
        {});
    if (probe.status != wire::EncodeStatus::buffer_too_small ||
        probe.required_bytes == 0U) {
        return false;
    }
    mms_required = probe.required_bytes;
    return framed_mms_response_size(
        presentation_context_id,
        probe.required_bytes,
        negotiated_tpdu_size_bytes,
        frame_required);
}
'''
replace_between(
    "src/mms/connection_runtime.cpp",
    "[[nodiscard]] bool write_outer_capacity(",
    "\n}\n\n} // namespace",
    core_capacity)

replace_once(
    "src/mms/connection_runtime.cpp",
    "                mms_presentation_context_id_,\n                write_mms_required,",
    "                mms_presentation_context_id_,\n                negotiated_tpdu_size_bytes_,\n                write_mms_required,")
replace_once(
    "src/mms/connection_runtime.cpp",
    "            if (negotiated_mms_pdu_size_ == 0U ||\n                write_mms_required > negotiated_mms_pdu_size_ ||\n                write_frame_required < osi::TpktSpanCodec::header_length ||\n                write_frame_required - osi::TpktSpanCodec::header_length >\n                    negotiated_tpdu_size_bytes_) {",
    "            if (negotiated_mms_pdu_size_ == 0U ||\n                write_mms_required > negotiated_mms_pdu_size_) {")
replace_once(
    "src/mms/connection_runtime.cpp",
    "            response,\n            workspace);\n        if (!wrapped.response_ready()) {",
    "            response);\n        if (!wrapped.response_ready()) {")
replace_once(
    "src/mms/connection_runtime.cpp",
    "        response,\n        workspace);\n    if (wrapped.response_ready()) {",
    "        response);\n    if (wrapped.response_ready()) {")
replace_once(
    "src/mms/connection_runtime.cpp",
    "            const auto fully_encoded = osi::PresentationSpanCodec::fully_encoded_data_size(\n                mms_presentation_context_id_, application.required_bytes);\n            std::size_t required{};\n            if (fully_encoded && add_overhead(*fully_encoded, 11U, required)) {",
    "            std::size_t required{};\n            if (framed_mms_response_size(\n                    mms_presentation_context_id_,\n                    application.required_bytes,\n                    negotiated_tpdu_size_bytes_,\n                    required)) {")
replace_once(
    "src/mms/connection_runtime.cpp",
    "                const auto fully_encoded = osi::PresentationSpanCodec::fully_encoded_data_size(\n                    mms_presentation_context_id_, rejected.required_bytes);\n                std::size_t required{};\n                if (fully_encoded && add_overhead(*fully_encoded, 11U, required)) {",
    "                std::size_t required{};\n                if (framed_mms_response_size(\n                        mms_presentation_context_id_,\n                        rejected.required_bytes,\n                        negotiated_tpdu_size_bytes_,\n                        required)) {")
replace_once(
    "src/mms/connection_runtime.cpp",
    "        if (p_data.status == wire::EncodeStatus::buffer_too_small) {\n            std::size_t required{};\n            if (add_overhead(p_data.required_bytes, 7U, required)) {\n                return make_workspace_capacity(state_, required);\n            }\n        }",
    "        if (p_data.status == wire::EncodeStatus::buffer_too_small) {\n            return make_workspace_capacity(state_, p_data.required_bytes);\n        }")


# IED-simulator runtime already had local segmentation. Route its planning and
# encoding through the same transport primitive so strict/core and simulator do
# not drift again.
replace_once(
    "src/mms/connection_runtime_iedsim.cpp",
    '#include "ariec61850/osi/cotp_span.hpp"\n',
    '#include "ariec61850/osi/cotp_span.hpp"\n#include "ariec61850/osi/cotp_tpkt_stream.hpp"\n')
iedsim_plan = r'''[[nodiscard]] bool cotp_stream_size(
    const std::size_t payload_bytes,
    const std::size_t negotiated_tpdu_size_bytes,
    std::size_t& maximum_user_data,
    std::size_t& segment_count,
    std::size_t& required) noexcept {
    osi::CotpTpktDataStreamPlan plan;
    if (!osi::CotpTpktDataStreamSpanCodec::try_plan(
            payload_bytes, negotiated_tpdu_size_bytes, plan)) {
        maximum_user_data = 0U;
        segment_count = 0U;
        required = 0U;
        return false;
    }
    maximum_user_data = plan.maximum_user_data;
    segment_count = plan.segment_count;
    required = plan.required_bytes;
    return true;
}

'''
replace_between(
    "src/mms/connection_runtime_iedsim.cpp",
    "[[nodiscard]] bool cotp_stream_size(",
    "[[nodiscard]] MmsStaticConnectionResult wrap_cotp_data_response(",
    iedsim_plan)
iedsim_wrap = r'''[[nodiscard]] MmsStaticConnectionResult wrap_cotp_data_response(
    const std::span<const std::uint8_t> session_or_presentation,
    const std::size_t consumed,
    const MmsStaticConnectionState state,
    const std::size_t negotiated_tpdu_size_bytes,
    const std::span<std::uint8_t> response) noexcept {
    const auto encoded = osi::CotpTpktDataStreamSpanCodec::encode_into(
        session_or_presentation, negotiated_tpdu_size_bytes, response);
    if (!encoded.success()) {
        if (encoded.status == wire::EncodeStatus::buffer_too_small) {
            return make_response_capacity(state, encoded.required_bytes);
        }
        return make_result(
            negotiated_tpdu_size_bytes == 0U
                ? MmsStaticConnectionStatus::peer_limit_exceeded
                : MmsStaticConnectionStatus::backend_failure,
            state,
            consumed);
    }
    return make_result(
        MmsStaticConnectionStatus::response_ready,
        state,
        consumed,
        encoded.bytes_written);
}

'''
replace_between(
    "src/mms/connection_runtime_iedsim.cpp",
    "[[nodiscard]] MmsStaticConnectionResult wrap_cotp_data_response(",
    "[[nodiscard]] MmsStaticConnectionResult application_rejected(",
    iedsim_wrap)


# URCB unsolicited InformationReport: segment after Presentation wrapping and
# only commit SqNum/GI/integrity state after the complete stream is staged.
replace_once(
    "src/mms/static_report_connection.cpp",
    '#include "ariec61850/osi/cotp_span.hpp"\n',
    '#include "ariec61850/osi/cotp_span.hpp"\n#include "ariec61850/osi/cotp_tpkt_stream.hpp"\n')
replace_once(
    "src/mms/static_report_connection.cpp",
    "[[nodiscard]] bool final_frame_size(\n    const std::uint32_t presentation_context_id,\n    const std::size_t mms_bytes,\n    std::size_t& required) noexcept {\n    const auto fully_encoded = osi::PresentationSpanCodec::fully_encoded_data_size(\n        presentation_context_id, mms_bytes);\n    return fully_encoded && add_size(*fully_encoded, 11U, required);\n}",
    "[[nodiscard]] bool final_frame_size(\n    const std::uint32_t presentation_context_id,\n    const std::size_t mms_bytes,\n    const std::size_t negotiated_tpdu_size_bytes,\n    std::size_t& required) noexcept {\n    required = 0U;\n    const auto fully_encoded = osi::PresentationSpanCodec::fully_encoded_data_size(\n        presentation_context_id, mms_bytes);\n    osi::CotpTpktDataStreamPlan plan;\n    if (!fully_encoded ||\n        !osi::CotpTpktDataStreamSpanCodec::try_plan(\n            *fully_encoded, negotiated_tpdu_size_bytes, plan)) {\n        return false;\n    }\n    required = plan.required_bytes;\n    return true;\n}")
replace_once(
    "src/mms/static_report_connection.cpp",
    "                    connection.mms_presentation_context_id(),\n                    encoded.required_bytes,\n                    final_required)) {",
    "                    connection.mms_presentation_context_id(),\n                    encoded.required_bytes,\n                    connection.negotiated_tpdu_size_bytes(),\n                    final_required)) {")
report_stream = r'''    const auto framed = osi::CotpTpktDataStreamSpanCodec::encode_into(
        workspace.first(p_data.bytes_written),
        connection.negotiated_tpdu_size_bytes(),
        response);
    if (!framed.success()) {
        if (framed.status == wire::EncodeStatus::buffer_too_small) {
            return response_capacity(
                plan, MmsStaticUrcbStatus::response_buffer_too_small,
                framed.required_bytes);
        }
        return make_result(
            MmsStaticReportConnectionStatus::report_encode_failed,
            MmsStaticUrcbStatus::report_encode_failed,
            plan);
    }

'''
replace_between(
    "src/mms/static_report_connection.cpp",
    "    std::size_t final_required{};\n    if (!add_size(p_data.bytes_written, 7U, final_required))",
    "    // Commit only after the complete TPKT image exists.",
    report_stream)
replace_once(
    "src/mms/static_report_connection.cpp",
    "    // Commit only after the complete TPKT image exists. If capacity failed at\n",
    "    // Commit only after the complete segmented COTP/TPKT stream exists. If capacity failed at\n")
replace_once(
    "src/mms/static_report_connection.cpp",
    "    std::copy_n(workspace.begin(), tpkt.bytes_written, response.begin());\n    auto result = make_result(\n",
    "    auto result = make_result(\n")
replace_once(
    "src/mms/static_report_connection.cpp",
    "    result.bytes_written = tpkt.bytes_written;\n    result.required_response_bytes = final_required;\n    result.required_workspace_bytes = final_required;",
    "    result.bytes_written = framed.bytes_written;\n    result.required_response_bytes = framed.required_bytes;\n    result.required_workspace_bytes = p_data.bytes_written;")


# BRCB retained delivery: same segmentation semantics, while the MMS PDU limit
# remains a separate hard peer limit.
replace_once(
    "src/mms/static_brcb_connection.cpp",
    '#include "ariec61850/osi/cotp_span.hpp"\n',
    '#include "ariec61850/osi/cotp_span.hpp"\n#include "ariec61850/osi/cotp_tpkt_stream.hpp"\n')
replace_once(
    "src/mms/static_brcb_connection.cpp",
    "        std::size_t final_required{};\n        if (!add_size(p_data.required_bytes, 7U, final_required)) {\n            return make_result(MmsStaticBrcbConnectionStatus::frame_encode_failed, &entry);\n        }\n        auto result = make_result(\n            MmsStaticBrcbConnectionStatus::workspace_too_small, &entry);\n        result.required_workspace_bytes = final_required;\n        result.required_response_bytes = final_required;",
    "        osi::CotpTpktDataStreamPlan plan;\n        if (!osi::CotpTpktDataStreamSpanCodec::try_plan(\n                p_data.required_bytes, connection.negotiated_tpdu_size_bytes(), plan)) {\n            return make_result(MmsStaticBrcbConnectionStatus::peer_limit_exceeded, &entry);\n        }\n        auto result = make_result(\n            MmsStaticBrcbConnectionStatus::workspace_too_small, &entry);\n        result.required_workspace_bytes = p_data.required_bytes;\n        result.required_response_bytes = plan.required_bytes;")
brcb_stream = r'''    const auto framed = osi::CotpTpktDataStreamSpanCodec::encode_into(
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

'''
replace_between(
    "src/mms/static_brcb_connection.cpp",
    "    std::size_t cotp_required{};\n    if (!add_size(p_data.bytes_written, 3U, cotp_required))",
    "    auto result = make_result(MmsStaticBrcbConnectionStatus::response_ready, &entry);",
    brcb_stream)
replace_once(
    "src/mms/static_brcb_connection.cpp",
    "    std::copy_n(workspace.begin(), tpkt.bytes_written, response.begin());\n    result.bytes_written = tpkt.bytes_written;\n    result.required_response_bytes = final_required;\n    result.required_workspace_bytes = final_required;",
    "    result.bytes_written = framed.bytes_written;\n    result.required_response_bytes = framed.required_bytes;\n    result.required_workspace_bytes = p_data.bytes_written;")


# Deterministic regression: 128-byte negotiated TPDU must produce a stream of
# bounded TPKTs instead of peer_limit_exceeded. Keep MMS max-PDU rejection tests.
helper = r'''[[nodiscard]] bool validate_segmented_stream(
    const std::span<const std::uint8_t> bytes,
    const std::size_t negotiated_tpdu_size_bytes,
    const bool require_multiple) noexcept {
    std::size_t offset{};
    std::size_t segments{};
    while (offset < bytes.size()) {
        const auto remaining = bytes.subspan(offset);
        const auto peek = osi::TpktSpanCodec::peek_frame(remaining);
        if (!peek.ready() || peek.frame_bytes == 0U ||
            peek.frame_bytes > negotiated_tpdu_size_bytes + osi::TpktSpanCodec::header_length) {
            return false;
        }
        osi::TpktFrameView tpkt;
        osi::CotpTpduView cotp;
        if (!osi::TpktSpanCodec::try_decode_view(remaining.first(peek.frame_bytes), tpkt) ||
            !osi::CotpSpanCodec::try_decode_view(tpkt.payload, cotp) ||
            cotp.kind != osi::CotpWireKind::data) {
            return false;
        }
        offset += peek.frame_bytes;
        ++segments;
        if ((offset == bytes.size()) != cotp.end_of_transmission) {
            return false;
        }
    }
    return offset == bytes.size() && segments != 0U &&
        (!require_multiple || segments > 1U);
}

'''
replace_once(
    "embedded/mms_negotiated_limits_hard_profile_smoke.cpp",
    "[[nodiscard]] bool set_maximum_mms_pdu(\n",
    helper + "[[nodiscard]] bool set_maximum_mms_pdu(\n")
replace_once(
    "embedded/mms_negotiated_limits_hard_profile_smoke.cpp",
    "    // A 128-byte TPDU cannot carry the fixed association-accept TSDU as one DT\n    // TPDU. Until outbound COTP segmentation is implemented, reject it rather\n    // than violate the negotiated limit.",
    "    // A 128-byte negotiated TPDU must segment the association accept and\n    // later confirmed responses into bounded TPKTs instead of rejecting them.")
replace_once(
    "embedded/mms_negotiated_limits_hard_profile_smoke.cpp",
    "    if (tiny_association.status != mms::MmsStaticConnectionStatus::peer_limit_exceeded ||\n        tiny_association.bytes_written != 0U ||\n        tiny_tpdu.negotiated_tpdu_size_bytes() != 128U ||\n        tiny_tpdu.state() != mms::MmsStaticConnectionState::awaiting_association) {\n        return 3;\n    }",
    "    if (!tiny_association.response_ready() ||\n        tiny_tpdu.negotiated_tpdu_size_bytes() != 128U ||\n        tiny_tpdu.state() != mms::MmsStaticConnectionState::established ||\n        !validate_segmented_stream(\n            std::span<const std::uint8_t>{response}.first(tiny_association.bytes_written),\n            128U,\n            true)) {\n        return 3;\n    }\n    const auto tiny_read_tpkt = build_mms_tpkt(\n        kReadRequest, request, presentation, scratch);\n    if (!tiny_read_tpkt.success()) return 21;\n    const auto tiny_read_result = tiny_tpdu.process_tcp_window(\n        std::span<const std::uint8_t>{request}.first(tiny_read_tpkt.bytes_written),\n        response,\n        workspace);\n    if (!tiny_read_result.response_ready() ||\n        !validate_segmented_stream(\n            std::span<const std::uint8_t>{response}.first(tiny_read_result.bytes_written),\n            128U,\n            true)) {\n        return 22;\n    }")


# URCB server regression: use the IEDScout-like 1024-byte offer in production,
# but force 128 here plus a long RptID so the InformationReport definitely spans
# multiple TPKTs. Reassemble only inside the test decoder.
replace_once(
    "tests/test_mms_server_urcb_report.cpp",
    "    constexpr std::array<std::uint8_t, 1U> tpdu_size{0x0AU};",
    "    constexpr std::array<std::uint8_t, 1U> tpdu_size{0x07U};")
urcb_decode = r'''[[nodiscard]] bool decode_report_frame(
    const std::span<const std::uint8_t> frame,
    mms::MmsInformationReportView& report,
    std::size_t* segment_count = nullptr) noexcept {
    std::array<std::uint8_t, 2048U> reassembled{};
    std::size_t input_offset{};
    std::size_t reassembled_size{};
    std::size_t segments{};
    while (input_offset < frame.size()) {
        const auto remaining = frame.subspan(input_offset);
        const auto peek = osi::TpktSpanCodec::peek_frame(remaining);
        if (!peek.ready() || peek.frame_bytes == 0U) return false;
        osi::TpktFrameView tpkt;
        osi::CotpTpduView cotp;
        if (!osi::TpktSpanCodec::try_decode_view(remaining.first(peek.frame_bytes), tpkt) ||
            !osi::CotpSpanCodec::try_decode_view(tpkt.payload, cotp) ||
            cotp.kind != osi::CotpWireKind::data ||
            cotp.user_data.size() > reassembled.size() - reassembled_size) {
            return false;
        }
        std::copy(cotp.user_data.begin(), cotp.user_data.end(),
            reassembled.begin() + static_cast<std::ptrdiff_t>(reassembled_size));
        reassembled_size += cotp.user_data.size();
        input_offset += peek.frame_bytes;
        ++segments;
        if ((input_offset == frame.size()) != cotp.end_of_transmission) return false;
    }
    if (segment_count != nullptr) *segment_count = segments;
    osi::SessionDataTransferView session;
    osi::PresentationPdvView pdv;
    return segments != 0U &&
        osi::SessionSpanCodec::try_decode_data_transfer_view(
            std::span<const std::uint8_t>{reassembled}.first(reassembled_size), session) &&
        osi::PresentationSpanCodec::try_decode_fully_encoded_data_view(
            session.presentation_payload, pdv) &&
        pdv.context_id == 3U &&
        mms::MmsInformationReportSpanCodec::try_decode_information_report(
            pdv.single_asn1_type, report);
}

'''
replace_between(
    "tests/test_mms_server_urcb_report.cpp",
    "[[nodiscard]] bool decode_report_frame(",
    "[[nodiscard]] bool decode_unsigned_item(",
    urcb_decode)
replace_once(
    "tests/test_mms_server_urcb_report.cpp",
    '            "LD0/LLN0$RP$Events",\n',
    '            "LD0/LLN0$RP$Events_P0_3_Negotiated_TPDU_Segmentation_Proof",\n')
replace_once(
    "tests/test_mms_server_urcb_report.cpp",
    "    mms::MmsInformationReportView report;\n    if (!decode_report_frame(\n            std::span<const std::uint8_t>{response}.first(poll.bytes_written), report) ||\n        report.item_count != 13U) {",
    "    mms::MmsInformationReportView report;\n    std::size_t report_segments{};\n    if (!decode_report_frame(\n            std::span<const std::uint8_t>{response}.first(poll.bytes_written),\n            report,\n            &report_segments) ||\n        report_segments < 2U || report.item_count != 13U) {")


# BRCB delivery regression: same small negotiated TPDU and stream reassembly.
replace_once(
    "embedded/mms_brcb_connection_hard_profile_smoke.cpp",
    "    result.owner_size = owner.size();\n    return result;\n}",
    "    result.owner_size = owner.size();\n    result.maximum_tpdu_size_code = 0x07U;\n    return result;\n}")
replace_once(
    "embedded/mms_brcb_connection_hard_profile_smoke.cpp",
    "    constexpr std::array<std::uint8_t, 1U> tpdu_size{0x0AU};",
    "    constexpr std::array<std::uint8_t, 1U> tpdu_size{0x07U};")
brcb_decode = urcb_decode
replace_between(
    "embedded/mms_brcb_connection_hard_profile_smoke.cpp",
    "[[nodiscard]] bool decode_report_frame(",
    "[[nodiscard]] bool decode_boolean(",
    brcb_decode)
replace_once(
    "embedded/mms_brcb_connection_hard_profile_smoke.cpp",
    '        "LD0", "LLN0$BR$Events", "LD0/LLN0$BR$Events",\n',
    '        "LD0", "LLN0$BR$Events",\n        "LD0/LLN0$BR$Events_P0_3_Negotiated_TPDU_Segmentation_Proof",\n')
replace_once(
    "embedded/mms_brcb_connection_hard_profile_smoke.cpp",
    "    mms::MmsInformationReportView decoded;\n    if (!decode_report_frame(\n            std::span<const std::uint8_t>{response}.first(retry.bytes_written), decoded) ||\n        decoded.item_count != 12U) {",
    "    mms::MmsInformationReportView decoded;\n    std::size_t report_segments{};\n    if (!decode_report_frame(\n            std::span<const std::uint8_t>{response}.first(retry.bytes_written),\n            decoded,\n            &report_segments) ||\n        report_segments < 2U || decoded.item_count != 12U) {")
