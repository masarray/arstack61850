// SPDX-License-Identifier: GPL-3.0-or-later
#include "IedEngineeringContextController.hpp"

#include <QFileInfo>

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <utility>

namespace mms = ar::iec61850::mms;
namespace scl = ar::iec61850::scl;

namespace {

std::string logicalNodeName(const scl::SclLogicalNode& logicalNode) {
    if (!logicalNode.name.empty()) return logicalNode.name;
    return logicalNode.prefix + logicalNode.ln_class + logicalNode.ln_inst;
}

std::string logicalNodeName(const scl::SclDataSetEntry& entry) {
    return entry.prefix + entry.ln_class + entry.ln_inst;
}

std::string mmsPath(std::string value) {
    std::replace(value.begin(), value.end(), '.', '$');
    return value;
}

std::string mmsItem(const scl::SclDataSetEntry& entry) {
    auto item = logicalNodeName(entry);
    if (!entry.functional_constraint.empty()) item += "$" + entry.functional_constraint;
    if (!entry.do_name.empty()) item += "$" + mmsPath(entry.do_name);
    if (!entry.da_name.empty()) item += "$" + mmsPath(entry.da_name);
    return item;
}

std::string mmsTypeForSclBasicType(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    if (value == "BOOLEAN") return "boolean";
    if (value.starts_with("INT") && value.ends_with("U")) return "unsigned";
    if (value.starts_with("INT") || value == "ENUM" || value == "ENUMERATED") return "integer";
    if (value.starts_with("FLOAT")) return "floating-point";
    if (value.starts_with("VISSTRING") || value == "CURRENCY") return "visible-string";
    if (value.starts_with("UNICODE") || value.starts_with("MMSSTRING")) return "mms-string";
    if (value == "QUALITY" || value == "CHECK" || value == "OPTFLDS" || value == "TRGOPS") return "bit-string";
    if (value == "TIMESTAMP" || value == "ENTRYTIME") return "utc-time";
    if (value.starts_with("OCTET") || value == "PHYCOMADDR") return "octet-string";
    return "unknown";
}

QString endpointText(const mms::MmsEndpoint& endpoint) {
    if (endpoint.host.empty()) return {};
    const auto host = QString::fromStdString(endpoint.host);
    const auto displayHost = host.contains(QLatin1Char(':')) ? QStringLiteral("[%1]").arg(host) : host;
    return QStringLiteral("%1:%2").arg(displayHost).arg(endpoint.port);
}

std::string referenceTail(const std::string& reference, const std::string& fallback) {
    if (reference.empty()) return fallback;
    const auto slash = reference.find_last_of("/.");
    return slash == std::string::npos || slash + 1U >= reference.size()
        ? reference
        : reference.substr(slash + 1U);
}

mms::MmsLiveModelDocument buildSclEngineeringModel(
    const scl::SclDocument& document,
    const std::string& iedName) {
    mms::MmsLiveModelDocument model;
    model.source = "OpenedSCL";
    model.identity.ied_name = iedName;
    model.identity.source = "OpenedSCL";
    model.identity.confidence = mms::MmsLiveModelConfidence::exact;
    model.identity.candidate_names = {iedName};
    model.identity.evidence.push_back("IED identity declared by the opened SCL source.");

    const auto accessPoint = std::find_if(
        document.mms_access_points.begin(),
        document.mms_access_points.end(),
        [&](const auto& item) { return item.ied_name == iedName; });
    if (accessPoint != document.mms_access_points.end()) {
        model.access_point_name = accessPoint->access_point_name.empty() ? "AP1" : accessPoint->access_point_name;
        model.endpoint.host = accessPoint->ip_address;
        model.endpoint.port = accessPoint->tcp_port;
    }

    std::map<std::string, std::size_t, std::less<>> deviceIndex;
    std::map<std::string, std::pair<std::size_t, std::size_t>, std::less<>> nodeIndex;

    const auto ensureDevice = [&](const std::string& ldInst) -> std::size_t {
        const auto domain = iedName + ldInst;
        if (const auto found = deviceIndex.find(domain); found != deviceIndex.end()) return found->second;
        mms::MmsLiveLogicalDevice device;
        device.mms_domain = domain;
        device.instance = ldInst;
        const auto index = model.logical_devices.size();
        model.logical_devices.push_back(std::move(device));
        deviceIndex.emplace(domain, index);
        model.identity.logical_device_aliases.emplace(domain, ldInst);
        return index;
    };

    const auto ensureNode = [&](
        const std::string& ldInst,
        const std::string& name,
        const std::string& prefix,
        const std::string& lnClass,
        const std::string& instance) -> std::pair<std::size_t, std::size_t> {
        const auto device = ensureDevice(ldInst);
        const auto domain = iedName + ldInst;
        const auto key = domain + "\x1f" + name;
        if (const auto found = nodeIndex.find(key); found != nodeIndex.end()) return found->second;
        mms::MmsLiveLogicalNode node;
        node.name = name;
        node.prefix = prefix;
        node.logical_node_class = lnClass;
        node.instance = instance;
        const auto nodePos = model.logical_devices[device].logical_nodes.size();
        model.logical_devices[device].logical_nodes.push_back(std::move(node));
        const auto result = std::pair{device, nodePos};
        nodeIndex.emplace(key, result);
        return result;
    };

    for (const auto& logicalNode : document.logical_nodes) {
        if (logicalNode.ied_name != iedName) continue;
        static_cast<void>(ensureNode(
            logicalNode.ld_inst,
            logicalNodeName(logicalNode),
            logicalNode.prefix,
            logicalNode.ln_class,
            logicalNode.ln_inst));
    }

    std::map<std::string, std::size_t, std::less<>> objectIndex;
    for (const auto& entry : document.model_entries) {
        if (entry.ied_name != iedName || entry.functional_constraint.empty()) continue;
        const auto lnName = logicalNodeName(entry);
        const auto [devicePos, nodePos] = ensureNode(entry.ld_inst, lnName, entry.prefix, entry.ln_class, entry.ln_inst);
        auto& logicalNode = model.logical_devices[devicePos].logical_nodes[nodePos];
        ++logicalNode.functional_constraint_counts[entry.functional_constraint];

        const auto domain = iedName + entry.ld_inst;
        const auto objectKey = domain + "\x1f" + lnName + "\x1f" + entry.do_name;
        std::size_t objectPos{};
        if (const auto found = objectIndex.find(objectKey); found != objectIndex.end()) {
            objectPos = found->second;
        } else {
            mms::MmsLiveDataObject object;
            object.name = entry.do_name;
            object.reference = domain + "/" + lnName + "." + entry.do_name;
            object.proposed_do_type_id = entry.type_id;
            object.inferred_cdc = entry.cdc;
            object.cdc_confidence = entry.cdc.empty() ? 0.0 : 1.0;
            object.confidence = entry.cdc.empty()
                ? mms::MmsLiveModelConfidence::high
                : mms::MmsLiveModelConfidence::exact;
            object.evidence.push_back("Opened SCL DataTypeTemplates projection.");
            objectPos = logicalNode.data_objects.size();
            logicalNode.data_objects.push_back(std::move(object));
            objectIndex.emplace(objectKey, objectPos);
        }

        mms::MmsLiveDataAttribute attribute;
        attribute.object_reference = entry.signal_reference.empty()
            ? domain + "/" + lnName + "." + entry.do_name +
                  (entry.da_name.empty() ? std::string{} : "." + entry.da_name)
            : entry.signal_reference;
        attribute.attribute_path = entry.da_name;
        attribute.functional_constraint = entry.functional_constraint;
        attribute.mms_item_name = mmsItem(entry);
        attribute.mms_reference = domain + "/" + attribute.mms_item_name;
        attribute.source = "OpenedSCL";
        attribute.scl_basic_type = entry.basic_type;
        attribute.mms_type = mmsTypeForSclBasicType(entry.basic_type);
        attribute.type_discovery_status = "Exact";
        attribute.type_discovery_message = "Type supplied by opened SCL; no live GVAA evidence required.";
        attribute.type_source = "OpenedSCL";
        attribute.type_confidence = mms::MmsLiveModelConfidence::exact;
        attribute.functional_constraint_confidence = mms::MmsLiveModelConfidence::exact;
        logicalNode.data_objects[objectPos].attributes.push_back(std::move(attribute));
    }

    for (const auto& dataSet : document.data_sets) {
        if (dataSet.ied_name != iedName) continue;
        mms::MmsLiveDataSet projected;
        projected.reference = dataSet.reference;
        projected.domain = iedName + dataSet.ld_inst;
        projected.logical_node = dataSet.logical_node_path;
        projected.name = dataSet.name;
        for (std::size_t index = 0U; index < dataSet.entries.size(); ++index) {
            const auto& entry = dataSet.entries[index];
            mms::MmsLiveDataSetMember member;
            member.index = index;
            member.reference = entry.signal_reference;
            member.functional_constraint = entry.functional_constraint;
            member.mms_reference = (entry.ied_name + entry.ld_inst) + "/" + mmsItem(entry);
            projected.members.push_back(std::move(member));
        }
        model.data_sets.push_back(std::move(projected));
    }

    for (const auto& control : document.report_controls) {
        if (control.ied_name != iedName) continue;
        mms::MmsLiveReportControl projected;
        projected.reference = control.control_block_reference;
        projected.domain = iedName + control.ld_inst;
        projected.logical_node = control.logical_node_path;
        projected.name = control.name;
        projected.buffered = control.buffered;
        projected.data_set_reference = control.data_set_reference;
        switch (control.data_set_binding_status) {
        case scl::SclDataSetBindingStatus::resolved: projected.data_set_binding_status = "Bound"; break;
        case scl::SclDataSetBindingStatus::resolved_empty: projected.data_set_binding_status = "Unbound"; break;
        default: projected.data_set_binding_status = "NotRead"; break;
        }
        projected.report_id = control.report_id;
        projected.configuration_revision = std::to_string(control.configuration_revision);
        projected.buffer_time_ms = std::to_string(control.buffer_time_milliseconds);
        projected.integrity_period_ms = std::to_string(control.integrity_period_milliseconds);
        model.report_controls.push_back(std::move(projected));
    }

    for (const auto& stream : document.goose_streams) {
        if (stream.ied_name != iedName) continue;
        mms::MmsLiveControlBlock projected;
        projected.kind = "GSEControl";
        projected.reference = stream.control_block_reference;
        projected.domain = iedName + stream.ld_inst;
        projected.logical_node = "LLN0";
        projected.name = stream.control_name;
        projected.functional_constraint = "GO";
        projected.data_set_reference = stream.data_set_reference;
        projected.control_id = stream.go_id;
        projected.app_id = stream.address.app_id_text;
        projected.configuration_revision = std::to_string(stream.configuration_revision);
        projected.minimum_time_ms = std::to_string(stream.min_time_milliseconds);
        projected.maximum_time_ms = std::to_string(stream.max_time_milliseconds);
        projected.address_status = stream.address.destination_mac_text.empty() ? "NotSpecified" : "DeclaredBySCL";
        projected.discovery_status = "OpenedSCL";
        model.goose_control_blocks.push_back(std::move(projected));
    }

    for (const auto& stream : document.sampled_values_streams) {
        if (stream.ied_name != iedName) continue;
        mms::MmsLiveControlBlock projected;
        projected.kind = "SampledValueControl";
        projected.reference = stream.control_block_reference;
        projected.domain = iedName + stream.ld_inst;
        projected.logical_node = "LLN0";
        projected.name = stream.control_name;
        projected.functional_constraint = "MS";
        projected.data_set_reference = stream.data_set_reference;
        projected.smv_id = stream.smv_id.empty() ? stream.sv_id : stream.smv_id;
        projected.app_id = stream.address.app_id_text;
        projected.configuration_revision = std::to_string(stream.configuration_revision);
        projected.sample_rate = std::to_string(stream.sample_rate);
        projected.sample_mode = stream.sample_mode;
        projected.number_of_asdu = std::to_string(stream.no_asdu);
        projected.address_status = stream.address.destination_mac_text.empty() ? "NotSpecified" : "DeclaredBySCL";
        projected.discovery_status = "OpenedSCL";
        model.sampled_value_control_blocks.push_back(std::move(projected));
    }

    for (const auto& setting : document.setting_controls) {
        if (setting.ied_name != iedName) continue;
        mms::MmsLiveControlBlock projected;
        projected.kind = "SettingGroupControl";
        projected.reference = setting.control_block_reference;
        projected.domain = iedName + setting.ld_inst;
        projected.logical_node = setting.logical_node_path;
        projected.name = referenceTail(setting.control_block_reference, "SGCB");
        projected.functional_constraint = "SP";
        projected.discovery_status = "OpenedSCL";
        projected.message = setting.valid()
            ? "SettingControl declared by opened SCL."
            : "SettingControl declared by opened SCL with incomplete runtime SG evidence.";
        model.setting_group_controls.push_back(std::move(projected));
    }

    for (auto& dataSet : model.data_sets) {
        for (const auto& report : model.report_controls) {
            if (!report.data_set_reference.empty() && report.data_set_reference == dataSet.reference)
                dataSet.used_by_report_controls.push_back(report.reference);
        }
        for (const auto& goose : model.goose_control_blocks) {
            if (!goose.data_set_reference.empty() && goose.data_set_reference == dataSet.reference)
                dataSet.used_by_goose_controls.push_back(goose.reference);
        }
        for (const auto& smv : model.sampled_value_control_blocks) {
            if (!smv.data_set_reference.empty() && smv.data_set_reference == dataSet.reference)
                dataSet.used_by_sampled_value_controls.push_back(smv.reference);
        }
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
        else ++model.coverage.report_control_binding_not_read_count;
    }
    model.coverage.goose_control_block_count = model.goose_control_blocks.size();
    model.coverage.sampled_value_control_block_count = model.sampled_value_control_blocks.size();
    model.coverage.setting_group_control_count = model.setting_group_controls.size();

    model.summary =
        "Opened SCL canonical IED context: " +
        std::to_string(model.coverage.logical_device_count) + " LD, " +
        std::to_string(model.coverage.logical_node_count) + " LN, " +
        std::to_string(model.coverage.data_attribute_count) + " DA, " +
        std::to_string(model.coverage.data_set_count) + " DataSet, " +
        std::to_string(model.coverage.report_control_count) + " ReportControl.";
    return model;
}

} // namespace

