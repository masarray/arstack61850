// SPDX-License-Identifier: GPL-3.0-or-later

#include "IedCommissioningModel.hpp"
#include "IedFleetController.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
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

bool loadAsync(IedFleetController& controller, const QString& path, const int timeoutMs = 15'000) {
    if (!controller.loadFileAsync(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()))) return false;
    if (!waitUntil([&controller] { return !controller.importing(); }, timeoutMs)) return false;
    return controller.imported() && controller.fatalError().isEmpty();
}

int findRow(IedCommissioningModel& model, const QString& kind, const QString& name) {
    for (int row = 0; row < model.itemCount(); ++row) {
        const auto item = model.item(row);
        if (item.value(QStringLiteral("kind")).toString() == kind &&
            item.value(QStringLiteral("name")).toString() == name) {
            return row;
        }
    }
    return -1;
}

int runPositive(const QString& path) {
    IedFleetController controller;
    if (!loadAsync(controller, path)) {
        qCritical().noquote() << "COMMISSIONING_DEPTH_FAIL import" << controller.fatalError();
        return 10;
    }

    IedCommissioningModel model;
    model.setBackend(&controller);
    if (model.dataSetCount() != 2 || model.reportCount() != 2 ||
        model.gooseCount() != 1 || model.controlCount() != 4 || model.itemCount() != 9) {
        qCritical().noquote()
            << "COMMISSIONING_DEPTH_FAIL counts"
            << model.dataSetCount() << model.reportCount()
            << model.gooseCount() << model.controlCount() << model.itemCount();
        return 11;
    }

    model.setKindFilter(QStringLiteral("Report"));
    if (model.itemCount() != 2) {
        qCritical() << "COMMISSIONING_DEPTH_FAIL report_filter";
        return 12;
    }
    const int urcbRow = findRow(model, QStringLiteral("Report"), QStringLiteral("URCB01"));
    const int brcbRow = findRow(model, QStringLiteral("Report"), QStringLiteral("BRCB01"));
    if (urcbRow < 0 || brcbRow < 0) {
        qCritical() << "COMMISSIONING_DEPTH_FAIL report_inventory";
        return 13;
    }
    const auto urcb = model.item(urcbRow);
    const auto brcb = model.item(brcbRow);
    if (urcb.value(QStringLiteral("buffered")).toBool() ||
        !brcb.value(QStringLiteral("buffered")).toBool() ||
        urcb.value(QStringLiteral("memberCount")).toInt() != 3 ||
        brcb.value(QStringLiteral("memberCount")).toInt() != 3 ||
        urcb.value(QStringLiteral("status")).toString() != QStringLiteral("Bound")) {
        qCritical() << "COMMISSIONING_DEPTH_FAIL report_metadata";
        return 14;
    }

    model.select(urcbRow);
    if (model.memberCount() != 3 || model.member(0).value(QStringLiteral("reference")).toString().isEmpty()) {
        qCritical() << "COMMISSIONING_DEPTH_FAIL report_members";
        return 15;
    }
    if (!model.focusBoundDataSet()) {
        qCritical() << "COMMISSIONING_DEPTH_FAIL report_dataset_jump";
        return 16;
    }
    const auto boundDataSet = model.selectedItem();
    if (boundDataSet.value(QStringLiteral("kind")).toString() != QStringLiteral("DataSet") ||
        boundDataSet.value(QStringLiteral("name")).toString() != QStringLiteral("dsGO") ||
        model.memberCount() != 3) {
        qCritical() << "COMMISSIONING_DEPTH_FAIL dataset_selection";
        return 17;
    }

    model.setKindFilter(QStringLiteral("GOOSE"));
    if (model.itemCount() != 1) {
        qCritical() << "COMMISSIONING_DEPTH_FAIL goose_filter";
        return 18;
    }
    const auto goose = model.item(0);
    if (goose.value(QStringLiteral("name")).toString() != QStringLiteral("GCB01") ||
        goose.value(QStringLiteral("goId")).toString() != QStringLiteral("trip-goose") ||
        goose.value(QStringLiteral("memberCount")).toInt() != 3 ||
        goose.value(QStringLiteral("appId")).toString() != QStringLiteral("1001") ||
        goose.value(QStringLiteral("vlanId")).toInt() != 100 ||
        goose.value(QStringLiteral("minTimeMs")).toULongLong() != 4ULL ||
        goose.value(QStringLiteral("maxTimeMs")).toULongLong() != 1000ULL) {
        qCritical().noquote() << "COMMISSIONING_DEPTH_FAIL goose_metadata" << goose;
        return 19;
    }

    model.setKindFilter(QStringLiteral("Control"));
    model.setFilterText(QStringLiteral("sbo-with-enhanced-security"));
    if (model.itemCount() != 1) {
        qCritical() << "COMMISSIONING_DEPTH_FAIL control_filter";
        return 20;
    }
    const auto control = model.item(0);
    if (control.value(QStringLiteral("name")).toString() != QStringLiteral("SPCSO4") ||
        control.value(QStringLiteral("cdc")).toString() != QStringLiteral("SPC") ||
        control.value(QStringLiteral("controlModel")).toString() !=
            QStringLiteral("sbo-with-enhanced-security")) {
        qCritical() << "COMMISSIONING_DEPTH_FAIL control_metadata";
        return 21;
    }

    model.setKindFilter(QStringLiteral("All"));
    model.setFilterText(QStringLiteral("trip-goose"));
    if (model.itemCount() != 1 ||
        model.item(0).value(QStringLiteral("kind")).toString() != QStringLiteral("GOOSE")) {
        qCritical() << "COMMISSIONING_DEPTH_FAIL service_search";
        return 22;
    }

    qInfo().noquote()
        << "COMMISSIONING_DEPTH_PASS datasets=2 reports=2 goose=1 controls=4"
           " dataset_members=3 report_members=3 goose_members=3 appid=1001 vlan=100"
           " virtualized_on_demand=1";
    return 0;
}

