// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/mms/live_model.hpp"
#include "ariec61850/mms/scl_assisted_connect.hpp"
#include "ariec61850/scl/model.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arstack::iedsim {

struct SclSnapshotValue final {
    std::string domain;
    std::string item;
    std::string display;
};

namespace detail {

[[nodiscard]] inline std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

[[nodiscard]] inline std::string mms_path(std::string value) {
    std::replace(value.begin(), value.end(), '.', '$');
    return value;
}

[[nodiscard]] inline std::string logical_node_name(
    const ar::iec61850::scl::SclLogicalNode& logical_node) {
    if (!logical_node.name.empty()) return logical_node.name;
    return logical_node.prefix + logical_node.ln_class + logical_node.ln_inst;
}

[[nodiscard]] inline std::string logical_node_name(
    const ar::iec61850::scl::SclDataSetEntry& entry) {
    return entry.prefix + entry.ln_class + entry.ln_inst;
}

[[nodiscard]] inline std::string mms_item(
    const ar::iec61850::scl::SclDataSetEntry& entry) {
    auto item = logical_node_name(entry);
    if (!entry.functional_constraint.empty()) item += "$" + entry.functional_constraint;
    if (!entry.do_name.empty()) item += "$" + mms_path(entry.do_name);
    if (!entry.da_name.empty()) item += "$" + mms_path(entry.da_name);
    return item;
}

[[nodiscard]] inline std::string mms_type_for_scl_basic_type(
    const std::string_view basic_type) {
    const auto type = upper(std::string{basic_type});
    if (type == "BOOLEAN") return "boolean";
    if (type.starts_with("INT") && type.ends_with("U")) return "unsigned";
    if (type.starts_with("INT") || type == "ENUM" || type == "ENUMERATED") return "integer";
    if (type.starts_with("FLOAT")) return "floating-point";
    if (type.starts_with("VISSTRING") || type == "CURRENCY") return "visible-string";
    if (type.starts_with("UNICODE") || type.starts_with("MMSSTRING")) return "mms-string";
    if (type == "QUALITY" || type == "CHECK" || type == "OPTFLDS" || type == "TRGOPS") {
        return "bit-string";
    }
    if (type == "TIMESTAMP" || type == "ENTRYTIME") return "utc-time";
    if (type.starts_with("OCTET") || type == "PHYCOMADDR") return "octet-string";
    return "unknown";
}

[[nodiscard]] inline std::string data_set_ln_name(
    const ar::iec61850::scl::SclDataSet& data_set) {
    if (!data_set.logical_node_path.empty()) return data_set.logical_node_path;
    if (!data_set.entries.empty()) return logical_node_name(data_set.entries.front());
    return "LLN0";
}

} // namespace detail

