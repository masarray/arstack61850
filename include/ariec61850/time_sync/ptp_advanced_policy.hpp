// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace ar::iec61850::time_sync {

// Runtime operator intent; never substitute a claimed GNSS clock quality for
// measured PTP time authority. Immutable decisions are ISR-free and alloc-free.
enum class PtpAdvancedMode : std::uint8_t {
    internal_gm_only = 0U,
    auto_prefer_external = 1U,
    external_only = 2U,
    monitor_only = 3U
};

struct PtpAdvancedPolicy final {
    [[nodiscard]] static constexpr bool valid(
        const PtpAdvancedMode mode) noexcept {
        return mode <= PtpAdvancedMode::monitor_only;
    }
    [[nodiscard]] static constexpr bool may_apply(
        const bool session_verified, const bool sv_stopped,
        const bool ptp_runtime_stopped,
        const bool no_role_cleanup_pending) noexcept {
        return session_verified && sv_stopped && ptp_runtime_stopped &&
            no_role_cleanup_pending;
    }
    [[nodiscard]] static constexpr bool auto_handover(
        const PtpAdvancedMode mode) noexcept {
        return mode == PtpAdvancedMode::auto_prefer_external;
    }
    [[nodiscard]] static constexpr bool permits_local_fallback(
        const PtpAdvancedMode mode, const bool bench_opt_in) noexcept {
        return auto_handover(mode) && bench_opt_in;
    }
    [[nodiscard]] static constexpr bool starts_source(
        const PtpAdvancedMode mode) noexcept {
        return mode == PtpAdvancedMode::internal_gm_only ||
            mode == PtpAdvancedMode::auto_prefer_external;
    }
    [[nodiscard]] static constexpr bool starts_receiver(
        const PtpAdvancedMode mode) noexcept {
        return mode == PtpAdvancedMode::external_only;
    }
    [[nodiscard]] static constexpr bool starts_monitor(
        const PtpAdvancedMode mode) noexcept {
        return mode == PtpAdvancedMode::monitor_only;
    }
};
} // namespace ar::iec61850::time_sync
