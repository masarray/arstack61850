// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/connection_runtime.hpp"

#include "ariec61850/acse/association_span.hpp"
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

constexpr std::array<std::uint8_t, 4U> kConservativeStructureType{
    0xA2U, 0x02U, 0xA1U, 0x00U};

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

[[nodiscard]] bool cotp_stream_size(
    const std::size_t payload_bytes,
    const std::uint8_t tpdu_size_code,
    std::size_t& maximum_user_data,
    std::size_t& segment_count,
    std::size_t& required) noexcept {
    maximum_user_data = 0U;
    segment_count = 0U;
    required = 0U;
    std::size_t tpdu_bytes{};
    if (!osi::CotpSpanCodec::try_tpdu_size_bytes(tpdu_size_code, tpdu_bytes) ||
        tpdu_bytes <= 3U) {
        return false;
    }
    maximum_user_data = tpdu_bytes - 3U;
    segment_count = payload_bytes == 0U
        ? 1U
        : 1U + ((payload_bytes - 1U) / maximum_user_data);
    if (segment_count >
        (std::numeric_limits<std::size_t>::max() - payload_bytes) / 7U) {
        return false;
    }
    required = payload_bytes + segment_count * 7U;
    return true;
}

[[nodiscard]] MmsStaticConnectionResult wrap_cotp_data_response(
    const std::span<const std::uint8_t> session_or_presentation,
    const std::size_t consumed,
    const MmsStaticConnectionState state,
    const std::uint8_t tpdu_size_code,
    const std::span<std::uint8_t> response) noexcept {
    std::size_t maximum_user_data{};
    std::size_t segment_count{};
    std::size_t required{};
    if (!cotp_stream_size(
            session_or_presentation.size(),
            tpdu_size_code,
            maximum_user_data,
            segment_count,
            required)) {
        return make_result(MmsStaticConnectionStatus::backend_failure, state);
    }
    if (response.size() < required) {
        return make_response_capacity(state, required);
    }

    std::size_t input_offset = 0U;
    std::size_t output_offset = 0U;
    for (std::size_t segment = 0U; segment < segment_count; ++segment) {
        const auto remaining = session_or_presentation.size() - input_offset;
        const auto chunk_size = std::min(remaining, maximum_user_data);
        const auto final_segment = segment + 1U == segment_count;
        const auto frame_bytes = chunk_size + 7U;
        if (frame_bytes > std::numeric_limits<std::uint16_t>::max()) {
            return make_result(MmsStaticConnectionStatus::backend_failure, state);
        }

        auto frame = response.subspan(output_offset, frame_bytes);
        const auto cotp = osi::CotpSpanCodec::encode_data_into(
            session_or_presentation.subspan(input_offset, chunk_size),
            frame.subspan(osi::TpktSpanCodec::header_length),
            final_segment,
            static_cast<std::uint8_t>(segment & 0x7FU));
        if (!cotp.success() || cotp.bytes_written != chunk_size + 3U) {
            return make_result(MmsStaticConnectionStatus::backend_failure, state);
        }

        frame[0] = 0x03U;
        frame[1] = 0x00U;
        frame[2] = static_cast<std::uint8_t>((frame_bytes >> 8U) & 0xFFU);
        frame[3] = static_cast<std::uint8_t>(frame_bytes & 0xFFU);
        input_offset += chunk_size;
        output_offset += frame_bytes;
    }

    if (input_offset != session_or_presentation.size() || output_offset != required) {
        return make_result(MmsStaticConnectionStatus::backend_failure, state);
    }
    return make_result(
        MmsStaticConnectionStatus::response_ready,
        state,
        consumed,
        required);
}

[[nodiscard]] MmsStaticConnectionResult application_rejected(
    const MmsStaticConnectionState state,
    const std::size_t consumed,
    const MmsStaticDispatchResult& application) noexcept {
    auto result = make_result(
        MmsStaticConnectionStatus::application_rejected, state, consumed);
    result.application_status = application.status;
    result.application_service = application.service;
    result.invoke_id = application.invoke_id;
    return result;
}

