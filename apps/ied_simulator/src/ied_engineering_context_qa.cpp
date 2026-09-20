// SPDX-License-Identifier: GPL-3.0-or-later
#include "IedEngineeringContextController.hpp"

#include <QCoreApplication>

#include <iostream>

namespace scl = ar::iec61850::scl;

namespace {

scl::SclDocument makeIed(const std::string& name, const std::string& ip) {
    scl::SclDocument document;
    document.source_name = name + ".cid";
    document.edition = scl::SclEdition::edition2;
    document.ieds.push_back({name, "ARStack", "QA", "1"});

    scl::SclMmsAccessPoint ap;
    ap.ied_name = name;
    ap.access_point_name = "AP1";
    ap.ip_address = ip;
    ap.tcp_port = 102;
    document.mms_access_points.push_back(ap);

    scl::SclLogicalNode ln;
    ln.ied_name = name;
    ln.ld_inst = "LD0";
    ln.ln_class = "LLN0";
    ln.name = "LLN0";
    document.logical_nodes.push_back(ln);

    scl::SclDataSetEntry status;
    status.signal_reference = name + "LD0/LLN0.Mod.stVal";
    status.ied_name = name;
    status.ld_inst = "LD0";
    status.ln_class = "LLN0";
    status.do_name = "Mod";
    status.da_name = "stVal";
    status.functional_constraint = "ST";
    status.cdc = "INC";
    status.basic_type = "INT32";
    status.type_id = "DO_INC";
    document.model_entries.push_back(status);

    scl::SclDataSet dataSet;
    dataSet.key = name + "LD0/LLN0.Status";
    dataSet.ied_name = name;
    dataSet.ld_inst = "LD0";
    dataSet.logical_node_path = "LLN0";
    dataSet.name = "Status";
    dataSet.reference = name + "LD0/LLN0.Status";
    dataSet.entries.push_back(status);
    document.data_sets.push_back(dataSet);

    scl::SclReportControl report;
    report.ied_name = name;
    report.ld_inst = "LD0";
    report.logical_node_path = "LLN0";
    report.name = "StatusReport";
    report.report_id = name + "/Status";
    report.data_set_name = "Status";
    report.data_set_reference = dataSet.reference;
    report.data_set_binding_status = scl::SclDataSetBindingStatus::resolved;
    report.control_block_reference = name + "LD0/LLN0.RP.StatusReport";
    document.report_controls.push_back(report);

    scl::SclGooseStream goose;
    goose.kind = "GOOSE";
    goose.ied_name = name;
    goose.ld_inst = "LD0";
    goose.control_name = "StatusGoose";
    goose.control_block_reference = name + "LD0/LLN0.GO.StatusGoose";
    goose.data_set_name = "Status";
    goose.data_set_reference = dataSet.reference;
    goose.configuration_revision = 1;
    goose.go_id = name + "/StatusGoose";
    goose.address.app_id_text = "1001";
    goose.address.destination_mac_text = "01-0C-CD-01-00-01";
    document.goose_streams.push_back(goose);

    scl::SclSettingControl setting;
    setting.ied_name = name;
    setting.ld_inst = "LD0";
    setting.logical_node_path = "LLN0";
    setting.control_block_reference = name + "LD0/LLN0.SG.SGCB";
    setting.number_of_setting_groups = 4;
    setting.active_setting_group = 1;
    document.setting_controls.push_back(setting);
    return document;
}

void appendIed(scl::SclDocument& target, const scl::SclDocument& source) {
    target.ieds.insert(target.ieds.end(), source.ieds.begin(), source.ieds.end());
    target.mms_access_points.insert(target.mms_access_points.end(), source.mms_access_points.begin(), source.mms_access_points.end());
    target.logical_nodes.insert(target.logical_nodes.end(), source.logical_nodes.begin(), source.logical_nodes.end());
    target.model_entries.insert(target.model_entries.end(), source.model_entries.begin(), source.model_entries.end());
    target.data_sets.insert(target.data_sets.end(), source.data_sets.begin(), source.data_sets.end());
    target.report_controls.insert(target.report_controls.end(), source.report_controls.begin(), source.report_controls.end());
    target.goose_streams.insert(target.goose_streams.end(), source.goose_streams.begin(), source.goose_streams.end());
    target.setting_controls.insert(target.setting_controls.end(), source.setting_controls.begin(), source.setting_controls.end());
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    IedEngineeringContextController context;

    const auto sclA = makeIed("IED_A", "192.0.2.10");
    if (!context.publishSclDocument(sclA, QStringLiteral("/tmp/IED_A.cid")) ||
        !context.loaded() ||
        context.authorityKey() != QStringLiteral("scl") ||
        context.iedName() != QStringLiteral("IED_A") ||
        context.endpoint() != QStringLiteral("192.0.2.10:102") ||
        context.logicalDeviceCount() != 1 ||
        context.logicalNodeCount() != 1 ||
        context.dataObjectCount() != 1 ||
        context.dataAttributeCount() != 1 ||
        context.dataSetCount() != 1 ||
        context.reportCount() != 1 ||
        context.gooseCount() != 1 ||
        context.settingGroupCount() != 1 ||
        context.treeModel()->totalNodeCount() < 4 ||
        context.structuralFingerprint().isEmpty()) {
        std::cerr << "IED_ENGINEERING_CONTEXT_FAIL scl_ingress\n";
        return 2;
    }

    const auto sclFingerprint = context.structuralFingerprint();
    const auto sourceGeneration = context.contextGeneration();
    context.setRuntimeOnline(true, QStringLiteral("192.0.2.10:102"));
    context.setRuntimeOnline(false);
    if (!context.loaded() ||
        context.structuralFingerprint() != sclFingerprint ||
        context.contextGeneration() != sourceGeneration ||
        context.online()) {
        std::cerr << "IED_ENGINEERING_CONTEXT_FAIL disconnect_persistence\n";
        return 3;
    }

    auto multi = sclA;
    appendIed(multi, makeIed("IED_B", "192.0.2.11"));
    if (!context.publishSclDocument(multi, QStringLiteral("/tmp/multi.scd")) ||
        context.loaded() ||
        !context.selectionRequired() ||
        context.candidateIeds().size() != 2) {
        std::cerr << "IED_ENGINEERING_CONTEXT_FAIL multi_ied_boundary\n";
        return 4;
    }
    if (!context.selectIed(QStringLiteral("IED_B")) ||
        !context.loaded() ||
        context.selectionRequired() ||
        context.iedName() != QStringLiteral("IED_B") ||
        context.endpoint() != QStringLiteral("192.0.2.11:102")) {
        std::cerr << "IED_ENGINEERING_CONTEXT_FAIL explicit_ied_selection\n";
        return 5;
    }

    const auto* selected = context.modelSnapshot();
    if (!selected) return 6;
    auto live = *selected;
    live.source = "LiveMmsDiscovery";
    live.endpoint.host = "192.0.2.11";
    live.endpoint.port = 102;
    const auto selectedFingerprint = context.structuralFingerprint();
    context.publishLiveDiscovery(live);
    if (!context.loaded() ||
        context.authorityKey() != QStringLiteral("live-discovery") ||
        !context.online() ||
        context.runtimeEndpoint() != QStringLiteral("192.0.2.11:102") ||
        context.structuralFingerprint() != selectedFingerprint) {
        std::cerr << "IED_ENGINEERING_CONTEXT_FAIL live_ingress\n";
        return 7;
    }

    context.setRuntimeOnline(false);
    if (!context.loaded() || context.online() || context.authorityKey() != QStringLiteral("live-discovery")) {
        std::cerr << "IED_ENGINEERING_CONTEXT_FAIL live_disconnect_persistence\n";
        return 8;
    }

    std::cout
        << "IED_ENGINEERING_CONTEXT_PASS"
        << " scl_ingress=pass"
        << " live_ingress=pass"
        << " multi_ied_selection=pass"
        << " disconnect_persistence=pass"
        << " structural_authority=pass"
        << " fingerprint=" << context.structuralFingerprint().toStdString()
        << "\n";
    return 0;
}
