// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/rcb_failover.hpp"

#include "ariec61850/mms/rcb_contention.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace ar::iec61850::mms {

std::string_view mms_rcb_preclaim_outcome_name(
    const MmsRcbPreclaimOutcome value) noexcept {
    switch (value) {
    case MmsRcbPreclaimOutcome::stable_proceed: return "StableProceed";
    case MmsRcbPreclaimOutcome::skipped_contended: return "SkippedContended";
    case MmsRcbPreclaimOutcome::attempts_exhausted: return "AttemptsExhausted";
    }
    return "Unknown";
}

MmsRcbPreclaimFailoverTracker::MmsRcbPreclaimFailoverTracker(
    const std::size_t maximum_candidate_attempts) {
    if (maximum_candidate_attempts == 0U) {
        throw std::invalid_argument(
            "RCB pre-claim failover requires at least one candidate attempt.");
    }
    snapshot_.maximum_candidate_attempts = maximum_candidate_attempts;
}

bool MmsRcbPreclaimFailoverTracker::may_attempt() const noexcept {
    return !snapshot_.selected() && !snapshot_.exhausted &&
           snapshot_.attempts.size() < snapshot_.maximum_candidate_attempts;
}

MmsRcbPreclaimOutcome MmsRcbPreclaimFailoverTracker::observe(
    const MmsRcbContentionProbeResult& result) {
    if (!may_attempt()) {
        throw std::logic_error(
            "RCB pre-claim failover is already selected or exhausted.");
    }
    if (result.rcb_reference.empty()) {
        throw std::invalid_argument(
            "RCB pre-claim probe result requires a candidate reference.");
    }
    const auto duplicate = std::find(
        snapshot_.excluded_rcb_references.begin(),
        snapshot_.excluded_rcb_references.end(),
        result.rcb_reference);
    if (duplicate != snapshot_.excluded_rcb_references.end()) {
        throw std::logic_error(
            "RCB pre-claim failover cannot probe an excluded candidate twice.");
    }

    MmsRcbPreclaimAttemptEvidence attempt;
    attempt.attempt_number = snapshot_.attempts.size() + 1U;
    attempt.rcb_reference = result.rcb_reference;
    attempt.busy = result.is_busy_at_probe;
    attempt.flapping = result.is_flapping;
    attempt.cooldown_seconds = result.cooldown_seconds;
    attempt.reason = result.reason;

    if (!result.is_contended) {
        attempt.outcome = MmsRcbPreclaimOutcome::stable_proceed;
        snapshot_.selected_rcb_reference = result.rcb_reference;
        snapshot_.attempts.push_back(std::move(attempt));
        return MmsRcbPreclaimOutcome::stable_proceed;
    }

    snapshot_.excluded_rcb_references.push_back(result.rcb_reference);
    const bool exhausted =
        snapshot_.attempts.size() + 1U >= snapshot_.maximum_candidate_attempts;
    attempt.outcome = exhausted
        ? MmsRcbPreclaimOutcome::attempts_exhausted
        : MmsRcbPreclaimOutcome::skipped_contended;
    snapshot_.exhausted = exhausted;
    snapshot_.attempts.push_back(std::move(attempt));
    return snapshot_.attempts.back().outcome;
}

} // namespace ar::iec61850::mms
