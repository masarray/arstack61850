// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ar::iec61850::scl {

enum class SclEdition {
    unknown,
    edition1,
    edition2,
    edition21,
};

struct SclIed final {
    std::string name;
    std::string manufacturer;
    std::string type;
    std::string config_version;

    friend bool operator==(const SclIed&, const SclIed&) = default;
};

struct SclDataSetEntry final {
    std::size_t index{};
    std::string signal_reference;
    std::string ied_name;
    std::string ld_inst;
    std::string prefix;
    std::string ln_class;
    std::string ln_inst;
    std::string do_name;
    std::string da_name;
    std::string functional_constraint;
    std::string cdc;
    std::string basic_type;
    std::string type_id;
    std::string enum_type;
    bool is_quality{};
    bool is_timestamp{};

    friend bool operator==(const SclDataSetEntry&, const SclDataSetEntry&) = default;
};

struct SclDataSet final {
    std::string key;
    std::string ied_name;
    std::string ld_inst;
    std::string logical_node_path;
    std::string name;
    std::string reference;
    std::vector<SclDataSetEntry> entries;

    friend bool operator==(const SclDataSet&, const SclDataSet&) = default;
};

struct SclStreamAddress final {
    std::string app_id_text;
    std::optional<std::uint16_t> app_id;
    std::string destination_mac_text;
    std::optional<std::array<std::uint8_t, 6>> destination_mac;
    std::optional<std::uint16_t> vlan_id;
    std::optional<std::uint8_t> vlan_priority;

    friend bool operator==(const SclStreamAddress&, const SclStreamAddress&) = default;
};

struct SclProcessBusStream {
    std::string kind;
    std::string ied_name;
    std::string ld_inst;
    std::string control_name;
    std::string control_block_reference;
    std::string data_set_name;
    std::string data_set_reference;
    std::uint32_t configuration_revision{};
    SclStreamAddress address;
    std::vector<SclDataSetEntry> entries;

    friend bool operator==(const SclProcessBusStream&, const SclProcessBusStream&) = default;
};

struct SclGooseStream final : SclProcessBusStream {
    std::string go_id;
    std::uint32_t min_time_milliseconds{};
    std::uint32_t max_time_milliseconds{};

    friend bool operator==(const SclGooseStream&, const SclGooseStream&) = default;
};

struct SclSmvOptions final {
    // Preserve both element presence and the normalized boolean attributes.
    // This lets later profile compilation distinguish an omitted SmvOpts
    // element from an explicitly present element whose options are all false.
    bool element_present{};
    bool refresh_time{};
    bool sample_synchronized{};
    bool sample_rate{};
    bool data_set{};
    bool security{};
    bool synch_source_id{};

    friend bool operator==(const SclSmvOptions&, const SclSmvOptions&) = default;
};

struct SclSampledValuesStream final : SclProcessBusStream {
    std::string sv_id;
    std::string smv_id;
    std::uint16_t sample_rate{};
    std::string sample_mode;
    std::uint16_t no_asdu{1U};
    SclSmvOptions smv_options;

    friend bool operator==(const SclSampledValuesStream&, const SclSampledValuesStream&) = default;
};

enum class SclDataSetBindingStatus {
    not_specified,
    unresolved,
    resolved_empty,
    resolved,
};

struct SclReportControl final {
    std::string ied_name;
    std::string ld_inst;
    std::string logical_node_path;
    std::string name;
    std::string report_id;
    std::string data_set_name;
    std::string data_set_reference;
    SclDataSetBindingStatus data_set_binding_status{SclDataSetBindingStatus::not_specified};
    std::string control_block_reference;
    bool buffered{};
    bool indexed{true};
    std::uint32_t configuration_revision{};
    std::uint32_t buffer_time_milliseconds{};
    std::uint32_t integrity_period_milliseconds{};
    std::vector<SclDataSetEntry> entries;

    friend bool operator==(const SclReportControl&, const SclReportControl&) = default;
};

struct SclConflict final {
    std::string kind;
    std::string key;
    std::string description;

    friend bool operator==(const SclConflict&, const SclConflict&) = default;
};

struct SclDocument final {
    std::string source_name;
    std::string namespace_uri;
    std::string header_id;
    std::string header_version;
    std::string header_revision;
    SclEdition edition{SclEdition::unknown};
    std::vector<SclIed> ieds;
    std::vector<SclDataSet> data_sets;
    std::vector<SclGooseStream> goose_streams;
    std::vector<SclSampledValuesStream> sampled_values_streams;
    std::vector<SclReportControl> report_controls;
    std::vector<std::string> warnings;
    std::vector<SclConflict> conflicts;

    friend bool operator==(const SclDocument&, const SclDocument&) = default;
};

} // namespace ar::iec61850::scl
