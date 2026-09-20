// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/live_model.hpp"
#include "ariec61850/scl/model.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ar::iec61850::scl {

struct SclLiveModelProjectionResult final {
    bool success{};
    SclDocument document;
    std::string error;
    std::vector<std::string> warnings;
};

namespace live_projection_detail {

inline std::optional<std::uint32_t> parse_u32(const std::string_view text) {
    if (text.empty()) return std::nullopt;
    std::uint32_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return std::nullopt;
    return value;
}

inline std::optional<std::uint16_t> parse_app_id(const std::string_view text) {
    if (text.empty()) return std::nullopt;
    auto value = std::string{text};
    int base = 16;
    if (value.size() > 2U && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
        value.erase(0U, 2U);
    }
    if (value.empty()) return std::nullopt;
    std::uint32_t parsedValue{};
    const auto parsed = std::from_chars(
        value.data(), value.data() + value.size(), parsedValue, base);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
        parsedValue > 0xFFFFU) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(parsedValue);
}

inline std::string normalize_app_id(const std::string_view text) {
    const auto parsed = parse_app_id(text);
    if (!parsed) return std::string{text};
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result{"0x0000"};
    result[2] = hex[(*parsed >> 12U) & 0xFU];
    result[3] = hex[(*parsed >> 8U) & 0xFU];
    result[4] = hex[(*parsed >> 4U) & 0xFU];
    result[5] = hex[*parsed & 0xFU];
    return result;
}

inline std::string signal_reference(
    const mms::MmsLiveModelDocument& model,
    const mms::MmsLiveLogicalDevice& device,
    const mms::MmsLiveLogicalNode& node,
    const mms::MmsLiveDataObject& object,
    const mms::MmsLiveDataAttribute& attribute) {
    return model.identity.ied_name + "/" + device.instance + "/" + node.name + "." +
        object.name + "." + attribute.attribute_path + " [" +
        attribute.functional_constraint + "]";
}

inline std::string data_set_reference(
    const mms::MmsLiveModelDocument& model,
    const mms::MmsLiveLogicalDevice& device,
    const mms::MmsLiveLogicalNode& node,
    const std::string_view name) {
    return model.identity.ied_name + device.instance + "/" + node.name + "$" +
        std::string{name};
}

inline std::string data_set_key(
    const mms::MmsLiveModelDocument& model,
    const mms::MmsLiveLogicalDevice& device,
    const mms::MmsLiveLogicalNode& node,
    const std::string_view name) {
    return model.identity.ied_name + "|" + device.instance + "|" + node.name + "|" +
        std::string{name};
}

inline std::string tail_name(const std::string_view reference) {
    const auto dot = reference.find_last_of(".$");
    if (dot == std::string_view::npos || dot + 1U >= reference.size()) return std::string{reference};
    return std::string{reference.substr(dot + 1U)};
}

inline const mms::MmsLiveLogicalDevice* find_device(
    const mms::MmsLiveModelDocument& model,
    const std::string_view domain) {
    const auto found = std::find_if(
        model.logical_devices.begin(), model.logical_devices.end(),
        [&](const auto& device) { return device.mms_domain == domain; });
    return found == model.logical_devices.end() ? nullptr : &*found;
}

inline const mms::MmsLiveLogicalNode* find_node(
    const mms::MmsLiveLogicalDevice& device,
    const std::string_view name) {
    const auto found = std::find_if(
        device.logical_nodes.begin(), device.logical_nodes.end(),
        [&](const auto& node) { return node.name == name; });
    return found == device.logical_nodes.end() ? nullptr : &*found;
}

