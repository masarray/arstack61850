// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace ar::iec61850::time_sync {

// Control-plane-only gate for opt-in isolated-LAB SOURCE startup. No task,
// allocation, ISR, UART, clock read or network operation is hidden here.
// Never start another GM when RECEIVER/MONITOR or an active runtime is present.
struct SvBootClockPolicy final {
    static constexpr std::uint32_t kPollPeriodMs = 250U;
    static constexpr std::uint32_t kMaxAttempts = 4U;

    [[nodiscard]] static constexpr bool may_start(
        const bool opted_in, const bool operator_overrode,
        const bool selected_source, const bool eth_tx_ready,
        const bool ptp_runtime_owned, const std::uint32_t attempts,
        const std::uint32_t now_ms, const std::uint32_t retry_after_ms) noexcept {
        return opted_in && !operator_overrode && selected_source &&
            eth_tx_ready && !ptp_runtime_owned && attempts < kMaxAttempts &&
            static_cast<std::int32_t>(now_ms - retry_after_ms) >= 0;
    }

    // Re-arm automatic startup ONLY after a genuine PHY link-down/up cycle.
    // A plain operator SV STOP, USB loss, SOURCE task fault or GUI action
    // never permits silent grandmaster promotion/re-seeding.
    [[nodiscard]] static constexpr bool may_recover_after_link_cycle(
        const bool once_ready, const bool observed_link_loss,
        const bool link_now_ready, const bool runtime_owned,
        const bool operator_overrode, const bool selected_source) noexcept {
        return once_ready && observed_link_loss && link_now_ready &&
            !runtime_owned && !operator_overrode && selected_source;
    }

    // Bounded exponential retry only after genuine startup failure. Never
    // keep re-seeding the shared EMAC timebase indefinitely.
    [[nodiscard]] static constexpr std::uint32_t retry_delay_ms(
        const std::uint32_t failed_attempts) noexcept {
        const auto capped = failed_attempts > 4U ? 4U : failed_attempts;
        return 1000U << capped; // 2, 4, 8, 16 seconds
    }
};
} // namespace ar::iec61850::time_sync
