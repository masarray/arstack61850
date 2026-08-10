// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/association_runtime.hpp"

#include <chrono>
#include <exception>
#include <stdexcept>

namespace ar::iec61850::mms {

bool MmsAssociationRuntime::try_poll_once_for(
    const std::chrono::milliseconds timeout,
    MmsPduEnvelope& envelope,
    const std::stop_token stop_token) {
    require_associated();
    if (timeout <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("Bounded MMS poll timeout must be positive.");
    }
    require_not_cancelled(stop_token);

    try {
        auto payload = receive_application_payload(
            deadline_after(timeout), stop_token);
        envelope = route_received_payload(payload);
        return true;
    } catch (const MmsTransportTimeoutError&) {
        // Intentional application-level expiry. The byte transport remains
        // owned by the active association; do not convert a missing
        // CommandTermination into an MMS association fault.
        return false;
    } catch (const MmsTransportCancelledError&) {
        // Caller cancellation means "stop waiting" for this API. Preserve the
        // association and let the caller decide whether to disconnect.
        throw;
    } catch (const std::exception& exception) {
        if (stop_token.stop_requested()) {
            throw MmsTransportCancelledError(exception.what());
        }
        fail(exception.what());
        transport_.close();
        invoke_router_.clear();
        information_reports_.clear();
        tpkt_decoder_.reset();
        remote_cotp_reference_ = 0U;
        throw;
    }
}

} // namespace ar::iec61850::mms