inline std::optional<SclDataSetEntry> entry_from_attribute(
    const mms::MmsLiveModelDocument& model,
    const mms::MmsLiveLogicalDevice& device,
    const mms::MmsLiveLogicalNode& node,
    const mms::MmsLiveDataObject& object,
    const mms::MmsLiveDataAttribute& attribute,
    std::string& error) {
    if (object.name.empty() || object.name.find('.') != std::string::npos) {
        error = "Live-to-SCL projection requires a resolved top-level DataObject name: " +
            object.reference;
        return std::nullopt;
    }
    if (object.inferred_cdc.empty()) {
        error = "Live-to-SCL projection refuses to invent an unresolved CDC for " +
            object.reference;
        return std::nullopt;
    }
    if (attribute.attribute_path.empty() || attribute.functional_constraint.empty()) {
        error = "Live-to-SCL projection requires resolved DA path and FC for " +
            attribute.object_reference;
        return std::nullopt;
    }
    if (attribute.scl_basic_type.empty()) {
        error = "Live-to-SCL projection refuses to invent an SCL basic type for " +
            attribute.object_reference;
        return std::nullopt;
    }
    if (attribute.scl_basic_type == "Enum" &&
        attribute.attribute_path != "ctlModel" &&
        !attribute.attribute_path.ends_with(".ctlModel")) {
        error = "Live-to-SCL projection cannot reconstruct unknown Enum ordinals for " +
            attribute.object_reference;
        return std::nullopt;
    }

    SclDataSetEntry entry;
    entry.signal_reference = signal_reference(model, device, node, object, attribute);
    entry.ied_name = model.identity.ied_name;
    entry.ld_inst = device.instance;
    entry.prefix = node.prefix;
    entry.ln_class = node.logical_node_class;
    entry.ln_inst = node.instance;
    entry.do_name = object.name;
    entry.da_name = attribute.attribute_path;
    entry.functional_constraint = attribute.functional_constraint;
    entry.cdc = object.inferred_cdc;
    entry.basic_type = attribute.scl_basic_type;
    if (entry.basic_type == "Enum") {
        entry.enum_type = "CtlModelKind";
        entry.type_id = "CtlModelKind";
    }
    entry.is_quality = entry.basic_type == "Quality";
    entry.is_timestamp = entry.basic_type == "Timestamp" || entry.basic_type == "EntryTime";
    return entry;
}

inline std::string node_key(
    const std::string_view domain,
    const std::string_view node) {
    return std::string{domain} + "\x1f" + std::string{node};
}

inline std::string attribute_key(const std::string_view reference) {
    return std::string{reference};
}

inline std::string dataset_name(const mms::MmsLiveDataSet& dataSet) {
    return !dataSet.name.empty() ? dataSet.name : tail_name(dataSet.reference);
}

inline SclDataSetBindingStatus binding_status(const std::string_view value) {
    if (value == "Bound") return SclDataSetBindingStatus::resolved;
    if (value == "Unbound") return SclDataSetBindingStatus::resolved_empty;
    if (value == "ReadFailed") return SclDataSetBindingStatus::unresolved;
    return SclDataSetBindingStatus::not_specified;
}

inline std::optional<std::uint32_t> runtime_u32(
    const mms::MmsLiveControlBlock& control,
    const std::string_view attributePath) {
    const auto found = std::find_if(
        control.runtime_attributes.begin(), control.runtime_attributes.end(),
        [&](const auto& attribute) {
            return attribute.attribute_path == attributePath &&
                   attribute.status == "ValueRead";
        });
    if (found == control.runtime_attributes.end()) return std::nullopt;
    return parse_u32(found->value);
}

} // namespace live_projection_detail

