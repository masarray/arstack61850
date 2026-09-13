// SPDX-License-Identifier: GPL-3.0-or-later

#include "IedFleetController.hpp"
#include "MmsReportController.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHostAddress>
#include <QTcpServer>
#include <QThread>
#include <QUrl>

#include <functional>

namespace {
bool waitUntil(const std::function<bool()>& predicate, const int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return predicate();
}

bool loadAsync(IedFleetController& controller, const QString& path) {
    if (!controller.loadFileAsync(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()))) return false;
    if (!waitUntil([&controller] { return !controller.importing(); }, 15'000)) return false;
    return controller.imported() && controller.fatalError().isEmpty();
}

int selectEligibleUrcb(MmsReportController& reports) {
    const auto controls = reports.reportControls();
    for (int row = 0; row < controls.size(); ++row) {
        const auto item = controls.at(row).toMap();
        if (item.value(QStringLiteral("buffered")).toBool()) continue;
        if (!item.value(QStringLiteral("probeOk")).toBool()) continue;
        if (item.value(QStringLiteral("dataSet")).toString().isEmpty()) continue;
        if (!reports.selectRcb(row)) continue;
        if (!reports.selectedDataSetMembers().isEmpty()) return row;
    }
    return -1;
}
} // namespace

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("ARSTACK_IEDSIM_QA", "1");
    QGuiApplication app(argc, argv);
    if (argc != 2) {
        qCritical() << "usage: ied_report_workspace_qa <scl>";
        return 2;
    }

    MmsReportController negative;
    const bool noConnectionEnableRejected = !negative.enableSelected(true);
    const bool inactiveDisableRejected = !negative.disableSelected();
    const bool invalidSelectionRejected = !negative.selectRcb(0) && !negative.selectDataSet(0);
    if (!noConnectionEnableRejected || !inactiveDisableRejected || !invalidSelectionRejected) {
        qCritical() << "REPORTS_WORKBENCH_FAIL negative_preconditions";
        return 3;
    }

    IedFleetController simulator;
    const QString sclPath = QString::fromLocal8Bit(argv[1]);
    if (!loadAsync(simulator, sclPath)) {
        qCritical().noquote() << "REPORTS_WORKBENCH_FAIL import" << simulator.fatalError();
        return 4;
    }

    QTcpServer reservation;
    if (!reservation.listen(QHostAddress::LocalHost, 0)) {
        qCritical() << "REPORTS_WORKBENCH_FAIL reserve_port";
        return 5;
    }
    const quint16 port = reservation.serverPort();
    reservation.close();

