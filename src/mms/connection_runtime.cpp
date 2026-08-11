// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/connection_runtime.hpp"

#include "ariec61850/acse/association_span.hpp"
#include "ariec61850/asn1/ber_span_writer.hpp"
#include "ariec61850/mms/pdu_span.hpp"
#include "ariec61850/mms/services_span.hpp"
#include "ariec61850/osi/cotp_span.hpp"
#include "ariec61850/osi/presentation_span.hpp"
#include "ariec61850/osi/session_span.hpp"
#include "ariec61850/osi/tpkt_span.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace ar::iec61850::mms {
namespace {

// Must remain aligned with MmsPduSpanCodec::encode_default_initiate_response_into().
constexpr std::uint32_t kServerMaximumMmsPduSize = 65'000U;
constexpr std::uint32_t kServerMaximumOutstandingCalling = 10U;
constexpr std::uint32_t kServerMaximumOutstandingCalled = 10U;
constexpr std::uint32_t kServerMaximumNestingLevel = 5U;

[[nodiscard]] MmsStaticConnectionResult make_result(
    const MmsStaticConnectionStatus status,
    const MmsStaticConnectionState state,
    const std::size_t consumed = 0U,
    const std::size_t written = 0U) noexcept {
    MmsStaticConnectionResult result;
    result.status = status;
    result.state = state;
    result.consumed_bytes = consumed;
    result.bytes_written = written;
    return result;
}

[[nodiscard]] MmsStaticConnectionResult make_response_capacity(
    const MmsStaticConnectionState state,
    const std::size_t required) noexcept {
    auto result = make_result(
        MmsStaticConnectionStatus::response_buffer_too_small, state);
    result.required_response_bytes = required;
    return result;
}

[[nodiscard]] MmsStaticConnectionResult make_workspace_capacity(
    const MmsStaticConnectionState state,
    const std::size_t required) noexcept {
    auto result = make_result(
        MmsStaticConnectionStatus::workspace_too_small, state);
    result.required_workspace_bytes = required;
    return result;
}

[[nodiscard]] bool add_overhead(
    const std::size_t base,
    const std::size_t overhead,
    std::size_t& total) noexcept {
    if (base > std::numeric_limits<std::size_t>::max() - overhead) {
        total = 0U;
        return false;
    }
    total = base + overhead;
    return true;
}

[[nodiscard]] bool selected_tpdu_size_bytes(
    const osi::CotpTpduView& request,
    const std::uint8_t local_maximum_code,
    std::size_t& selected_bytes) noexcept {
    selected_bytes = 0U;
    std::size_t local_bytes{};
    if (!osi::CotpSpanCodec::try_tpdu_size_bytes(local_maximum_code, local_bytes)) {
        return false;
    }

    std::span<const std::uint8_t> offered;
    if (!request.try_parameter(osi::CotpSpanCodec::tpdu_size_parameter, offered)) {
        selected_bytes = local_bytes;
        return true;
    }
    if (offered.size() != 1U) {
        return false;
    }

    std::size_t peer_bytes{};
    if (!osi::CotpSpanCodec::try_tpdu_size_bytes(offered[0], peer_bytes)) {
        return false;
    }
    selected_bytes = std::min(local_bytes, peer_bytes);
    return true;
}

[[nodiscard]] MmsStaticConnectionResult wrap_cotp_data_response(
    const std::span<const std::uint8_t> session_or_presentation,
    const std::size_t consumed,
    const MmsStaticConnectionState state,
    const std::size_t negotiated_tpdu_size_bytes,
    const std::span<std::uint8_t> response,
    const std::span<std::uint8_t> workspace) noexcept {
    std::size_t cotp_required{};
    if (!add_overhead(session_or_presentation.size(), 3U, cotp_required)) {
        return make_result(MmsStaticConnectionStatus::backend_failure, state);
    }
    if (negotiated_tpdu_size_bytes == 0U ||
        cotp_required > negotiated_tpdu_size_bytes) {
        return make_result(
            MmsStaticConnectionStatus::peer_limit_exceeded, state, consumed);
    }

    std::size_t required{};
    if (!add_overhead(cotp_required, osi::TpktSpanCodec::header_length, required)) {
        return make_result(MmsStaticConnectionStatus::backend_failure, state);
    }
    if (response.size() < required) {
        return make_response_capacity(state, required);
    }
    if (workspace.size() < required) {
        return make_workspace_capacity(state, required);
    }

    const auto cotp = osi::CotpSpanCodec::encode_data_into(
        session_or_presentation,
        response.first(required - osi::TpktSpanCodec::header_length));
    if (!cotp.success()) {
        if (cotp.status == wire::EncodeStatus::buffer_too_small) {
            return make_response_capacity(
                state, cotp.required_bytes + osi::TpktSpanCodec::header_length);
        }
        return make_result(MmsStaticConnectionStatus::backend_failure, state);
    }

    const auto tpkt = osi::TpktSpanCodec::encode_into(
        response.first(cotp.bytes_written), workspace.first(required));
    if (!tpkt.success() || tpkt.bytes_written != required) {
        if (tpkt.status == wire::EncodeStatus::buffer_too_small) {
            return make_workspace_capacity(state, tpkt.required_bytes);
        }
        return make_result(MmsStaticConnectionStatus::backend_failure, state);
    }
    std::copy_n(workspace.begin(), tpkt.bytes_written, response.begin());
    return make_result(
        MmsStaticConnectionStatus::response_ready,
        state,
        consumed,
        tpkt.bytes_written);
}

[[nodiscard]] MmsConfirmedRequestRejectReason reject_reason_for(
    const MmsStaticDispatchStatus status) noexcept {
    switch (status) {
    case MmsStaticDispatchStatus::unsupported_service:
        return MmsConfirmedRequestRejectReason::unrecognized_service;
    case MmsStaticDispatchStatus::malformed_request:
    case MmsStaticDispatchStatus::unsupported_request:
        return MmsConfirmedRequestRejectReason::invalid_argument;
    case MmsStaticDispatchStatus::object_not_found:
    case MmsStaticDispatchStatus::response_ready:
    case MmsStaticDispatchStatus::invalid_object_table:
    case MmsStaticDispatchStatus::workspace_too_small:
    case MmsStaticDispatchStatus::response_buffer_too_small:
    case MmsStaticDispatchStatus::backend_failure:
        return MmsConfirmedRequestRejectReason::other;
    }
    return MmsConfirmedRequestRejectReason::other;
}

[[nodiscard]] std::size_t positive_integer_size(const std::uint32_t value) noexcept {
    std::size_t bytes = 1U;
    auto remaining = value;
    while (remaining > 0xFFU) {
        ++bytes;
        remaining >>= 8U;
    }
    const auto leading = static_cast<std::uint8_t>(
        value >> static_cast<unsigned>((bytes - 1U) * 8U));
    return bytes + ((leading & 0x80U) != 0U ? 1U : 0U);
}

[[nodiscard]] bool write_positive_integer(
    asn1::BerSpanWriter& writer,
    const std::uint32_t value,
    const std::size_t bytes) noexcept {
    const auto encoded_bytes = bytes -
        ((bytes > 1U &&
          (value >> static_cast<unsigned>((bytes - 2U) * 8U)) <= 0xFFU)
            ? 1U
            : 0U);
    if (encoded_bytes < bytes && !writer.write_byte(0U)) {
        return false;
    }
    for (std::size_t index = encoded_bytes; index > 0U; --index) {
        const auto shift = static_cast<unsigned>((index - 1U) * 8U);
        if (!writer.write_byte(static_cast<std::uint8_t>((value >> shift) & 0xFFU))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] wire::EncodeResult encode_object_not_found_confirmed_error(
    const std::uint32_t invoke_id,
    const std::span<std::uint8_t> destination) noexcept {
    if (invoke_id > MmsPduSpanCodec::maximum_invoke_id) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }

    // ConfirmedErrorPDU ::= [2] { invokeID [0], serviceError [2] }
    // serviceError.errorClass uses access[7] object-non-existent(2).
    const auto invoke_bytes = positive_integer_size(invoke_id);
    const auto invoke_tlv = asn1::BerSpanWriter::tlv_size(0, invoke_bytes);
    const auto access_tlv = asn1::BerSpanWriter::tlv_size(7, 1U);
    if (!invoke_tlv || !access_tlv) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto error_class_tlv = asn1::BerSpanWriter::tlv_size(0, *access_tlv);
    if (!error_class_tlv) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto service_error_tlv = asn1::BerSpanWriter::tlv_size(2, *error_class_tlv);
    if (!service_error_tlv ||
        *invoke_tlv > std::numeric_limits<std::size_t>::max() - *service_error_tlv) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    const auto content = *invoke_tlv + *service_error_tlv;
    const auto required = asn1::BerSpanWriter::tlv_size(2, content);
    if (!required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    if (destination.size() < *required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, *required};
    }

    asn1::BerSpanWriter writer{destination.first(*required)};
    if (!writer.write_tlv_header(asn1::BerClass::context_specific, true, 2, content) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, false, 0, invoke_bytes) ||
        !write_positive_integer(writer, invoke_id, invoke_bytes) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 2, *error_class_tlv) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, true, 0, *access_tlv) ||
        !writer.write_tlv_header(
            asn1::BerClass::context_specific, false, 7, 1U) ||
        !writer.write_byte(2U) || writer.size() != *required) {
        return {wire::EncodeStatus::value_out_of_range, 0U, *required};
    }
    return {wire::EncodeStatus::ok, *required, *required};
}

