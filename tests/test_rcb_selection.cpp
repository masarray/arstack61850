// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/rcb_availability.hpp"
#include "ariec61850/mms/rcb_selection.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
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

[[nodiscard]] mms::MmsReportControlCandidate make_rcb(
    std::string name,
    const bool buffered = true,
    std::string domain = "IEDLD0",
    std::string logical_node = "LLN0") {
    mms::MmsReportControlCandidate candidate;
    candidate.domain = std::move(domain);
    candidate.logical_node = std::move(logical_node);
    candidate.functional_constraint = buffered ? "BR" : "RP";
    candidate.name = std::move(name);
    candidate.reference = candidate.domain + "/" + candidate.logical_node + "." +
        candidate.functional_constraint + "." + candidate.name;
    candidate.buffered = buffered;
    candidate.attributes = buffered
        ? std::vector<std::string>{"DatSet", "RptID", "ConfRev", "RptEna", "ResvTms", "Owner"}
        : std::vector<std::string>{"DatSet", "RptID", "ConfRev", "RptEna", "Resv", "Owner"};
    return candidate;
}

void add_state(
    mms::MmsLiveDiscoveryResult& result,
    const mms::MmsReportControlCandidate& candidate,
    std::string data_set_reference,
    const std::optional<bool> report_enabled,
    const std::optional<bool> reserved = std::nullopt,
    const std::optional<std::uint64_t> reservation_time_seconds = std::nullopt,
    const bool fail_data_set_attribute = false,
    std::vector<std::uint8_t> owner = {}) {
    mms::MmsReportControlEvidence evidence;
    evidence.candidate = candidate;
    evidence.requested_attributes = candidate.buffered
        ? std::vector<std::string>{"DatSet", "RptID", "ConfRev", "RptEna", "ResvTms", "Owner"}
        : std::vector<std::string>{"DatSet", "RptID", "ConfRev", "RptEna", "Resv", "Owner"};

    mms::MmsReportControlState state;
    state.candidate = candidate;
    state.data_set_reference = std::move(data_set_reference);
    state.report_id = "RPT-" + candidate.name;
    state.configuration_revision = 7U;
    state.report_enabled = report_enabled;
    state.reserved = reserved;
    state.reservation_time_seconds = reservation_time_seconds;
    state.owner = std::move(owner);
    if (fail_data_set_attribute) {
        state.diagnostics.push_back("DatSet read failed (3)");
    }
    evidence.state = std::move(state);
    result.report_controls.push_back(std::move(evidence));
}

void add_populated_directory(
    mms::MmsLiveDiscoveryResult& result,
    const std::string& reference) {
    mms::MmsDataSetDirectoryEvidence evidence;
    evidence.candidate.domain = "IEDLD0";
    evidence.candidate.logical_node = "LLN0";
    evidence.candidate.name = "DataSetA";
    evidence.candidate.reference = reference;

    mms::MmsDataSetDirectoryResponse directory;
    directory.invoke_id = 1U;
    directory.deletable = false;
    mms::MmsDataSetDirectoryMember member;
    member.object_name = mms::MmsObjectName::domain_specific(
        "IEDLD0", "LLN0$ST$Mod$stVal");
    member.mms_reference = "IEDLD0/LLN0$ST$Mod$stVal";
    member.user_reference = "IEDLD0/LLN0.Mod.stVal";
    member.functional_constraint = "ST";
    member.logical_node = "LLN0";
    member.data_object_path = "Mod.stVal";
    member.confidence = 100U;
    directory.members.push_back(std::move(member));
    evidence.directory = std::move(directory);
    result.data_set_directories.push_back(std::move(evidence));
}

void add_empty_directory(
    mms::MmsLiveDiscoveryResult& result,
    const std::string& reference) {
    mms::MmsDataSetDirectoryEvidence evidence;
    evidence.candidate.reference = reference;
    mms::MmsDataSetDirectoryResponse directory;
    directory.invoke_id = 1U;
    directory.deletable = false;
    evidence.directory = std::move(directory);
    result.data_set_directories.push_back(std::move(evidence));
}

void add_failed_directory(
    mms::MmsLiveDiscoveryResult& result,
    const std::string& reference,
    std::string error) {
    mms::MmsDataSetDirectoryEvidence evidence;
    evidence.candidate.reference = reference;
    evidence.error = std::move(error);
    result.data_set_directories.push_back(std::move(evidence));
}