[[nodiscard]] MmsStaticConnectionResult wrap_mms_response(
    const wire::EncodeResult encoded_mms,
    const std::size_t consumed,
    const MmsStaticConnectionState state,
    const std::uint32_t presentation_context_id,
    const std::uint8_t tpdu_size_code,
    const std::span<std::uint8_t> response,
    const std::span<std::uint8_t> workspace,
    const MmsStaticDispatchStatus application_status,
    const MmsWireConfirmedService service,
    const std::uint32_t invoke_id) noexcept {
    if (!encoded_mms.success()) {
        if (encoded_mms.status == wire::EncodeStatus::buffer_too_small &&
            encoded_mms.required_bytes > 0U) {
            const auto fully_encoded = osi::PresentationSpanCodec::fully_encoded_data_size(
                presentation_context_id, encoded_mms.required_bytes);
            if (fully_encoded) {
                std::size_t maximum_user_data{};
                std::size_t segment_count{};
                std::size_t required{};
                if (cotp_stream_size(
                        *fully_encoded,
                        tpdu_size_code,
                        maximum_user_data,
                        segment_count,
                        required)) {
                    auto result = make_response_capacity(state, required);
                    result.application_status = application_status;
                    result.application_service = service;
                    result.invoke_id = invoke_id;
                    return result;
                }
            }
        }
        return make_result(MmsStaticConnectionStatus::backend_failure, state);
    }

    const auto p_data = osi::PresentationSpanCodec::encode_p_data_into(
        response.first(encoded_mms.bytes_written),
        workspace,
        presentation_context_id,
        true);
    if (!p_data.success()) {
        if (p_data.status == wire::EncodeStatus::buffer_too_small) {
            auto result = make_workspace_capacity(state, p_data.required_bytes);
            result.application_status = application_status;
            result.application_service = service;
            result.invoke_id = invoke_id;
            return result;
        }
        return make_result(MmsStaticConnectionStatus::backend_failure, state);
    }

    auto wrapped = wrap_cotp_data_response(
        workspace.first(p_data.bytes_written),
        consumed,
        state,
        tpdu_size_code,
        response);
    if (wrapped.response_ready()) {
        wrapped.application_status = application_status;
        wrapped.application_service = service;
        wrapped.invoke_id = invoke_id;
    }
    return wrapped;
}

[[nodiscard]] bool write_outer_capacity(
    const MmsConfirmedPduView& confirmed,
    const MmsStaticApplicationDispatcher& dispatcher,
    const std::uint32_t presentation_context_id,
    const std::uint8_t tpdu_size_code,
    std::size_t& required) noexcept {
    required = 0U;
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
    const auto fully_encoded = osi::PresentationSpanCodec::fully_encoded_data_size(
        presentation_context_id, probe.required_bytes);
    if (!fully_encoded) {
        return false;
    }
    std::size_t maximum_user_data{};
    std::size_t segment_count{};
    return cotp_stream_size(
        *fully_encoded,
        tpdu_size_code,
        maximum_user_data,
        segment_count,
        required);
}

[[nodiscard]] bool compatibility_error_status(
    const MmsStaticDispatchStatus status) noexcept {
    return status == MmsStaticDispatchStatus::malformed_request ||
        status == MmsStaticDispatchStatus::unsupported_service ||
        status == MmsStaticDispatchStatus::unsupported_request ||
        status == MmsStaticDispatchStatus::object_not_found;
}

} // namespace

void MmsStaticConnectionRuntime::clear_cotp_reassembly() noexcept {
    cotp_reassembly_size_ = 0U;
    cotp_reassembly_complete_ = false;
    cotp_reassembly_uses_workspace_ = false;
    cotp_reassembly_workspace_data_ = nullptr;
    cotp_reassembly_workspace_size_ = 0U;
}

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
    clear_cotp_reassembly();
    state_ = MmsStaticConnectionState::closed;
    mms_presentation_context_id_ = 0U;
}

void MmsStaticConnectionRuntime::reset() noexcept {
    notify_association_closed();
    clear_cotp_reassembly();
    state_ = MmsStaticConnectionState::awaiting_cotp_connect;
    mms_presentation_context_id_ = 0U;
    negotiated_tpdu_size_code_ = policy_.maximum_tpdu_size_code;
    association_active_ = false;
    association_close_notified_ = false;
}