[[nodiscard]] inline ar::iec61850::mms::MmsLiveModelDocument build_scl_live_model(
    const ar::iec61850::scl::SclDocument& document,
    const ar::iec61850::mms::MmsSclAssistedConnectResult& snapshot) {
    namespace mms = ar::iec61850::mms;

    mms::MmsLiveModelDocument model;
    model.source = "TrustedSclInitialSnapshot";
    model.endpoint = snapshot.endpoint;
    model.identity.ied_name = snapshot.ied_name;
    model.identity.source = "TrustedSCL";
    model.identity.confidence = mms::MmsLiveModelConfidence::exact;
    model.identity.candidate_names = {snapshot.ied_name};
    model.identity.evidence.push_back(
        "Trusted SCL identity validated against online MMS Domain inventory.");

    std::map<std::string, std::size_t, std::less<>> device_index;
    std::map<std::string, std::pair<std::size_t, std::size_t>, std::less<>> node_index;

    const auto ensure_device = [&](const std::string& ld_inst) -> std::size_t {
        const auto domain = snapshot.ied_name + ld_inst;
        if (const auto found = device_index.find(domain); found != device_index.end()) {
            return found->second;
        }
        mms::MmsLiveLogicalDevice device;
        device.mms_domain = domain;
        device.instance = ld_inst;
        const auto index = model.logical_devices.size();
        model.logical_devices.push_back(std::move(device));
        device_index.emplace(domain, index);
        model.identity.logical_device_aliases.emplace(domain, ld_inst);
        return index;
    };

    const auto ensure_node = [&](const std::string& ld_inst,
                                 const std::string& name,
                                 const std::string& prefix,
                                 const std::string& ln_class,
                                 const std::string& instance)
        -> std::pair<std::size_t, std::size_t> {
        const auto device = ensure_device(ld_inst);
        const auto domain = snapshot.ied_name + ld_inst;
        const auto key = domain + "\x1f" + name;
        if (const auto found = node_index.find(key); found != node_index.end()) {
            return found->second;
        }
        mms::MmsLiveLogicalNode node;
        node.name = name;
        node.prefix = prefix;
        node.logical_node_class = ln_class;
        node.instance = instance;
        const auto node_pos = model.logical_devices[device].logical_nodes.size();
        model.logical_devices[device].logical_nodes.push_back(std::move(node));
        const auto result = std::pair{device, node_pos};
        node_index.emplace(key, result);
        return result;
    };

    for (const auto& logical_node : document.logical_nodes) {
        if (logical_node.ied_name != snapshot.ied_name) continue;
        static_cast<void>(ensure_node(
            logical_node.ld_inst,
            detail::logical_node_name(logical_node),
            logical_node.prefix,
            logical_node.ln_class,
            logical_node.ln_inst));
    }

    std::map<std::string, std::size_t, std::less<>> object_index;
    for (const auto& entry : document.model_entries) {
        if (entry.ied_name != snapshot.ied_name || entry.functional_constraint.empty()) continue;
        const auto ln_name = detail::logical_node_name(entry);
        const auto [device_pos, node_pos] = ensure_node(
            entry.ld_inst, ln_name, entry.prefix, entry.ln_class, entry.ln_inst);
        auto& logical_node = model.logical_devices[device_pos].logical_nodes[node_pos];
        ++logical_node.functional_constraint_counts[entry.functional_constraint];

        const auto domain = snapshot.ied_name + entry.ld_inst;
        const auto object_key = domain + "\x1f" + ln_name + "\x1f" + entry.do_name;
        std::size_t data_object_pos{};
        if (const auto found = object_index.find(object_key); found != object_index.end()) {
            data_object_pos = found->second;
        } else {
            mms::MmsLiveDataObject object;
            object.name = entry.do_name;
            object.reference = domain + "/" + ln_name + "." + entry.do_name;
            object.inferred_cdc = entry.cdc;
            object.cdc_confidence = entry.cdc.empty() ? 0.0 : 1.0;
            object.confidence = entry.cdc.empty()
                ? mms::MmsLiveModelConfidence::high
                : mms::MmsLiveModelConfidence::exact;
            object.evidence.push_back("Trusted SCL DataTypeTemplates projection.");
            data_object_pos = logical_node.data_objects.size();
            logical_node.data_objects.push_back(std::move(object));
            object_index.emplace(object_key, data_object_pos);
        }

        mms::MmsLiveDataAttribute attribute;
        attribute.object_reference = entry.signal_reference;
        attribute.attribute_path = entry.da_name;
        attribute.functional_constraint = entry.functional_constraint;
        attribute.mms_item_name = detail::mms_item(entry);
        attribute.mms_reference = domain + "/" + attribute.mms_item_name;
        attribute.source = "TrustedSCL";
        attribute.scl_basic_type = entry.basic_type;
        attribute.mms_type = detail::mms_type_for_scl_basic_type(entry.basic_type);
        attribute.type_discovery_status = "Exact";
        attribute.type_discovery_message = "Type supplied by trusted SCL; no GVAA probe required.";
        attribute.type_source = "TrustedSCL";
        attribute.type_confidence = mms::MmsLiveModelConfidence::exact;
        attribute.functional_constraint_confidence = mms::MmsLiveModelConfidence::exact;
        logical_node.data_objects[data_object_pos].attributes.push_back(std::move(attribute));
    }

    for (const auto& data_set : document.data_sets) {
        if (data_set.ied_name != snapshot.ied_name) continue;
        mms::MmsLiveDataSet projected;
        projected.reference = data_set.reference;
        projected.domain = snapshot.ied_name + data_set.ld_inst;
        projected.logical_node = detail::data_set_ln_name(data_set);
        projected.name = data_set.name;
        for (std::size_t index = 0U; index < data_set.entries.size(); ++index) {
            const auto& entry = data_set.entries[index];
            mms::MmsLiveDataSetMember member;
            member.index = index;
            member.reference = entry.signal_reference;
            member.functional_constraint = entry.functional_constraint;
            member.mms_reference = (entry.ied_name + entry.ld_inst) + "/" + detail::mms_item(entry);
            projected.members.push_back(std::move(member));
        }
        model.data_sets.push_back(std::move(projected));
    }

    for (const auto& control : document.report_controls) {
        if (control.ied_name != snapshot.ied_name) continue;
        mms::MmsLiveReportControl projected;
        projected.reference = control.control_block_reference;
        projected.domain = snapshot.ied_name + control.ld_inst;
        projected.logical_node = control.logical_node_path;
        projected.name = control.name;
        projected.buffered = control.buffered;
        projected.data_set_reference = control.data_set_reference;
        projected.data_set_binding_status = control.data_set_reference.empty() ? "Unbound" : "Bound";
        projected.report_id = control.report_id;
        projected.configuration_revision = std::to_string(control.configuration_revision);
        projected.buffer_time_ms = std::to_string(control.buffer_time_milliseconds);
        projected.integrity_period_ms = std::to_string(control.integrity_period_milliseconds);
        model.report_controls.push_back(std::move(projected));
    }

    model.coverage.logical_device_count = model.logical_devices.size();
    for (const auto& device : model.logical_devices) {
        model.coverage.logical_node_count += device.logical_nodes.size();
        for (const auto& node : device.logical_nodes) {
            model.coverage.data_object_count += node.data_objects.size();
            for (const auto& object : node.data_objects) {
                model.coverage.data_attribute_count += object.attributes.size();
                model.coverage.exact_functional_constraint_count += object.attributes.size();
                model.coverage.exact_mms_type_count += static_cast<std::size_t>(std::count_if(
                    object.attributes.begin(), object.attributes.end(), [](const auto& attribute) {
                        return attribute.mms_type != "unknown";
                    }));
            }
        }
    }
    model.coverage.data_set_count = model.data_sets.size();
    model.coverage.report_control_count = model.report_controls.size();
    for (const auto& report : model.report_controls) {
        if (report.buffered) ++model.coverage.buffered_report_control_count;
        else ++model.coverage.unbuffered_report_control_count;
        if (report.data_set_binding_status == "Bound") ++model.coverage.report_control_bound_count;
        else if (report.data_set_binding_status == "Unbound") ++model.coverage.report_control_unbound_count;
    }

    model.summary = "Trusted SCL + online FC-root snapshot: " +
        std::to_string(model.coverage.logical_device_count) + " LD, " +
        std::to_string(model.coverage.logical_node_count) + " LN, " +
        std::to_string(model.coverage.data_attribute_count) + " DA; " +
        std::to_string(snapshot.read_request_count) + " initial Read request(s).";

    for (const auto& missing : snapshot.domains.missing) {
        model.warnings.push_back({
            "SclDomainUnavailable", missing,
            "Trusted SCL domain was not present in the online Domain inventory."});
    }
    for (const auto& extra : snapshot.domains.extra) {
        model.warnings.push_back({
            "UnexpectedOnlineDomain", extra,
            "Online domain is not part of the trusted SCL model and was not merged."});
    }
    return model;
}

[[nodiscard]] inline std::vector<SclSnapshotValue> collect_scl_snapshot_values(
    const ar::iec61850::scl::SclDocument& document,
    const ar::iec61850::mms::MmsSclAssistedConnectResult& snapshot) {
    std::vector<SclSnapshotValue> values;
    values.reserve(snapshot.mapped_leaf_count);
    for (const auto& batch : snapshot.batches) {
        for (const auto& root : batch.roots) {
            if (!root.mapping_success()) continue;
            for (const auto& leaf : root.mapped_leaves) {
                if (!leaf.value || leaf.model_entry_index >= document.model_entries.size()) continue;
                const auto& entry = document.model_entries[leaf.model_entry_index];
                if (entry.ied_name != snapshot.ied_name) continue;
                values.push_back({
                    entry.ied_name + entry.ld_inst,
                    detail::mms_item(entry),
                    ar::iec61850::mms::MmsDataCodec::to_display_string(*leaf.value),
                });
            }
        }
    }
    return values;
}

} // namespace arstack::iedsim
