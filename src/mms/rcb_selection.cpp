// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/rcb_selection.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>

namespace ar::iec61850::mms {
namespace {

[[nodiscard]] std::string trim(std::string value) {
    const auto not_space = [](const unsigned char character) {
        return std::isspace(character) == 0;
    };
    const auto first = std::find_if(value.begin(), value.end(), [&](const char character) {
        return not_space(static_cast<unsigned char>(character));
    });
    const auto last = std::find_if(value.rbegin(), value.rend(), [&](const char character) {
        return not_space(static_cast<unsigned char>(character));
    }).base();
    if (first >= last) {
        return {};
    }
    return std::string{first, last};
}

[[nodiscard]] std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const char character) {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    });
    return value;
}

[[nodiscard]] std::string normalize_reference(std::string value) {
    value = trim(std::move(value));
    std::replace(value.begin(), value.end(), '$', '.');
    return value;
}

[[nodiscard]] bool same_reference(
    const std::string_view left,
    const std::string_view right) {
    return lower_ascii(normalize_reference(std::string{left})) ==
           lower_ascii(normalize_reference(std::string{right}));
}

[[nodiscard]] bool same_text(
    const std::string_view left,
    const std::string_view right) {
    return lower_ascii(trim(std::string{left})) ==
           lower_ascii(trim(std::string{right}));
}

[[nodiscard]] std::string bool_text(const std::optional<bool> value) {
    if (!value) {
        return {};
    }
    return *value ? "true" : "false";
}

[[nodiscard]] std::string unsigned_text(
    const std::optional<std::uint64_t> value) {
    return value ? std::to_string(*value) : std::string{};
}

[[nodiscard]] std::string text_or_dash(const std::string_view value) {
    const auto text = trim(std::string{value});
    return text.empty() ? std::string{"-"} : text;
}

[[nodiscard]] bool contains_ci(
    const std::string_view value,
    const std::string_view needle) {
    return lower_ascii(std::string{value}).find(lower_ascii(std::string{needle})) !=
           std::string::npos;
}

struct RuntimeCandidate final {
    const MmsReportControlCandidate* candidate{};
    const MmsReportControlEvidence* evidence{};
    const MmsReportControlState* state{};
    bool was_probed{};
    std::string data_set_reference;
    std::string report_id;
    std::string configuration_revision;
    std::optional<bool> report_enabled;
    std::optional<bool> reserved;
    std::optional<std::uint64_t> reservation_time_seconds;
};

[[nodiscard]] const MmsReportControlEvidence* find_evidence(
    const MmsLiveDiscoveryResult& discovery,
    const MmsReportControlCandidate& candidate) {
    const auto found = std::find_if(
        discovery.report_controls.begin(),
        discovery.report_controls.end(),
        [&candidate](const auto& evidence) {
            return same_reference(
                evidence.candidate.reference,
                candidate.reference);
        });
    return found == discovery.report_controls.end() ? nullptr : &*found;
}

[[nodiscard]] bool critical_probe_attribute_failed(
    const MmsReportControlEvidence& evidence) {
    if (!evidence.state) {
        return true;
    }
    return std::any_of(
        evidence.state->diagnostics.begin(),
        evidence.state->diagnostics.end(),
        [](const auto& diagnostic) {
            return (contains_ci(diagnostic, "DatSet") ||
                    contains_ci(diagnostic, "RptEna")) &&
                   contains_ci(diagnostic, "failed");
        });
}

[[nodiscard]] RuntimeCandidate project_candidate(
    const MmsLiveDiscoveryResult& discovery,
    const MmsReportControlCandidate& candidate) {
    RuntimeCandidate runtime;
    runtime.candidate = &candidate;
    runtime.evidence = find_evidence(discovery, candidate);
    if (runtime.evidence == nullptr || !runtime.evidence->state) {
        return runtime;
    }

    runtime.state = &*runtime.evidence->state;
    runtime.was_probed = !critical_probe_attribute_failed(*runtime.evidence);
    runtime.data_set_reference = normalize_reference(runtime.state->data_set_reference);
    runtime.report_id = runtime.state->report_id;
    runtime.configuration_revision = unsigned_text(
        runtime.state->configuration_revision);
    runtime.report_enabled = runtime.state->report_enabled;
    runtime.reserved = runtime.state->reserved;
    runtime.reservation_time_seconds = runtime.state->reservation_time_seconds;
    return runtime;
}

