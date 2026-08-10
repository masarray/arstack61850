// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/invoke_router.hpp"
#include "ariec61850/mms/pdu.hpp"
#include "ariec61850/osi/tpkt.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <vector>

namespace ar::iec61850::mms {

class MmsAssociationRuntimeError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class MmsTransportTimeoutError final : public MmsAssociationRuntimeError {
public:
    using MmsAssociationRuntimeError::MmsAssociationRuntimeError;
};

class MmsTransportCancelledError final : public MmsAssociationRuntimeError {
public:
    using MmsAssociationRuntimeError::MmsAssociationRuntimeError;
};

struct MmsEndpoint final {
    std::string host;
    std::uint16_t port{102U};

    friend bool operator==(const MmsEndpoint&, const MmsEndpoint&) = default;
};

class MmsByteTransport {
public:
    using Clock = std::chrono::steady_clock;
    using Deadline = Clock::time_point;

    virtual ~MmsByteTransport() = default;

    virtual void connect(
        const MmsEndpoint& endpoint,
        Deadline deadline,
        std::stop_token stop_token) = 0;
    virtual void send(
        std::span<const std::uint8_t> bytes,
        Deadline deadline,
        std::stop_token stop_token) = 0;
    [[nodiscard]] virtual std::vector<std::uint8_t> receive(
        Deadline deadline,
        std::stop_token stop_token) = 0;
    virtual void close() noexcept = 0;
    [[nodiscard]] virtual bool connected() const noexcept = 0;
};

enum class MmsAssociationRuntimeState : std::uint8_t {
    disconnected,
    transport_connecting,
    transport_connected,
    cotp_connecting,
    cotp_connected,
    acse_associating,
    associated,
    closing,
    faulted,
};

enum class MmsAssociationEventKind : std::uint8_t {
    state_changed,
    transport_connected,
    cotp_connected,
    association_accepted,
    confirmed_request_sent,
    confirmed_result_received,
    information_report_received,
    unmatched_pdu_received,
    disconnected,
    cancelled,
    timed_out,
    faulted,
};

struct MmsAssociationEvent final {
    MmsAssociationEventKind kind{MmsAssociationEventKind::state_changed};
    MmsAssociationRuntimeState state{MmsAssociationRuntimeState::disconnected};
    std::optional<std::uint32_t> invoke_id;
    std::string message;
};

struct MmsAssociationAttemptEvidence final {
    std::string profile_name;
    bool accepted{};
    std::string message;

