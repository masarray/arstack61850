// SPDX-License-Identifier: GPL-3.0-or-later
#include "IedFleetController.hpp"
#include "MmsClientController.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QTcpServer>
#include <QThread>
#include <QUrl>

#include <functional>
#include <iostream>

namespace {
bool waitFor(const std::function<bool()>& predicate, const int timeoutMs) {
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

quint16 reservePort() {
    QTcpServer socket;
    if (!socket.listen(QHostAddress::LocalHost, 0)) return 0;
    const auto port = socket.serverPort();
    socket.close();
    return port;
}

QString selectedValue(MmsClientController& client) {
    return client.treeModel()->selectedNode().value(QStringLiteral("value")).toString();
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        std::cerr << "Usage: ied_mms_client_workbench_qa <fixture.scd>\n";
        return 2;
    }

    const auto port = reservePort();
    if (port == 0) {
        std::cerr << "Could not reserve a loopback test port.\n";
        return 3;
    }

    IedFleetController simulator;
    simulator.setListenAddress(QStringLiteral("127.0.0.1"));
    simulator.setPort(port);
    if (!simulator.loadFile(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])))) {
        std::cerr << "Fixture import failed.\n";
        return 4;
    }
    if (!simulator.startSimulation() || !waitFor([&] { return simulator.running(); }, 8'000)) {
        std::cerr << "Simulator did not start: " << simulator.fatalError().toStdString() << '\n';
        return 5;
    }

    MmsClientController client;
    client.setHost(QStringLiteral("127.0.0.1"));
    client.setPort(port);
    if (!client.connectToIed() || !waitFor([&] { return client.connected(); }, 12'000)) {
        std::cerr << "Client connection/discovery failed: " << client.lastError().toStdString() << '\n';
        simulator.stopSimulation();
        return 6;
    }

    if (client.logicalDeviceCount() < 1 || client.logicalNodeCount() < 2 || client.dataAttributeCount() < 4) {
        std::cerr << "Live model projection is incomplete.\n";
        simulator.stopSimulation();
        return 7;
    }
    if (!client.treeModel()->selectMmsItem(
            QStringLiteral("CLIENTLD0"), QStringLiteral("GGIO1$SP$SetPoint$setVal"))) {
        std::cerr << "Writable set point was not projected.\n";
        simulator.stopSimulation();
        return 8;
    }

    const auto selected = client.treeModel()->selectedNode();
    if (selected.value(QStringLiteral("functionalConstraint")).toString() != QStringLiteral("SP") ||
        selected.value(QStringLiteral("mmsType")).toString() != QStringLiteral("integer") ||
        !selected.value(QStringLiteral("writable")).toBool()) {
        std::cerr << "Exact-type guarded Write policy did not classify the SP integer correctly.\n";
        simulator.stopSimulation();
        return 9;
    }

    if (!client.readSelected() || !waitFor([&] { return !client.operationBusy(); }, 6'000)) {
        std::cerr << "Selected Read did not complete.\n";
        simulator.stopSimulation();
        return 10;
    }
    const auto before = selectedValue(client);
    if (before != QStringLiteral("7")) {
        std::cerr << "Unexpected initial Read value: " << before.toStdString() << '\n';
        simulator.stopSimulation();
        return 11;
    }

    if (!client.writeSelected(QStringLiteral("9")) ||
        !waitFor([&] { return !client.operationBusy(); }, 6'000)) {
        std::cerr << "Guarded Write did not complete: " << client.lastError().toStdString() << '\n';
        simulator.stopSimulation();
        return 12;
    }
    const auto after = selectedValue(client);
    if (after != QStringLiteral("9")) {
        std::cerr << "Write verification Read did not observe 9; got " << after.toStdString() << '\n';
        simulator.stopSimulation();
        return 13;
    }

    std::cout << "MMS_CLIENT_WORKBENCH_PASS"
              << " ld=" << client.logicalDeviceCount()
              << " ln=" << client.logicalNodeCount()
              << " do=" << client.dataObjectCount()
              << " da=" << client.dataAttributeCount()
              << " read=" << before.toStdString()
              << " write_verified=" << after.toStdString()
              << " exact_type=integer fc=SP persistent_association=pass\n";

    const auto staleGeneration = client.generation();
    if (!client.readSelected()) {
        std::cerr << "Could not start stale-session negative Read.\n";
        simulator.stopSimulation();
        return 14;
    }
    client.disconnectFromIed();
    const auto currentGeneration = client.generation();
    if (currentGeneration <= staleGeneration || !waitFor([&] { return !client.operationBusy(); }, 1'000)) {
        std::cerr << "Disconnect did not invalidate the session generation.\n";
        simulator.stopSimulation();
        return 15;
    }
    QThread::msleep(100);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (client.connected() || client.treeModel()->totalNodeCount() != 0 || !client.treeModel()->selectedNode().isEmpty()) {
        std::cerr << "Stale session state was applied after disconnect.\n";
        simulator.stopSimulation();
        return 16;
    }

    std::cout << "MMS_CLIENT_STALE_SESSION_NEGATIVE_PASS"
              << " stale_generation=" << staleGeneration
              << " current_generation=" << currentGeneration
              << " stale_state_not_applied=true bounded_io_worker=1\n";

    simulator.stopSimulation();
    waitFor([&] { return !simulator.anyRunning(); }, 4'000);
    return 0;
}
