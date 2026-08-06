// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/association_runtime.hpp"

#include "ariec61850/acse/association.hpp"
#include "ariec61850/osi/cotp.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <limits>
#include <utility>

namespace ar::iec61850::mms {
namespace {

[[nodiscard]] std::uint8_t negotiated_tpdu_size(
    const osi::CotpTpdu& confirm,
    const std::uint8_t fallback) {
    const auto value = confirm.parameter(osi::CotpFrameCodec::tpdu_size_parameter);
    if (!value || value->size() != 1U) {
        return fallback;
    }
    static_cast<void>(osi::CotpFrameCodec::tpdu_size_bytes((*value)[0]));
    return (*value)[0];
}



} // namespace

MmsAssociationRuntime::MmsAssociationRuntime(
    MmsByteTransport& transport,
    MmsAssociationOptions options)
    : transport_{transport}, options_{std::move(options)} {
    if (options_.connect_timeout <= std::chrono::milliseconds::zero() ||
        options_.request_timeout <= std::chrono::milliseconds::zero() ||
        options_.maximum_receive_chunks_per_operation == 0U ||
        options_.maximum_queued_information_reports == 0U ||
        options_.maximum_events == 0U) {
        throw std::invalid_argument("MMS association runtime limits are invalid.");
    }
    static_cast<void>(osi::CotpFrameCodec::tpdu_size_bytes(options_.tpdu_size_code));
    if (options_.presentation_context_id == 0U) {
        throw std::invalid_argument("MMS presentation context ID must be positive.");
    }
    negotiated_.tpdu_size_code = options_.tpdu_size_code;
    negotiated_.presentation_context_id = options_.presentation_context_id;
}

MmsAssociationRuntime::~MmsAssociationRuntime() {
    disconnect();
}

MmsAssociationRuntime::Deadline MmsAssociationRuntime::deadline_after(
    const std::chrono::milliseconds timeout) const {
    return Clock::now() + timeout;
}

void MmsAssociationRuntime::require_not_cancelled(
    const std::stop_token stop_token) const {
    if (stop_token.stop_requested()) {
        throw MmsTransportCancelledError("MMS association operation was cancelled.");
    }
}

void MmsAssociationRuntime::require_associated() const {
    if (!associated() || !transport_.connected()) {
        throw MmsAssociationRuntimeError("MMS association is not active.");
    }
}

void MmsAssociationRuntime::set_state(
    const MmsAssociationRuntimeState state,
    std::string message) {
    state_ = state;
    add_event(MmsAssociationEventKind::state_changed, std::move(message));
}

void MmsAssociationRuntime::add_event(
    const MmsAssociationEventKind kind,
    std::string message,
    const std::optional<std::uint32_t> invoke_id) {
    if (events_.size() >= options_.maximum_events) {
        events_.pop_front();
    }
    events_.push_back(MmsAssociationEvent{kind, state_, invoke_id, std::move(message)});
}

void MmsAssociationRuntime::fail(std::string message) {
    last_fault_ = message;
    state_ = MmsAssociationRuntimeState::faulted;
    add_event(MmsAssociationEventKind::faulted, std::move(message));
}

void MmsAssociationRuntime::send_tpkt_payload(
    const std::span<const std::uint8_t> cotp_payload,
    const Deadline deadline,
    const std::stop_token stop_token) {
    require_not_cancelled(stop_token);
    const auto frame = osi::TpktFrameCodec::encode(cotp_payload);
    transport_.send(frame, deadline, stop_token);
}

void MmsAssociationRuntime::send_application_payload(
    const std::span<const std::uint8_t> application_payload,
    const Deadline deadline,
    const std::stop_token stop_token) {
    const auto segments = osi::CotpFrameCodec::encode_data_segments(
        application_payload, negotiated_.tpdu_size_code);
    for (const auto& segment : segments) {
        send_tpkt_payload(segment, deadline, stop_token);
    }
}

std::vector<std::uint8_t> MmsAssociationRuntime::receive_application_payload(
    const Deadline deadline,
    const std::stop_token stop_token) {
    osi::CotpDataReassembler reassembler;
    std::size_t receive_chunks = 0U;
    while (!reassembler.is_complete()) {
        require_not_cancelled(stop_token);
        if (receive_chunks >= options_.maximum_receive_chunks_per_operation) {
            throw MmsAssociationRuntimeError(
                "MMS receive chunk limit exceeded before a complete COTP payload arrived.");
        }
        auto bytes = transport_.receive(deadline, stop_token);
        ++receive_chunks;
        if (bytes.empty()) {
            throw MmsAssociationRuntimeError("MMS transport returned an empty receive chunk.");
        }
        tpkt_decoder_.append(bytes);
        osi::TpktFrame frame;
        while (tpkt_decoder_.try_pop(frame)) {
            const auto tpdu = osi::CotpFrameCodec::decode(frame.payload);
            if (tpdu.kind == osi::CotpTpduKind::disconnect_request) {
                throw MmsAssociationRuntimeError("Remote endpoint requested COTP disconnect.");
            }
            if (tpdu.kind == osi::CotpTpduKind::error) {
                throw MmsAssociationRuntimeError("Remote endpoint returned a COTP error TPDU.");
            }
            if (tpdu.kind != osi::CotpTpduKind::data) {
                throw MmsAssociationRuntimeError(
                    "Expected COTP Data TPDU while receiving an application payload.");
            }
            reassembler.append(tpdu);
            if (reassembler.is_complete()) {
                break;
            }
        }
    }
    return reassembler.complete();
}

void MmsAssociationRuntime::connect(
    const MmsEndpoint& endpoint,
    const std::stop_token stop_token) {
    if (endpoint.host.empty()) {
        throw std::invalid_argument("MMS endpoint host must not be empty.");
    }
    if (associated()) {
        throw MmsAssociationRuntimeError("MMS association is already active.");
    }

    disconnect();
    endpoint_ = endpoint;
    last_fault_.clear();
    invoke_router_.clear();
    information_reports_.clear();
    tpkt_decoder_.reset();
    next_invoke_id_ = 0U;
    negotiated_ = {};
    negotiated_.tpdu_size_code = options_.tpdu_size_code;
    negotiated_.presentation_context_id = options_.presentation_context_id;

    try {
        require_not_cancelled(stop_token);
        const auto deadline = deadline_after(options_.connect_timeout);
        set_state(MmsAssociationRuntimeState::transport_connecting,
                  "Connecting byte transport to " + endpoint.host + ":" +
                      std::to_string(endpoint.port) + ".");
        transport_.connect(endpoint, deadline, stop_token);
        if (!transport_.connected()) {
            throw MmsAssociationRuntimeError(
                "Byte transport returned without entering the connected state.");
        }
        state_ = MmsAssociationRuntimeState::transport_connected;
        add_event(MmsAssociationEventKind::transport_connected,
                  "Byte transport connected.");

        state_ = MmsAssociationRuntimeState::cotp_connecting;
        add_event(MmsAssociationEventKind::state_changed,
                  "Sending COTP Connection Request.");
        const auto connection_request = osi::CotpFrameCodec::encode_default_connection_request();
        send_tpkt_payload(connection_request, deadline, stop_token);

        std::size_t chunks = 0U;
        std::optional<osi::CotpTpdu> connection_confirm;
        while (!connection_confirm) {
            require_not_cancelled(stop_token);
            if (chunks >= options_.maximum_receive_chunks_per_operation) {
                throw MmsAssociationRuntimeError(
                    "COTP Connection Confirm did not arrive within the receive bound.");
            }
            auto bytes = transport_.receive(deadline, stop_token);
            ++chunks;
            if (bytes.empty()) {
                throw MmsAssociationRuntimeError(
                    "MMS transport returned an empty chunk during COTP connect.");
            }
            tpkt_decoder_.append(bytes);
            osi::TpktFrame frame;
            while (tpkt_decoder_.try_pop(frame)) {
                auto tpdu = osi::CotpFrameCodec::decode(frame.payload);
                if (tpdu.kind == osi::CotpTpduKind::connection_confirm) {
                    connection_confirm = std::move(tpdu);
                    break;
                }
                if (tpdu.kind == osi::CotpTpduKind::error ||
                    tpdu.kind == osi::CotpTpduKind::disconnect_request) {
                    throw MmsAssociationRuntimeError(
                        "Remote endpoint rejected the COTP connection.");
                }
                throw MmsAssociationRuntimeError(
                    "Unexpected TPDU received while waiting for COTP Connection Confirm.");
            }
        }

        if (connection_confirm->destination_reference != local_cotp_reference_) {
            throw MmsAssociationRuntimeError(
                "COTP Connection Confirm destination reference does not match the request.");
        }
        remote_cotp_reference_ = connection_confirm->source_reference;
        negotiated_.tpdu_size_code = negotiated_tpdu_size(
            *connection_confirm, options_.tpdu_size_code);
        state_ = MmsAssociationRuntimeState::cotp_connected;
        add_event(MmsAssociationEventKind::cotp_connected,
                  "COTP connected with TPDU size code " +
                      std::to_string(negotiated_.tpdu_size_code) + ".");

        state_ = MmsAssociationRuntimeState::acse_associating;
        add_event(MmsAssociationEventKind::state_changed,
                  "Sending ISO Session/Presentation/ACSE association request.");
        const auto association_request =
            acse::AcseAssociationCodec::build_default_association_request();
        send_application_payload(association_request, deadline, stop_token);
        const auto association_payload =
            receive_application_payload(deadline, stop_token);
        const auto response =
            acse::AcseAssociationCodec::decode_association_response(association_payload);
        if (!response.aare.accepted()) {
            throw MmsAssociationRuntimeError(
                "ACSE association was rejected with result=" +
                std::to_string(response.aare.result) + ".");
        }
        if (!response.aare.user_information) {
            throw MmsAssociationRuntimeError(
                "Accepted ACSE response did not contain MMS Initiate response data.");
        }
        const auto initiate = MmsPduCodec::decode_initiate_response(
            response.aare.user_information->single_asn1_type);
        negotiated_.maximum_mms_pdu_size = initiate.negotiated_maximum_mms_pdu_size;
        negotiated_.maximum_outstanding_calling =
            initiate.negotiated_maximum_outstanding_calling;
        negotiated_.maximum_outstanding_called =
            initiate.negotiated_maximum_outstanding_called;
        negotiated_.data_structure_nesting_level =
            initiate.negotiated_data_structure_nesting_level;
        negotiated_.presentation_context_id = options_.presentation_context_id;

        state_ = MmsAssociationRuntimeState::associated;
        add_event(MmsAssociationEventKind::association_accepted,
                  "MMS association accepted; maximum PDU=" +
                      std::to_string(negotiated_.maximum_mms_pdu_size) + ".");
    } catch (const MmsTransportCancelledError& exception) {
        state_ = MmsAssociationRuntimeState::faulted;
        last_fault_ = exception.what();
        add_event(MmsAssociationEventKind::cancelled, exception.what());
        transport_.close();
        throw;
    } catch (const MmsTransportTimeoutError& exception) {
        state_ = MmsAssociationRuntimeState::faulted;
        last_fault_ = exception.what();
        add_event(MmsAssociationEventKind::timed_out, exception.what());
        transport_.close();
        throw;
    } catch (const std::exception& exception) {
        if (stop_token.stop_requested()) {
            state_ = MmsAssociationRuntimeState::faulted;
            last_fault_ = exception.what();
            add_event(MmsAssociationEventKind::cancelled, exception.what());
        } else {
            fail(exception.what());
        }
        transport_.close();
        throw;
    }
}

void MmsAssociationRuntime::disconnect(const std::stop_token stop_token) noexcept {
    if (state_ == MmsAssociationRuntimeState::disconnected &&
        !transport_.connected()) {
        return;
    }
    state_ = MmsAssociationRuntimeState::closing;
    add_event(MmsAssociationEventKind::state_changed,
              "Closing MMS association and byte transport.");
    try {
        if (transport_.connected() && remote_cotp_reference_ != 0U) {
            const auto deadline = deadline_after(options_.request_timeout);
            const auto disconnect_request = osi::CotpFrameCodec::encode_disconnect_request(
                remote_cotp_reference_, local_cotp_reference_);
            send_tpkt_payload(disconnect_request, deadline, stop_token);
        }
    } catch (...) {
    }
    transport_.close();
    invoke_router_.clear();
    information_reports_.clear();
    tpkt_decoder_.reset();
    remote_cotp_reference_ = 0U;
    state_ = MmsAssociationRuntimeState::disconnected;
    add_event(MmsAssociationEventKind::disconnected,
              "MMS association disconnected.");
}

MmsConfirmedExchangeResult MmsAssociationRuntime::exchange_confirmed(
    const std::span<const std::uint8_t> presentation_payload,
    const std::uint32_t expected_invoke_id,
    const std::stop_token stop_token) {
    require_associated();
    if (expected_invoke_id == 0U ||
        expected_invoke_id > MmsPduCodec::maximum_invoke_id) {
        throw std::invalid_argument("Expected MMS invoke ID is outside the supported range.");
    }
    const auto request_envelope = MmsPduCodec::decode_envelope(presentation_payload);
    if (request_envelope.kind != MmsPduKind::confirmed_request ||
        !request_envelope.matches_invoke(expected_invoke_id)) {
        throw std::invalid_argument(
            "Confirmed exchange payload does not contain the expected invoke ID.");
    }

    MmsPduEnvelope queued;
    if (invoke_router_.try_dequeue(expected_invoke_id, queued)) {
        return {std::move(queued), {}};
    }

    const auto deadline = deadline_after(options_.request_timeout);
    try {
        send_application_payload(presentation_payload, deadline, stop_token);
        add_event(MmsAssociationEventKind::confirmed_request_sent,
                  "Confirmed MMS request sent.", expected_invoke_id);

        for (std::size_t index = 0U;
             index < options_.maximum_receive_chunks_per_operation;
             ++index) {
            auto received = receive_application_payload(deadline, stop_token);
            auto envelope = route_received_payload(received);
            if (envelope.confirmed_result() &&
                envelope.matches_invoke(expected_invoke_id)) {
                MmsPduEnvelope matched;
                if (!invoke_router_.try_dequeue(expected_invoke_id, matched)) {
                    throw MmsAssociationRuntimeError(
                        "Matched MMS response was not available in the invoke router.");
                }
                add_event(MmsAssociationEventKind::confirmed_result_received,
                          "Confirmed MMS result received.", expected_invoke_id);
                return {std::move(matched), std::move(received)};
            }
        }
        throw MmsAssociationRuntimeError(
            "MMS confirmed exchange exceeded the bounded receive iteration count.");
    } catch (const MmsTransportCancelledError& exception) {
        state_ = MmsAssociationRuntimeState::faulted;
        last_fault_ = exception.what();
        add_event(MmsAssociationEventKind::cancelled,
                  exception.what(), expected_invoke_id);
        throw;
    } catch (const MmsTransportTimeoutError& exception) {
        state_ = MmsAssociationRuntimeState::faulted;
        last_fault_ = exception.what();
        add_event(MmsAssociationEventKind::timed_out,
                  exception.what(), expected_invoke_id);
        throw;
    } catch (const std::exception& exception) {
        if (stop_token.stop_requested()) {
            state_ = MmsAssociationRuntimeState::faulted;
            last_fault_ = exception.what();
            add_event(MmsAssociationEventKind::cancelled,
                      exception.what(), expected_invoke_id);
        } else {
            fail(exception.what());
        }
        throw;
    }
}

MmsPduEnvelope MmsAssociationRuntime::poll_once(
    const std::stop_token stop_token) {
    require_associated();
    try {
        auto payload = receive_application_payload(
            deadline_after(options_.request_timeout), stop_token);
        return route_received_payload(payload);
    } catch (const MmsTransportCancelledError& exception) {
        state_ = MmsAssociationRuntimeState::faulted;
        last_fault_ = exception.what();
        add_event(MmsAssociationEventKind::cancelled, exception.what());
        throw;
    } catch (const MmsTransportTimeoutError& exception) {
        state_ = MmsAssociationRuntimeState::faulted;
        last_fault_ = exception.what();
        add_event(MmsAssociationEventKind::timed_out, exception.what());
        throw;
    } catch (const std::exception& exception) {
        if (stop_token.stop_requested()) {
            state_ = MmsAssociationRuntimeState::faulted;
            last_fault_ = exception.what();
            add_event(MmsAssociationEventKind::cancelled, exception.what());
        } else {
            fail(exception.what());
        }
        throw;
    }
}

MmsPduEnvelope MmsAssociationRuntime::route_received_payload(
    const std::span<const std::uint8_t> presentation_payload) {
    auto envelope = MmsPduCodec::decode_envelope(presentation_payload);
    if (envelope.information_report) {
        queue_information_report(presentation_payload);
        add_event(MmsAssociationEventKind::information_report_received,
                  "MMS InformationReport queued.");
        return envelope;
    }
    const auto route = invoke_router_.route(presentation_payload);
    if (route.action == MmsInvokeRouteAction::queued_unmatched) {
        add_event(MmsAssociationEventKind::unmatched_pdu_received,
                  "Unmatched MMS PDU queued.", envelope.invoke_id);
    }
    return envelope;
}

void MmsAssociationRuntime::queue_information_report(
    const std::span<const std::uint8_t> payload) {
    if (information_reports_.size() >=
        options_.maximum_queued_information_reports) {
        information_reports_.pop_front();
    }
    information_reports_.emplace_back(payload.begin(), payload.end());
}

bool MmsAssociationRuntime::try_pop_information_report(
    std::vector<std::uint8_t>& presentation_payload) {
    if (information_reports_.empty()) {
        return false;
    }
    presentation_payload = std::move(information_reports_.front());
    information_reports_.pop_front();
    return true;
}

std::uint32_t MmsAssociationRuntime::next_invoke_id() {
    if (next_invoke_id_ >= MmsPduCodec::maximum_invoke_id) {
        next_invoke_id_ = 0U;
    }
    ++next_invoke_id_;
    return next_invoke_id_;
}

} // namespace ar::iec61850::mms