[[nodiscard]] const mms::MmsRcbCandidateEvaluation& find_evaluation(
    const mms::MmsRcbSelectionEvidence& selection,
    const std::string_view name) {
    const auto found = std::find_if(
        selection.candidates.begin(), selection.candidates.end(),
        [name](const auto& candidate) { return candidate.name == name; });
    if (found == selection.candidates.end()) {
        throw std::runtime_error("Expected RCB evaluation was not found.");
    }
    return *found;
}

[[nodiscard]] const mms::MmsRcbOperationalAvailabilitySnapshot& find_snapshot(
    const mms::MmsRcbOperationalAvailabilityResult& result,
    const std::string_view name) {
    const auto found = std::find_if(
        result.report_controls.begin(), result.report_controls.end(),
        [name](const auto& snapshot) { return snapshot.name == name; });
    if (found == result.report_controls.end()) {
        throw std::runtime_error("Expected RCB availability snapshot was not found.");
    }
    return *found;
}

void dynamic_empty_slot_is_selected() {
    mms::MmsLiveDiscoveryResult result;
    const auto empty = make_rcb("brcbEmpty");
    result.report_inventory.report_controls = {empty};
    add_state(result, empty, {}, false, std::nullopt, 0U);

    const auto selection = mms::MmsRcbPoolSelector::build_dynamic_selection(result);
    CHECK(selection.selected());
    CHECK(selection.selected_rcb_reference == empty.reference);
    CHECK(selection.mode == mms::MmsRcbSelectionMode::dynamic_data_set);
    CHECK(selection.blockers.empty());
    const auto& evaluation = find_evaluation(selection, "brcbEmpty");
    CHECK(evaluation.availability ==
          mms::MmsRcbAvailabilityKind::available_dynamic_empty);
    CHECK(evaluation.decision == mms::MmsRcbSelectionDecision::selected);
    CHECK(evaluation.data_set_reference.empty());
    CHECK(evaluation.report_enabled == "false");
    CHECK(mms::MmsRcbPoolSelector::select_report_control(result, selection) ==
          &result.report_inventory.report_controls.front());
}

void dynamic_busy_and_bound_slots_are_not_selected() {
    mms::MmsLiveDiscoveryResult result;
    const auto enabled = make_rcb("brcbEnabled");
    const auto reserved = make_rcb("brcbReserved");
    const auto bound = make_rcb("brcbBound");
    const auto free = make_rcb("brcbFree");
    result.report_inventory.report_controls = {enabled, reserved, bound, free};

    add_state(result, enabled, {}, true, std::nullopt, 0U);
    add_state(result, reserved, {}, false, std::nullopt, 30U);
    add_state(result, bound, "IEDLD0/LLN0$DataSetA", false, std::nullopt, 0U);
    add_state(result, free, {}, false, std::nullopt, 0U);

    const auto selection = mms::MmsRcbPoolSelector::build_dynamic_selection(result);
    CHECK(selection.selected_rcb_reference == free.reference);
    CHECK(find_evaluation(selection, "brcbEnabled").availability ==
          mms::MmsRcbAvailabilityKind::busy_enabled);
    CHECK(find_evaluation(selection, "brcbReserved").availability ==
          mms::MmsRcbAvailabilityKind::busy_reserved);
    CHECK(find_evaluation(selection, "brcbBound").availability ==
          mms::MmsRcbAvailabilityKind::not_applicable);
    CHECK(find_evaluation(selection, "brcbFree").availability ==
          mms::MmsRcbAvailabilityKind::available_dynamic_empty);
}

void unprobed_or_failed_slot_needs_probe() {
    mms::MmsLiveDiscoveryResult result;
    const auto unprobed = make_rcb("brcbUnprobed");
    const auto failed = make_rcb("brcbFailed");
    result.report_inventory.report_controls = {unprobed, failed};
    add_state(result, failed, {}, false, std::nullopt, 0U, true);

    const auto selection = mms::MmsRcbPoolSelector::build_dynamic_selection(result);
    CHECK(!selection.selected());
    CHECK(!selection.blockers.empty());
    CHECK(find_evaluation(selection, "brcbUnprobed").availability ==
          mms::MmsRcbAvailabilityKind::unknown_needs_probe);
    CHECK(find_evaluation(selection, "brcbFailed").availability ==
          mms::MmsRcbAvailabilityKind::unknown_needs_probe);
}