[[nodiscard]] bool write_outer_capacity(
    const MmsConfirmedPduView& confirmed,
    const MmsStaticApplicationDispatcher& dispatcher,
    const std::uint32_t presentation_context_id,
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
    const auto fully_encoded = osi::PresentationSpanCodec::fully_encoded_data_size(
        presentation_context_id, probe.required_bytes);
    return fully_encoded && add_overhead(*fully_encoded, 11U, frame_required);
}

} // namespace

void MmsStaticConnectionRuntime::notify_association_closed() noexcept {
    if (!association_active_ || association_close_notified_) {
        return;
    }

    association_close_notified_ = true;
    association_active_ = false;
    if (policy_.association_closed == nullptr || policy_.association_id == 0U) {
        return;
    }

    const auto now = policy_.now_ms == nullptr
        ? std::uint64_t{0U}
        : policy_.now_ms(policy_.now_context);
    policy_.association_closed(
        policy_.association_closed_context,
        policy_.association_id,
        now);
}

void MmsStaticConnectionRuntime::close_transport() noexcept {
    notify_association_closed();
    state_ = MmsStaticConnectionState::closed;
    mms_presentation_context_id_ = 0U;
    negotiated_tpdu_size_bytes_ = 0U;
    negotiated_mms_pdu_size_ = 0U;
}