    if (!simulator.configureIedEndpoint(0, QStringLiteral("127.0.0.1"), port) ||
        !simulator.startSimulation() ||
        !waitUntil([&simulator] { return simulator.running(); }, 6'000)) {
        qCritical() << "REPORTS_WORKBENCH_FAIL simulator_start";
        return 6;
    }

    MmsReportController reports;
    reports.setHost(QStringLiteral("127.0.0.1"));
    reports.setPort(port);
    if (!reports.connectToIed() ||
        !waitUntil([&reports] { return reports.connected() && !reports.busy(); }, 12'000)) {
        qCritical().noquote() << "REPORTS_WORKBENCH_FAIL connect" << reports.lastError();
        return 7;
    }

    const auto dataSets = reports.dataSets();
    const auto reportControls = reports.reportControls();
    const auto staticCandidates = reports.staticCandidates();
    const auto dynamicCandidates = reports.dynamicCandidates();
    if (dataSets.isEmpty() || reportControls.size() < 2 || staticCandidates.isEmpty()) {
        qCritical() << "REPORTS_WORKBENCH_FAIL inventory"
                    << dataSets.size() << reportControls.size() << staticCandidates.size();
        return 8;
    }

    bool sawUrcb = false;
    bool sawBrcb = false;
    bool sawBrcbEntryId = false;
    for (const auto& value : reportControls) {
        const auto item = value.toMap();
        if (item.value(QStringLiteral("buffered")).toBool()) {
            sawBrcb = true;
            sawBrcbEntryId = sawBrcbEntryId || item.value(QStringLiteral("supportsEntryId")).toBool();
        } else {
            sawUrcb = true;
        }
    }
    if (!sawUrcb || !sawBrcb || !sawBrcbEntryId) {
        qCritical() << "REPORTS_WORKBENCH_FAIL rcb_modes"
                    << sawUrcb << sawBrcb << sawBrcbEntryId;
        return 9;
    }

    const int selected = selectEligibleUrcb(reports);
    if (selected < 0 || reports.selectedDataSetMembers().isEmpty()) {
        qCritical() << "REPORTS_WORKBENCH_FAIL eligible_urcb";
        return 10;
    }
    const int memberCount = reports.selectedDataSetMembers().size();

    if (!reports.enableSelected(true) ||
        !waitUntil([&reports] { return reports.active() && !reports.busy(); }, 8'000)) {
        qCritical().noquote() << "REPORTS_WORKBENCH_FAIL enable_gi" << reports.lastError();
        return 11;
    }

    if (!waitUntil([&reports] { return reports.receivedReportCount() > 0; }, 5'000)) {
        qCritical().noquote() << "REPORTS_WORKBENCH_FAIL gi_report" << reports.diagnosticsText();
        return 12;
    }
    const auto reportCountAfterGi = reports.receivedReportCount();
    const auto frames = reports.receivedReports();
    if (frames.isEmpty()) {
        qCritical() << "REPORTS_WORKBENCH_FAIL report_projection";
        return 13;
    }
    const auto firstFrame = frames.first().toMap();
    if (firstFrame.value(QStringLiteral("reportId")).toString().isEmpty() ||
        firstFrame.value(QStringLiteral("dataSet")).toString().isEmpty() ||
        firstFrame.value(QStringLiteral("values")).toStringList().isEmpty() ||
        !firstFrame.contains(QStringLiteral("entryId")) ||
        !firstFrame.contains(QStringLiteral("overflow"))) {
        qCritical().noquote() << "REPORTS_WORKBENCH_FAIL report_fields" << firstFrame;
        return 14;
    }

    // The controller uses the association runtime's bounded non-fatal polling
    // primitive. Several quiet poll intervals must leave the association and
    // subscription alive instead of converting normal idle time into a fault.
    QElapsedTimer idle;
    idle.start();
    while (idle.elapsed() < 450) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    if (!reports.active() || !reports.connected() || reports.cleanupRequired()) {
        qCritical().noquote() << "REPORTS_WORKBENCH_FAIL idle_poll" << reports.lastError();
        return 15;
    }

    if (!reports.disableSelected() ||
        !waitUntil([&reports] { return !reports.active() && !reports.busy(); }, 6'000) ||
        reports.cleanupRequired()) {
        qCritical().noquote() << "REPORTS_WORKBENCH_FAIL cleanup" << reports.lastError();
        return 16;
    }

    reports.disconnectFromIed();
    simulator.stopSimulation();
    if (!waitUntil([&simulator] { return !simulator.anyRunning(); }, 4'000)) {
        qCritical() << "REPORTS_WORKBENCH_FAIL simulator_stop";
        return 17;
    }

    qInfo().noquote()
        << "REPORTS_WORKBENCH_PASS"
        << "datasets=" + QString::number(dataSets.size())
        << "rcbs=" + QString::number(reportControls.size())
        << "static_candidates=" + QString::number(staticCandidates.size())
        << "dynamic_candidates=" + QString::number(dynamicCandidates.size())
        << "members=" + QString::number(memberCount)
        << "gi_reports=" + QString::number(reportCountAfterGi)
        << "urcb=pass"
        << "brcb_inventory=pass"
        << "entryid_indicator=pass"
        << "cleanup=pass";
    qInfo().noquote()
        << "REPORTS_WORKBENCH_NEGATIVE_PASS"
        << "no_connection_enable=rejected"
        << "inactive_disable=rejected"
        << "invalid_selection=rejected"
        << "strict_single_candidate=true"
        << "idle_poll_nonfatal=true";
    return 0;
}