IedEngineeringContextController::IedEngineeringContextController(QObject* parent)
    : QObject(parent), treeModel_(this) {}

QString IedEngineeringContextController::authority() const {
    switch (authority_) {
    case Authority::openedScl: return QStringLiteral("Open SCL");
    case Authority::liveDiscovery: return QStringLiteral("Live Discovery");
    case Authority::none: return QStringLiteral("None");
    }
    return QStringLiteral("None");
}

QString IedEngineeringContextController::authorityKey() const {
    switch (authority_) {
    case Authority::openedScl: return QStringLiteral("scl");
    case Authority::liveDiscovery: return QStringLiteral("live-discovery");
    case Authority::none: return QStringLiteral("none");
    }
    return QStringLiteral("none");
}

int IedEngineeringContextController::logicalDeviceCount() const noexcept { return model_ ? static_cast<int>(model_->coverage.logical_device_count) : 0; }
int IedEngineeringContextController::logicalNodeCount() const noexcept { return model_ ? static_cast<int>(model_->coverage.logical_node_count) : 0; }
int IedEngineeringContextController::dataObjectCount() const noexcept { return model_ ? static_cast<int>(model_->coverage.data_object_count) : 0; }
int IedEngineeringContextController::dataAttributeCount() const noexcept { return model_ ? static_cast<int>(model_->coverage.data_attribute_count) : 0; }
int IedEngineeringContextController::dataSetCount() const noexcept { return model_ ? static_cast<int>(model_->data_sets.size()) : 0; }
int IedEngineeringContextController::reportCount() const noexcept { return model_ ? static_cast<int>(model_->report_controls.size()) : 0; }
int IedEngineeringContextController::gooseCount() const noexcept { return model_ ? static_cast<int>(model_->goose_control_blocks.size()) : 0; }
int IedEngineeringContextController::sampledValueCount() const noexcept { return model_ ? static_cast<int>(model_->sampled_value_control_blocks.size()) : 0; }
int IedEngineeringContextController::settingGroupCount() const noexcept { return model_ ? static_cast<int>(model_->setting_group_controls.size()) : 0; }

