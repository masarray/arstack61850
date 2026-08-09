// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/static_dispatcher.hpp"

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
    backend_failure,
    closed,
};

struct MmsStaticConnectionPolicy final {
    std::uint16_t cotp_source_reference{0x1001U};
    std::uint8_t maximum_tpdu_size_code{0x0AU};
    bool require_end_of_transmission{true};
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

    constexpr void reset() noexcept {
        state_ = MmsStaticConnectionState::awaiting_cotp_connect;
        mms_presentation_context_id_ = 0U;
    }

    [[nodiscard]] constexpr MmsStaticConnectionState state() const noexcept {
        return state_;
    }

    [[nodiscard]] constexpr std::uint32_t mms_presentation_context_id() const noexcept {
        return mms_presentation_context_id_;
    }

private:
    const MmsStaticApplicationDispatcher& dispatcher_;
    MmsStaticConnectionPolicy policy_{};
    MmsStaticConnectionState state_{MmsStaticConnectionState::awaiting_cotp_connect};
    std::uint32_t mms_presentation_context_id_{};
};

} // namespace ar::iec61850::mms
