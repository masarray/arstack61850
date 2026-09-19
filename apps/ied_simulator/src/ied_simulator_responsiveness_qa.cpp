// SPDX-License-Identifier: GPL-3.0-or-later

#include "IedFleetController.hpp"
#include "IedSignalModel.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHostAddress>
#include <QThread>
#include <QTimer>
#include <QTcpServer>
#include <QUrl>

#include <algorithm>
#include <cmath>
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

bool loadAsync(IedFleetController& controller, const QString& path, const int timeoutMs) {
    if (!controller.loadFileAsync(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()))) return false;
    if (!waitUntil([&controller] { return !controller.importing(); }, timeoutMs)) return false;
    return controller.imported() && controller.fatalError().isEmpty();
}

int runResponsiveness(const QString& path) {
    IedFleetController controller;

    qint64 maxHeartbeatGapMs{};
    QElapsedTimer heartbeatElapsed;
    heartbeatElapsed.start();
    QTimer heartbeat;
    heartbeat.setInterval(10);
    QObject::connect(&heartbeat, &QTimer::timeout, [&] {
        const auto gap = heartbeatElapsed.restart();
        maxHeartbeatGapMs = std::max(maxHeartbeatGapMs, gap);
    });
    heartbeat.start();

    if (!loadAsync(controller, path, 90'000)) {
        qCritical().noquote() << "IEDSIM_RESPONSIVENESS_FAIL import" << controller.fatalError();
        return 20;
    }

    const int iedCount = controller.ieds().size();
    if (iedCount <= 0 || controller.preparedIedCount() != iedCount) {
        qCritical().noquote()
            << "IEDSIM_RESPONSIVENESS_FAIL prepared_ieds=" << controller.preparedIedCount()
            << "ieds=" << iedCount;
        return 21;
    }

    qint64 maxSwitchMs{};
    for (int index = 0; index < iedCount; ++index) {
        QElapsedTimer switchElapsed;
        switchElapsed.start();
        controller.selectIed(index);
        maxSwitchMs = std::max(maxSwitchMs, switchElapsed.elapsed());
        if (!controller.hasPreparedValueIndex()) {
            qCritical().noquote() << "IEDSIM_RESPONSIVENESS_FAIL unprepared_switch=" << index;
            return 22;
        }
    }
    controller.selectIed(0);

    IedSignalModel model;
    model.setBackend(&controller);
    const auto& navigation = controller.navigationIndexView();
    if (navigation.isEmpty()) {
        qCritical() << "IEDSIM_RESPONSIVENESS_FAIL empty_navigation";
        return 23;
    }
    const auto scope = navigation.constFirst().toMap();
    model.setLogicalDevice(scope.value(QStringLiteral("logicalDevice")).toString());
    model.setLogicalNode(scope.value(QStringLiteral("logicalNode")).toString());
    if (!waitUntil([&model] { return model.rowCount() > 0; }, 2'000)) {
        qCritical() << "IEDSIM_RESPONSIVENESS_FAIL empty_projection";
        return 24;
    }
    QThread::msleep(100);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    model.resetPerformanceCounters();
    QElapsedTimer burstCall;
    burstCall.start();
    const int applied = controller.qaBurstValues(5'000, std::min(64, controller.valueCount()));
    const qint64 burstCallMs = burstCall.elapsed();
    if (applied != 5'000) {
        qCritical().noquote() << "IEDSIM_RESPONSIVENESS_FAIL burst_applied=" << applied;
        return 25;
    }
    if (!waitUntil([&model] { return model.refreshFlushCount() >= 1; }, 2'000)) {
        qCritical() << "IEDSIM_RESPONSIVENESS_FAIL no_refresh_flush";
        return 26;
    }
    QThread::msleep(40);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    const auto requests = model.refreshRequestCount();
    const auto flushes = model.refreshFlushCount();
    const double coalescing = flushes == 0
        ? 0.0
        : static_cast<double>(requests) / static_cast<double>(flushes);

    const auto* firstPoint = controller.valueRecord(0);
    if (firstPoint == nullptr) {
        qCritical() << "IEDSIM_RESPONSIVENESS_FAIL no_point";
        return 27;
    }
    model.resetPerformanceCounters();
    model.setFilterText(firstPoint->dataObject);
    QThread::msleep(120);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 250);
    const qint64 filterRebuildMs = model.maxRebuildMilliseconds();

    heartbeat.stop();
    const bool passes =
        requests >= 5'000 &&
        flushes > 0 && flushes <= 8 &&
        coalescing >= 500.0 &&
        maxSwitchMs <= 50 &&
        burstCallMs <= 250 &&
        model.maxRefreshLatencyMilliseconds() <= 150 &&
        filterRebuildMs <= 500 &&
        maxHeartbeatGapMs <= 250;

    qInfo().noquote()
        << QStringLiteral(
               "IEDSIM_RESPONSIVENESS_%1 ieds=%2 prepared=%3 switch_max_ms=%4 "
               "burst_updates=%5 burst_call_ms=%6 refresh_requests=%7 refresh_flushes=%8 "
               "coalescing=%9 refresh_max_ms=%10 filter_rebuild_max_ms=%11 heartbeat_max_gap_ms=%12")
               .arg(passes ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
               .arg(iedCount)
               .arg(controller.preparedIedCount())
               .arg(maxSwitchMs)
               .arg(applied)
               .arg(burstCallMs)
               .arg(requests)
               .arg(flushes)
               .arg(coalescing, 0, 'f', 1)
               .arg(model.maxRefreshLatencyMilliseconds())
               .arg(filterRebuildMs)
               .arg(maxHeartbeatGapMs);
    return passes ? 0 : 28;
}

int runFleetRollback(const QString& path) {
    IedFleetController controller;
    if (!loadAsync(controller, path, 15'000) || controller.ieds().size() < 2) {
        qCritical() << "FLEET_PARTIAL_START_RECOVERY_FAIL import_or_ied_count";
        return 30;
    }
    if (controller.preparedIedCount() != controller.ieds().size()) {
        qCritical() << "FLEET_PARTIAL_START_RECOVERY_FAIL profiles_not_prepared";
        return 31;
    }

    QTcpServer blocker;
    if (!blocker.listen(QHostAddress(QStringLiteral("127.0.0.1")), 0)) {
        qCritical() << "FLEET_PARTIAL_START_RECOVERY_FAIL reserve_port";
        return 32;
    }
    const int port = static_cast<int>(blocker.serverPort());
    if (!controller.configureIedEndpoint(0, QStringLiteral("127.0.0.1"), port) ||
        !controller.configureIedEndpoint(1, QStringLiteral("127.0.0.2"), port)) {
        qCritical() << "FLEET_PARTIAL_START_RECOVERY_FAIL configure";
        return 33;
    }
    for (int index = 2; index < controller.ieds().size(); ++index) controller.setIedEnabled(index, false);

    const int launched = controller.startAllSimulations();
    if (launched <= 0) {
        qCritical() << "FLEET_PARTIAL_START_RECOVERY_FAIL no_launch";
        return 34;
    }
    if (!waitUntil(
            [&controller] {
                return controller.fleetStartRollbackCount() > 0 && !controller.anyRunning();
            },
            10'000)) {
        qCritical().noquote()
            << "FLEET_PARTIAL_START_RECOVERY_FAIL timeout rollback="
            << controller.fleetStartRollbackCount()
            << "running=" << controller.runningCount();
        return 35;
    }

    qInfo().noquote()
        << QStringLiteral("FLEET_PARTIAL_START_RECOVERY_PASS rollback_count=%1 port=%2 launched=%3")
               .arg(controller.fleetStartRollbackCount())
               .arg(port)
               .arg(launched);
    return 0;
}

int runNonblockingClear(const QString& path) {
    IedFleetController controller;
    if (!loadAsync(controller, path, 15'000)) {
        qCritical() << "NONBLOCKING_CLEAR_FAIL import";
        return 40;
    }

    QTcpServer reservation;
    if (!reservation.listen(QHostAddress(QStringLiteral("127.0.0.1")), 0)) {
        qCritical() << "NONBLOCKING_CLEAR_FAIL reserve_port";
        return 41;
    }
    const int port = static_cast<int>(reservation.serverPort());
    reservation.close();
    if (!controller.configureIedEndpoint(0, QStringLiteral("127.0.0.1"), port) ||
        !controller.startSimulation()) {
        qCritical() << "NONBLOCKING_CLEAR_FAIL start";
        return 42;
    }
    if (!waitUntil([&controller] { return controller.running(); }, 8'000)) {
        qCritical() << "NONBLOCKING_CLEAR_FAIL readiness";
        return 43;
    }

    QElapsedTimer callElapsed;
    callElapsed.start();
    controller.clear();
    const qint64 callMs = callElapsed.elapsed();
    if (callMs > 50) {
        qCritical().noquote() << "NONBLOCKING_CLEAR_FAIL call_ms=" << callMs;
        return 44;
    }
    if (!controller.clearPending()) {
        qCritical() << "NONBLOCKING_CLEAR_FAIL pending_not_set";
        return 45;
    }
    if (!waitUntil([&controller] { return !controller.imported() && !controller.clearPending(); }, 5'000)) {
        qCritical() << "NONBLOCKING_CLEAR_FAIL completion";
        return 46;
    }

    qInfo().noquote()
        << QStringLiteral("NONBLOCKING_CLEAR_PASS call_ms=%1 port=%2").arg(callMs).arg(port);
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("ARSTACK_IEDSIM_QA", "1");
    QGuiApplication app(argc, argv);

    const auto args = QCoreApplication::arguments();
    if (args.size() != 3) {
        qCritical() << "Usage: ied_simulator_responsiveness_qa --responsiveness|--fleet-rollback|--clear-nonblocking <SCL>";
        return 2;
    }
    if (args.at(1) == QStringLiteral("--responsiveness")) return runResponsiveness(args.at(2));
    if (args.at(1) == QStringLiteral("--fleet-rollback")) return runFleetRollback(args.at(2));
    if (args.at(1) == QStringLiteral("--clear-nonblocking")) return runNonblockingClear(args.at(2));
    qCritical() << "Unknown QA mode:" << args.at(1);
    return 2;
}