QVariantList IedEngineeringContextController::dataSets() const {
    QVariantList result;
    if (!model_) return result;
    result.reserve(static_cast<qsizetype>(model_->data_sets.size()));
    for (const auto& dataSet : model_->data_sets) {
        QVariantMap item;
        item.insert(QStringLiteral("reference"), QString::fromStdString(dataSet.reference));
        item.insert(QStringLiteral("domain"), QString::fromStdString(dataSet.domain));
        item.insert(QStringLiteral("logicalNode"), QString::fromStdString(dataSet.logical_node));
        item.insert(QStringLiteral("name"), QString::fromStdString(dataSet.name));
        item.insert(QStringLiteral("deletable"),
                    dataSet.deletable ? QVariant::fromValue(*dataSet.deletable) : QVariant{});
        QStringList members;
        members.reserve(static_cast<qsizetype>(dataSet.members.size()));
        for (const auto& member : dataSet.members) {
            auto text = QString::fromStdString(member.reference);
            if (!member.functional_constraint.empty()) {
                text += QStringLiteral("  [") +
                    QString::fromStdString(member.functional_constraint) +
                    QLatin1Char(']');
            }
            members.push_back(text);
        }
        item.insert(QStringLiteral("members"), members);
        item.insert(QStringLiteral("memberCount"), members.size());
        QStringList reportUsers;
        for (const auto& reference : dataSet.used_by_report_controls)
            reportUsers.push_back(QString::fromStdString(reference));
        item.insert(QStringLiteral("usedByReports"), reportUsers);
        result.push_back(item);
    }
    return result;
}

