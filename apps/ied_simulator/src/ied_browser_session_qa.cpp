// SPDX-License-Identifier: GPL-3.0-or-later
#include "IedBrowserSessionController.hpp"
#include "IedBrowserFleetController.hpp"

#include <QCoreApplication>

#include <iostream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    MmsClientController client;
    MmsReportController reports;
    MmsFileSettingsController utilities;
    MmsControlController controls;
    IedBrowserSessionController browser;
    IedEngineeringContextController engineeringContext;

    browser.setClient(&client);
    browser.setReports(&reports);
    browser.setUtilities(&utilities);
    browser.setControls(&controls);
    browser.setEngineeringContext(&engineeringContext);

    ar::iec61850::scl::SclDocument document;
    document.source_name = "browser-context.cid";
    document.edition = ar::iec61850::scl::SclEdition::edition2;
    document.ieds.push_back({"BROWSERIED", "ARStack", "QA", "1"});
    ar::iec61850::scl::SclMmsAccessPoint accessPoint;
    accessPoint.ied_name = "BROWSERIED";
    accessPoint.access_point_name = "AP1";
    accessPoint.ip_address = "192.0.2.77";
    accessPoint.tcp_port = 8102;
    document.mms_access_points.push_back(accessPoint);
    ar::iec61850::scl::SclLogicalNode lln0;
    lln0.ied_name = "BROWSERIED";
    lln0.ld_inst = "LD0";
    lln0.ln_class = "LLN0";
    lln0.name = "LLN0";
    document.logical_nodes.push_back(lln0);

    ar::iec61850::scl::SclDataSetEntry status;
    status.signal_reference = "BROWSERIEDLD0/LLN0.Mod.stVal";
    status.ied_name = "BROWSERIED";
    status.ld_inst = "LD0";
    status.ln_class = "LLN0";
    status.do_name = "Mod";
    status.da_name = "stVal";
    status.functional_constraint = "ST";
    status.cdc = "INC";
    status.basic_type = "INT32";
    status.type_id = "DO_INC";
    document.model_entries.push_back(status);

    ar::iec61850::scl::SclDataSet dataSet;
    dataSet.key = "BROWSERIEDLD0/LLN0.Status";
    dataSet.ied_name = "BROWSERIED";
    dataSet.ld_inst = "LD0";
    dataSet.logical_node_path = "LLN0";
    dataSet.name = "Status";
    dataSet.reference = "BROWSERIEDLD0/LLN0.Status";
    dataSet.entries.push_back(status);
    document.data_sets.push_back(dataSet);

    ar::iec61850::scl::SclReportControl report;
    report.ied_name = "BROWSERIED";
    report.ld_inst = "LD0";
    report.logical_node_path = "LLN0";
    report.name = "StatusReport";
    report.report_id = "BROWSERIED/Status";
    report.data_set_name = "Status";
    report.data_set_reference = dataSet.reference;
    report.data_set_binding_status =
        ar::iec61850::scl::SclDataSetBindingStatus::resolved;
    report.indexed = true;
    report.max_clients = 2U;
    report.control_block_reference =
        "BROWSERIEDLD0/LLN0.RP.StatusReport";
    document.report_controls.push_back(report);

    ar::iec61850::scl::SclGooseStream goose;
    goose.kind = "GOOSE";
    goose.ied_name = "BROWSERIED";
    goose.ld_inst = "LD0";
    goose.control_name = "StatusGoose";
    goose.control_block_reference =
        "BROWSERIEDLD0/LLN0.GO.StatusGoose";
    goose.data_set_name = "Status";
    goose.data_set_reference = dataSet.reference;
    goose.go_id = "BROWSERIED/StatusGoose";
    goose.configuration_revision = 1;
    goose.address.app_id_text = "1001";
    goose.address.destination_mac_text = "01-0C-CD-01-00-01";
    goose.entries.push_back(status);
    document.goose_streams.push_back(goose);

    ar::iec61850::scl::SclSettingControl setting;
    setting.ied_name = "BROWSERIED";
    setting.ld_inst = "LD0";
    setting.logical_node_path = "LLN0";
    setting.control_block_reference =
        "BROWSERIEDLD0/LLN0.SG.SGCB";
    setting.number_of_setting_groups = 4;
    setting.active_setting_group = 1;
    document.setting_controls.push_back(setting);

    if (!engineeringContext.publishSclDocument(
            document, QStringLiteral("/tmp/browser-context.cid")) ||
        browser.host() != QStringLiteral("192.0.2.77") ||
        browser.port() != 8102 ||
        client.engineeringContext() != &engineeringContext ||
        reports.engineeringContext() != &engineeringContext ||
        utilities.engineeringContext() != &engineeringContext ||
        controls.engineeringContext() != &engineeringContext ||
        engineeringContext.dataSets().size() != 1 ||
        engineeringContext.reportControls().size() != 2 ||
        engineeringContext.gooseStreams().size() != 1 ||
        engineeringContext.settingGroups().size() != 1 ||
        reports.dataSets().size() != 1 ||
        reports.reportControls().size() != 2 ||
        utilities.settingGroupCount() != 1) {
        std::cerr << "Canonical Browser engineering-context adoption failed.\n";
        return 2;
    }

    const auto canonicalReports = engineeringContext.reportControls();
    const auto firstReportRef =
        canonicalReports.at(0).toMap().value(QStringLiteral("reference")).toString();
    const auto secondReportRef =
        canonicalReports.at(1).toMap().value(QStringLiteral("reference")).toString();
    if (!firstReportRef.endsWith(QStringLiteral("StatusReport01")) ||
        !secondReportRef.endsWith(QStringLiteral("StatusReport02"))) {
        std::cerr << "Indexed SCL RCB canonical expansion failed.\n";
        return 10;
    }

    const auto gooseProjection = engineeringContext.gooseStreams();
    if (gooseProjection.constFirst().toMap()
            .value(QStringLiteral("destinationMac")).toString() !=
            QStringLiteral("01-0C-CD-01-00-01") ||
        reports.selectedDataSetMembers().size() != 1 ||
        utilities.selectedSettingGroup().value(
            QStringLiteral("engineeringOnly")).toBool() != true) {
        std::cerr << "Canonical Browser service projection failed.\n";
        return 9;
    }

    browser.setHost(QStringLiteral("192.0.2.40"));
    browser.setPort(8102);
    browser.setTrustedSclPath(QStringLiteral("/tmp/browser-context.cid"));

    if (browser.host() != QStringLiteral("192.0.2.40") || browser.port() != 8102 ||
        browser.endpoint() != QStringLiteral("192.0.2.40:8102") ||
        client.host() != browser.host() || reports.host() != browser.host() ||
        utilities.host() != browser.host() || controls.host() != browser.host() ||
        client.port() != browser.port() ||
        reports.port() != browser.port() || utilities.port() != browser.port() ||
        controls.port() != browser.port() ||
        client.trustedSclPath() != browser.trustedSclPath()) {
        std::cerr << "Browser endpoint/trusted-SCL propagation failed.\n";
        return 3;
    }

    browser.setHost(QStringLiteral("[2001:db8::40]"));
    browser.setPort(102);
    if (browser.host() != QStringLiteral("2001:db8::40") ||
        browser.endpoint() != QStringLiteral("[2001:db8::40]:102") ||
        client.host() != browser.host() || reports.host() != browser.host() ||
        utilities.host() != browser.host() || controls.host() != browser.host()) {
        std::cerr << "Browser IPv6 endpoint normalization/propagation failed.\n";
        return 4;
    }

    browser.setPort(0);
    if (browser.port() != 102 || browser.lastError().isEmpty()) {
        std::cerr << "Invalid Browser port did not fail closed.\n";
        return 5;
    }

    browser.setHost(QStringLiteral("invalid host"));
    if (browser.host() != QStringLiteral("2001:db8::40") || browser.lastError().isEmpty()) {
        std::cerr << "Invalid Browser host did not fail closed.\n";
        return 6;
    }

    browser.setHost(QStringLiteral("relay.local"));
    if (browser.host() != QStringLiteral("relay.local") || !browser.lastError().isEmpty() ||
        client.host() != QStringLiteral("relay.local") ||
        reports.host() != QStringLiteral("relay.local") ||
        utilities.host() != QStringLiteral("relay.local") ||
        controls.host() != QStringLiteral("relay.local")) {
        std::cerr << "Browser recovery after invalid configuration failed.\n";
        return 7;
    }

    browser.disconnectFromIed();
    if (browser.connected() || client.connected() || reports.connected() || utilities.connected() || controls.connected()) {
        std::cerr << "Coordinated Browser disconnect contract failed.\n";
        return 8;
    }

    // P6E: each live Browser service and canonical engineering authority is
    // independently owned. Switching the presentation never retargets a
    // previously created controller or destroys another IED's model.
    IedBrowserFleetController fleet;
    auto multi = document;
    multi.ieds.push_back({"BACKUPIED", "ARStack", "QA", "1"});
    auto secondAccessPoint = accessPoint;
    secondAccessPoint.ied_name = "BACKUPIED";
    secondAccessPoint.ip_address = "192.0.2.88";
    multi.mms_access_points.push_back(secondAccessPoint);
    auto secondLn = lln0;
    secondLn.ied_name = "BACKUPIED";
    multi.logical_nodes.push_back(secondLn);
    auto secondStatus = status;
    secondStatus.ied_name = "BACKUPIED";
    secondStatus.signal_reference = "BACKUPIEDLD0/LLN0.Mod.stVal";
    multi.model_entries.push_back(secondStatus);

    if (fleet.workspaceCount() != 1 || fleet.activeIndex() != 0 ||
        !fleet.activeContext()->publishSclDocument(
            multi, QStringLiteral("/tmp/multi-ied.scd"),
            QStringLiteral("BROWSERIED"))) {
        std::cerr << "P6E initial canonical workspace failed.\\n";
        return 20;
    }
    auto* originalContext = fleet.activeContext();
    auto* originalSession = fleet.activeSession();
    auto* originalClient = fleet.activeClient();
    auto* originalReports = fleet.activeReports();
    auto* originalControls = fleet.activeControls();
    auto* originalUtilities = fleet.activeUtilities();
    auto* originalEngineering = fleet.activeEngineering();
    if (originalSession->host() != QStringLiteral("192.0.2.77") ||
        !fleet.openSclIedInNewWorkspace(QStringLiteral("BACKUPIED")) ||
        fleet.workspaceCount() != 2 || fleet.activeIndex() != 1 ||
        fleet.activeContext() == originalContext ||
        fleet.activeClient() == originalClient ||
        fleet.activeReports() == originalReports ||
        fleet.activeControls() == originalControls ||
        fleet.activeUtilities() == originalUtilities ||
        fleet.activeEngineering() == originalEngineering ||
        fleet.activeSession() == originalSession ||
        fleet.activeContext()->iedName() != QStringLiteral("BACKUPIED") ||
        fleet.activeSession()->host() != QStringLiteral("192.0.2.88") ||
        fleet.activeClient()->engineeringContext() != fleet.activeContext() ||
        fleet.activeReports()->engineeringContext() != fleet.activeContext() ||
        fleet.activeControls()->engineeringContext() != fleet.activeContext() ||
        fleet.activeUtilities()->engineeringContext() != fleet.activeContext() ||
        fleet.activeSession()->trustedSclPath() != QStringLiteral("/tmp/multi-ied.scd")) {
        std::cerr << "P6E independent context/service fork failed.\\n";
        return 21;
    }
    auto* secondContext = fleet.activeContext();
    auto* secondSession = fleet.activeSession();
    secondSession->setHost(QStringLiteral("192.0.2.199"));
    if (!fleet.switchTo(0) ||
        fleet.activeContext() != originalContext ||
        fleet.activeSession() != originalSession ||
        fleet.activeClient() != originalClient ||
        fleet.activeContext()->iedName() != QStringLiteral("BROWSERIED") ||
        fleet.activeSession()->host() != QStringLiteral("192.0.2.77") ||
        fleet.activeContext()->treeModel() == secondContext->treeModel()) {
        std::cerr << "P6E switch leaked the other IED endpoint or tree.\\n";
        return 22;
    }
    if (!fleet.openSclIedInNewWorkspace(QStringLiteral("BACKUPIED")) ||
        fleet.workspaceCount() != 2 || fleet.activeIndex() != 1 ||
        fleet.activeContext() != secondContext ||
        fleet.activeSession()->host() != QStringLiteral("192.0.2.199")) {
        std::cerr << "P6E duplicate SCL IED was not deduplicated.\\n";
        return 23;
    }
    if (!fleet.newWorkspace() || fleet.workspaceCount() != 3 ||
        fleet.activeContext()->hasSource() ||
        !fleet.prepareForNewSource() || fleet.workspaceCount() != 3) {
        std::cerr << "P6E new/empty workspace allocation failed.\\n";
        return 24;
    }
    if (!fleet.activeContext()->publishSclDocument(
            document, QStringLiteral("/tmp/another.cid")) ||
        !fleet.prepareForNewSource() || fleet.workspaceCount() != 4 ||
        fleet.activeContext()->hasSource() ||
        fleet.contextAt(0) != originalContext ||
        fleet.contextAt(1) != secondContext) {
        std::cerr << "P6E new source overwrote an existing workspace.\\n";
        return 25;
    }
    if (!fleet.closeWorkspace(2) || fleet.workspaceCount() != 3 ||
        fleet.contextAt(0) != originalContext ||
        fleet.contextAt(1) != secondContext ||
        fleet.activeContext() == nullptr ||
        !fleet.switchTo(1) ||
        fleet.activeSession() != secondSession ||
        fleet.activeSession()->host() != QStringLiteral("192.0.2.199")) {
        std::cerr << "P6E offline close or index adjustment failed.\\n";
        return 26;
    }
    while (fleet.workspaceCount() < IedBrowserFleetController::maximumWorkspaces) {
        if (!fleet.newWorkspace()) {
            std::cerr << "P6E bounded slot creation failed.\\n";
            return 27;
        }
    }
    if (fleet.newWorkspace() || fleet.lastError().isEmpty() ||
        fleet.workspaceCount() != IedBrowserFleetController::maximumWorkspaces ||
        fleet.switchTo(-1)) {
        std::cerr << "P6E workspace capacity/invalid-index guard failed.\\n";
        return 28;
    }
    while (fleet.workspaceCount() > 1) {
        if (!fleet.closeWorkspace(fleet.workspaceCount() - 1)) {
            std::cerr << "P6E offline workspace close failed.\\n";
            return 29;
        }
    }
    if (fleet.closeWorkspace(0) || fleet.workspaceCount() != 1 ||
        fleet.activeContext() != originalContext) {
        std::cerr << "P6E final workspace invariant failed.\\n";
        return 30;
    }

    std::cout << "IED_BROWSER_SESSION_PASS"
              << " endpoint=relay.local:102"
              << " propagation=pass"
              << " canonical_context=pass"
              << " service_reuse=pass"
              << " control_service=coordinated"
              << " indexed_rcb_expansion=pass"
              << " offline_service_inventory=pass"
              << " engineering_endpoint=pass"
              << " trusted_scl=pass"
              << " invalid_endpoint=fail_closed"
              << " disconnect=coordinated\n";
    return 0;
}
