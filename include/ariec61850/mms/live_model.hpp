// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/live_discovery.hpp"
#include "ariec61850/mms/model_reference.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <string_view>
#include <tuple>
#include <utility>

namespace ar::iec61850::mms {

enum class MmsLiveModelConfidence : std::uint8_t {
    exact,
    high,
    medium,
    low,
    unknown,
};

struct MmsLiveIedIdentity final {
    std::string ied_name;
    std::string source;
    MmsLiveModelConfidence confidence{MmsLiveModelConfidence::unknown};
    bool ambiguous{};
    std::vector<std::string> candidate_names;
    std::map<std::string, std::string, std::less<>> logical_device_aliases;
    std::vector<std::string> evidence;
};

struct MmsLiveDataAttribute final {
    std::string object_reference;
    std::string attribute_path;
    std::string functional_constraint;
    std::string mms_reference;
    std::string mms_item_name;
    std::string source;
    std::string scl_basic_type;
    std::string mms_type;
    std::string mms_type_signature;
    std::string type_discovery_status{"NotRead"};
    std::string type_discovery_message;
    std::string type_source{"NameListHeuristic"};
    MmsLiveModelConfidence type_confidence{MmsLiveModelConfidence::low};
    MmsLiveModelConfidence functional_constraint_confidence{
        MmsLiveModelConfidence::exact};
};

struct MmsLiveDataObject final {
    std::string reference;
    std::string name;
    std::string proposed_do_type_id;
    std::string inferred_cdc;
    double cdc_confidence{};
    MmsLiveModelConfidence confidence{MmsLiveModelConfidence::unknown};
    std::vector<std::string> evidence;
    std::vector<MmsLiveDataAttribute> attributes;
};

struct MmsLiveLogicalNode final {
    std::string name;
    std::string prefix;
    std::string logical_node_class;
    std::string instance;
    std::string proposed_type_id;
    std::map<std::string, std::size_t, std::less<>> functional_constraint_counts;
    std::vector<MmsLiveDataObject> data_objects;
};

struct MmsLiveLogicalDevice final {
    std::string mms_domain;
    std::string instance;
    std::vector<MmsLiveLogicalNode> logical_nodes;
};

struct MmsLiveDataSetMember final {
    std::size_t index{};
    std::string reference;
    std::string functional_constraint;
    std::string mms_reference;
    MmsLiveModelConfidence confidence{MmsLiveModelConfidence::exact};
};

struct MmsLiveDataSet final {
    std::string reference;
    std::string domain;
    std::string logical_node;
    std::string name;
    std::optional<bool> deletable;
    std::vector<MmsLiveDataSetMember> members;
    std::vector<std::string> used_by_report_controls;
    std::vector<std::string> used_by_goose_controls;
    std::vector<std::string> used_by_sampled_value_controls;
};

struct MmsLiveReportControl final {
    std::string reference;
    std::string domain;
    std::string logical_node;
    std::string name;
    bool buffered{};
    std::string data_set_reference;
    // Runtime observation only.  "Unbound" is a valid state for an empty
    // dynamic RCB slot and must not be treated as a missing/corrupt DataSet.
    // Values: NotRead, Bound, Unbound, ReadFailed.
    std::string data_set_binding_status{"NotRead"};
    std::string data_set_binding_message;
    std::string report_id;
    std::string configuration_revision;
    std::string trigger_options;
    std::string optional_fields;
    std::string buffer_time_ms;
    std::string integrity_period_ms;
    std::string enabled_state;
    std::string reservation_state;
    std::string reservation_time_seconds;
    std::string status;
};

// Read-only inventory projection matching ARIEC61850 LiveIedControlBlockModel.
// Values stay empty until a future, explicitly bounded deep value reader exists.
struct MmsLiveControlBlock final {
    std::string kind;
    std::string reference;
    std::string domain;
    std::string logical_node;
    std::string name;
    std::string functional_constraint;
    std::vector<std::string> attributes;
    std::string data_set_reference;
    std::string data_set_reference_status{"NotRead"};
    std::string control_id;
    std::string app_id;
    std::string smv_id;
    std::string configuration_revision;
    std::string minimum_time_ms;
    std::string maximum_time_ms;
    std::string sample_rate;
    std::string sample_mode;
    std::string number_of_asdu;
    std::string address_status{"NotDiscovered"};
    std::string discovery_status{"AttributeInventoryOnly"};
    std::string message;

