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
    // SOURCE->SOURCE policy selection preserves only a still-fresh, explicitly
    // operator-provisioned bench UTC seed; never carry it into a receiver.
    // Single threshold shared with the EMAC PTP seed gate.
    static constexpr std::int64_t kBenchSeedMaxAgeUs = 30'000'000LL;
    [[nodiscard]] static constexpr bool may_preserve_bench_seed(
        const PtpAdvancedMode requested, const bool previous_source,
        const bool bench_enabled, const std::uint32_t epoch_seconds,
        const std::uint16_t epoch_millis, const std::int64_t received_mono_us,
        const std::int64_t now_mono_us) noexcept {
        return starts_source(requested) && previous_source && bench_enabled &&
            epoch_seconds >= 1577836800U && epoch_millis < 1000U &&
            received_mono_us > 0LL && now_mono_us >= received_mono_us &&
            now_mono_us - received_mono_us <= kBenchSeedMaxAgeUs;
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
