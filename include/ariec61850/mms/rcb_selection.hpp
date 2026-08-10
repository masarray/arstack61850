// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/live_discovery.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ar::iec61850::mms {

enum class MmsRcbSelectionMode : std::uint8_t {
    static_data_set,
    dynamic_data_set,
};

enum class MmsRcbAvailabilityKind : std::uint8_t {
    available_static,
    available_dynamic_empty,
    busy_enabled,
    busy_reserved,
    contended_flapping,
    claim_cooldown,
    unknown_needs_probe,
    not_applicable,
    not_usable,
};

enum class MmsRcbSelectionDecision : std::uint8_t {
    selected,
    candidate,
    skipped,
    blocked_preferred,
    filtered_out,
};

[[nodiscard]] std::string_view mms_rcb_selection_mode_name(
    MmsRcbSelectionMode value) noexcept;
[[nodiscard]] std::string_view mms_rcb_availability_kind_name(
    MmsRcbAvailabilityKind value) noexcept;
[[nodiscard]] std::string_view mms_rcb_selection_decision_name(
    MmsRcbSelectionDecision value) noexcept;

struct MmsRcbCandidateEvaluation final {
    std::string reference;
    std::string mode;
    std::string domain;
    std::string logical_node;
    std::string name;
    std::string data_set_reference;
    std::string report_id;
    std::string configuration_revision;
    std::string report_enabled;
    std::string reservation_state;
    std::string reservation_time_seconds;
    bool buffered{};
    bool preferred{};
    bool same_data_set{};
    bool same_logical_device{};
    bool has_data_set_directory{};
    int score{};
    MmsRcbAvailabilityKind availability{MmsRcbAvailabilityKind::unknown_needs_probe};
    MmsRcbSelectionDecision decision{MmsRcbSelectionDecision::skipped};
    std::string reason;
    std::string recommended_action;

    [[nodiscard]] bool selectable() const noexcept {
        return decision == MmsRcbSelectionDecision::candidate ||
               decision == MmsRcbSelectionDecision::selected;
    }
    [[nodiscard]] std::string summary() const;
};

struct MmsRcbSelectionEvidence final {
    MmsRcbSelectionMode mode{MmsRcbSelectionMode::static_data_set};
    std::string preferred_rcb_reference;
    bool strict_rcb{};
    bool allow_urcb_fallback{true};
    bool allow_polling_fallback{true};
    std::string requested_data_set_reference;
    std::string requested_logical_device;
    std::string selected_rcb_reference;
    bool fallback_used{};
    std::vector<MmsRcbCandidateEvaluation> candidates;
    std::vector<std::string> warnings;
    std::vector<std::string> blockers;

    [[nodiscard]] bool selected() const noexcept {
        return !selected_rcb_reference.empty();
    }
    [[nodiscard]] std::string summary() const;
};

struct MmsRcbStaticSelectionOptions final {
    std::string preferred_rcb_reference;
    std::string preferred_data_set_reference;
    bool strict_rcb{};
    bool allow_urcb_fallback{true};
    bool allow_polling_fallback{true};
    std::vector<std::string> excluded_rcb_references;
};

struct MmsRcbDynamicSelectionOptions final {
    std::string preferred_logical_device;
    std::string preferred_rcb_reference;
    bool strict_rcb{};
    bool allow_urcb_fallback{true};
    bool allow_polling_fallback{true};
    std::vector<std::string> excluded_rcb_references;
};

// Pure read-only planning layer ported from ARIEC61850 MmsRcbPoolSelector.
// It consumes already-collected discovery/Read evidence and never performs
// MMS Write, reservation, RptEna, GI, or dynamic DataSet mutation operations.
class MmsRcbPoolSelector final {
public:
    [[nodiscard]] static MmsRcbSelectionEvidence build_static_selection(
        const MmsLiveDiscoveryResult& discovery,
        const MmsRcbStaticSelectionOptions& options = {});

    [[nodiscard]] static MmsRcbSelectionEvidence build_dynamic_selection(
        const MmsLiveDiscoveryResult& discovery,
        const MmsRcbDynamicSelectionOptions& options = {});

    [[nodiscard]] static const MmsReportControlCandidate* select_report_control(
        const MmsLiveDiscoveryResult& discovery,
        const MmsRcbSelectionEvidence& selection) noexcept;
};

} // namespace ar::iec61850::mms