[[nodiscard]] bool explicitly_enabled(const RuntimeCandidate& candidate) {
    return candidate.report_enabled.value_or(false);
}

[[nodiscard]] bool explicitly_disabled(const RuntimeCandidate& candidate) {
    return candidate.report_enabled.has_value() && !*candidate.report_enabled;
}

[[nodiscard]] bool reserved_by_other_client(const RuntimeCandidate& candidate) {
    return candidate.reserved.value_or(false) ||
           candidate.reservation_time_seconds.value_or(0U) > 0U;
}

// Intentionally mirrors ARIEC61850 MmsRcbPoolSelector.IsReservationFree:
// absence of a positive busy indication is enough for the score bonus.  The
// actual selectable classification is still controlled by successful probe
// evidence and RptEna state; this function never performs a claim.
[[nodiscard]] bool reservation_free_for_score(const RuntimeCandidate& candidate) {
    return !candidate.reserved.value_or(false) &&
           candidate.reservation_time_seconds.value_or(0U) == 0U;
}

[[nodiscard]] MmsRcbAvailabilityKind classify(
    const RuntimeCandidate& candidate,
    const bool require_empty_data_set,
    const bool require_data_set) {
    if (explicitly_enabled(candidate)) {
        return MmsRcbAvailabilityKind::busy_enabled;
    }
    if (reserved_by_other_client(candidate)) {
        return MmsRcbAvailabilityKind::busy_reserved;
    }

    const bool has_data_set = !candidate.data_set_reference.empty();
    const bool rpt_ena_not_true = !candidate.report_enabled.has_value() ||
                                  !*candidate.report_enabled;

    // Preserve the C# oracle ordering. A successfully populated probe snapshot
    // is sufficient for the selector to classify a free static/dynamic slot;
    // explicit positive busy evidence always wins above.
    if (require_data_set && has_data_set && rpt_ena_not_true &&
        candidate.was_probed) {
        return MmsRcbAvailabilityKind::available_static;
    }
    if (require_empty_data_set && !has_data_set && rpt_ena_not_true &&
        candidate.was_probed) {
        return MmsRcbAvailabilityKind::available_dynamic_empty;
    }
    if (!candidate.was_probed || !candidate.report_enabled.has_value()) {
        return MmsRcbAvailabilityKind::unknown_needs_probe;
    }
    if (require_data_set && !has_data_set) {
        return MmsRcbAvailabilityKind::not_applicable;
    }
    if (require_empty_data_set && has_data_set) {
        return MmsRcbAvailabilityKind::not_applicable;
    }
    return MmsRcbAvailabilityKind::not_usable;
}

[[nodiscard]] std::set<std::string, std::less<>> build_excluded_set(
    const std::vector<std::string>& references) {
    std::set<std::string, std::less<>> result;
    for (const auto& reference : references) {
        auto normalized = lower_ascii(normalize_reference(reference));
        if (!normalized.empty()) {
            result.insert(std::move(normalized));
        }
    }
    return result;
}

[[nodiscard]] bool excluded(
    const std::set<std::string, std::less<>>& references,
    const std::string_view reference) {
    return references.contains(
        lower_ascii(normalize_reference(std::string{reference})));
}

[[nodiscard]] bool has_populated_data_set_directory(
    const MmsLiveDiscoveryResult& discovery,
    const std::string_view data_set_reference) {
    if (data_set_reference.empty()) {
        return false;
    }
    const auto found = std::find_if(
        discovery.data_set_directories.begin(),
        discovery.data_set_directories.end(),
        [data_set_reference](const auto& evidence) {
            return same_reference(
                evidence.candidate.reference,
                data_set_reference);
        });
    return found != discovery.data_set_directories.end() && found->success() &&
           !found->directory->members.empty();
}

