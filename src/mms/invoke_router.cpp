// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/invoke_router.hpp"

#include <algorithm>
#include <stdexcept>

namespace ar::iec61850::mms {

MmsInvokeRouter::MmsInvokeRouter(
    const std::size_t maximum_queued_results,
    const std::size_t maximum_results_per_invoke)
    : maximum_queued_results_{maximum_queued_results},
      maximum_results_per_invoke_{maximum_results_per_invoke} {
    if (maximum_queued_results_ == 0U || maximum_results_per_invoke_ == 0U ||
        maximum_results_per_invoke_ > maximum_queued_results_) {
        throw std::invalid_argument("MMS invoke-router limits are invalid.");
    }
}

MmsInvokeRouteResult MmsInvokeRouter::route(
    const std::span<const std::uint8_t> presentation_or_mms_payload) {
    auto envelope = MmsPduCodec::decode_envelope(presentation_or_mms_payload);
    std::lock_guard lock{mutex_};

    if (envelope.confirmed_result() && envelope.invoke_id) {
        if (confirmed_count_ >= maximum_queued_results_) {
            throw MmsFormatError("MMS invoke-router total queue limit exceeded.");
        }
        auto& queue = confirmed_[*envelope.invoke_id];
        if (queue.size() >= maximum_results_per_invoke_) {
            throw MmsFormatError("MMS invoke-router per-invoke queue limit exceeded.");
        }
        queue.push_back(envelope);
        ++confirmed_count_;
        return {MmsInvokeRouteAction::queued_confirmed_result, std::move(envelope)};
    }

    if (unmatched_.size() >= maximum_queued_results_) {
        unmatched_.pop_front();
    }
    unmatched_.push_back(envelope);
    return {MmsInvokeRouteAction::queued_unmatched, std::move(envelope)};
}

bool MmsInvokeRouter::try_dequeue(
    const std::uint32_t invoke_id,
    MmsPduEnvelope& envelope) {
    std::lock_guard lock{mutex_};
    const auto found = confirmed_.find(invoke_id);
    if (found == confirmed_.end() || found->second.empty()) {
        return false;
    }
    envelope = std::move(found->second.front());
    found->second.pop_front();
    --confirmed_count_;
    if (found->second.empty()) {
        confirmed_.erase(found);
    }
    return true;
}

bool MmsInvokeRouter::try_dequeue_unmatched(MmsPduEnvelope& envelope) {
    std::lock_guard lock{mutex_};
    if (unmatched_.empty()) {
        return false;
    }
    envelope = std::move(unmatched_.front());
    unmatched_.pop_front();
    return true;
}

std::size_t MmsInvokeRouter::queued_confirmed_count() const noexcept {
    std::lock_guard lock{mutex_};
    return confirmed_count_;
}

std::size_t MmsInvokeRouter::queued_unmatched_count() const noexcept {
    std::lock_guard lock{mutex_};
    return unmatched_.size();
}

void MmsInvokeRouter::clear() noexcept {
    std::lock_guard lock{mutex_};
    confirmed_.clear();
    unmatched_.clear();
    confirmed_count_ = 0U;
}

} // namespace ar::iec61850::mms