QVariantList IedEngineeringContextController::reportControls() const {
    QVariantList result;
    if (!model_) return result;
    result.reserve(static_cast<qsizetype>(model_->report_controls.size()));
    for (const auto& control : model_->report_controls) {
        QVariantMap item;
        item.insert(QStringLiteral("reference"), QString::fromStdString(control.reference));
        item.insert(QStringLiteral("mode"), control.buffered ? QStringLiteral("BRCB") : QStringLiteral("URCB"));
        item.insert(QStringLiteral("buffered"), control.buffered);
        item.insert(QStringLiteral("domain"), QString::fromStdString(control.domain));
        item.insert(QStringLiteral("logicalNode"), QString::fromStdString(control.logical_node));
        item.insert(QStringLiteral("name"), QString::fromStdString(control.name));
        item.insert(QStringLiteral("dataSet"), QString::fromStdString(control.data_set_reference));
        item.insert(QStringLiteral("reportId"), QString::fromStdString(control.report_id));
        item.insert(QStringLiteral("confRev"), QString::fromStdString(control.configuration_revision));
        item.insert(QStringLiteral("bufTm"), QString::fromStdString(control.buffer_time_ms));
        item.insert(QStringLiteral("intgPd"), QString::fromStdString(control.integrity_period_ms));
        item.insert(QStringLiteral("enabled"), QString::fromStdString(control.enabled_state));
        item.insert(QStringLiteral("reserved"), QString::fromStdString(control.reservation_state));
        item.insert(QStringLiteral("probeOk"), false);
        item.insert(QStringLiteral("engineeringOnly"), true);
        result.push_back(item);
    }
    return result;
}