inline SclLiveModelProjectionResult project_live_model_to_scl(
    const mms::MmsLiveModelDocument& model,
    const SclEdition edition = SclEdition::edition2) {
    using namespace live_projection_detail;
    SclLiveModelProjectionResult result;

    if (edition == SclEdition::unknown) {
        result.error = "Live-to-SCL projection requires an explicit target edition.";
        return result;
    }
    if (model.identity.ied_name.empty() || model.identity.ambiguous) {
        result.error =
            "Live-to-SCL projection requires one unambiguous discovered IED identity.";
        return result;
    }
    if (model.logical_devices.empty()) {
        result.error = "Live-to-SCL projection requires at least one discovered LogicalDevice.";
        return result;
    }

    auto& document = result.document;
    document.source_name = model.identity.ied_name + ".discovered";
    document.header_id = "ARSTACK_DISCOVERY_" + model.identity.ied_name;
    document.header_version = "1";
    document.header_revision = "0";
    document.edition = edition;
    document.ieds.push_back({model.identity.ied_name, {}, {}, {}});

    if (!model.endpoint.host.empty()) {
        SclMmsAccessPoint endpoint;
        endpoint.ied_name = model.identity.ied_name;
        endpoint.access_point_name =
            model.access_point_name.empty() ? "AP1" : model.access_point_name;
        endpoint.ip_address = model.endpoint.host;
        endpoint.tcp_port = model.endpoint.port;
        document.mms_access_points.push_back(std::move(endpoint));
    }

    std::map<std::string, SclDataSetEntry, std::less<>> attributeByMmsReference;
    std::map<std::string, SclDataSetEntry, std::less<>> attributeByObjectReference;
    std::map<std::string, SclDataSetEntry, std::less<>> objectByReference;
    std::map<std::string, std::vector<SclDataSetEntry>, std::less<>> objectLeavesByReference;

    for (const auto& device : model.logical_devices) {
        if (device.instance.empty()) {
            result.error = "Live-to-SCL projection found an unresolved LogicalDevice instance for " +
                device.mms_domain;
            return result;
        }
        for (const auto& node : device.logical_nodes) {
            if (node.name.empty() || node.logical_node_class.empty()) {
                result.error = "Live-to-SCL projection found an unresolved LogicalNode in " +
                    device.mms_domain;
                return result;
            }
            SclLogicalNode logicalNode;
            logicalNode.ied_name = model.identity.ied_name;
            logicalNode.ld_inst = device.instance;
            logicalNode.prefix = node.prefix;
            logicalNode.ln_class = node.logical_node_class;
            logicalNode.ln_inst = node.instance;
            logicalNode.name = node.name;
            document.logical_nodes.push_back(std::move(logicalNode));
            for (const auto& object : node.data_objects) {
                for (const auto& attribute : object.attributes) {
                    std::string error;
                    auto projected = entry_from_attribute(
                        model, device, node, object, attribute, error);
                    if (!projected) {
                        result.error = std::move(error);
                        return result;
                    }
                    projected->index = document.model_entries.size() + 1U;
                    document.model_entries.push_back(*projected);
                    if (!attribute.mms_reference.empty()) {
                        attributeByMmsReference[attribute_key(attribute.mms_reference)] = *projected;
                    }
                    if (!attribute.object_reference.empty()) {
                        attributeByObjectReference[attribute_key(attribute.object_reference)] = *projected;
                    }
                    if (!object.reference.empty()) {
                        objectLeavesByReference[object.reference].push_back(*projected);
                        if (objectByReference.find(object.reference) == objectByReference.end()) {
                            auto wholeObject = *projected;
                            wholeObject.signal_reference =
                                model.identity.ied_name + "/" + device.instance + "/" +
                                node.name + "." + object.name + " [" +
                                attribute.functional_constraint + "]";
                            wholeObject.da_name.clear();
                            wholeObject.basic_type.clear();
                            wholeObject.type_id.clear();
                            wholeObject.enum_type.clear();
                            wholeObject.is_quality = false;
                            wholeObject.is_timestamp = false;
                            objectByReference.emplace(object.reference, std::move(wholeObject));
                        }
                    }
                }
            }
        }
    }

    std::map<std::string, std::size_t, std::less<>> dataSetsByLiveReference;
    for (const auto& dataSet : model.data_sets) {
        const auto* device = find_device(model, dataSet.domain);
        const auto* node = device ? find_node(*device, dataSet.logical_node) : nullptr;
        if (!device || !node || dataSet.reference.empty()) {
            result.error = "Live-to-SCL projection cannot resolve DataSet owner " + dataSet.reference;
            return result;
        }

        SclDataSet projected;
        projected.ied_name = model.identity.ied_name;
        projected.ld_inst = device->instance;
        projected.logical_node_path = node->name;
        projected.name = dataset_name(dataSet);
        projected.key = data_set_key(model, *device, *node, projected.name);
        projected.reference = data_set_reference(model, *device, *node, projected.name);

        std::size_t configuredIndex = 1U;
        std::size_t expandedIndex = 1U;
        for (const auto& member : dataSet.members) {
            auto found = attributeByMmsReference.end();
            if (!member.mms_reference.empty()) {
                found = attributeByMmsReference.find(attribute_key(member.mms_reference));
            }
            if (found == attributeByMmsReference.end() && !member.reference.empty()) {
                const auto attributeFound =
                    attributeByObjectReference.find(attribute_key(member.reference));
                if (attributeFound != attributeByObjectReference.end()) {
                    auto entry = attributeFound->second;
                    entry.index = configuredIndex++;
                    projected.entries.push_back(entry);
                    entry.index = expandedIndex++;
                    projected.expanded_entries.push_back(std::move(entry));
                    continue;
                }
                const auto objectFound = objectByReference.find(member.reference);
                if (objectFound != objectByReference.end()) {
                    auto entry = objectFound->second;
                    entry.index = configuredIndex++;
                    entry.functional_constraint = member.functional_constraint.empty()
                        ? entry.functional_constraint
                        : member.functional_constraint;
                    entry.signal_reference =
                        model.identity.ied_name + "/" + device->instance + "/" +
                        node->name + "." + entry.do_name + " [" +
                        entry.functional_constraint + "]";
                    projected.entries.push_back(entry);

                    const auto leaves = objectLeavesByReference.find(member.reference);
                    const auto expandedBefore = projected.expanded_entries.size();
                    if (leaves != objectLeavesByReference.end()) {
                        for (auto expanded : leaves->second) {
                            if (!member.functional_constraint.empty() &&
                                expanded.functional_constraint != member.functional_constraint) {
                                continue;
                            }
                            expanded.index = expandedIndex++;
                            expanded.functional_constraint = entry.functional_constraint;
                            expanded.signal_reference =
                                model.identity.ied_name + "/" + device->instance + "/" +
                                node->name + "." + expanded.do_name + "." +
                                expanded.da_name + " [" +
                                expanded.functional_constraint + "]";
                            projected.expanded_entries.push_back(std::move(expanded));
                        }
                    }
                    if (projected.expanded_entries.size() == expandedBefore) {
                        result.error =
                            "Live-to-SCL projection cannot expand whole-DataObject member " +
                            member.reference + " for FC " + entry.functional_constraint;
                        return result;
                    }
                    continue;
                }
            }
            if (found == attributeByMmsReference.end()) {
                result.error =
                    "Live-to-SCL projection cannot resolve ordered DataSet member " +
                    std::to_string(configuredIndex) + " of " + dataSet.reference;
                return result;
            }
            auto entry = found->second;
            entry.index = configuredIndex++;
            projected.entries.push_back(entry);
            entry.index = expandedIndex++;
            projected.expanded_entries.push_back(std::move(entry));
        }

        const auto projectedIndex = document.data_sets.size();
        document.data_sets.push_back(std::move(projected));
        dataSetsByLiveReference[dataSet.reference] = projectedIndex;
    }

    const auto findDataSet = [&](const std::string& reference) -> const SclDataSet* {
        if (const auto found = dataSetsByLiveReference.find(reference);
            found != dataSetsByLiveReference.end() &&
            found->second < document.data_sets.size()) {
            return &document.data_sets[found->second];
        }
        return nullptr;
    };

    for (const auto& report : model.report_controls) {
        const auto* device = find_device(model, report.domain);
        const auto* node = device ? find_node(*device, report.logical_node) : nullptr;
        if (!device || !node || report.name.empty()) {
            result.error = "Live-to-SCL projection cannot resolve ReportControl owner " +
                report.reference;
            return result;
        }
        SclReportControl projected;
        projected.ied_name = model.identity.ied_name;
        projected.ld_inst = device->instance;
        projected.logical_node_path = node->name;
        projected.name = report.name;
        projected.report_id = report.report_id;
        projected.data_set_binding_status = binding_status(report.data_set_binding_status);
        projected.control_block_reference =
            model.identity.ied_name + device->instance + "/" + node->name +
            (report.buffered ? "$BR$" : "$RP$") + report.name;
        projected.buffered = report.buffered;
        projected.indexed = false;
        if (const auto value = parse_u32(report.configuration_revision)) {
            projected.configuration_revision = *value;
        }
        if (const auto value = parse_u32(report.buffer_time_ms)) {
            projected.buffer_time_milliseconds = *value;
        }
        if (const auto value = parse_u32(report.integrity_period_ms)) {
            projected.integrity_period_milliseconds = *value;
        }
        if (const auto* dataSet = findDataSet(report.data_set_reference)) {
            projected.data_set_name = dataSet->name;
            projected.data_set_reference = dataSet->reference;
            projected.entries = dataSet->entries;
        }
        document.report_controls.push_back(std::move(projected));
    }

    for (const auto& control : model.goose_control_blocks) {
        const auto* device = find_device(model, control.domain);
        const auto* node = device ? find_node(*device, control.logical_node) : nullptr;
        if (!device || !node || control.name.empty()) {
            result.error = "Live-to-SCL projection cannot resolve GOOSE owner " +
                control.reference;
            return result;
        }
        SclGooseStream projected;
        projected.kind = "GOOSE";
        projected.ied_name = model.identity.ied_name;
        projected.ld_inst = device->instance;
        projected.control_name = control.name;
        projected.control_block_reference =
            model.identity.ied_name + device->instance + "/LLN0$GO$" + control.name;
        if (const auto* dataSet = findDataSet(control.data_set_reference)) {
            projected.data_set_name = dataSet->name;
            projected.data_set_reference = dataSet->reference;
            projected.entries = dataSet->expanded_entries.empty()
                ? dataSet->entries
                : dataSet->expanded_entries;
        }
        if (const auto value = parse_u32(control.configuration_revision)) {
            projected.configuration_revision = *value;
        }
        projected.go_id = control.control_id;
        projected.address.app_id_text = normalize_app_id(control.app_id);
        projected.address.app_id = parse_app_id(control.app_id);
        if (const auto value = parse_u32(control.minimum_time_ms)) {
            projected.min_time_milliseconds = *value;
        }
        if (const auto value = parse_u32(control.maximum_time_ms)) {
            projected.max_time_milliseconds = *value;
        }
        document.goose_streams.push_back(std::move(projected));
    }

    for (const auto& control : model.sampled_value_control_blocks) {
        const auto* device = find_device(model, control.domain);
        const auto* node = device ? find_node(*device, control.logical_node) : nullptr;
        if (!device || !node || control.name.empty()) {
            result.error = "Live-to-SCL projection cannot resolve SampledValue owner " +
                control.reference;
            return result;
        }
        SclSampledValuesStream projected;
        projected.kind = "SMV";
        projected.ied_name = model.identity.ied_name;
        projected.ld_inst = device->instance;
        projected.control_name = control.name;
        projected.control_block_reference =
            model.identity.ied_name + device->instance + "/LLN0$SV$" + control.name;
        if (const auto* dataSet = findDataSet(control.data_set_reference)) {
            projected.data_set_name = dataSet->name;
            projected.data_set_reference = dataSet->reference;
            projected.entries = dataSet->expanded_entries.empty()
                ? dataSet->entries
                : dataSet->expanded_entries;
        }
        if (const auto value = parse_u32(control.configuration_revision)) {
            projected.configuration_revision = *value;
        }
        projected.smv_id = control.smv_id;
        projected.sv_id = control.smv_id;
        projected.address.app_id_text = normalize_app_id(control.app_id);
        projected.address.app_id = parse_app_id(control.app_id);
        if (const auto value = parse_u32(control.sample_rate);
            value && *value <= 0xFFFFU) {
            projected.sample_rate = static_cast<std::uint16_t>(*value);
        }
        projected.sample_mode = control.sample_mode;
        if (const auto value = parse_u32(control.number_of_asdu);
            value && *value > 0U && *value <= 0xFFFFU) {
            projected.no_asdu = static_cast<std::uint16_t>(*value);
        }
        document.sampled_values_streams.push_back(std::move(projected));
    }

    for (const auto& control : model.setting_group_controls) {
        const auto* device = find_device(model, control.domain);
        const auto* node = device ? find_node(*device, control.logical_node) : nullptr;
        if (!device || !node) continue;
        const auto num = runtime_u32(control, "NumOfSG");
        const auto active = runtime_u32(control, "ActSG");
        if (!num || !active || *num == 0U || *active == 0U || *active > *num) {
            result.warnings.push_back(
                "SettingGroupControl " + control.reference +
                " has incomplete live ActSG/NumOfSG evidence and is omitted from SCL.");
            continue;
        }
        SclSettingControl projected;
        projected.ied_name = model.identity.ied_name;
        projected.ld_inst = device->instance;
        projected.logical_node_path = node->name;
        projected.control_block_reference =
            model.identity.ied_name + device->instance + "/" + node->name + "$SP$SGCB";
        projected.number_of_setting_groups = *num;
        projected.active_setting_group = *active;
        document.setting_controls.push_back(std::move(projected));
    }

    document.warnings.push_back(
        "Projected from live MMS discovery; vendor XML extensions, original type IDs, descriptions and Substation topology are not recoverable from MMS.");
    result.warnings = document.warnings;
    result.success = true;
    return result;
}

} // namespace ar::iec61850::scl