MmsStaticConnectionResult MmsStaticConnectionRuntime::process_tcp_window(
    const std::span<const std::uint8_t> tcp_bytes,
    const std::span<std::uint8_t> response,
    const std::span<std::uint8_t> workspace_storage) noexcept {
    auto workspace = workspace_storage;
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
        clear_cotp_reassembly();
        return make_result(MmsStaticConnectionStatus::malformed_transport, state_);
    }

    const auto frame_bytes = tcp_bytes.first(peek.frame_bytes);
    osi::TpktFrameView tpkt;
    osi::CotpTpduView cotp;
    if (!osi::TpktSpanCodec::try_decode_view(frame_bytes, tpkt) ||
        !osi::CotpSpanCodec::try_decode_view(tpkt.payload, cotp)) {
        state_ = MmsStaticConnectionState::fault;
        clear_cotp_reassembly();
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
            clear_cotp_reassembly();
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

        osi::CotpTpduView confirmed;
        std::span<const std::uint8_t> selected;
        if (!osi::CotpSpanCodec::try_decode_view(
                workspace.first(cc.bytes_written), confirmed) ||
            !confirmed.try_parameter(osi::CotpSpanCodec::tpdu_size_parameter, selected) ||
            selected.size() != 1U) {
            state_ = MmsStaticConnectionState::fault;
            return make_result(MmsStaticConnectionStatus::backend_failure, state_);
        }
        negotiated_tpdu_size_code_ = selected[0];

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

        state_ = MmsStaticConnectionState::awaiting_association;
        return make_result(
            MmsStaticConnectionStatus::response_ready,
            state_,
            peek.frame_bytes,
            encoded.bytes_written);
    }

    if (cotp.kind != osi::CotpWireKind::data) {
        state_ = MmsStaticConnectionState::fault;
        clear_cotp_reassembly();
        return make_result(
            MmsStaticConnectionStatus::protocol_violation,
            state_,
            peek.frame_bytes);
    }

    bool using_reassembly = false;
    std::span<std::uint8_t> reassembly = policy_.cotp_reassembly;
    const auto select_workspace_reassembly = [this, workspace_storage, &workspace, &reassembly]() noexcept {
        if (workspace_storage.size() < 2U) {
            return false;
        }
        const auto processing_bytes = workspace_storage.size() / 2U;
        const auto reassembly_bytes = workspace_storage.size() - processing_bytes;
        if (processing_bytes == 0U || reassembly_bytes == 0U) {
            return false;
        }
        if (cotp_reassembly_uses_workspace_) {
            if (cotp_reassembly_workspace_data_ != workspace_storage.data() ||
                cotp_reassembly_workspace_size_ != workspace_storage.size()) {
                return false;
            }
        } else {
            cotp_reassembly_uses_workspace_ = true;
            cotp_reassembly_workspace_data_ = workspace_storage.data();
            cotp_reassembly_workspace_size_ = workspace_storage.size();
        }
        workspace = workspace_storage.first(processing_bytes);
        reassembly = workspace_storage.subspan(processing_bytes, reassembly_bytes);
        return true;
    };

    if (cotp_reassembly_complete_) {
        if (cotp_reassembly_size_ == 0U || !cotp.end_of_transmission) {
            state_ = MmsStaticConnectionState::fault;
            clear_cotp_reassembly();
            return make_result(
                MmsStaticConnectionStatus::protocol_violation,
                state_,
                peek.frame_bytes);
        }
        if (cotp_reassembly_uses_workspace_ && !select_workspace_reassembly()) {
            state_ = MmsStaticConnectionState::fault;
            clear_cotp_reassembly();
            return make_result(MmsStaticConnectionStatus::backend_failure, state_);
        }
        if (reassembly.empty() || cotp_reassembly_size_ > reassembly.size()) {
            state_ = MmsStaticConnectionState::fault;
            clear_cotp_reassembly();
            return make_result(MmsStaticConnectionStatus::backend_failure, state_);
        }
        cotp.user_data = std::span<const std::uint8_t>{reassembly}.first(cotp_reassembly_size_);
        using_reassembly = true;
    } else if (cotp_reassembly_size_ != 0U || !cotp.end_of_transmission) {
        if (reassembly.empty()) {
            if (!policy_.require_end_of_transmission) {
                // Explicit compatibility opt-out: expose the partial TPDU to the
                // upper decoder exactly as the historical strict runtime did.
            } else if (!select_workspace_reassembly()) {
                state_ = MmsStaticConnectionState::fault;
                clear_cotp_reassembly();
                return make_result(MmsStaticConnectionStatus::backend_failure, state_);
            }
        }
        if (!reassembly.empty()) {
            if (cotp_reassembly_size_ > reassembly.size() ||
                cotp.user_data.size() > reassembly.size() - cotp_reassembly_size_) {
                state_ = MmsStaticConnectionState::fault;
                clear_cotp_reassembly();
                return make_result(
                    MmsStaticConnectionStatus::protocol_violation,
                    state_,
                    peek.frame_bytes);
            }
            std::copy(
                cotp.user_data.begin(),
                cotp.user_data.end(),
                reassembly.begin() + static_cast<std::ptrdiff_t>(cotp_reassembly_size_));
            cotp_reassembly_size_ += cotp.user_data.size();
            if (!cotp.end_of_transmission) {
                return make_result(
                    MmsStaticConnectionStatus::consumed_no_response,
                    state_,
                    peek.frame_bytes);
            }
            cotp_reassembly_complete_ = true;
            cotp.user_data = std::span<const std::uint8_t>{reassembly}.first(cotp_reassembly_size_);
            using_reassembly = true;
        }
    }

    const auto finish = [this, using_reassembly](MmsStaticConnectionResult result) noexcept {
        if (using_reassembly && result.consumed_bytes != 0U) {
            clear_cotp_reassembly();
        }
        return result;
    };

    if (state_ == MmsStaticConnectionState::awaiting_association) {
        acse::AssociationRequestView association;
        if (!acse::AcseSpanCodec::try_decode_association_request_compat_view(
                cotp.user_data, association)) {
            state_ = MmsStaticConnectionState::fault;
            clear_cotp_reassembly();
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
            clear_cotp_reassembly();
            return make_result(
                MmsStaticConnectionStatus::protocol_violation,
                state_,
                peek.frame_bytes);
        }

        const auto accept = acse::AcseSpanCodec::build_accept_response_into(
            association, workspace);
        if (!accept.success()) {
            if (accept.status == wire::EncodeStatus::buffer_too_small) {
                return make_workspace_capacity(state_, accept.required_bytes);
            }
            state_ = MmsStaticConnectionState::fault;
            clear_cotp_reassembly();
            return make_result(MmsStaticConnectionStatus::backend_failure, state_);
        }

        const auto wrapped = wrap_cotp_data_response(
            workspace.first(accept.bytes_written),
            peek.frame_bytes,
            MmsStaticConnectionState::established,
            negotiated_tpdu_size_code_,
            response);
        if (!wrapped.response_ready()) {
            return wrapped;
        }
        mms_presentation_context_id_ = association.mms_presentation_context_id;
        state_ = MmsStaticConnectionState::established;
        association_active_ = true;
        association_close_notified_ = false;
        auto result = wrapped;
        result.state = state_;
        return finish(result);
    }

    if (state_ != MmsStaticConnectionState::established) {
        state_ = MmsStaticConnectionState::fault;
        clear_cotp_reassembly();
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
        clear_cotp_reassembly();
        return make_result(
            MmsStaticConnectionStatus::protocol_violation,
            state_,
            peek.frame_bytes);
    }

    if (MmsPduSpanCodec::is_conclude_request(pdv.single_asn1_type)) {
        const auto conclude = MmsPduSpanCodec::encode_conclude_response_into(response);
        return finish(wrap_mms_response(
            conclude,
            peek.frame_bytes,
            state_,
            mms_presentation_context_id_,
            negotiated_tpdu_size_code_,
            response,
            workspace,
            MmsStaticDispatchStatus::response_ready,
            MmsWireConfirmedService::unknown,
            0U));
    }

    MmsConfirmedPduView confirmed;
    const bool confirmed_decoded = MmsPduSpanCodec::try_decode_confirmed_request_view(
        pdv.single_asn1_type, confirmed);
    if (confirmed_decoded) {
        if (confirmed.service() == MmsWireConfirmedService::identify) {
            const auto identify = !confirmed.service_constructed && confirmed.service_value.empty()
                ? MmsPduSpanCodec::encode_identify_response_into(
                    confirmed.invoke_id, response)
                : MmsPduSpanCodec::encode_confirmed_error_into(
                    confirmed.invoke_id, response);
            return finish(wrap_mms_response(
                identify,
                peek.frame_bytes,
                state_,
                mms_presentation_context_id_,
                negotiated_tpdu_size_code_,
                response,
                workspace,
                MmsStaticDispatchStatus::response_ready,
                MmsWireConfirmedService::identify,
                confirmed.invoke_id));
        }

        std::size_t write_required{};
        if (write_outer_capacity(
                confirmed,
                dispatcher_,
                mms_presentation_context_id_,
                negotiated_tpdu_size_code_,
                write_required)) {
            if (response.size() < write_required) {
                return make_response_capacity(state_, write_required);
            }
            if (workspace.size() < write_required) {
                return make_workspace_capacity(state_, write_required);
            }
        }
    }

    const auto application = dispatcher_.dispatch(
        pdv.single_asn1_type, response, workspace, policy_.access_context());
    if (!application.success()) {
        if (application.status == MmsStaticDispatchStatus::response_buffer_too_small) {
            const auto fully_encoded = osi::PresentationSpanCodec::fully_encoded_data_size(
                mms_presentation_context_id_, application.required_bytes);
            if (fully_encoded) {
                std::size_t maximum_user_data{};
                std::size_t segment_count{};
                std::size_t required{};
                if (cotp_stream_size(
                        *fully_encoded,
                        negotiated_tpdu_size_code_,
                        maximum_user_data,
                        segment_count,
                        required)) {
                    auto result = make_response_capacity(state_, required);
                    result.application_status = application.status;
                    result.application_service = application.service;
                    result.invoke_id = application.invoke_id;
                    return result;
                }
            }
            state_ = MmsStaticConnectionState::fault;
            clear_cotp_reassembly();
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
            clear_cotp_reassembly();
            auto result = make_result(
                MmsStaticConnectionStatus::backend_failure,
                state_,
                peek.frame_bytes);
            result.application_status = application.status;
            result.application_service = application.service;
            result.invoke_id = application.invoke_id;
            return result;
        }

        if (confirmed_decoded &&
            application.status == MmsStaticDispatchStatus::object_not_found &&
            confirmed.service() == MmsWireConfirmedService::get_variable_access_attributes) {
            MmsVariableAccessAttributesRequestView attributes;
            if (MmsServiceSpanCodec::try_decode_variable_access_attributes_request(
                    confirmed, attributes) &&
                attributes.name.kind == MmsObjectNameViewKind::vmd_specific &&
                !attributes.name.item.empty()) {
                const auto fallback =
                    MmsServiceSpanCodec::encode_variable_access_attributes_response_into(
                        confirmed.invoke_id,
                        false,
                        kConservativeStructureType,
                        response);
                return finish(wrap_mms_response(
                    fallback,
                    peek.frame_bytes,
                    state_,
                    mms_presentation_context_id_,
                    negotiated_tpdu_size_code_,
                    response,
                    workspace,
                    application.status,
                    confirmed.service(),
                    confirmed.invoke_id));
            }
        }

        if (confirmed_decoded && compatibility_error_status(application.status)) {
            const auto error = MmsPduSpanCodec::encode_confirmed_error_into(
                confirmed.invoke_id, response);
            return finish(wrap_mms_response(
                error,
                peek.frame_bytes,
                state_,
                mms_presentation_context_id_,
                negotiated_tpdu_size_code_,
                response,
                workspace,
                application.status,
                confirmed.service(),
                confirmed.invoke_id));
        }
        return finish(application_rejected(state_, peek.frame_bytes, application));
    }

    const auto p_data = osi::PresentationSpanCodec::encode_p_data_into(
        response.first(application.bytes_written),
        workspace,
        mms_presentation_context_id_,
        true);
    if (!p_data.success()) {
        if (p_data.status == wire::EncodeStatus::buffer_too_small) {
            return make_workspace_capacity(state_, p_data.required_bytes);
        }
        state_ = MmsStaticConnectionState::fault;
        clear_cotp_reassembly();
        return make_result(MmsStaticConnectionStatus::backend_failure, state_);
    }

    auto wrapped = wrap_cotp_data_response(
        workspace.first(p_data.bytes_written),
        peek.frame_bytes,
        state_,
        negotiated_tpdu_size_code_,
        response);
    if (wrapped.response_ready()) {
        wrapped.application_status = application.status;
        wrapped.application_service = application.service;
        wrapped.invoke_id = application.invoke_id;
    }
    return finish(wrapped);
}

} // namespace ar::iec61850::mms
