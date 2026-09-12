// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/scl/model.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ar::iec61850::simulation {

enum class SimulatorPointKind : std::uint8_t {
    measurement,
    status,
    quality,
    timestamp,
};

struct IedSimulatorPoint final {
    std::string reference;
    std::string relative_reference;
    std::string ied_name;
    std::string logical_device;
    std::string logical_node;
    std::string ln_class;
    std::string data_object;
    std::string data_attribute;
    std::string functional_constraint;
    std::string cdc;
    std::string basic_type;
    std::string enum_type;
    std::string mms_domain;
    std::string mms_item;
    std::string display_type;
    std::string unit;
    std::string initial_value;
    double base_value{};
    double amplitude{};
    double phase_degrees{};
    std::size_t source_order{};
    bool dynamic{};
    SimulatorPointKind kind{SimulatorPointKind::status};

    friend bool operator==(const IedSimulatorPoint&, const IedSimulatorPoint&) = default;
};

struct IedSimulatorLogicalNode final {
    std::string name;
    std::string ln_class;
    std::vector<IedSimulatorPoint> points;

    friend bool operator==(const IedSimulatorLogicalNode&, const IedSimulatorLogicalNode&) = default;
};

struct IedSimulatorLogicalDevice final {
    std::string name;
    std::string ld_inst;
    std::vector<IedSimulatorLogicalNode> logical_nodes;

    friend bool operator==(const IedSimulatorLogicalDevice&, const IedSimulatorLogicalDevice&) = default;
};

struct IedSimulatorDataSet final {
    std::string reference;
    std::string mms_domain;
    std::string mms_item;
    std::vector<std::string> members;

    friend bool operator==(const IedSimulatorDataSet&, const IedSimulatorDataSet&) = default;
};

struct IedSimulatorReportControlBlock final {
    std::string reference;
    std::string mms_domain;
    std::string mms_item;
    bool buffered{};
    std::string data_set_reference;
    std::string report_id;
    std::uint32_t configuration_revision{1U};
    std::uint32_t buffer_time_milliseconds{};
    std::uint32_t integrity_period_milliseconds{};
    std::string trigger_options;
    std::string optional_fields;

    [[nodiscard]] std::string mode() const;

    friend bool operator==(const IedSimulatorReportControlBlock&, const IedSimulatorReportControlBlock&) = default;
};

struct IedSimulatorProfile final {
    std::string name{"AR Demo IED"};
    std::string vendor{"ARIEC61850"};
    std::string edition{"IEC 61850 Ed2-style lab profile"};
    std::vector<IedSimulatorLogicalDevice> logical_devices;
    std::vector<IedSimulatorDataSet> data_sets;
    std::vector<IedSimulatorReportControlBlock> report_control_blocks;

    [[nodiscard]] std::size_t logical_node_count() const noexcept;
    [[nodiscard]] std::size_t point_count() const noexcept;
    [[nodiscard]] std::size_t data_set_member_count() const noexcept;

    [[nodiscard]] static IedSimulatorProfile create_default_feeder_profile();
};

struct IedSimulatorProfileFromSclOptions final {
    std::string ied_name;
    std::string runtime_ied_name;
    bool include_quality_and_timestamp_points{true};
    double nominal_frequency_hz{50.0};
};

struct IedSimulatorProfileFromSclResult final {
    IedSimulatorProfile profile;
    std::string selected_ied_name;
    std::string source_ied_name;
    std::size_t data_set_member_count{};
    std::size_t structural_data_attribute_count{};
    std::size_t skipped_member_count{};
    std::vector<std::string> findings;
};

class IedSimulatorProfileBuilder final {
public:
    [[nodiscard]] static IedSimulatorProfileFromSclResult build(
        const scl::SclDocument& document,
        const IedSimulatorProfileFromSclOptions& options = {});
};

} // namespace ar::iec61850::simulation