    friend bool operator==(const MmsAssociationAttemptEvidence&,
                           const MmsAssociationAttemptEvidence&) = default;
};

struct MmsAssociationOptions final {
    std::chrono::milliseconds connect_timeout{5'000};
    std::chrono::milliseconds request_timeout{5'000};
    std::uint8_t tpdu_size_code{0x0AU};
    std::uint32_t presentation_context_id{3U};
    std::size_t maximum_receive_chunks_per_operation{4'096U};
    std::size_t maximum_queued_information_reports{1'024U};
    std::size_t maximum_events{1'024U};
};

struct MmsNegotiatedAssociation final {
    std::uint8_t tpdu_size_code{0x0AU};
    std::uint32_t presentation_context_id{3U};
    std::uint32_t maximum_mms_pdu_size{65'000U};
    std::uint32_t maximum_outstanding_calling{10U};
    std::uint32_t maximum_outstanding_called{10U};
    std::uint32_t data_structure_nesting_level{5U};
};

struct MmsConfirmedExchangeResult final {
    MmsPduEnvelope envelope;
    std::vector<std::uint8_t> presentation_payload;
};

class MmsAssociationRuntime final {
public:
    explicit MmsAssociationRuntime(
        MmsByteTransport& transport,
        MmsAssociationOptions options = {});
    ~MmsAssociationRuntime();

    MmsAssociationRuntime(const MmsAssociationRuntime&) = delete;
    MmsAssociationRuntime& operator=(const MmsAssociationRuntime&) = delete;

    void connect(
        const MmsEndpoint& endpoint,
        std::stop_token stop_token = {});
    void disconnect(std::stop_token stop_token = {}) noexcept;

    [[nodiscard]] MmsConfirmedExchangeResult exchange_confirmed(
        std::span<const std::uint8_t> presentation_payload,
        std::uint32_t expected_invoke_id,
        std::stop_token stop_token = {});

    [[nodiscard]] MmsPduEnvelope poll_once(
        std::stop_token stop_token = {});

    // Bounded receive primitive for application workflows where expiry is a
    // normal application result rather than an association fault. A transport
    // timeout returns false and leaves the association active. Caller
    // cancellation is propagated without rewriting association state. Other
    // transport/protocol failures retain the normal fail-closed behavior.
    [[nodiscard]] bool try_poll_once_for(
        std::chrono::milliseconds timeout,
        MmsPduEnvelope& envelope,
        std::stop_token stop_token = {});

    [[nodiscard]] bool try_pop_information_report(
        std::vector<std::uint8_t>& presentation_payload);

    [[nodiscard]] std::uint32_t next_invoke_id();
    [[nodiscard]] MmsAssociationRuntimeState state() const noexcept { return state_; }
    [[nodiscard]] bool associated() const noexcept {
        return state_ == MmsAssociationRuntimeState::associated;
    }
    [[nodiscard]] const MmsEndpoint& endpoint() const noexcept { return endpoint_; }
    [[nodiscard]] const MmsNegotiatedAssociation& negotiated() const noexcept {
        return negotiated_;
    }
    [[nodiscard]] const std::deque<MmsAssociationEvent>& events() const noexcept {
        return events_;
    }
    [[nodiscard]] const std::vector<MmsAssociationAttemptEvidence>&
        association_attempts() const noexcept {
        return association_attempts_;
    }
    [[nodiscard]] const std::string& active_association_profile() const noexcept {
        return active_association_profile_;
    }
    [[nodiscard]] std::size_t queued_information_report_count() const noexcept {
        return information_reports_.size();
    }
    [[nodiscard]] const std::string& last_fault() const noexcept { return last_fault_; }

private:
    using Clock = MmsByteTransport::Clock;
    using Deadline = MmsByteTransport::Deadline;

    [[nodiscard]] Deadline deadline_after(std::chrono::milliseconds timeout) const;
    void require_not_cancelled(std::stop_token stop_token) const;
    void require_associated() const;
    void set_state(MmsAssociationRuntimeState state, std::string message);
    void add_event(
        MmsAssociationEventKind kind,
        std::string message,
        std::optional<std::uint32_t> invoke_id = std::nullopt);
    void fail(std::string message);

    void send_tpkt_payload(
        std::span<const std::uint8_t> cotp_payload,
        Deadline deadline,
        std::stop_token stop_token);
    void send_application_payload(
        std::span<const std::uint8_t> application_payload,
        Deadline deadline,
        std::stop_token stop_token);
    [[nodiscard]] std::vector<std::uint8_t> receive_application_payload(
        Deadline deadline,
        std::stop_token stop_token);
    [[nodiscard]] MmsPduEnvelope route_received_payload(
        std::span<const std::uint8_t> presentation_payload);
    void queue_information_report(std::span<const std::uint8_t> payload);

    MmsByteTransport& transport_;
    MmsAssociationOptions options_;
    MmsAssociationRuntimeState state_{MmsAssociationRuntimeState::disconnected};
    MmsEndpoint endpoint_;
    MmsNegotiatedAssociation negotiated_;
    MmsInvokeRouter invoke_router_;
    osi::TpktStreamDecoder tpkt_decoder_;
    std::deque<std::vector<std::uint8_t>> information_reports_;
    std::deque<MmsAssociationEvent> events_;
    std::vector<MmsAssociationAttemptEvidence> association_attempts_;
    std::string active_association_profile_;
    std::uint32_t next_invoke_id_{};
    std::uint16_t local_cotp_reference_{1U};
    std::uint16_t remote_cotp_reference_{};
    std::string last_fault_;
};

} // namespace ar::iec61850::mms
