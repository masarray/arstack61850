// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>

// Single publisher writer; control/diagnostics readers may run on another core.
// This records observed Ethernet TX failures. It must never turn a failed
// transmit into a successful sample or automatically restart the publisher.
namespace ar::iec61850::sampled_values {

struct SvTxFaultSnapshot final {
    std::uint32_t failed_frames{};
    std::uint32_t failure_bursts{};
    std::uint32_t recovered_bursts{};
    std::uint32_t current_streak{};
    std::int32_t first_error{};
    std::int32_t last_error{};
    bool last_link_up{};
    std::uint32_t stop_sequence{};
    std::uint32_t stop_uptime_ms{};
    std::uint32_t stop_streak{};
    std::int32_t stop_first_error{};
    std::int32_t stop_last_error{};
    bool stop_link_up{};
};

class SvTxFaultBurst final {
public:
    // Returns true only on the first error of each contiguous TX failure burst.
    // Caller can revoke sync authority once; it must still count each miss.
    [[nodiscard]] bool failure(
        const std::int32_t error, const bool link_up) noexcept {
        failed_frames_.fetch_add(1U, std::memory_order_relaxed);
        const auto was = current_streak_.fetch_add(1U, std::memory_order_acq_rel);
        if (was == 0U) {
            first_error_.store(error, std::memory_order_relaxed);
            failure_bursts_.fetch_add(1U, std::memory_order_relaxed);
        }
        last_error_.store(error, std::memory_order_relaxed);
        last_link_up_.store(link_up, std::memory_order_release);
        return was == 0U;
    }

    // Call only following a successful canonical TX after a nonzero streak.
    void recovered() noexcept {
        if (current_streak_.exchange(0U, std::memory_order_acq_rel) != 0U)
            recovered_bursts_.fetch_add(1U, std::memory_order_relaxed);
    }

    // START clears the active error streak, NOT the persistent last-stop record.
    void new_session() noexcept {
        current_streak_.store(0U, std::memory_order_release);
    }

    // Called immediately BEFORE fail-closed STOP, while the writer still owns
    // the current burst. Keep the real ESP-IDF error across a manual restart.
    void stopped(const std::uint32_t uptime_ms) noexcept {
        stop_streak_.store(
            current_streak_.load(std::memory_order_acquire),
            std::memory_order_relaxed);
        stop_first_error_.store(
            first_error_.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        stop_last_error_.store(
            last_error_.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        stop_link_up_.store(
            last_link_up_.load(std::memory_order_acquire),
            std::memory_order_relaxed);
        stop_uptime_ms_.store(uptime_ms, std::memory_order_relaxed);
        stop_sequence_.fetch_add(1U, std::memory_order_release);
    }

    [[nodiscard]] SvTxFaultSnapshot snapshot() const noexcept {
        return {
            failed_frames_.load(std::memory_order_relaxed),
            failure_bursts_.load(std::memory_order_relaxed),
            recovered_bursts_.load(std::memory_order_relaxed),
            current_streak_.load(std::memory_order_acquire),
            first_error_.load(std::memory_order_relaxed),
            last_error_.load(std::memory_order_relaxed),
            last_link_up_.load(std::memory_order_acquire),
            stop_sequence_.load(std::memory_order_acquire),
            stop_uptime_ms_.load(std::memory_order_relaxed),
            stop_streak_.load(std::memory_order_relaxed),
            stop_first_error_.load(std::memory_order_relaxed),
            stop_last_error_.load(std::memory_order_relaxed),
            stop_link_up_.load(std::memory_order_relaxed),
        };
    }

private:
    std::atomic<std::uint32_t> failed_frames_{0U};
    std::atomic<std::uint32_t> failure_bursts_{0U};
    std::atomic<std::uint32_t> recovered_bursts_{0U};
    std::atomic<std::uint32_t> current_streak_{0U};
    std::atomic<std::int32_t> first_error_{0};
    std::atomic<std::int32_t> last_error_{0};
    std::atomic<bool> last_link_up_{false};
    std::atomic<std::uint32_t> stop_sequence_{0U};
    std::atomic<std::uint32_t> stop_uptime_ms_{0U};
    std::atomic<std::uint32_t> stop_streak_{0U};
    std::atomic<std::int32_t> stop_first_error_{0};
    std::atomic<std::int32_t> stop_last_error_{0};
    std::atomic<bool> stop_link_up_{false};
};

} // namespace ar::iec61850::sampled_values