[[nodiscard]] std::string build_reason(
    const RuntimeCandidate& runtime,
    const MmsRcbAvailabilityKind availability,
    const bool has_data_set_directory,
    const bool requested_matches,
    const bool strict_rcb,
    const bool filtered_out,
    const bool excluded_by_previous_claim,
    const bool static_mode) {
    const auto& rcb = *runtime.candidate;
    if (excluded_by_previous_claim) {
        return "Excluded after a previous claim/write failure or pre-claim contention/cooldown in this command; trying the next Smart RCB candidate.";
    }
    if (filtered_out) {
        return strict_rcb
            ? "Filtered out by strict preferred RCB policy."
            : "Filtered out by user policy/filter.";
    }

    switch (availability) {
    case MmsRcbAvailabilityKind::available_static:
        return has_data_set_directory
            ? "Static RCB has DatSet, RptEna=false, no active reservation, and DataSet directory is usable."
            : "Static RCB is free, but DataSet directory is missing/empty; value mapping would be unsafe.";
    case MmsRcbAvailabilityKind::available_dynamic_empty:
        return "Dynamic slot has empty DatSet, RptEna=false, and no active reservation.";
    case MmsRcbAvailabilityKind::busy_enabled:
        return "RptEna=true; another client or previous session appears to own this RCB. Do not disable automatically.";
    case MmsRcbAvailabilityKind::busy_reserved:
        return rcb.buffered
            ? "BRCB ResvTms=" + text_or_dash(unsigned_text(runtime.reservation_time_seconds)) +
                  " before claim; treat as reserved/busy."
            : "URCB Resv=" + text_or_dash(bool_text(runtime.reserved)) +
                  " before claim; treat as reserved/busy.";
    case MmsRcbAvailabilityKind::contended_flapping:
        return "RCB state flips across probes; treat as contended/flapping and do not claim automatically.";
    case MmsRcbAvailabilityKind::claim_cooldown:
        return "RCB is in command-local claim cooldown after contention/write rejection; do not claim automatically.";
    case MmsRcbAvailabilityKind::unknown_needs_probe:
        return "RCB runtime state is not explicit; probe attributes before selecting automatically.";
    case MmsRcbAvailabilityKind::not_applicable:
        return static_mode
            ? "RCB has no DatSet, so it is not a static report candidate."
            : "RCB already has a DatSet, so it is not an empty dynamic slot.";
    case MmsRcbAvailabilityKind::not_usable:
        return requested_matches
            ? "RCB state is incomplete or not safe for automatic claim."
            : "RCB does not match the requested scope.";
    }
    return "RCB state is incomplete or not safe for automatic claim.";
}

[[nodiscard]] MmsRcbCandidateEvaluation create_evaluation(
    const RuntimeCandidate& runtime,
    const bool preferred,
    const bool same_data_set,
    const bool same_logical_device,
    const bool has_data_set_directory,
    const int score,
    const MmsRcbAvailabilityKind availability,
    const MmsRcbSelectionDecision decision,
    std::string reason,
    std::string action) {
    const auto& candidate = *runtime.candidate;
    MmsRcbCandidateEvaluation result;
    result.reference = candidate.reference;
    result.mode = candidate.mode();
    result.domain = candidate.domain;
    result.logical_node = candidate.logical_node;
    result.name = candidate.name;
    result.data_set_reference = runtime.data_set_reference;
    result.report_id = runtime.report_id;
    result.configuration_revision = runtime.configuration_revision;
    result.report_enabled = bool_text(runtime.report_enabled);
    result.reservation_state = bool_text(runtime.reserved);
    result.reservation_time_seconds = unsigned_text(
        runtime.reservation_time_seconds);
    result.buffered = candidate.buffered;
    result.preferred = preferred;
    result.same_data_set = same_data_set;
    result.same_logical_device = same_logical_device;
    result.has_data_set_directory = has_data_set_directory;
    result.score = score;
    result.availability = availability;
    result.decision = decision;
    result.reason = std::move(reason);
    result.recommended_action = std::move(action);
    return result;
}