void MmsStaticConnectionRuntime::reset() noexcept {
    notify_association_closed();
    state_ = MmsStaticConnectionState::awaiting_cotp_connect;
    mms_presentation_context_id_ = 0U;
    negotiated_tpdu_size_bytes_ = 0U;
    negotiated_mms_pdu_size_ = 0U;
    association_active_ = false;
    association_close_notified_ = false;
}

MmsStaticConnectionResult MmsStaticConnectionRuntime::process_tcp_window(
    const std::span<const std::uint8_t> tcp_bytes,
    const std::span<std::uint8_t> response,
    const std::span<std::uint8_t> workspace) noexcept {
    if (state_ == MmsStaticConnectionState::closed) {
        return make_result(MmsStaticConnectionStatus::closed, state_);
    }
    if (state_ == MmsStaticConnectionState::fault) {
        return make_result(MmsStaticConnectionStatus::protocol_violation, state_);
    }

    const auto peek = osi::TpktSpanCodec::peek_frame(tcp_bytes);
    if (peek.status == osi::TpktPeekStatus::need_more) {
        return make_result(MmsStaticConnectionStatus::need_more, state_);
    }
    if (peek.status != osi::TpktPeekStatus::ready || peek.frame_bytes == 0U) {
        state_ = MmsStaticConnectionState::fault;
        return make_result(MmsStaticConnectionStatus::malformed_transport, state_);
    }

    const auto frame_bytes = tcp_bytes.first(peek.frame_bytes);
    osi::TpktFrameView tpkt;
    osi::CotpTpduView cotp;
    if (!osi::TpktSpanCodec::try_decode_view(frame_bytes, tpkt) ||
        !osi::CotpSpanCodec::try_decode_view(tpkt.payload, cotp)) {
        state_ = MmsStaticConnectionState::fault;
        return make_result(
            MmsStaticConnectionStatus::malformed_transport, state_, peek.frame_bytes);
    }

    if (cotp.kind == osi::CotpWireKind::disconnect_request) {
        close_transport();
        return make_result(
            MmsStaticConnectionStatus::closed, state_, peek.frame_bytes);
    }

    if (state_ == MmsStaticConnectionState::awaiting_cotp_connect) {
        if (cotp.kind != osi::CotpWireKind::connection_request) {
            state_ = MmsStaticConnectionState::fault;
            return make_result(
                MmsStaticConnectionStatus::protocol_violation,
                state_,
                peek.frame_bytes);
        }

        std::size_t selected_tpdu_bytes{};
        if (!selected_tpdu_size_bytes(
                cotp, policy_.maximum_tpdu_size_code, selected_tpdu_bytes)) {
            state_ = MmsStaticConnectionState::fault;
            return make_result(
                MmsStaticConnectionStatus::protocol_violation,
                state_,
                peek.frame_bytes);
        }

        const auto cc = osi::CotpSpanCodec::encode_connection_confirm_from_request_into(
            cotp,
            policy_.cotp_source_reference,
            workspace,
            policy_.maximum_tpdu_size_code);
        if (!cc.success()) {
            if (cc.status == wire::EncodeStatus::buffer_too_small) {
                return make_workspace_capacity(state_, cc.required_bytes);
            }
            state_ = MmsStaticConnectionState::fault;
            return make_result(MmsStaticConnectionStatus::backend_failure, state_);
        }

        std::size_t required{};
        if (!add_overhead(cc.bytes_written, osi::TpktSpanCodec::header_length, required)) {
            state_ = MmsStaticConnectionState::fault;
            return make_result(MmsStaticConnectionStatus::backend_failure, state_);
        }
        if (response.size() < required) {
            return make_response_capacity(state_, required);
        }
        const auto encoded = osi::TpktSpanCodec::encode_into(
            workspace.first(cc.bytes_written), response.first(required));
        if (!encoded.success()) {
            if (encoded.status == wire::EncodeStatus::buffer_too_small) {
                return make_response_capacity(state_, encoded.required_bytes);
            }
            state_ = MmsStaticConnectionState::fault;
            return make_result(MmsStaticConnectionStatus::backend_failure, state_);
        }

        negotiated_tpdu_size_bytes_ = selected_tpdu_bytes;
        state_ = MmsStaticConnectionState::awaiting_association;
        return make_result(
            MmsStaticConnectionStatus::response_ready,
            state_,
            peek.frame_bytes,
            encoded.bytes_written);
    }

    if (cotp.kind != osi::CotpWireKind::data ||
        (policy_.require_end_of_transmission && !cotp.end_of_transmission)) {
        state_ = MmsStaticConnectionState::fault;
        return make_result(
            MmsStaticConnectionStatus::protocol_violation,
            state_,
            peek.frame_bytes);
    }

    if (state_ == MmsStaticConnectionState::awaiting_association) {
        acse::AssociationRequestView association;
        if (!acse::AcseSpanCodec::try_decode_association_request_view(
                cotp.user_data, association)) {
            state_ = MmsStaticConnectionState::fault;
            return make_result(
                MmsStaticConnectionStatus::protocol_violation,
                state_,
                peek.frame_bytes);
        }

        MmsInitiateView initiate;
        if (!MmsPduSpanCodec::try_decode_initiate_request_view(
                association.aarq.user_information.single_asn1_type, initiate) ||
            association.mms_presentation_context_id == 0U) {
            state_ = MmsStaticConnectionState::fault;
            return make_result(
                MmsStaticConnectionStatus::protocol_violation,
                state_,
                peek.frame_bytes);
        }
        const auto selected_mms_pdu_size = std::min(
            initiate.maximum_mms_pdu_size, kServerMaximumMmsPduSize);

        const auto selected_outstanding_calling = std::min(
            initiate.maximum_outstanding_calling,
            kServerMaximumOutstandingCalling);
        const auto selected_outstanding_called = std::min(
            initiate.maximum_outstanding_called,
            kServerMaximumOutstandingCalled);
        const auto selected_nesting_level = std::min(
            initiate.data_structure_nesting_level,
            kServerMaximumNestingLevel);
        std::array<std::uint8_t, 64U> initiate_response{};
        const auto initiate_encoded = MmsPduSpanCodec::encode_initiate_response_into(
            selected_mms_pdu_size,
            selected_outstanding_calling,
            selected_outstanding_called,
            selected_nesting_level,
            initiate_response);
        if (!initiate_encoded.success()) {
            state_ = MmsStaticConnectionState::fault;
            return make_result(MmsStaticConnectionStatus::backend_failure, state_);
        }

        const auto accept = acse::AcseSpanCodec::build_accept_response_into(
            association,
            std::span<const std::uint8_t>{initiate_response}.first(
                initiate_encoded.bytes_written),
            workspace);
        if (!accept.success()) {
            if (accept.status == wire::EncodeStatus::buffer_too_small) {
                return make_workspace_capacity(state_, accept.required_bytes);
            }
            state_ = MmsStaticConnectionState::fault;
            return make_result(MmsStaticConnectionStatus::backend_failure, state_);
        }

        const auto wrapped = wrap_cotp_data_response(
            workspace.first(accept.bytes_written),
            peek.frame_bytes,
            MmsStaticConnectionState::established,
            negotiated_tpdu_size_bytes_,
            response,
            workspace);
        if (!wrapped.response_ready()) {
            return wrapped;
        }
        mms_presentation_context_id_ = association.mms_presentation_context_id;
        negotiated_mms_pdu_size_ = selected_mms_pdu_size;
        state_ = MmsStaticConnectionState::established;
        association_active_ = true;
        association_close_notified_ = false;
        auto result = wrapped;
        result.state = state_;
        return result;
    }

    if (state_ != MmsStaticConnectionState::established) {
        state_ = MmsStaticConnectionState::fault;
        return make_result(
            MmsStaticConnectionStatus::protocol_violation,
            state_,
            peek.frame_bytes);
    }

    osi::SessionDataTransferView session;
    osi::PresentationPdvView pdv;
    if (!osi::SessionSpanCodec::try_decode_data_transfer_view(cotp.user_data, session) ||
        !osi::PresentationSpanCodec::try_decode_fully_encoded_data_view(
            session.presentation_payload, pdv) ||
        pdv.context_id != mms_presentation_context_id_) {
        state_ = MmsStaticConnectionState::fault;
        return make_result(
            MmsStaticConnectionStatus::protocol_violation,
            state_,
            peek.frame_bytes);
    }

    MmsConfirmedPduView confirmed;
    const bool is_confirmed_request = MmsPduSpanCodec::try_decode_confirmed_request_view(
        pdv.single_asn1_type, confirmed);
    if (is_confirmed_request) {
        std::size_t write_mms_required{};
        std::size_t write_frame_required{};
        if (write_outer_capacity(
                confirmed,
                dispatcher_,
                mms_presentation_context_id_,
                write_mms_required,
                write_frame_required)) {
            if (negotiated_mms_pdu_size_ == 0U ||
                write_mms_required > negotiated_mms_pdu_size_ ||
                write_frame_required < osi::TpktSpanCodec::header_length ||
                write_frame_required - osi::TpktSpanCodec::header_length >
                    negotiated_tpdu_size_bytes_) {
                return make_result(
                    MmsStaticConnectionStatus::peer_limit_exceeded,
                    state_,
                    peek.frame_bytes);
            }
            if (response.size() < write_frame_required) {
                return make_response_capacity(state_, write_frame_required);
            }
            if (workspace.size() < write_frame_required) {
                return make_workspace_capacity(state_, write_frame_required);
            }
        }
    }

    const auto application = dispatcher_.dispatch(
        pdv.single_asn1_type, response, workspace, policy_.access_context());
    std::size_t mms_response_bytes = application.bytes_written;
    if (!application.success()) {
        if (application.status == MmsStaticDispatchStatus::response_buffer_too_small) {
            const auto fully_encoded = osi::PresentationSpanCodec::fully_encoded_data_size(
                mms_presentation_context_id_, application.required_bytes);
            std::size_t required{};
            if (fully_encoded && add_overhead(*fully_encoded, 11U, required)) {
                auto result = make_response_capacity(state_, required);
                result.application_status = application.status;
                result.application_service = application.service;
                result.invoke_id = application.invoke_id;
                return result;
            }
            state_ = MmsStaticConnectionState::fault;
            return make_result(MmsStaticConnectionStatus::backend_failure, state_);
        }
        if (application.status == MmsStaticDispatchStatus::workspace_too_small) {
            auto result = make_workspace_capacity(state_, application.required_bytes);
            result.application_status = application.status;
            result.application_service = application.service;
            result.invoke_id = application.invoke_id;
            return result;
        }
        if (application.status == MmsStaticDispatchStatus::backend_failure ||
            application.status == MmsStaticDispatchStatus::invalid_object_table) {
            state_ = MmsStaticConnectionState::fault;
            auto result = make_result(
                MmsStaticConnectionStatus::backend_failure,
                state_,
                peek.frame_bytes);
            result.application_status = application.status;
            result.application_service = application.service;
            result.invoke_id = application.invoke_id;
            return result;
        }
        if (!is_confirmed_request) {
            auto result = make_result(
                MmsStaticConnectionStatus::application_rejected,
                state_,
                peek.frame_bytes);
            result.application_status = application.status;
            result.application_service = application.service;
            result.invoke_id = application.invoke_id;
            return result;
        }

        const auto rejected = application.status == MmsStaticDispatchStatus::object_not_found
            ? encode_object_not_found_confirmed_error(confirmed.invoke_id, response)
            : MmsPduSpanCodec::encode_confirmed_request_reject_into(
                confirmed.invoke_id,
                reject_reason_for(application.status),
                response);
        if (!rejected.success()) {
            if (rejected.status == wire::EncodeStatus::buffer_too_small) {
                const auto fully_encoded = osi::PresentationSpanCodec::fully_encoded_data_size(
                    mms_presentation_context_id_, rejected.required_bytes);
                std::size_t required{};
                if (fully_encoded && add_overhead(*fully_encoded, 11U, required)) {
                    auto result = make_response_capacity(state_, required);
                    result.application_status = application.status;
                    result.application_service = application.service;
                    result.invoke_id = confirmed.invoke_id;
                    return result;
                }
            }
            state_ = MmsStaticConnectionState::fault;
            return make_result(MmsStaticConnectionStatus::backend_failure, state_);
        }
        mms_response_bytes = rejected.bytes_written;
    }

    if (negotiated_mms_pdu_size_ == 0U ||
        mms_response_bytes > negotiated_mms_pdu_size_) {
        return make_result(
            MmsStaticConnectionStatus::peer_limit_exceeded,
            state_,
            peek.frame_bytes);
    }

    const auto p_data = osi::PresentationSpanCodec::encode_p_data_into(
        response.first(mms_response_bytes),
        workspace,
        mms_presentation_context_id_,
        true);
    if (!p_data.success()) {
        if (p_data.status == wire::EncodeStatus::buffer_too_small) {
            std::size_t required{};
            if (add_overhead(p_data.required_bytes, 7U, required)) {
                return make_workspace_capacity(state_, required);
            }
        }
        state_ = MmsStaticConnectionState::fault;
        return make_result(MmsStaticConnectionStatus::backend_failure, state_);
    }

    auto wrapped = wrap_cotp_data_response(
        workspace.first(p_data.bytes_written),
        peek.frame_bytes,
        state_,
        negotiated_tpdu_size_bytes_,
        response,
        workspace);
    if (wrapped.response_ready()) {
        wrapped.application_status = application.status;
        wrapped.application_service = application.service;
        wrapped.invoke_id = is_confirmed_request ? confirmed.invoke_id : application.invoke_id;
    }
    return wrapped;
}

} // namespace ar::iec61850::mms