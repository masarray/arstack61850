// SPDX-License-Identifier: GPL-3.0-or-later
#include "IedEngineeringContextController.hpp"
#include "SclWorkspaceController.hpp"

#include "ariec61850/scl/parser.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include <functional>
#include <iostream>

namespace mms = ar::iec61850::mms;
namespace scl = ar::iec61850::scl;

namespace {

bool waitFor(const std::function<bool()>& predicate, const int timeoutMs = 8'000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (predicate()) return true;
        QThread::msleep(10);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return predicate();
}

mms::MmsLiveModelDocument makeLiveModel() {
    mms::MmsLiveModelDocument model;
    model.source = "LiveMmsDiscovery";
    model.endpoint = {"192.0.2.44", 8102U};
    model.identity.ied_name = "IEDLIVE";
    model.identity.source = "MultiDomainNamingConsensus";
    model.identity.confidence = mms::MmsLiveModelConfidence::high;
    model.identity.candidate_names = {"IEDLIVE"};
    model.identity.logical_device_aliases.emplace("IEDLIVELD0", "LD0");
    model.access_point_name = "P1";

    mms::MmsLiveLogicalDevice device;
    device.mms_domain = "IEDLIVELD0";
    device.instance = "LD0";

    mms::MmsLiveLogicalNode node;
    node.name = "LLN0";
    node.logical_node_class = "LLN0";

    mms::MmsLiveDataObject object;
    object.name = "Mod";
    object.reference = "IEDLIVELD0/LLN0.Mod";
    object.inferred_cdc = "INC";
    object.cdc_confidence = 1.0;
    object.confidence = mms::MmsLiveModelConfidence::exact;

    mms::MmsLiveDataAttribute stVal;
    stVal.object_reference = "IEDLIVELD0/LLN0.Mod.stVal";
    stVal.attribute_path = "stVal";
    stVal.functional_constraint = "ST";
    stVal.mms_reference = "IEDLIVELD0/LLN0$ST$Mod$stVal";
    stVal.mms_item_name = "LLN0$ST$Mod$stVal";
    stVal.source = "GetVariableAccessAttributes";
    stVal.scl_basic_type = "INT32";
    stVal.mms_type = "integer";
    stVal.type_discovery_status = "Exact";
    stVal.type_source = "GetVariableAccessAttributes";
    stVal.type_confidence = mms::MmsLiveModelConfidence::exact;
    object.attributes.push_back(stVal);

    mms::MmsLiveDataAttribute quality;
    quality.object_reference = "IEDLIVELD0/LLN0.Mod.q";
    quality.attribute_path = "q";
    quality.functional_constraint = "ST";
    quality.mms_reference = "IEDLIVELD0/LLN0$ST$Mod$q";
    quality.mms_item_name = "LLN0$ST$Mod$q";
    quality.source = "GetVariableAccessAttributes";
    quality.scl_basic_type = "Quality";
    quality.mms_type = "bit-string";
    quality.type_discovery_status = "Exact";
    quality.type_source = "GetVariableAccessAttributes";
    quality.type_confidence = mms::MmsLiveModelConfidence::exact;
    object.attributes.push_back(quality);

    node.data_objects.push_back(object);
    node.functional_constraint_counts["ST"] = 2U;
    device.logical_nodes.push_back(node);
    model.logical_devices.push_back(device);

    mms::MmsLiveDataSet dataSet;
    dataSet.reference = "IEDLIVELD0/LLN0.Status";
    dataSet.domain = "IEDLIVELD0";
    dataSet.logical_node = "LLN0";
    dataSet.name = "Status";
    dataSet.members.push_back({
        0U, stVal.object_reference, "ST", stVal.mms_reference,
        mms::MmsLiveModelConfidence::exact});
    dataSet.members.push_back({
        1U, quality.object_reference, "ST", quality.mms_reference,
        mms::MmsLiveModelConfidence::exact});
    dataSet.used_by_report_controls.push_back("IEDLIVELD0/LLN0.RP.StatusReport");
    dataSet.used_by_goose_controls.push_back("IEDLIVELD0/LLN0.GO.StatusGoose");
    model.data_sets.push_back(dataSet);

    mms::MmsLiveReportControl report;
    report.reference = "IEDLIVELD0/LLN0.RP.StatusReport";
    report.domain = "IEDLIVELD0";
    report.logical_node = "LLN0";
    report.name = "StatusReport";
    report.buffered = false;
    report.data_set_reference = dataSet.reference;
    report.data_set_binding_status = "Bound";
    report.report_id = "IEDLIVE/Status";
    report.configuration_revision = "7";
    report.buffer_time_ms = "20";
    report.integrity_period_ms = "1000";
    model.report_controls.push_back(report);

    mms::MmsLiveControlBlock goose;
    goose.kind = "GSEControl";
    goose.reference = "IEDLIVELD0/LLN0.GO.StatusGoose";
    goose.domain = "IEDLIVELD0";
    goose.logical_node = "LLN0";
    goose.name = "StatusGoose";
    goose.functional_constraint = "GO";
    goose.data_set_reference = dataSet.reference;
    goose.data_set_reference_status = "ValueRead";
    goose.control_id = "IEDLIVE/StatusGoose";
    goose.app_id = "1001";
    goose.configuration_revision = "3";
    goose.minimum_time_ms = "4";
    goose.maximum_time_ms = "1000";
    goose.discovery_status = "ValueReadComplete";
    model.goose_control_blocks.push_back(goose);

    mms::MmsLiveControlBlock setting;
    setting.kind = "SettingGroupControl";
    setting.reference = "IEDLIVELD0/LLN0.SP.SGCB";
    setting.domain = "IEDLIVELD0";
    setting.logical_node = "LLN0";
    setting.name = "SGCB";
    setting.functional_constraint = "SP";
    setting.discovery_status = "ValueReadComplete";
    setting.runtime_attributes.push_back(
        {"ActSG", "IEDLIVELD0/LLN0$SP$SGCB$ActSG", "2", "ValueRead", std::nullopt});
    setting.runtime_attributes.push_back(
        {"NumOfSG", "IEDLIVELD0/LLN0$SP$SGCB$NumOfSG", "4", "ValueRead", std::nullopt});
    model.setting_group_controls.push_back(setting);

    model.coverage.logical_device_count = 1U;
    model.coverage.logical_node_count = 1U;
    model.coverage.data_object_count = 1U;
    model.coverage.data_attribute_count = 2U;
    model.coverage.exact_functional_constraint_count = 2U;
    model.coverage.exact_mms_type_count = 2U;
    model.coverage.data_set_count = 1U;
    model.coverage.report_control_count = 1U;
    model.coverage.unbuffered_report_control_count = 1U;
    model.coverage.report_control_bound_count = 1U;
    model.coverage.goose_control_block_count = 1U;
    model.coverage.setting_group_control_count = 1U;
    return model;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    if (!temp.isValid()) {
        std::cerr << "DISCOVERY_SCL_EXPORT_FAIL temporary_directory\n";
        return 2;
    }

    const auto live = makeLiveModel();
    IedEngineeringContextController context;
    context.publishLiveDiscovery(live);
    const auto fingerprintBeforeDisconnect = context.structuralFingerprint();
    context.setRuntimeOnline(false);
    if (!context.loaded() || context.online() ||
        context.structuralFingerprint() != fingerprintBeforeDisconnect) {
        std::cerr << "DISCOVERY_SCL_EXPORT_FAIL offline_context\n";
        return 3;
    }

    SclWorkspaceController workspace;
    workspace.setEngineeringContext(&context);
    const auto target = temp.filePath(QStringLiteral("IEDLIVE-discovered.iid"));
    if (!workspace.engineeringContextExportSupported() ||
        !workspace.exportEngineeringContext(QUrl::fromLocalFile(target), QStringLiteral("ed2")) ||
        !waitFor([&] { return !workspace.busy(); })) {
        std::cerr << "DISCOVERY_SCL_EXPORT_FAIL export_start\n";
        return 4;
    }
    if (!workspace.lastExportVerified() || !workspace.lastError().isEmpty()) {
        std::cerr << "DISCOVERY_SCL_EXPORT_FAIL export_verify "
                  << workspace.lastError().toStdString() << "\n";
        return 5;
    }

    scl::SclParser parser;
    const auto reopened = parser.load(target.toStdString());
    if (reopened.edition != scl::SclEdition::edition2 ||
        reopened.ieds.size() != 1U ||
        reopened.ieds.front().name != "IEDLIVE" ||
        reopened.mms_access_points.size() != 1U ||
        reopened.mms_access_points.front().access_point_name != "P1" ||
        reopened.mms_access_points.front().ip_address != "192.0.2.44" ||
        reopened.mms_access_points.front().tcp_port != 8102U ||
        reopened.logical_nodes.size() != 1U ||
        reopened.model_entries.size() != 2U ||
        reopened.data_sets.size() != 1U ||
        reopened.data_sets.front().entries.size() != 2U ||
        reopened.data_sets.front().entries[0].da_name != "stVal" ||
        reopened.data_sets.front().entries[1].da_name != "q" ||
        reopened.report_controls.size() != 1U ||
        reopened.goose_streams.size() != 1U ||
        reopened.setting_controls.size() != 1U ||
        !reopened.setting_controls.front().valid() ||
        *reopened.setting_controls.front().number_of_setting_groups != 4U ||
        *reopened.setting_controls.front().active_setting_group != 2U) {
        std::cerr << "DISCOVERY_SCL_EXPORT_FAIL reopened_semantics\n";
        return 6;
    }

    IedEngineeringContextController reopenedContext;
    if (!reopenedContext.publishSclDocument(
            reopened, QStringLiteral("IEDLIVE-discovered.iid")) ||
        !reopenedContext.loaded() ||
        reopenedContext.iedName() != QStringLiteral("IEDLIVE") ||
        reopenedContext.endpointHost() != QStringLiteral("192.0.2.44") ||
        reopenedContext.endpointPort() != 8102 ||
        reopenedContext.logicalDeviceCount() != 1 ||
        reopenedContext.logicalNodeCount() != 1 ||
        reopenedContext.dataAttributeCount() != 2 ||
        reopenedContext.dataSetCount() != 1 ||
        reopenedContext.reportCount() != 1 ||
        reopenedContext.gooseCount() != 1 ||
        reopenedContext.settingGroupCount() != 1) {
        std::cerr << "DISCOVERY_SCL_EXPORT_FAIL reopen_context\n";
        return 7;
    }

    auto unresolved = live;
    unresolved.logical_devices.front().logical_nodes.front()
        .data_objects.front().inferred_cdc.clear();
    context.publishLiveDiscovery(unresolved);
    const auto rejected = temp.filePath(QStringLiteral("unresolved.iid"));
    if (workspace.exportEngineeringContext(
            QUrl::fromLocalFile(rejected), QStringLiteral("ed2")) ||
        workspace.lastError().isEmpty()) {
        std::cerr << "DISCOVERY_SCL_EXPORT_FAIL unresolved_not_rejected\n";
        return 8;
    }

    std::cout << "DISCOVERY_SCL_EXPORT_PASS"
              << " offline_save=true"
              << " iid=true"
              << " endpoint=192.0.2.44:8102"
              << " dataset_order=true"
              << " reports=true"
              << " goose=true"
              << " setting_groups=true"
              << " reopen=true"
              << " unresolved=fail_closed\n";
    return 0;
}
