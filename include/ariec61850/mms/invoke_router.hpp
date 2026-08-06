// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/pdu.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <span>
#include <unordered_map>

namespace ar::iec61850::mms {

enum class MmsInvokeRouteAction : std::uint8_t {
    queued_confirmed_result,
    queued_unmatched,
};

struct MmsInvokeRouteResult final {
    MmsInvokeRouteAction action{MmsInvokeRouteAction::queued_unmatched};
    MmsPduEnvelope envelope;
};

class MmsInvokeRouter final {
public:
    explicit MmsInvokeRouter(
        std::size_t maximum_queued_results = 1'024U,
        std::size_t maximum_results_per_invoke = 8U);

    [[nodiscard]] MmsInvokeRouteResult route(
        std::span<const std::uint8_t> presentation_or_mms_payload);

    [[nodiscard]] bool try_dequeue(
        std::uint32_t invoke_id,
        MmsPduEnvelope& envelope);
    [[nodiscard]] bool try_dequeue_unmatched(MmsPduEnvelope& envelope);

    [[nodiscard]] std::size_t queued_confirmed_count() const noexcept;
    [[nodiscard]] std::size_t queued_unmatched_count() const noexcept;

    void clear() noexcept;

private:
    std::size_t maximum_queued_results_;
    std::size_t maximum_results_per_invoke_;
    mutable std::mutex mutex_;
    std::unordered_map<std::uint32_t, std::deque<MmsPduEnvelope>> confirmed_;
    std::deque<MmsPduEnvelope> unmatched_;
    std::size_t confirmed_count_{};
};

} // namespace ar::iec61850::mms