int runNegative(const QString& path) {
    IedFleetController controller;
    if (!loadAsync(controller, path)) {
        qCritical().noquote() << "COMMISSIONING_NEGATIVE_FAIL import" << controller.fatalError();
        return 30;
    }

    IedCommissioningModel model;
    model.setBackend(&controller);
    model.setKindFilter(QStringLiteral("Report"));
    if (model.itemCount() != 1) {
        qCritical() << "COMMISSIONING_NEGATIVE_FAIL report_count";
        return 31;
    }
    const auto report = model.item(0);
    model.select(0);
    if (report.value(QStringLiteral("status")).toString() != QStringLiteral("Unresolved") ||
        model.memberCount() != 0 || model.focusBoundDataSet()) {
        qCritical().noquote() << "COMMISSIONING_NEGATIVE_FAIL unresolved_report" << report;
        return 32;
    }

    model.setKindFilter(QStringLiteral("GOOSE"));
    if (model.itemCount() != 1 || model.item(0).value(QStringLiteral("memberCount")).toInt() != 0) {
        qCritical() << "COMMISSIONING_NEGATIVE_FAIL unresolved_goose";
        return 33;
    }

    qInfo().noquote()
        << "COMMISSIONING_NEGATIVE_PASS unresolved_report=visible member_count=0"
           " dataset_jump=rejected unresolved_goose=visible";
    return 0;
}
} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    if (argc != 3) {
        qCritical() << "usage: ied_simulator_commissioning_qa <commissioning.scd> <unresolved.scd>";
        return 2;
    }
    qputenv("ARSTACK_IEDSIM_QA", "1");
    const int positive = runPositive(QString::fromLocal8Bit(argv[1]));
    if (positive != 0) return positive;
    return runNegative(QString::fromLocal8Bit(argv[2]));
}
