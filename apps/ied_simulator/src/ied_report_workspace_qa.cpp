// SPDX-License-Identifier: GPL-3.0-or-later

#include "IedFleetController.hpp"
#include "IedReportControlManifest.hpp"
#include "MmsReportController.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHostAddress>
#include <QSet>
#include <QStringList>
#include <QTcpServer>
#include <QThread>
#include <QUrl>

#include <functional>

namespace {
bool isReferenceClientIndexedFixture(const QString& sclPath) {
    return QFileInfo(sclPath).fileName() == QStringLiteral("reference_client-indexed-reports.scd");
}

bool verifyIndexedRcbManifestExpansion() {
    using ar::iec61850::scl::SclReportControl;

    QSet<QString> emitted;
    SclReportControl brcb;
    brcb.ied_name = "AA1E1F06R4";
    brcb.ld_inst = "Application";
    brcb.logical_node_path = "LLN0";
    brcb.name = "Buffer";
    brcb.data_set_name = "dsDin";
    brcb.buffered = true;
    brcb.indexed = true;
    brcb.max_clients = 2U;
    brcb.configuration_revision = 1U;
    brcb.buffer_time_milliseconds = 100U;
    brcb.integrity_period_milliseconds = 10'000U;

    const auto brcbLines = arstack::iedsim::reportControlManifestLines(
        brcb,
        QStringLiteral("AA1E1F06R4"),
        emitted);
    const bool brcbExpanded =
        brcbLines.count('\n') == 2 &&
        brcbLines.contains("\tLLN0$BR$Buffer01\t1\t") &&
        brcbLines.contains("\tLLN0$BR$Buffer02\t1\t") &&
        !brcbLines.contains("\tLLN0$BR$Buffer\t1\t");

    emitted.clear();
    SclReportControl urcb = brcb;
    urcb.name = "Unbuffer";
    urcb.buffered = false;
    const auto urcbLines = arstack::iedsim::reportControlManifestLines(
        urcb,
        QStringLiteral("AA1E1F06R4"),
        emitted);
    const bool urcbExpanded =
        urcbLines.count('\n') == 2 &&
        urcbLines.contains("\tLLN0$RP$Unbuffer01\t0\t") &&
        urcbLines.contains("\tLLN0$RP$Unbuffer02\t0\t") &&
        !urcbLines.contains("\tLLN0$RP$Unbuffer\t0\t");

    emitted.clear();
    SclReportControl nonIndexed = urcb;
    nonIndexed.name = "Static";
    nonIndexed.indexed = false;
    nonIndexed.max_clients = 4U;
    const auto nonIndexedLines = arstack::iedsim::reportControlManifestLines(
        nonIndexed,
        QStringLiteral("AA1E1F06R4"),
        emitted);
    const bool nonIndexedStable =
        nonIndexedLines.count('\n') == 1 &&
        nonIndexedLines.contains("\tLLN0$RP$Static\t0\t") &&
        !nonIndexedLines.contains("LLN0$RP$Static01");

    return brcbExpanded && urcbExpanded && nonIndexedStable;
}

bool verifyReferenceClientIndexedInventory(
    const QString& sclPath,
    const QVariantList& reportControls) {
    if (!isReferenceClientIndexedFixture(sclPath)) return true;

    QSet<QString> references;
    bool allConcreteInstancesReadable = true;
    for (const auto& value : reportControls) {
        const auto item = value.toMap();
        const auto reference = item.value(QStringLiteral("reference")).toString();
        if (reference.isEmpty()) continue;
        references.insert(reference);

        const bool buffered = item.value(QStringLiteral("buffered")).toBool();
        const auto expectedReportId = buffered
            ? QStringLiteral("GOLDEN01LD0/LLN0$BR$Buffer")
            : QStringLiteral("GOLDEN01LD0/LLN0$RP$Unbuffer");
        allConcreteInstancesReadable = allConcreteInstancesReadable &&
            item.value(QStringLiteral("probeOk")).toBool() &&
            !item.value(QStringLiteral("dataSet")).toString().isEmpty() &&
            item.value(QStringLiteral("reportId")).toString() == expectedReportId;
    }

    static const QSet<QString> expected{
        QStringLiteral("GOLDEN01LD0/LLN0.Buffer01"),
        QStringLiteral("GOLDEN01LD0/LLN0.Buffer02"),
        QStringLiteral("GOLDEN01LD0/LLN0.Unbuffer01"),
        QStringLiteral("GOLDEN01LD0/LLN0.Unbuffer02")};
    return references == expected && allConcreteInstancesReadable;
}

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

int selectEligibleRcb(
    MmsReportController& reports,
    const bool buffered,
    const QString& exactReference = {}) {
    const auto controls = reports.reportControls();
    for (int row = 0; row < controls.size(); ++row) {
        const auto item = controls.at(row).toMap();
        if (item.value(QStringLiteral("buffered")).toBool() != buffered) continue;
        if (!exactReference.isEmpty() &&
            item.value(QStringLiteral("reference")).toString() != exactReference) {
            continue;
        }
        if (!item.value(QStringLiteral("probeOk")).toBool()) continue;
        if (item.value(QStringLiteral("dataSet")).toString().isEmpty()) continue;
        if (!reports.selectRcb(row)) continue;
        if (!reports.selectedDataSetMembers().isEmpty()) return row;
    }
    return -1;
}

bool reportProjectionPresent(const MmsReportController& reports) {
    const auto frames = reports.receivedReports();
    if (frames.isEmpty()) return false;
    const auto frame = frames.first().toMap();
    return !frame.value(QStringLiteral("reportId")).toString().isEmpty() &&
        !frame.value(QStringLiteral("dataSet")).toString().isEmpty() &&
        !frame.value(QStringLiteral("values")).toStringList().isEmpty() &&
        frame.contains(QStringLiteral("entryId")) &&
        frame.contains(QStringLiteral("overflow"));
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

    if (!verifyIndexedRcbManifestExpansion()) {
        qCritical() << "REPORTS_WORKBENCH_FAIL indexed_rcb_manifest";
        return 20;
    }

    MmsReportController negative;
    const bool noConnectionEnableRejected = !negative.enableSelected(true);
    const bool inactiveDisableRejected = !negative.disableSelected();
    const bool invalidSelectionRejected = !negative.selectRcb(0) && !negative.selectDataSet(0);
    const bool authoredEnableRejected = !negative.enableSelectedAuthored(
        QStringLiteral("LD0/LLN0.Test"),
        {QStringLiteral("data-change")},
        {QStringLiteral("sequence-number")},
        false);
    const bool dynamicCreateRejected = !negative.createDynamicDataSet(
        QStringLiteral("LD0/LLN0.Test"), {});
    const bool dynamicDeleteRejected = !negative.deleteDynamicDataSet(
        QStringLiteral("LD0/LLN0.Test"));
    if (!noConnectionEnableRejected || !inactiveDisableRejected ||
        !invalidSelectionRejected || !authoredEnableRejected ||
        !dynamicCreateRejected || !dynamicDeleteRejected) {
        qCritical() << "REPORTS_WORKBENCH_FAIL negative_preconditions";
        return 3;
    }

    IedFleetController simulator;
    const QString sclPath = QString::fromLocal8Bit(argv[1]);
    const bool referenceClientFixture = isReferenceClientIndexedFixture(sclPath);
    const QString exactUrcb = referenceClientFixture
        ? QStringLiteral("GOLDEN01LD0/LLN0.Unbuffer01")
        : QString{};
    const QString exactBrcb = referenceClientFixture
        ? QStringLiteral("GOLDEN01LD0/LLN0.Buffer01")
        : QString{};
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
    for (const auto& value : dataSets) {
        const auto item = value.toMap();
        if (item.value(QStringLiteral("dynamicOwned")).toBool() ||
            !item.value(QStringLiteral("immutable")).toBool()) {
            qCritical() << "REPORTS_WORKBENCH_FAIL static_dataset_mutability";
            return 27;
        }
    }
    if (!reports.ownedDynamicDataSets().isEmpty()) {
        qCritical() << "REPORTS_WORKBENCH_FAIL unexpected_dynamic_ownership";
        return 28;
    }
    if (!verifyReferenceClientIndexedInventory(sclPath, reportControls)) {
        QStringList actual;
        for (const auto& value : reportControls) {
            const auto item = value.toMap();
            actual.push_back(
                item.value(QStringLiteral("reference")).toString() +
                QStringLiteral("|probe=") +
                (item.value(QStringLiteral("probeOk")).toBool() ? QStringLiteral("ok") : QStringLiteral("fail")) +
                QStringLiteral("|rptID=") + item.value(QStringLiteral("reportId")).toString());
        }
        qCritical().noquote() << "REPORTS_WORKBENCH_FAIL reference_client_indexed_inventory"
                              << actual.join(QLatin1Char(','));
        return 21;
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

    const int selected = selectEligibleRcb(reports, false, exactUrcb);
    if (selected < 0 || reports.selectedDataSetMembers().isEmpty()) {
        QStringList controls;
        for (const auto& value : reportControls) {
            const auto item = value.toMap();
            controls.push_back(
                item.value(QStringLiteral("reference")).toString() +
                QStringLiteral("|probe=") +
                (item.value(QStringLiteral("probeOk")).toBool()
                    ? QStringLiteral("ok")
                    : QStringLiteral("fail")) +
                QStringLiteral("|dataset=") +
                item.value(QStringLiteral("dataSet")).toString() +
                QStringLiteral("|error=") +
                item.value(QStringLiteral("probeError")).toString());
        }
        QStringList sets;
        for (const auto& value : dataSets) {
            const auto item = value.toMap();
            sets.push_back(
                item.value(QStringLiteral("reference")).toString() +
                QStringLiteral("|members=") +
                QString::number(item.value(QStringLiteral("memberCount")).toInt()) +
                QStringLiteral("|directory=") +
                (item.value(QStringLiteral("directoryAvailable")).toBool()
                    ? QStringLiteral("yes")
                    : QStringLiteral("no")));
        }
        qCritical().noquote()
            << "REPORTS_WORKBENCH_FAIL eligible_urcb"
            << "controls=" + controls.join(QLatin1Char(','))
            << "datasets=" + sets.join(QLatin1Char(','));
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
    if (!reportProjectionPresent(reports)) {
        qCritical() << "REPORTS_WORKBENCH_FAIL report_projection";
        return 13;
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

    // Reconnect while the URCB is still active. The reconnect path must clean
    // up RptEna/Resv before replacing the association. Reacquiring the same
    // URCB and receiving a fresh GI is direct evidence that no zombie ownership
    // was left on the server by the interrupted session.
    if (!reports.reconnect() ||
        !waitUntil([&reports] { return reports.connected() && !reports.busy(); }, 12'000) ||
        reports.cleanupRequired()) {
        qCritical().noquote() << "REPORTS_WORKBENCH_FAIL active_reconnect" << reports.lastError();
        return 16;
    }
    if (selectEligibleRcb(reports, false, exactUrcb) < 0 ||
        !reports.enableSelected(true) ||
        !waitUntil([&reports] { return reports.active() && !reports.busy(); }, 8'000) ||
        !waitUntil([&reports] { return reports.receivedReportCount() > 0; }, 5'000)) {
        qCritical().noquote() << "REPORTS_WORKBENCH_FAIL reconnect_reacquire" << reports.lastError();
        return 17;
    }

    if (!reports.disableSelected() ||
        !waitUntil([&reports] { return !reports.active() && !reports.busy(); }, 6'000) ||
        reports.cleanupRequired()) {
        qCritical().noquote() << "REPORTS_WORKBENCH_FAIL cleanup" << reports.lastError();
        return 18;
    }

    if (selectEligibleRcb(reports, false, exactUrcb) < 0) {
        qCritical() << "REPORTS_WORKBENCH_FAIL authored_urcb_select";
        return 29;
    }
    const auto authoredDataSet =
        reports.selectedRcb().value(QStringLiteral("dataSet")).toString();
    const QStringList authoredTriggers{
        QStringLiteral("data-change"),
        QStringLiteral("quality-change"),
        QStringLiteral("integrity"),
        QStringLiteral("general-interrogation")};
    const QStringList authoredOptional{
        QStringLiteral("sequence-number"),
        QStringLiteral("report-time-stamp"),
        QStringLiteral("reason-for-inclusion"),
        QStringLiteral("data-set-name"),
        QStringLiteral("data-reference"),
        QStringLiteral("configuration-revision")};
    if (!reports.enableSelectedAuthored(
            authoredDataSet,
            authoredTriggers,
            authoredOptional,
            true) ||
        !waitUntil([&reports] { return reports.active() && !reports.busy(); }, 8'000) ||
        !waitUntil([&reports] { return reports.receivedReportCount() > 0; }, 5'000)) {
        qCritical().noquote() << "REPORTS_WORKBENCH_FAIL authored_static_enable"
                              << reports.lastError()
                              << reports.diagnosticsText();
        return 30;
    }
    if (!reports.disableSelected() ||
        !waitUntil([&reports] { return !reports.active() && !reports.busy(); }, 6'000) ||
        reports.cleanupRequired()) {
        qCritical().noquote() << "REPORTS_WORKBENCH_FAIL authored_static_cleanup"
                              << reports.lastError();
        return 31;
    }

    qulonglong brcbGiReports = 0;
    if (referenceClientFixture) {
        if (selectEligibleRcb(reports, true, exactBrcb) < 0 ||
            reports.selectedDataSetMembers().isEmpty()) {
            qCritical() << "REPORTS_WORKBENCH_FAIL eligible_brcb";
            return 22;
        }
        if (!reports.enableSelected(true) ||
            !waitUntil([&reports] { return reports.active() && !reports.busy(); }, 8'000)) {
            qCritical().noquote() << "REPORTS_WORKBENCH_FAIL brcb_enable_gi" << reports.lastError();
            return 23;
        }
        if (!waitUntil([&reports] { return reports.receivedReportCount() > 0; }, 5'000)) {
            qCritical().noquote() << "REPORTS_WORKBENCH_FAIL brcb_gi_report"
                                  << reports.diagnosticsText();
            return 24;
        }
        brcbGiReports = reports.receivedReportCount();
        if (!reportProjectionPresent(reports)) {
            qCritical() << "REPORTS_WORKBENCH_FAIL brcb_report_projection";
            return 25;
        }
        if (!reports.disableSelected() ||
            !waitUntil([&reports] { return !reports.active() && !reports.busy(); }, 6'000) ||
            reports.cleanupRequired()) {
            qCritical().noquote() << "REPORTS_WORKBENCH_FAIL brcb_cleanup" << reports.lastError();
            return 26;
        }
    }

    reports.disconnectFromIed();
    simulator.stopSimulation();
    if (!waitUntil([&simulator] { return !simulator.anyRunning(); }, 4'000)) {
        qCritical() << "REPORTS_WORKBENCH_FAIL simulator_stop";
        return 19;
    }

    qInfo().noquote()
        << "REPORTS_WORKBENCH_PASS"
        << "datasets=" + QString::number(dataSets.size())
        << "rcbs=" + QString::number(reportControls.size())
        << "static_candidates=" + QString::number(staticCandidates.size())
        << "dynamic_candidates=" + QString::number(dynamicCandidates.size())
        << "members=" + QString::number(memberCount)
        << "urcb_gi_reports=" + QString::number(reportCountAfterGi)
        << "brcb_gi_reports=" + QString::number(brcbGiReports)
        << "rcb_manifest_instances=pass"
        << QStringLiteral("reference_client_indexed_inventory=%1").arg(referenceClientFixture ? QStringLiteral("pass") : QStringLiteral("n/a"))
        << "urcb=pass"
        << QStringLiteral("brcb_gi=%1").arg(referenceClientFixture ? QStringLiteral("pass") : QStringLiteral("n/a"))
        << "brcb_inventory=pass"
        << "entryid_indicator=pass"
        << "cleanup=pass"
        << "active_reconnect_cleanup=pass"
        << "authored_static_rcb=pass"
        << "static_dataset_immutable=pass";
    qInfo().noquote()
        << "REPORTS_WORKBENCH_NEGATIVE_PASS"
        << "no_connection_enable=rejected"
        << "inactive_disable=rejected"
        << "invalid_selection=rejected"
        << "strict_single_candidate=true"
        << "idle_poll_nonfatal=true"
        << "reconnect_reacquire=true"
        << "dynamic_offline_actions=rejected";
    return 0;
}
