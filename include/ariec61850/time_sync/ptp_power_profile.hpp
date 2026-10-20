// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/time_sync/ptp_runtime.hpp"

#include <chrono>

namespace ar::iec61850::time_sync {

// SOURCE health is NOT a lifetime frame counter: following ANY TX failure,
// Announce and a *complete* Sync/Follow_Up pair must both be freshly sent.
// Successful Pdelay alone cannot requalify PTP SOURCE for the SV phase servo.
// This fixed-size policy has no platform/timebase assumptions.
class PtpSourceReadinessGate final {
public:
    constexpr void invalidate() noexcept {
        fresh_announce_ = false;
        fresh_sync_pair_ = false;
    }

    [[nodiscard]] constexpr bool ready() const noexcept {
        return fresh_announce_ && fresh_sync_pair_;
    }

    constexpr void observe_announce(const bool delivered) noexcept {
        if (!delivered) {
            invalidate();
        } else {
            fresh_announce_ = true;
        }
    }

    constexpr void observe_sync_pair(const bool delivered) noexcept {
        if (!delivered) {
            invalidate();
        } else {
            fresh_sync_pair_ = true;
        }
    }

    constexpr void observe_peer_delay(const bool delivered) noexcept {
        if (!delivered) invalidate();
    }

private:
    bool fresh_announce_{};
    bool fresh_sync_pair_{};
};

// A live Ethernet SOURCE is not healthy merely because Announce and Sync were
// successfully transmitted once. Require BOTH recent on-wire observations;
// every input is in the same monotonic microsecond timebase. This portable
// check is also used by the exposed SOURCE status, so stalled FreeRTOS
// scheduling cannot leave a stale READY boolean visible to SV phase control.
struct PtpSourceEvidenceFreshness final {
    [[nodiscard]] static constexpr bool fresh(
        const std::int64_t now_us, const std::int64_t announce_us,
        const std::int64_t sync_follow_up_us,
        const std::int64_t timeout_us) noexcept {
        if (now_us <= 0 || timeout_us <= 0 ||
            announce_us <= 0 || sync_follow_up_us <= 0 ||
            now_us < announce_us || now_us < sync_follow_up_us) {
            return false;
        }
        return now_us - announce_us < timeout_us &&
               now_us - sync_follow_up_us < timeout_us;
    }
};

// A plausible RTC and a configurable UTC offset do not by themselves prove
// the TAI/PTP timescale. Only set the timescale AND offset-valid flags when
// the EMAC was actually seeded and the offset was explicitly verified.
// No option here asserts time/frequency traceability or GM accuracy.
[[nodiscard]] constexpr PtpTimeProperties conservative_lab_time_properties(
    const bool hardware_tai_seeded,
    const bool verified_tai_utc_offset) noexcept {
    PtpTimeProperties result{};
    result.ptp_timescale = hardware_tai_seeded && verified_tai_utc_offset;
    result.current_utc_offset_valid = result.ptp_timescale;
    return result;
}

// Static configuration compatibility, NOT a declaration of IEEE/IEC
// conformance, grandmaster accuracy, or IED time lock. Runtime hardware
// timestamps, clock quality and Pdelay exchanges need independent evidence.
struct PtpPowerProfileCadence final {
    bool announce_1hz{};
    bool sync_1hz{};
    bool peer_delay_supported{};

    [[nodiscard]] constexpr bool compatible() const noexcept {
        return announce_1hz && sync_1hz && peer_delay_supported;
    }
};

// IEC/IEEE 61850-9-3:2016 profile admission, common to Studio control
// preflight and ESP32-P4 firmware. No clock-quality/IED-lock claim is made.
// Domain/VLAN are intentionally configurable to match the process-bus LAN.
[[nodiscard]] constexpr PtpPowerProfileCadence inspect_ptp_power_profile_cadence(
    const std::uint32_t announce_ms, const std::uint32_t sync_ms,
    const bool pdelay_responder) noexcept {
    return {announce_ms == 1000U, sync_ms == 1000U, pdelay_responder};
}

// Adapter for the validated protocol runtime. Must not diverge from the
// operator profile contract above.
[[nodiscard]] constexpr PtpPowerProfileCadence inspect_ptp_power_profile_cadence(
    const PtpPublisherOptions& options) noexcept {
    using namespace std::chrono_literals;
    return {
        options.announce_interval == 1000ms,
        options.sync_interval == 1000ms,
        options.respond_to_peer_delay,
    };
}

} // namespace ar::iec61850::time_sync
