// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <utility>

namespace ar::iec61850::time_sync {

// Serialized PTP START/config and asynchronous STOP admission. STOP is
// published immediately even while START is blocking on PTP readiness.
// The current owner MUST drain STOP before releasing its slot. All state
// changes use one atomic word: no lost wakeup between final check and unlock.
class PtpControlCancellationGate final {
public:
    [[nodiscard]] bool try_acquire() noexcept {
        std::uint8_t expected = 0U;
        return state_.compare_exchange_strong(expected, kBusy,
            std::memory_order_acq_rel, std::memory_order_acquire);
    }

    // Returns true only when the caller owns the idle->STOP transition and
    // can immediately drain STOP. When false, an existing owner must drain.
    [[nodiscard]] bool request_stop_and_try_acquire() noexcept {
        const auto previous = state_.fetch_or(
            kStop, std::memory_order_acq_rel);
        if ((previous & kBusy) != 0U) return false;
        std::uint8_t expected = kStop;
        return state_.compare_exchange_strong(expected,
            static_cast<std::uint8_t>(kBusy | kStop),
            std::memory_order_acq_rel, std::memory_order_acquire);
    }

    [[nodiscard]] bool stop_requested() const noexcept {
        return (state_.load(std::memory_order_acquire) & kStop) != 0U;
    }

    // Only a successful acquisition owner may call finish(). The callback
    // is the existing bounded EMAC rollback and runs at most once per
    // acquisition. Repeated STOPs during rollback coalesce into that call:
    // no new START can enter until the single atomic release.
    template<class StopCallback>
    [[nodiscard]] bool finish(StopCallback&& stop_callback) noexcept {
        bool cancelled = false;
        for (;;) {
            const std::uint8_t value =
                state_.load(std::memory_order_acquire);
            if ((value & kStop) != 0U) {
                if (!cancelled) {
                    cancelled = true;
                    std::forward<StopCallback>(stop_callback)();
                }
                std::uint8_t expected =
                    static_cast<std::uint8_t>(kBusy | kStop);
                if (state_.compare_exchange_strong(expected, 0U,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire)) return false;
            } else {
                std::uint8_t expected = kBusy;
                if (state_.compare_exchange_strong(expected, 0U,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire)) return !cancelled;
            }
        }
    }

private:
    static constexpr std::uint8_t kBusy = 1U;
    static constexpr std::uint8_t kStop = 2U;
    std::atomic<std::uint8_t> state_{0U};
};

} // namespace ar::iec61850::time_sync