void preferred_busy_slot_uses_smart_fallback() {
    mms::MmsLiveDiscoveryResult result;
    const auto preferred = make_rcb("brcbPreferred");
    const auto fallback = make_rcb("brcbFallback");
    result.report_inventory.report_controls = {preferred, fallback};
    add_state(result, preferred, {}, true, std::nullopt, 0U);
    add_state(result, fallback, {}, false, std::nullopt, 0U);

    mms::MmsRcbDynamicSelectionOptions options;
    options.preferred_rcb_reference = preferred.reference;
    const auto selection = mms::MmsRcbPoolSelector::build_dynamic_selection(
        result, options);
    CHECK(selection.selected_rcb_reference == fallback.reference);
    CHECK(selection.fallback_used);
    CHECK(!selection.warnings.empty());
}

void strict_preferred_busy_slot_blocks_selection() {
    mms::MmsLiveDiscoveryResult result;
    const auto preferred = make_rcb("brcbPreferred");
    const auto otherwise_free = make_rcb("brcbOther");
    result.report_inventory.report_controls = {preferred, otherwise_free};
    add_state(result, preferred, {}, true, std::nullopt, 0U);
    add_state(result, otherwise_free, {}, false, std::nullopt, 0U);

    mms::MmsRcbDynamicSelectionOptions options;
    options.preferred_rcb_reference = preferred.reference;
    options.strict_rcb = true;
    const auto selection = mms::MmsRcbPoolSelector::build_dynamic_selection(
        result, options);
    CHECK(!selection.selected());
    CHECK(!selection.blockers.empty());
    CHECK(find_evaluation(selection, "brcbOther").decision ==
          mms::MmsRcbSelectionDecision::filtered_out);
}

void exclusion_and_urcb_policy_are_preserved() {
    mms::MmsLiveDiscoveryResult result;
    const auto first = make_rcb("brcbA");
    const auto second = make_rcb("brcbB");
    const auto urcb = make_rcb("urcbA", false);
    result.report_inventory.report_controls = {first, second, urcb};
    add_state(result, first, {}, false, std::nullopt, 0U);
    add_state(result, second, {}, false, std::nullopt, 0U);
    add_state(result, urcb, {}, false, false, std::nullopt);

    mms::MmsRcbDynamicSelectionOptions options;
    options.excluded_rcb_references = {first.reference};
    options.allow_urcb_fallback = false;
    const auto selection = mms::MmsRcbPoolSelector::build_dynamic_selection(
        result, options);
    CHECK(selection.selected_rcb_reference == second.reference);
    CHECK(find_evaluation(selection, "brcbA").decision ==
          mms::MmsRcbSelectionDecision::filtered_out);
    CHECK(find_evaluation(selection, "urcbA").decision ==
          mms::MmsRcbSelectionDecision::filtered_out);
}

void static_selection_requires_populated_directory() {
    mms::MmsLiveDiscoveryResult result;
    const auto rcb = make_rcb("brcbStatic");
    result.report_inventory.report_controls = {rcb};
    add_state(
        result, rcb, "IEDLD0/LLN0$DataSetA", false,
        std::nullopt, 0U);

    const auto without_directory =
        mms::MmsRcbPoolSelector::build_static_selection(result);
    CHECK(!without_directory.selected());
    CHECK(find_evaluation(without_directory, "brcbStatic").availability ==
          mms::MmsRcbAvailabilityKind::available_static);
    CHECK(!find_evaluation(without_directory, "brcbStatic").has_data_set_directory);

    add_populated_directory(result, "IEDLD0/LLN0.DataSetA");
    const auto with_directory =
        mms::MmsRcbPoolSelector::build_static_selection(result);
    CHECK(with_directory.selected_rcb_reference == rcb.reference);
    CHECK(find_evaluation(with_directory, "brcbStatic").has_data_set_directory);
}