void finalize_decisions(
    std::vector<MmsRcbCandidateEvaluation>& evaluations,
    const std::string& selected_reference) {
    if (selected_reference.empty()) {
        return;
    }
    for (auto& evaluation : evaluations) {
        if (same_reference(evaluation.reference, selected_reference)) {
            evaluation.decision = MmsRcbSelectionDecision::selected;
            evaluation.recommended_action = "Selected by Smart RCB policy.";
        } else if (evaluation.decision != MmsRcbSelectionDecision::filtered_out) {
            evaluation.decision = evaluation.selectable()
                ? MmsRcbSelectionDecision::candidate
                : MmsRcbSelectionDecision::skipped;
        }
    }
}

[[nodiscard]] std::string first_selectable_reference(
    const std::vector<MmsRcbCandidateEvaluation>& evaluations) {
    const auto found = std::find_if(
        evaluations.begin(), evaluations.end(),
        [](const auto& evaluation) { return evaluation.selectable(); });
    return found == evaluations.end() ? std::string{} : found->reference;
}

} // namespace

std::string_view mms_rcb_selection_mode_name(
    const MmsRcbSelectionMode value) noexcept {
    switch (value) {
    case MmsRcbSelectionMode::static_data_set: return "StaticDataSet";
    case MmsRcbSelectionMode::dynamic_data_set: return "DynamicDataSet";
    }
    return "Unknown";
}

std::string_view mms_rcb_availability_kind_name(
    const MmsRcbAvailabilityKind value) noexcept {
    switch (value) {
    case MmsRcbAvailabilityKind::available_static: return "AvailableStatic";
    case MmsRcbAvailabilityKind::available_dynamic_empty: return "AvailableDynamicEmpty";
    case MmsRcbAvailabilityKind::busy_enabled: return "BusyEnabled";
    case MmsRcbAvailabilityKind::busy_reserved: return "BusyReserved";
    case MmsRcbAvailabilityKind::contended_flapping: return "ContendedFlapping";
    case MmsRcbAvailabilityKind::claim_cooldown: return "ClaimCooldown";
    case MmsRcbAvailabilityKind::unknown_needs_probe: return "UnknownNeedsProbe";
    case MmsRcbAvailabilityKind::not_applicable: return "NotApplicable";
    case MmsRcbAvailabilityKind::not_usable: return "NotUsable";
    }
    return "Unknown";
}

std::string_view mms_rcb_selection_decision_name(
    const MmsRcbSelectionDecision value) noexcept {
    switch (value) {
    case MmsRcbSelectionDecision::selected: return "Selected";
    case MmsRcbSelectionDecision::candidate: return "Candidate";
    case MmsRcbSelectionDecision::skipped: return "Skipped";
    case MmsRcbSelectionDecision::blocked_preferred: return "BlockedPreferred";
    case MmsRcbSelectionDecision::filtered_out: return "FilteredOut";
    }
    return "Unknown";
}

std::string MmsRcbCandidateEvaluation::summary() const {
    return std::string{mms_rcb_selection_decision_name(decision)} + ' ' + mode +
           ' ' + reference + " score=" + std::to_string(score) +
           " availability=" + std::string{mms_rcb_availability_kind_name(availability)} +
           " reason=" + reason;
}

std::string MmsRcbSelectionEvidence::summary() const {
    const auto selected_reference = selected_rcb_reference.empty()
        ? std::string{"-"}
        : selected_rcb_reference;
    const auto preferred_reference = preferred_rcb_reference.empty()
        ? std::string{"-"}
        : preferred_rcb_reference;
    return "RCB selection: mode=" + std::string{mms_rcb_selection_mode_name(mode)} +
           ", selected=" + selected_reference +
           ", preferred=" + preferred_reference +
           ", strict=" + (strict_rcb ? "true" : "false") +
           ", fallbackUsed=" + (fallback_used ? "yes" : "no") +
           ", candidates=" + std::to_string(candidates.size());
}

