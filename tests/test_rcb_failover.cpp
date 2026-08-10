// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/rcb_contention.hpp"
#include "ariec61850/mms/rcb_failover.hpp"
#include "ariec61850/mms/rcb_selection.hpp"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace ar::iec61850;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            throw std::runtime_error( \
                std::string{"CHECK failed: "} + #condition + \
                " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (false)

[[nodiscard]] mms::MmsRcbContentionProbeResult probe(
    std::string reference,
    const bool busy,
    const bool flapping = false) {
    mms::MmsRcbContentionProbeResult result;
    result.rcb_reference = std::move(reference);
    result.is_busy_at_probe = busy;
    result.is_flapping = flapping;
    result.is_contended = busy || flapping;
    result.cooldown_seconds = result.is_contended ? 60 : 0;
    result.reason = result.is_contended ? "contended" : "stable";
    return result;
}

[[nodiscard]] mms::MmsReportControlCandidate candidate(std::string name) {
    mms::MmsReportControlCandidate value;
    value.domain = "LD0";
    value.logical_node = "LLN0";
    value.functional_constraint = "BR";
    value.name = std::move(name);
    value.reference = "LD0/LLN0.BR." + value.name;
    value.buffered = true;
    value.attributes = {"DatSet", "RptEna", "ResvTms"};
    return value;
}

void add_free_state(
    mms::MmsLiveDiscoveryResult& discovery,
    const mms::MmsReportControlCandidate& value) {
    mms::MmsReportControlEvidence evidence;
    evidence.candidate = value;
    evidence.requested_attributes = {"DatSet", "RptEna", "ResvTms"};
    mms::MmsReportControlState state;
    state.candidate = value;
    state.report_enabled = false;
    state.reservation_time_seconds = 0U;
    evidence.state = std::move(state);
    discovery.report_controls.push_back(std::move(evidence));
}

void stable_candidate_is_accepted_once() {
    mms::MmsRcbPreclaimFailoverTracker tracker{3U};
    CHECK(tracker.may_attempt());
    CHECK(tracker.observe(probe("LD0/LLN0.BR.a", false)) ==
          mms::MmsRcbPreclaimOutcome::stable_proceed);
    CHECK(!tracker.may_attempt());
    CHECK(tracker.snapshot().selected_rcb_reference == "LD0/LLN0.BR.a");
    CHECK(tracker.snapshot().excluded_rcb_references.empty());
}

void contended_candidate_is_excluded_before_fallback() {
    mms::MmsRcbPreclaimFailoverTracker tracker{3U};
    CHECK(tracker.observe(probe("LD0/LLN0.BR.a", true)) ==
          mms::MmsRcbPreclaimOutcome::skipped_contended);
    CHECK(tracker.may_attempt());
    CHECK(tracker.excluded_rcb_references() ==
          std::vector<std::string>{"LD0/LLN0.BR.a"});
    CHECK(tracker.observe(probe("LD0/LLN0.BR.b", false)) ==
          mms::MmsRcbPreclaimOutcome::stable_proceed);
    CHECK(tracker.snapshot().attempts.size() == 2U);
    CHECK(tracker.snapshot().selected_rcb_reference == "LD0/LLN0.BR.b");
}

void maximum_attempts_are_hard_bounded() {
    mms::MmsRcbPreclaimFailoverTracker tracker{2U};
    CHECK(tracker.observe(probe("LD0/LLN0.BR.a", false, true)) ==
          mms::MmsRcbPreclaimOutcome::skipped_contended);
    CHECK(tracker.observe(probe("LD0/LLN0.BR.b", true)) ==
          mms::MmsRcbPreclaimOutcome::attempts_exhausted);
    CHECK(tracker.snapshot().exhausted);
    CHECK(!tracker.may_attempt());
}

void excluded_preferred_candidate_reranks_to_next_slot() {
    mms::MmsLiveDiscoveryResult discovery;
    const auto preferred = candidate("a");
    const auto fallback = candidate("b");
    discovery.report_inventory.report_controls = {preferred, fallback};
    add_free_state(discovery, preferred);
    add_free_state(discovery, fallback);

    mms::MmsRcbDynamicSelectionOptions options;
    options.preferred_rcb_reference = preferred.reference;
    auto selection = mms::MmsRcbPoolSelector::build_dynamic_selection(
        discovery, options);
    CHECK(selection.selected_rcb_reference == preferred.reference);

    mms::MmsRcbPreclaimFailoverTracker tracker{3U};
    CHECK(tracker.observe(probe(preferred.reference, true)) ==
          mms::MmsRcbPreclaimOutcome::skipped_contended);
    options.excluded_rcb_references = tracker.excluded_rcb_references();
    selection = mms::MmsRcbPoolSelector::build_dynamic_selection(
        discovery, options);
    CHECK(selection.selected_rcb_reference == fallback.reference);
    CHECK(selection.fallback_used);
}

void invalid_and_duplicate_observations_are_rejected() {
    bool zero_rejected = false;
    try {
        static_cast<void>(mms::MmsRcbPreclaimFailoverTracker{0U});
    } catch (const std::invalid_argument&) {
        zero_rejected = true;
    }
    CHECK(zero_rejected);

    mms::MmsRcbPreclaimFailoverTracker tracker{3U};
    static_cast<void>(tracker.observe(probe("LD0/LLN0.BR.a", true)));
    bool duplicate_rejected = false;
    try {
        static_cast<void>(tracker.observe(probe("LD0/LLN0.BR.a", true)));
    } catch (const std::logic_error&) {
        duplicate_rejected = true;
    }
    CHECK(duplicate_rejected);
}

} // namespace

int main() {
    try {
        stable_candidate_is_accepted_once();
        contended_candidate_is_excluded_before_fallback();
        maximum_attempts_are_hard_bounded();
        excluded_preferred_candidate_reranks_to_next_slot();
        invalid_and_duplicate_observations_are_rejected();
        std::cout << "RCB failover tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