QVariantList IedEngineeringContextController::gooseStreams() const {
    QVariantList result;
    if (!model_) return result;

    if (authority_ == Authority::openedScl && sclSource_) {
        for (const auto& stream : sclSource_->goose_streams) {
            if (QString::fromStdString(stream.ied_name) != iedName_) continue;
            QVariantMap item;
            item.insert(QStringLiteral("iedName"), QString::fromStdString(stream.ied_name));
            item.insert(QStringLiteral("ldInst"), QString::fromStdString(stream.ld_inst));
            item.insert(QStringLiteral("name"), QString::fromStdString(stream.control_name));
            item.insert(QStringLiteral("reference"), QString::fromStdString(stream.control_block_reference));
            item.insert(QStringLiteral("dataSet"), QString::fromStdString(stream.data_set_reference));
            item.insert(QStringLiteral("goId"), QString::fromStdString(stream.go_id));
            item.insert(QStringLiteral("confRev"), static_cast<qulonglong>(stream.configuration_revision));
            item.insert(QStringLiteral("appId"), QString::fromStdString(stream.address.app_id_text));
            item.insert(QStringLiteral("destinationMac"), QString::fromStdString(stream.address.destination_mac_text));
            item.insert(QStringLiteral("vlanId"),
                        stream.address.vlan_id ? QVariant::fromValue(static_cast<int>(*stream.address.vlan_id))
                                               : QVariant{});
            item.insert(QStringLiteral("vlanPriority"),
                        stream.address.vlan_priority
                            ? QVariant::fromValue(static_cast<int>(*stream.address.vlan_priority))
                            : QVariant{});
            item.insert(QStringLiteral("minTimeMs"), static_cast<qulonglong>(stream.min_time_milliseconds));
            item.insert(QStringLiteral("maxTimeMs"), static_cast<qulonglong>(stream.max_time_milliseconds));
            QStringList members;
            for (const auto& entry : stream.entries)
                members.push_back(QString::fromStdString(entry.signal_reference));
            item.insert(QStringLiteral("members"), members);
            result.push_back(item);
        }
        return result;
    }

    result.reserve(static_cast<qsizetype>(model_->goose_control_blocks.size()));
    for (const auto& control : model_->goose_control_blocks) {
        QVariantMap item;
        item.insert(QStringLiteral("iedName"), iedName_);
        item.insert(QStringLiteral("name"), QString::fromStdString(control.name));
        item.insert(QStringLiteral("reference"), QString::fromStdString(control.reference));
        item.insert(QStringLiteral("dataSet"), QString::fromStdString(control.data_set_reference));
        item.insert(QStringLiteral("goId"), QString::fromStdString(control.control_id));
        item.insert(QStringLiteral("confRev"), QString::fromStdString(control.configuration_revision));
        item.insert(QStringLiteral("appId"), QString::fromStdString(control.app_id));
        item.insert(QStringLiteral("destinationMac"), QString{});
        item.insert(QStringLiteral("vlanId"), QVariant{});
        item.insert(QStringLiteral("vlanPriority"), QVariant{});
        item.insert(QStringLiteral("minTimeMs"), QString::fromStdString(control.minimum_time_ms));
        item.insert(QStringLiteral("maxTimeMs"), QString::fromStdString(control.maximum_time_ms));
        QStringList members;
        const auto dataSet = std::find_if(
            model_->data_sets.begin(), model_->data_sets.end(),
            [&](const auto& candidate) {
                return candidate.reference == control.data_set_reference;
            });
        if (dataSet != model_->data_sets.end()) {
            for (const auto& member : dataSet->members)
                members.push_back(QString::fromStdString(member.reference));
        }
        item.insert(QStringLiteral("members"), members);
        result.push_back(item);
    }
    return result;
}