MmsRcbSelectionEvidence MmsRcbPoolSelector::build_static_selection(
    const MmsLiveDiscoveryResult& discovery,
    const MmsRcbStaticSelectionOptions& options) {
    const auto excluded_references = build_excluded_set(
        options.excluded_rcb_references);
    const auto preferred_rcb = normalize_reference(
        options.preferred_rcb_reference);
    auto requested_data_set = normalize_reference(
        options.preferred_data_set_reference);

    if (requested_data_set.empty() && !preferred_rcb.empty()) {
        const auto found = std::find_if(
            discovery.report_inventory.report_controls.begin(),
            discovery.report_inventory.report_controls.end(),
            [&preferred_rcb](const auto& candidate) {
                return same_reference(candidate.reference, preferred_rcb);
            });
        if (found != discovery.report_inventory.report_controls.end()) {
            requested_data_set = project_candidate(discovery, *found).data_set_reference;
        }
    }

    std::vector<MmsRcbCandidateEvaluation> evaluations;
    evaluations.reserve(discovery.report_inventory.report_controls.size());
    for (const auto& candidate : discovery.report_inventory.report_controls) {
        const auto runtime = project_candidate(discovery, candidate);
        const bool is_preferred = !preferred_rcb.empty() &&
            same_reference(candidate.reference, preferred_rcb);
        const bool excluded_by_previous_claim = excluded(
            excluded_references, candidate.reference);
        const bool requested_matches = requested_data_set.empty() ||
            same_reference(runtime.data_set_reference, requested_data_set);
        const bool has_directory = has_populated_data_set_directory(
            discovery, runtime.data_set_reference);
        const auto availability = classify(runtime, false, true);

        bool filtered_out = options.strict_rcb && !preferred_rcb.empty() &&
                            !is_preferred;
        if (!options.allow_urcb_fallback && !candidate.buffered) {
            filtered_out = true;
        }
        if (!requested_matches || excluded_by_previous_claim) {
            filtered_out = true;
        }
        const bool selectable = !filtered_out &&
            availability == MmsRcbAvailabilityKind::available_static &&
            has_directory;

        int score = 0;
        if (is_preferred) score += 500;
        if (requested_matches) score += 120;
        if (has_directory) score += 100;
        if (candidate.buffered) score += 40;
        if (same_text(candidate.logical_node, "LLN0")) score += 15;
        if (explicitly_disabled(runtime)) score += 25;
        if (reservation_free_for_score(runtime)) score += 25;
        if (!runtime.configuration_revision.empty()) score += 10;
        if (!runtime.report_id.empty()) score += 10;
        if (availability == MmsRcbAvailabilityKind::busy_enabled ||
            availability == MmsRcbAvailabilityKind::busy_reserved) {
            score -= 1'000;
        }
        if (!has_directory) score -= 250;

        evaluations.push_back(create_evaluation(
            runtime,
            is_preferred,
            requested_matches,
            true,
            has_directory,
            score,
            availability,
            filtered_out
                ? MmsRcbSelectionDecision::filtered_out
                : selectable
                    ? MmsRcbSelectionDecision::candidate
                    : MmsRcbSelectionDecision::skipped,
            build_reason(
                runtime, availability, has_directory, requested_matches,
                options.strict_rcb, filtered_out,
                excluded_by_previous_claim, true),
            selectable
                ? "Safe static report candidate."
                : "Do not claim this RCB automatically."));
    }

    std::sort(evaluations.begin(), evaluations.end(), [](const auto& left, const auto& right) {
        return std::tuple{
                   left.decision == MmsRcbSelectionDecision::filtered_out ? 1 : 0,
                   left.selectable() ? 0 : 1,
                   -left.score,
                   left.buffered ? 0 : 1,
                   lower_ascii(left.domain),
                   same_text(left.logical_node, "LLN0") ? 0 : 1,
                   lower_ascii(left.logical_node),
                   lower_ascii(left.name)} <
               std::tuple{
                   right.decision == MmsRcbSelectionDecision::filtered_out ? 1 : 0,
                   right.selectable() ? 0 : 1,
                   -right.score,
                   right.buffered ? 0 : 1,
                   lower_ascii(right.domain),
                   same_text(right.logical_node, "LLN0") ? 0 : 1,
                   lower_ascii(right.logical_node),
                   lower_ascii(right.name)};
    });

    const auto selected_reference = first_selectable_reference(evaluations);
    finalize_decisions(evaluations, selected_reference);

    MmsRcbSelectionEvidence result;
    result.mode = MmsRcbSelectionMode::static_data_set;
    result.preferred_rcb_reference = options.preferred_rcb_reference;
    result.strict_rcb = options.strict_rcb;
    result.allow_urcb_fallback = options.allow_urcb_fallback;
    result.allow_polling_fallback = options.allow_polling_fallback;
    result.requested_data_set_reference = options.preferred_data_set_reference;
    result.selected_rcb_reference = selected_reference;
    result.fallback_used = !options.preferred_rcb_reference.empty() &&
        !selected_reference.empty() &&
        !same_reference(selected_reference, options.preferred_rcb_reference);
    result.candidates = std::move(evaluations);

    if (result.selected_rcb_reference.empty()) {
        result.blockers.push_back(
            options.strict_rcb && !options.preferred_rcb_reference.empty()
                ? "Strict RCB selection blocked the session because " +
                      options.preferred_rcb_reference +
                      " is not available for static reporting."
                : "No available static RCB matched the requested DataSet/filter.");
        if (options.allow_polling_fallback) {
            result.warnings.push_back(
                "No safe RCB was selected. Smart polling fallback is allowed by policy, but report monitor will remain blocked until polling fallback mode is implemented by the caller.");
        }
    } else if (!options.preferred_rcb_reference.empty() &&
               !same_reference(
                   result.selected_rcb_reference,
                   options.preferred_rcb_reference)) {
        result.warnings.push_back(
            "Preferred RCB " + options.preferred_rcb_reference +
            " was not selected; smart fallback selected " +
            result.selected_rcb_reference +
            " to avoid an unsafe/busy RCB.");
    }
    return result;
}

