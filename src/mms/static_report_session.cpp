// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_report_session.hpp"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <utility>

namespace ar::iec61850::mms {
namespace {

[[nodiscard]] bool same_object_name(
    const std::string& left,
    const std::string& right) noexcept {
    try {
        return MmsDataSetDirectoryCodec::parse_data_set_reference(left) ==
               MmsDataSetDirectoryCodec::parse_data_set_reference(right);
    } catch (...) {
        return false;
    }
}

[[nodiscard]] const MmsReportControlEvidence* find_control_evidence(
    const MmsLiveDiscoveryResult& discovery,
    const std::string& reference) noexcept {
    const auto found = std::find_if(
        discovery.report_controls.begin(),
        discovery.report_controls.end(),
        [&reference](const auto& evidence) {
            return evidence.candidate.reference == reference;
        });
    return found == discovery.report_controls.end() ? nullptr : &*found;
}

[[nodiscard]] const MmsDataSetDirectoryEvidence* find_directory_evidence(
    const MmsLiveDiscoveryResult& discovery,
    const std::string& reference) noexcept {
    const auto found = std::find_if(
        discovery.data_set_directories.begin(),
        discovery.data_set_directories.end(),
        [&reference](const auto& evidence) {
            return same_object_name(evidence.candidate.reference, reference);
        });
    return found == discovery.data_set_directories.end() ? nullptr : &*found;
}

} // namespace

MmsStaticReportSessionRuntime::MmsStaticReportSessionRuntime(
    MmsAssociationRuntime& association,
    const MmsLiveDiscoveryResult& discovery,
    MmsStaticReportSessionOptions options)
    : association_{association},
      discovery_{discovery},
      options_{std::move(options)},
      failover_{options_.maximum_candidate_attempts} {
    if (options_.subscription.write_data_set_reference) {
        throw std::invalid_argument(
            "Static report session cannot rewrite the RCB DatSet binding.");
    }
}

void MmsStaticReportSessionRuntime::prepare(
    const std::stop_token stop_token) {
    if (prepared_ || selected_candidate_ || subscription_) {
        throw MmsStaticReportSessionError(
            "Static report session prepare may be called only once.");
    }
    if (!association_.associated()) {
        throw MmsStaticReportSessionError(
            "Static report session requires an active MMS association.");
    }

    MmsRcbContentionProbeClient probe{association_};
    while (failover_.may_attempt()) {
        auto selection_options = options_.selection;
        selection_options.excluded_rcb_references =
            failover_.excluded_rcb_references();
        const auto selection = MmsRcbPoolSelector::build_static_selection(
            discovery_, selection_options);
        if (!selection.selected()) {
            blockers_ = selection.blockers;
            break;
        }
        const auto* candidate = MmsRcbPoolSelector::select_report_control(
            discovery_, selection);
        if (candidate == nullptr) {
            blockers_.push_back(
                "Static RCB selection returned no matching inventory candidate.");
            break;
        }

        const auto contention = probe.probe(
            *candidate, options_.contention, stop_token);
        const auto outcome = failover_.observe(contention);
        if (outcome == MmsRcbPreclaimOutcome::stable_proceed) {
            selected_candidate_ = *candidate;
            resolve_selected_static_binding();
            prepared_ = true;
            return;
        }
    }

    if (blockers_.empty()) {
        blockers_.push_back(
            failover_.snapshot().exhausted
                ? "Static report pre-claim candidate attempt limit was exhausted."
                : "No stable static RCB candidate remained after pre-claim selection.");
    }
    throw MmsStaticReportSessionError(blockers_.front());
}

void MmsStaticReportSessionRuntime::resolve_selected_static_binding() {
    if (!selected_candidate_) {
        throw MmsStaticReportSessionError(
            "Cannot resolve a static DataSet without a selected RCB.");
    }
    const auto* control = find_control_evidence(
        discovery_, selected_candidate_->reference);
    if (control == nullptr || !control->state) {
        throw MmsStaticReportSessionError(
            "Selected static RCB has no successful runtime-state evidence.");
    }
    selected_data_set_reference_ = control->state->data_set_reference;
    if (selected_data_set_reference_.empty()) {
        throw MmsStaticReportSessionError(
            "Selected static RCB has no bound DataSet reference.");
    }
    const auto* directory = find_directory_evidence(
        discovery_, selected_data_set_reference_);
    if (directory == nullptr || !directory->directory ||
        directory->directory->members.empty()) {
        throw MmsStaticReportSessionError(
            "Selected static RCB DataSet directory is missing or empty.");
    }
    selected_directory_ = *directory->directory;
}

void MmsStaticReportSessionRuntime::start(
    const std::stop_token stop_token) {
    if (!prepared_ || !selected_candidate_ || !selected_directory_) {
        throw MmsStaticReportSessionError(
            "Static report session must be prepared before start.");
    }
    if (subscription_) {
        throw MmsStaticReportSessionError(
            "Static report subscription has already been started.");
    }
    subscription_ = std::make_unique<MmsReportSubscriptionRuntime>(
        association_, *selected_candidate_, *selected_directory_,
        options_.subscription);
    subscription_->start(stop_token);
}

void MmsStaticReportSessionRuntime::stop(
    const std::stop_token stop_token) noexcept {
    if (subscription_) {
        subscription_->stop(stop_token);
    }
}

bool MmsStaticReportSessionRuntime::poll_once(
    const std::stop_token stop_token) {
    if (!subscription_) {
        throw MmsStaticReportSessionError(
            "Static report session has not been started.");
    }
    return subscription_->poll_once(stop_token);
}

std::size_t MmsStaticReportSessionRuntime::drain_queued_reports() {
    if (!subscription_) {
        throw MmsStaticReportSessionError(
            "Static report session has not been started.");
    }
    return subscription_->drain_queued_reports();
}

MmsStaticReportSessionSnapshot MmsStaticReportSessionRuntime::snapshot() const {
    MmsStaticReportSessionSnapshot result;
    result.prepared = prepared_;
    result.active = active();
    result.failover = failover_.snapshot();
    result.blockers = blockers_;
    result.selected_rcb_reference = selected_candidate_
        ? selected_candidate_->reference
        : std::string{};
    result.data_set_reference = selected_data_set_reference_;
    result.data_set_member_count = selected_directory_
        ? selected_directory_->members.size()
        : 0U;
    if (subscription_) {
        result.subscription = subscription_->snapshot();
    }
    return result;
}

} // namespace ar::iec61850::mms
