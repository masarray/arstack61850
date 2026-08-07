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
};

struct MmsLiveReportControl final {
    std::string reference;
    std::string domain;
    std::string logical_node;
    std::string name;
    bool buffered{};
    std::string data_set_reference;
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