QVariantList IedEngineeringContextController::settingGroups() const {
    QVariantList result;
    if (!model_) return result;
    result.reserve(static_cast<qsizetype>(model_->setting_group_controls.size()));
    for (const auto& control : model_->setting_group_controls) {
        QVariantMap item;
        item.insert(QStringLiteral("reference"), QString::fromStdString(control.reference));
        item.insert(QStringLiteral("domain"), QString::fromStdString(control.domain));
        item.insert(QStringLiteral("logicalNode"), QString::fromStdString(control.logical_node));
        item.insert(QStringLiteral("name"), QString::fromStdString(control.name));
        item.insert(QStringLiteral("functionalConstraints"), QString::fromStdString(control.functional_constraint));
        item.insert(QStringLiteral("complete"), false);
        item.insert(QStringLiteral("attributes"), QVariantList{});
        item.insert(QStringLiteral("attributeCount"), 0);
        item.insert(QStringLiteral("canActivate"), false);
        item.insert(QStringLiteral("activationBlockedReason"),
                    QStringLiteral("Go Online to read and verify this canonical Setting Group control."));
        item.insert(QStringLiteral("fullEditSupported"), false);
        item.insert(QStringLiteral("engineeringOnly"), true);
        result.push_back(item);
    }
    return result;
}