void requested_logical_device_affects_dynamic_ranking() {
    mms::MmsLiveDiscoveryResult result;
    const auto other = make_rcb("brcbA", true, "OTHERLD");
    const auto requested = make_rcb("brcbB", true, "IEDLD0");
    result.report_inventory.report_controls = {other, requested};
    add_state(result, other, {}, false, std::nullopt, 0U);
    add_state(result, requested, {}, false, std::nullopt, 0U);

    mms::MmsRcbDynamicSelectionOptions options;
    options.preferred_logical_device = "IEDLD0";
    const auto selection = mms::MmsRcbPoolSelector::build_dynamic_selection(
        result, options);
    CHECK(selection.selected_rcb_reference == requested.reference);
    CHECK(find_evaluation(selection, "brcbB").same_logical_device);
    CHECK(!find_evaluation(selection, "brcbA").same_logical_device);
}

void operational_brcb_requires_explicit_reservation_evidence() {
    mms::MmsLiveDiscoveryResult uncertain;
    const auto brcb = make_rcb("brcbUncertain");
    uncertain.report_inventory.report_controls = {brcb};
    add_state(
        uncertain, brcb, "IEDLD0/LLN0$DataSetA", false,
        std::nullopt, std::nullopt);
    add_populated_directory(uncertain, "IEDLD0/LLN0.DataSetA");

    const auto uncertain_result =
        mms::MmsRcbOperationalAvailabilityEvaluator::evaluate(uncertain);
    const auto& uncertain_snapshot =
        find_snapshot(uncertain_result, "brcbUncertain");
    CHECK(uncertain_snapshot.availability ==
          mms::MmsRcbOperationalAvailability::unknown);
    CHECK(uncertain_snapshot.confidence ==
          mms::MmsRcbOperationalAvailabilityConfidence::reduced);

    mms::MmsLiveDiscoveryResult free;
    const auto free_brcb = make_rcb("brcbFree");
    free.report_inventory.report_controls = {free_brcb};
    add_state(
        free, free_brcb, "IEDLD0/LLN0$DataSetA", false,
        std::nullopt, 0U);
    add_populated_directory(free, "IEDLD0/LLN0.DataSetA");

    const auto free_result =
        mms::MmsRcbOperationalAvailabilityEvaluator::evaluate(free);
    const auto& free_snapshot = find_snapshot(free_result, "brcbFree");
    CHECK(free_snapshot.availability ==
          mms::MmsRcbOperationalAvailability::available);
    CHECK(free_snapshot.confidence ==
          mms::MmsRcbOperationalAvailabilityConfidence::exact);
}

void operational_busy_and_owner_evidence_are_conservative() {
    mms::MmsLiveDiscoveryResult result;
    const auto timed = make_rcb("brcbTimed");
    const auto owner = make_rcb("brcbOwner");
    const auto zero_owner = make_rcb("brcbZeroOwner");
    result.report_inventory.report_controls = {timed, owner, zero_owner};
    add_state(
        result, timed, "IEDLD0/LLN0$DataSetA", false,
        std::nullopt, 30U);
    add_state(
        result, owner, "IEDLD0/LLN0$DataSetA", false,
        std::nullopt, std::nullopt, false, {0U, 0U, 1U});
    add_state(
        result, zero_owner, "IEDLD0/LLN0$DataSetA", false,
        std::nullopt, 0U, false, {0U, 0U, 0U});
    add_populated_directory(result, "IEDLD0/LLN0.DataSetA");

    const auto availability =
        mms::MmsRcbOperationalAvailabilityEvaluator::evaluate(result);
    CHECK(find_snapshot(availability, "brcbTimed").availability ==
          mms::MmsRcbOperationalAvailability::in_use);
    CHECK(find_snapshot(availability, "brcbTimed").confidence ==
          mms::MmsRcbOperationalAvailabilityConfidence::exact);
    CHECK(find_snapshot(availability, "brcbOwner").availability ==
          mms::MmsRcbOperationalAvailability::in_use);
    CHECK(!find_snapshot(availability, "brcbOwner").owner.empty());
    CHECK(find_snapshot(availability, "brcbZeroOwner").availability ==
          mms::MmsRcbOperationalAvailability::available);
    CHECK(find_snapshot(availability, "brcbZeroOwner").owner.empty());
}