MmsRcbSelectionEvidence MmsRcbPoolSelector::build_dynamic_selection(
    const MmsLiveDiscoveryResult& discovery,
    const MmsRcbDynamicSelectionOptions& options) {
    const auto excluded_references = build_excluded_set(
        options.excluded_rcb_references);
    const auto preferred_rcb = normalize_reference(
        options.preferred_rcb_reference);
    const auto requested_logical_device = trim(
        options.preferred_logical_device);

    std::vector<MmsRcbCandidateEvaluation> evaluations;
    evaluations.reserve(discovery.report_inventory.report_controls.size());
    for (const auto& candidate : discovery.report_inventory.report_controls) {
        const auto runtime = project_candidate(discovery, candidate);
        const bool is_preferred = !preferred_rcb.empty() &&
            same_reference(candidate.reference, preferred_rcb);
        const bool excluded_by_previous_claim = excluded(
            excluded_references, candidate.reference);
        const bool same_logical_device = requested_logical_device.empty() ||
            same_text(candidate.domain, requested_logical_device);
        const auto availability = classify(runtime, true, false);

        bool filtered_out = options.strict_rcb && !preferred_rcb.empty() &&
                            !is_preferred;
        if (!options.allow_urcb_fallback && !candidate.buffered) {
            filtered_out = true;
        }
        if (excluded_by_previous_claim) {
            filtered_out = true;
        }
        const bool selectable = !filtered_out &&
            availability == MmsRcbAvailabilityKind::available_dynamic_empty;

        int score = 0;
        if (is_preferred) score += 500;
        if (runtime.data_set_reference.empty()) score += 140;
        if (same_logical_device) score += 100;
        if (candidate.buffered) score += 40;
        if (same_text(candidate.logical_node, "LLN0")) score += 15;
        if (explicitly_disabled(runtime)) score += 25;
        if (reservation_free_for_score(runtime)) score += 25;
        if (!runtime.configuration_revision.empty()) score += 10;
        if (availability == MmsRcbAvailabilityKind::busy_enabled ||
            availability == MmsRcbAvailabilityKind::busy_reserved) {
            score -= 1'000;
        }
        if (!runtime.data_set_reference.empty()) score -= 600;

        evaluations.push_back(create_evaluation(
            runtime,
            is_preferred,
            runtime.data_set_reference.empty(),
            same_logical_device,
            false,
            score,
            availability,
            filtered_out
                ? MmsRcbSelectionDecision::filtered_out
                : selectable
                    ? MmsRcbSelectionDecision::candidate
                    : MmsRcbSelectionDecision::skipped,
            build_reason(
                runtime, availability, false, same_logical_device,
                options.strict_rcb, filtered_out,
                excluded_by_previous_claim, false),
            selectable
                ? "Safe dynamic empty-slot candidate."
                : "Do not bind a dynamic DataSet to this RCB automatically."));
    }

    std::sort(evaluations.begin(), evaluations.end(), [](const auto& left, const auto& right) {
        return std::tuple{
                   left.decision == MmsRcbSelectionDecision::filtered_out ? 1 : 0,
                   left.selectable() ? 0 : 1,
                   -left.score,
                   left.buffered ? 0 : 1,
                   left.same_logical_device ? 0 : 1,
                   lower_ascii(left.domain),
                   same_text(left.logical_node, "LLN0") ? 0 : 1,
                   lower_ascii(left.logical_node),
                   lower_ascii(left.name)} <
               std::tuple{
                   right.decision == MmsRcbSelectionDecision::filtered_out ? 1 : 0,
                   right.selectable() ? 0 : 1,
                   -right.score,
                   right.buffered ? 0 : 1,
                   right.same_logical_device ? 0 : 1,
                   lower_ascii(right.domain),
                   same_text(right.logical_node, "LLN0") ? 0 : 1,
                   lower_ascii(right.logical_node),
                   lower_ascii(right.name)};
    });

    const auto selected_reference = first_selectable_reference(evaluations);
    finalize_decisions(evaluations, selected_reference);

    MmsRcbSelectionEvidence result;
    result.mode = MmsRcbSelectionMode::dynamic_data_set;
    result.preferred_rcb_reference = options.preferred_rcb_reference;
    result.strict_rcb = options.strict_rcb;
    result.allow_urcb_fallback = options.allow_urcb_fallback;
    result.allow_polling_fallback = options.allow_polling_fallback;
    result.requested_logical_device = options.preferred_logical_device;
    result.selected_rcb_reference = selected_reference;
    result.fallback_used = !options.preferred_rcb_reference.empty() &&
        !selected_reference.empty() &&
        !same_reference(selected_reference, options.preferred_rcb_reference);
    result.candidates = std::move(evaluations);

    if (result.selected_rcb_reference.empty()) {
        result.blockers.push_back(
            options.strict_rcb && !options.preferred_rcb_reference.empty()
                ? "Strict RCB selection blocked the session because " +
                      options.preferred_rcb_reference +
                      " is not an available empty dynamic slot."
                : "No available empty dynamic RCB slot matched the requested filter.");
        if (options.allow_polling_fallback) {
            result.warnings.push_back(
                "No dynamic RCB slot was selected. Smart polling fallback is allowed by policy, but dynamic reporting remains blocked until the caller chooses polling fallback.");
        }
    } else if (!options.preferred_rcb_reference.empty() &&
               !same_reference(
                   result.selected_rcb_reference,
                   options.preferred_rcb_reference)) {
        result.warnings.push_back(
            "Preferred RCB " + options.preferred_rcb_reference +
            " was not selected; smart fallback selected " +
            result.selected_rcb_reference +
            " to avoid an unsafe/busy RCB.");
    }
    return result;
}

const MmsReportControlCandidate* MmsRcbPoolSelector::select_report_control(
    const MmsLiveDiscoveryResult& discovery,
    const MmsRcbSelectionEvidence& selection) noexcept {
    if (selection.selected_rcb_reference.empty()) {
        return nullptr;
    }
    const auto found = std::find_if(
        discovery.report_inventory.report_controls.begin(),
        discovery.report_inventory.report_controls.end(),
        [&selection](const auto& candidate) {
            return same_reference(
                candidate.reference,
                selection.selected_rcb_reference);
        });
    return found == discovery.report_inventory.report_controls.end()
        ? nullptr
        : &*found;
}

} // namespace ar::iec61850::mms