void IedEngineeringContextController::applyModel(std::shared_ptr<mms::MmsLiveModelDocument> model) {
    model_ = std::move(model);
    if (!model_) {
        treeModel_.clear();
        iedName_.clear();
        accessPointName_.clear();
        endpoint_.clear();
        structuralFingerprint_.clear();
        return;
    }
    treeModel_.applyDocument(*model_);
    iedName_ = QString::fromStdString(model_->identity.ied_name);
    accessPointName_ = QString::fromStdString(model_->access_point_name);
    endpoint_ = endpointText(model_->endpoint);
    structuralFingerprint_ = QString::fromStdString(model_->structural_fingerprint_hex());
}

bool IedEngineeringContextController::publishSclDocument(
    const scl::SclDocument& document,
    const QString& sourcePath,
    const QString& selectedIedName) {
    authority_ = Authority::openedScl;
    sclSource_ = std::make_shared<scl::SclDocument>(document);
    sourcePath_ = sourcePath.trimmed();
    sourceName_ = !sourcePath_.isEmpty() ? QFileInfo(sourcePath_).fileName() : QString::fromStdString(document.source_name);
    candidateIeds_.clear();
    for (const auto& ied : document.ieds) candidateIeds_.push_back(QString::fromStdString(ied.name));

    online_ = false;
    runtimeEndpoint_.clear();
    selectionRequired_ = false;
    lastError_.clear();
    applyModel({});

    if (candidateIeds_.isEmpty()) {
        lastError_ = QStringLiteral("Opened SCL source contains no selectable IED.");
        ++contextGeneration_;
        ++runtimeGeneration_;
        emit contextChanged();
        emit runtimeChanged();
        return false;
    }

    auto requested = selectedIedName.trimmed();
    if (requested.isEmpty() && candidateIeds_.size() == 1) requested = candidateIeds_.constFirst();
    if (requested.isEmpty()) {
        selectionRequired_ = true;
        lastError_ = QStringLiteral("Opened SCL contains %1 IEDs; select the active IED before Browser binding.")
                         .arg(candidateIeds_.size());
        ++contextGeneration_;
        ++runtimeGeneration_;
        emit contextChanged();
        emit runtimeChanged();
        return true;
    }
    return activateSclIed(requested);
}