void operational_urcb_and_caller_owned_order_matches_csharp() {
    mms::MmsLiveDiscoveryResult result;
    const auto enabled_empty = make_rcb("urcbEnabledEmpty", false);
    const auto disabled_empty = make_rcb("urcbDisabledEmpty", false);
    const auto unknown_resv = make_rcb("urcbUnknownResv", false);
    const auto caller = make_rcb("brcbCaller");
    result.report_inventory.report_controls = {
        enabled_empty, disabled_empty, unknown_resv, caller};

    add_state(result, enabled_empty, {}, true, std::nullopt, std::nullopt);
    add_state(result, disabled_empty, {}, false, false, std::nullopt);
    add_state(
        result, unknown_resv, "IEDLD0/LLN0$DataSetA", false,
        std::nullopt, std::nullopt);
    add_state(result, caller, {}, true, std::nullopt, 60U);
    add_populated_directory(result, "IEDLD0/LLN0.DataSetA");

    mms::MmsRcbOperationalAvailabilityOptions options;
    options.caller_owned_rcb_references = {caller.reference};
    const auto availability =
        mms::MmsRcbOperationalAvailabilityEvaluator::evaluate(result, options);

    CHECK(find_snapshot(availability, "urcbEnabledEmpty").availability ==
          mms::MmsRcbOperationalAvailability::in_use);
    CHECK(find_snapshot(availability, "urcbDisabledEmpty").availability ==
          mms::MmsRcbOperationalAvailability::no_data_set);
    CHECK(find_snapshot(availability, "urcbDisabledEmpty").confidence ==
          mms::MmsRcbOperationalAvailabilityConfidence::exact);
    CHECK(find_snapshot(availability, "urcbUnknownResv").availability ==
          mms::MmsRcbOperationalAvailability::unknown);
    CHECK(find_snapshot(availability, "urcbUnknownResv").confidence ==
          mms::MmsRcbOperationalAvailabilityConfidence::reduced);
    CHECK(find_snapshot(availability, "brcbCaller").availability ==
          mms::MmsRcbOperationalAvailability::used_by_caller);
}

void operational_dataset_failure_and_empty_are_preserved() {
    {
        mms::MmsLiveDiscoveryResult result;
        const auto rcb = make_rcb("brcbUnreadable");
        result.report_inventory.report_controls = {rcb};
        add_state(
            result, rcb, "IEDLD0/LLN0$DataSetA", false,
            std::nullopt, 0U);
        add_failed_directory(
            result, "IEDLD0/LLN0.DataSetA", "object-access-denied");

        const auto availability =
            mms::MmsRcbOperationalAvailabilityEvaluator::evaluate(result);
        CHECK(find_snapshot(availability, "brcbUnreadable").availability ==
              mms::MmsRcbOperationalAvailability::data_set_unreadable);
        CHECK(find_snapshot(availability, "brcbUnreadable").confidence ==
              mms::MmsRcbOperationalAvailabilityConfidence::exact);
    }

    {
        mms::MmsLiveDiscoveryResult result;
        const auto rcb = make_rcb("brcbEmptyDataSet");
        result.report_inventory.report_controls = {rcb};
        add_state(
            result, rcb, "IEDLD0/LLN0$DataSetA", false,
            std::nullopt, 0U);
        add_empty_directory(result, "IEDLD0/LLN0.DataSetA");

        const auto availability =
            mms::MmsRcbOperationalAvailabilityEvaluator::evaluate(result);
        CHECK(find_snapshot(availability, "brcbEmptyDataSet").availability ==
              mms::MmsRcbOperationalAvailability::data_set_empty);
        CHECK(find_snapshot(availability, "brcbEmptyDataSet").confidence ==
              mms::MmsRcbOperationalAvailabilityConfidence::exact);
    }
}

} // namespace

int main() {
    try {
        dynamic_empty_slot_is_selected();
        dynamic_busy_and_bound_slots_are_not_selected();
        unprobed_or_failed_slot_needs_probe();
        preferred_busy_slot_uses_smart_fallback();
        strict_preferred_busy_slot_blocks_selection();
        exclusion_and_urcb_policy_are_preserved();
        static_selection_requires_populated_directory();
        requested_logical_device_affects_dynamic_ranking();
        operational_brcb_requires_explicit_reservation_evidence();
        operational_busy_and_owner_evidence_are_conservative();
        operational_urcb_and_caller_owned_order_matches_csharp();
        operational_dataset_failure_and_empty_are_preserved();
        std::cout << "Smart RCB selection and availability parity tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