    [[nodiscard]] std::size_t attribute_count() const noexcept {
        return attributes.size();
    }
};

// Pure model projections matching ARIEC61850 LiveIedTypeTemplateCandidate and
// LiveIedVariableTypeDiscoveryModel.  They never trigger additional MMS IO.
struct MmsLiveTypeTemplateCandidate final {
    std::string template_kind;
    std::string id;
    std::string source_reference;
    std::string inferred_type;
    double confidence{};
    std::vector<std::string> members;
};

struct MmsLiveVariableTypeDiscovery final {
    std::string reference;
    std::string domain;
    std::string mms_item_name;
    std::string functional_constraint;
    bool success{};
    std::string mms_type;
    std::string scl_basic_type;
    std::string type_signature;
    std::optional<bool> mms_deletable;
    std::string message;
    std::string source{"GetVariableAccessAttributes"};
};

struct MmsLiveModelCoverage final {
    std::size_t logical_device_count{};
    std::size_t logical_node_count{};
    std::size_t data_object_count{};
    std::size_t data_attribute_count{};
    std::size_t exact_functional_constraint_count{};
    std::size_t high_confidence_cdc_count{};
    std::size_t medium_confidence_cdc_count{};
    std::size_t low_confidence_cdc_count{};
    std::size_t unknown_cdc_count{};
    std::size_t data_set_count{};
    std::size_t report_control_count{};
    std::size_t buffered_report_control_count{};
    std::size_t unbuffered_report_control_count{};
    std::size_t report_control_bound_count{};
    std::size_t report_control_unbound_count{};
    std::size_t report_control_binding_not_read_count{};
    std::size_t report_control_binding_read_failed_count{};
    std::size_t goose_control_block_count{};
    std::size_t sampled_value_control_block_count{};
    std::size_t setting_group_control_count{};
    std::size_t log_control_count{};
    std::size_t variable_type_read_attempt_count{};
    std::size_t variable_type_read_success_count{};
    std::size_t variable_type_read_failure_count{};
    std::size_t exact_mms_type_count{};
};

struct MmsLiveModelWarning final {
    std::string code;
    std::string reference;
    std::string message;
};

struct MmsLiveModelDocument final {
    std::string schema_version{"live-ied-model-v1"};
    std::string source{"LiveMmsDiscovery"};
    MmsEndpoint endpoint;
    MmsLiveIedIdentity identity;
    std::string access_point_name{"AP1"};
    std::string summary;
    MmsLiveModelCoverage coverage;
    std::vector<MmsLiveLogicalDevice> logical_devices;
    std::vector<MmsLiveDataSet> data_sets;
    std::vector<MmsLiveReportControl> report_controls;
    std::vector<MmsLiveControlBlock> goose_control_blocks;
    std::vector<MmsLiveControlBlock> sampled_value_control_blocks;
    std::vector<MmsLiveControlBlock> setting_group_controls;
    std::vector<MmsLiveControlBlock> log_controls;
    std::vector<MmsLiveTypeTemplateCandidate> type_templates;
    std::vector<MmsLiveVariableTypeDiscovery> variable_type_discoveries;
    std::vector<MmsLiveModelWarning> warnings;

    [[nodiscard]] std::string canonical_manifest() const;
    [[nodiscard]] std::uint64_t canonical_fingerprint() const;
    [[nodiscard]] std::string canonical_fingerprint_hex() const;
    [[nodiscard]] std::string to_json() const;
};

struct MmsLiveModelBuildOptions final {
    std::string explicit_ied_name;
    std::string fallback_ied_name;
    std::string access_point_name{"AP1"};
    bool include_low_confidence_templates{true};
};

class MmsLiveModelBuilder final {
public:
    [[nodiscard]] static MmsLiveModelDocument build(
        const MmsLiveDiscoveryResult& discovery,
        const MmsLiveModelBuildOptions& options = {});
};

enum class MmsLiveModelFindingSeverity : std::uint8_t {
    information,
    warning,
    error,
};

enum class MmsLiveModelFindingKind : std::uint8_t {
    identity_mismatch,
    missing_attribute,
    unexpected_attribute,
    functional_constraint_mismatch,
    type_mismatch,
    missing_data_set,
    unexpected_data_set,
    data_set_member_count_mismatch,
    missing_report_control,
    unexpected_report_control,
    report_control_mode_mismatch,
};

struct MmsLiveModelFinding final {
    MmsLiveModelFindingSeverity severity{MmsLiveModelFindingSeverity::information};
    MmsLiveModelFindingKind kind{MmsLiveModelFindingKind::unexpected_attribute};
    std::string reference;
    std::string expected;
    std::string observed;
    std::string message;
};

struct MmsLiveModelParityResult final {
    std::string expected_ied_name;
    std::string observed_ied_name;
    std::size_t expected_attribute_count{};
    std::size_t observed_attribute_count{};
    std::size_t matched_attribute_count{};
    std::vector<MmsLiveModelFinding> findings;

    [[nodiscard]] std::size_t blocking_finding_count() const noexcept;
    [[nodiscard]] bool compatible() const noexcept {
        return blocking_finding_count() == 0U;
    }
};

class MmsLiveModelParityComparer final {
public:
    [[nodiscard]] static MmsLiveModelParityResult compare(
        const MmsLiveModelDocument& expected,
        const MmsLiveModelDocument& observed);
};

} // namespace ar::iec61850::mms

#include "ariec61850/mms/live_model.ipp"