// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/static_dispatcher.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::mms {

enum class MmsStaticConnectionState : std::uint8_t {
    awaiting_cotp_connect,
    awaiting_association,
    established,
    closed,
    fault,
};

enum class MmsStaticConnectionStatus : std::uint8_t {
    need_more,
    response_ready,
    consumed_no_response,
    malformed_transport,
    protocol_violation,
    application_rejected,
    response_buffer_too_small,
    workspace_too_small,
    peer_limit_exceeded,
    backend_failure,
    closed,
};

using MmsStaticConnectionNowMsCallback = std::uint64_t (*)(
    const void* context) noexcept;

using MmsStaticAssociationClosedCallback = void (*)(
    void* context,
    std::uint64_t association_id,
    std::uint64_t now_ms) noexcept;

struct MmsStaticConnectionPolicy final {
    static constexpr std::size_t maximum_owner_bytes = 16U;

    std::uint16_t cotp_source_reference{0x1001U};
    std::uint8_t maximum_tpdu_size_code{0x0AU};
    bool require_end_of_transmission{true};

    // Optional portable association identity used by contextual MMS writes.
    // The transport/server adapter assigns association_id and stable Owner.
    // Leaving these zero/empty preserves legacy non-contextual behavior.
    std::uint64_t association_id{};
    std::array<std::uint8_t, maximum_owner_bytes> owner{};
    std::size_t owner_size{};

    // Optional lifecycle bridge. The connection runtime remains independent of
    // reporting/control classes: a server adapter may forward this event to a
    // BRCB control block, session registry, audit sink, or several fan-out
    // targets. `now_ms` must use the same monotonic clock as ResvTms handling.
    MmsStaticConnectionNowMsCallback now_ms{};
    const void* now_context{};
    MmsStaticAssociationClosedCallback association_closed{};
    void* association_closed_context{};

    [[nodiscard]] constexpr MmsStaticRequestAccessContext access_context() const noexcept {
        return {
            association_id,
            owner_size > 0U && owner_size <= owner.size()
                ? std::span<const std::uint8_t>{owner.data(), owner_size}
                : std::span<const std::uint8_t>{}};
    }
};

struct MmsStaticConnectionResult final {
    MmsStaticConnectionStatus status{MmsStaticConnectionStatus::need_more};
    MmsStaticConnectionState state{MmsStaticConnectionState::awaiting_cotp_connect};
    std::size_t consumed_bytes{};
    std::size_t bytes_written{};
    std::size_t required_response_bytes{};
    std::size_t required_workspace_bytes{};
    MmsStaticDispatchStatus application_status{MmsStaticDispatchStatus::malformed_request};
    MmsWireConfirmedService application_service{MmsWireConfirmedService::unknown};
    std::uint32_t invoke_id{};

    [[nodiscard]] constexpr bool response_ready() const noexcept {
        return status == MmsStaticConnectionStatus::response_ready;
    }
};

class MmsStaticConnectionRuntime final {
public:
    explicit constexpr MmsStaticConnectionRuntime(
        const MmsStaticApplicationDispatcher& dispatcher,
        const MmsStaticConnectionPolicy policy = {}) noexcept
        : dispatcher_{dispatcher}, policy_{policy} {}

    // Process at most the first complete TPKT frame in a TCP receive window.
    // `consumed_bytes` identifies the prefix the transport adapter may remove.
    // On need_more or buffer-capacity failures no input is consumed, allowing
    // the same frame to be retried once more bytes/capacity are available.
    [[nodiscard]] MmsStaticConnectionResult process_tcp_window(
        std::span<const std::uint8_t> tcp_bytes,
        std::span<std::uint8_t> response,
        std::span<std::uint8_t> workspace) noexcept;

    // Notify the lifecycle bridge exactly once for an established association
    // and mark the transport closed. Call this for TCP EOF, socket error, local
    // close, or any fatal transport teardown not represented by a COTP DR TPDU.
    void close_transport() noexcept;

    // Reuse this runtime for a fresh transport. If an association was still
    // active, reset first emits the same single association-closed event.
    void reset() noexcept;

    [[nodiscard]] constexpr MmsStaticConnectionState state() const noexcept {
        return state_;
    }

    [[nodiscard]] constexpr std::uint32_t mms_presentation_context_id() const noexcept {
        return mms_presentation_context_id_;
    }

    // Negotiated outbound limits. They are zero until the corresponding COTP
    // and MMS association handshakes have completed successfully.
    [[nodiscard]] constexpr std::size_t negotiated_tpdu_size_bytes() const noexcept {
        return negotiated_tpdu_size_bytes_;
    }

    [[nodiscard]] constexpr std::uint32_t negotiated_mms_pdu_size() const noexcept {
        return negotiated_mms_pdu_size_;
    }

    [[nodiscard]] constexpr MmsStaticRequestAccessContext access_context() const noexcept {
        return policy_.access_context();
    }

    [[nodiscard]] constexpr bool association_active() const noexcept {
        return association_active_;
    }

private:
    void notify_association_closed() noexcept;

    const MmsStaticApplicationDispatcher& dispatcher_;
    MmsStaticConnectionPolicy policy_{};
    MmsStaticConnectionState state_{MmsStaticConnectionState::awaiting_cotp_connect};
    std::uint32_t mms_presentation_context_id_{};
    std::size_t negotiated_tpdu_size_bytes_{};
    std::uint32_t negotiated_mms_pdu_size_{};
    bool association_active_{};
    bool association_close_notified_{};
};

} // namespace ar::iec61850::mms