bool IedEngineeringContextController::activateSclIed(const QString& iedName) {
    if (authority_ != Authority::openedScl || !sclSource_) {
        lastError_ = QStringLiteral("No opened SCL source is available for IED selection.");
        emit contextChanged();
        return false;
    }
    const auto requested = iedName.trimmed();
    const auto found = std::find(candidateIeds_.begin(), candidateIeds_.end(), requested);
    if (requested.isEmpty() || found == candidateIeds_.end()) {
        lastError_ = QStringLiteral("IED '%1' is not present in the opened SCL source.").arg(requested);
        emit contextChanged();
        return false;
    }

    applyModel(std::make_shared<mms::MmsLiveModelDocument>(
        buildSclEngineeringModel(*sclSource_, requested.toStdString())));
    selectionRequired_ = false;
    online_ = false;
    runtimeEndpoint_.clear();
    lastError_.clear();
    ++contextGeneration_;
    ++runtimeGeneration_;
    emit contextChanged();
    emit runtimeChanged();
    return true;
}

bool IedEngineeringContextController::selectIed(const QString& iedName) {
    return activateSclIed(iedName);
}

void IedEngineeringContextController::publishLiveDiscovery(const mms::MmsLiveModelDocument& model) {
    authority_ = Authority::liveDiscovery;
    sclSource_.reset();
    sourcePath_.clear();
    sourceName_ = QString::fromStdString(model.identity.ied_name);
    candidateIeds_ = {QString::fromStdString(model.identity.ied_name)};
    selectionRequired_ = false;
    lastError_.clear();
    applyModel(std::make_shared<mms::MmsLiveModelDocument>(model));
    online_ = true;
    runtimeEndpoint_ = endpoint_;
    ++contextGeneration_;
    ++runtimeGeneration_;
    emit contextChanged();
    emit runtimeChanged();
}

void IedEngineeringContextController::publishTrustedSclOnline(
    const mms::MmsLiveModelDocument& onlineModel,
    const QString& sourcePath) {
    const auto onlineIed = QString::fromStdString(onlineModel.identity.ied_name);
    if (authority_ == Authority::openedScl && model_ && iedName_ == onlineIed) {
        if (sourcePath_.isEmpty()) {
            sourcePath_ = sourcePath.trimmed();
            sourceName_ = QFileInfo(sourcePath_).fileName();
            ++contextGeneration_;
            emit contextChanged();
        }
        setRuntimeOnline(true, endpointText(onlineModel.endpoint));
        return;
    }

    authority_ = Authority::openedScl;
    sclSource_.reset();
    sourcePath_ = sourcePath.trimmed();
    sourceName_ = QFileInfo(sourcePath_).fileName();
    candidateIeds_ = {onlineIed};
    selectionRequired_ = false;
    lastError_.clear();
    applyModel(std::make_shared<mms::MmsLiveModelDocument>(onlineModel));
    online_ = true;
    runtimeEndpoint_ = endpointText(onlineModel.endpoint);
    ++contextGeneration_;
    ++runtimeGeneration_;
    emit contextChanged();
    emit runtimeChanged();
}

void IedEngineeringContextController::setRuntimeOnline(const bool online, const QString& endpoint) {
    const auto normalizedEndpoint = online ? endpoint.trimmed() : QString{};
    if (online_ == online && runtimeEndpoint_ == normalizedEndpoint) return;
    online_ = online;
    runtimeEndpoint_ = normalizedEndpoint;
    ++runtimeGeneration_;
    emit runtimeChanged();
}

void IedEngineeringContextController::clearContext() {
    authority_ = Authority::none;
    sclSource_.reset();
    applyModel({});
    sourcePath_.clear();
    sourceName_.clear();
    candidateIeds_.clear();
    selectionRequired_ = false;
    lastError_.clear();
    online_ = false;
    runtimeEndpoint_.clear();
    ++contextGeneration_;
    ++runtimeGeneration_;
    emit contextChanged();
    emit runtimeChanged();
}
