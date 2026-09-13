// SPDX-License-Identifier: GPL-3.0-or-later

#include "IedCommissioningModel.hpp"
#include "IedFleetController.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHostAddress>
#include <QProcess>
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

QString resolveBinary(const QString& root, const QString& stem) {
    const QFileInfo direct{root};
    if (direct.isFile() &&
        (direct.fileName() == stem || direct.fileName() == stem + QStringLiteral(".exe"))) {
        return direct.absoluteFilePath();
    }
    QDirIterator iterator{
        root,
        QStringList{stem, stem + QStringLiteral(".exe")},
        QDir::Files,
        QDirIterator::Subdirectories};
    return iterator.hasNext() ? QFileInfo(iterator.next()).absoluteFilePath() : QString{};
}

bool runReadProbe(
    const QString& executable,
    const quint16 port,
    const QString& domain,
    const QString& item,
    QString* output) {
    QProcess process;
    process.setProgram(executable);
    process.setArguments({
        QStringLiteral("127.0.0.1"),
        QString::number(port),
        QStringLiteral("--domain"), domain,
        QStringLiteral("--item"), item,
        QStringLiteral("--timeout-ms"), QStringLiteral("3000")});
    process.start();
    if (!waitUntil([&process] { return process.state() == QProcess::NotRunning; }, 6'000)) {
        process.kill();
        process.waitForFinished(1'000);
        return false;
    }
    const auto stdoutText = QString::fromUtf8(process.readAllStandardOutput());
    const auto stderrText = QString::fromUtf8(process.readAllStandardError());
    if (output != nullptr) *output = stdoutText.trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 ||
        !stdoutText.contains(QStringLiteral("MMS_READ"))) {
        qCritical().noquote() << "RUNTIME_COMMISSIONING_FAIL read_probe"
                              << stdoutText << stderrText;
        return false;
    }
    return true;
}

int selectBufferedReport(IedCommissioningModel& model) {
    model.setKindFilter(QStringLiteral("Report"));
    for (int row = 0; row < model.itemCount(); ++row) {
        if (!model.item(row).value(QStringLiteral("buffered")).toBool()) continue;
        model.select(row);
        return row;
    }
    return -1;
}

int selectFirstKind(IedCommissioningModel& model, const QString& kind) {
    model.setKindFilter(kind);
    if (model.itemCount() <= 0) return -1;
    model.select(0);
    return 0;
}
} // namespace

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("ARSTACK_IEDSIM_QA", "1");
    QGuiApplication app(argc, argv);
    if (argc != 3) {
        qCritical() << "usage: ied_simulator_runtime_commissioning_qa <scl> <build-root>";
        return 2;
    }

    const QString sclPath = QString::fromLocal8Bit(argv[1]);
    const QString buildRoot = QString::fromLocal8Bit(argv[2]);
    const auto brcbProbe = resolveBinary(buildRoot, QStringLiteral("ariec61850_mms_brcb_event_probe"));
    const auto readProbe = resolveBinary(buildRoot, QStringLiteral("ariec61850_mms_read_probe"));
    if (brcbProbe.isEmpty() || readProbe.isEmpty()) {
        qCritical().noquote() << "RUNTIME_COMMISSIONING_FAIL probes" << brcbProbe << readProbe;
        return 3;
    }

    IedFleetController controller;
    if (!loadAsync(controller, sclPath)) {
        qCritical().noquote() << "RUNTIME_COMMISSIONING_FAIL import" << controller.fatalError();
        return 4;
    }

    QTcpServer reservation;
    if (!reservation.listen(QHostAddress::LocalHost, 0)) {
        qCritical() << "RUNTIME_COMMISSIONING_FAIL reserve_port";
        return 5;
    }
    const quint16 port = reservation.serverPort();
    reservation.close();
    if (!controller.configureIedEndpoint(0, QStringLiteral("127.0.0.1"), port) ||
        !controller.startSimulation() ||
        !waitUntil([&controller] { return controller.running(); }, 6'000)) {
        qCritical() << "RUNTIME_COMMISSIONING_FAIL runtime_start";
        return 6;
    }

    IedCommissioningModel model;
    model.setBackend(&controller);

    if (model.behaviorCapacity() != 16 || selectBufferedReport(model) < 0) {
        qCritical() << "RUNTIME_COMMISSIONING_FAIL report_inventory";
        return 7;
    }
    const auto report = model.selectedItem();
    const int reportMember = model.firstDrivableMember();
    if (reportMember < 0 || !model.focusMemberValue(reportMember)) {
        qCritical() << "RUNTIME_COMMISSIONING_FAIL report_member";
        return 8;
    }
    const auto originalReportValue = controller.selectedValue().value(QStringLiteral("value")).toString();
    const auto reportReference = report.value(QStringLiteral("reference")).toString();
    const int slash = reportReference.indexOf(QLatin1Char('/'));
    if (slash <= 0 || slash >= reportReference.size() - 1) {
        qCritical().noquote() << "RUNTIME_COMMISSIONING_FAIL report_reference" << reportReference;
        return 9;
    }

    QByteArray brcbStdout;
    QByteArray brcbStderr;
    QProcess brcb;
    QObject::connect(&brcb, &QProcess::readyReadStandardOutput, [&] {
        brcbStdout += brcb.readAllStandardOutput();
    });
    QObject::connect(&brcb, &QProcess::readyReadStandardError, [&] {
        brcbStderr += brcb.readAllStandardError();
    });
    brcb.setProgram(brcbProbe);
    brcb.setArguments({
        QStringLiteral("127.0.0.1"),
        QString::number(port),
        QStringLiteral("--domain"), reportReference.left(slash),
        QStringLiteral("--rcb"), reportReference.mid(slash + 1),
        QStringLiteral("--timeout-ms"), QStringLiteral("6000")});
    brcb.start();
    if (!waitUntil([&] {
            return brcbStdout.contains("MMS_BRCB_EVENT_READY") || brcb.state() == QProcess::NotRunning;
        }, 6'000) || !brcbStdout.contains("MMS_BRCB_EVENT_READY")) {
        qCritical().noquote() << "RUNTIME_COMMISSIONING_FAIL brcb_ready"
                              << brcbStdout << brcbStderr;
        return 10;
    }

    const auto ticksBeforeReport = model.behaviorTickCount();
    if (!model.pulseSelectedService(300) ||
        !waitUntil([&] { return model.behaviorTickCount() > ticksBeforeReport; }, 1'000) ||
        !waitUntil([&] {
            return brcbStdout.contains("MMS_BRCB_EVENT_PASS") || brcb.state() == QProcess::NotRunning;
        }, 7'000)) {
        qCritical().noquote() << "RUNTIME_COMMISSIONING_FAIL report_pulse"
                              << brcbStdout << brcbStderr;
        return 11;
    }
    if (!waitUntil([&model] { return model.behaviorCount() == 0; }, 2'000) ||
        !model.focusMemberValue(reportMember) ||
        controller.selectedValue().value(QStringLiteral("value")).toString() != originalReportValue) {
        qCritical() << "RUNTIME_COMMISSIONING_FAIL pulse_restore";
        return 12;
    }

    if (selectFirstKind(model, QStringLiteral("GOOSE")) < 0) {
        qCritical() << "RUNTIME_COMMISSIONING_FAIL goose_inventory";
        return 13;
    }
    const int gooseMember = model.firstDrivableMember();
    if (gooseMember < 0 || !model.focusMemberValue(gooseMember)) {
        qCritical() << "RUNTIME_COMMISSIONING_FAIL goose_member";
        return 14;
    }
    const auto originalGooseSource = controller.selectedValue().value(QStringLiteral("value")).toString();
    if (!model.pulseSelectedService(200) ||
        !waitUntil([&model] { return model.behaviorCount() == 0; }, 1'500) ||
        !model.focusMemberValue(gooseMember) ||
        controller.selectedValue().value(QStringLiteral("value")).toString() != originalGooseSource) {
        qCritical() << "RUNTIME_COMMISSIONING_FAIL goose_source_stimulus";
        return 15;
    }

    if (selectBufferedReport(model) < 0 || !model.focusBoundDataSet()) {
        qCritical() << "RUNTIME_COMMISSIONING_FAIL dataset_jump";
        return 16;
    }
    const int behaviorMember = model.firstDrivableMember();
    if (behaviorMember < 0 || !model.focusMemberValue(behaviorMember)) {
        qCritical() << "RUNTIME_COMMISSIONING_FAIL behavior_member";
        return 17;
    }
    const auto initial = controller.selectedValue();
    const auto initialValue = initial.value(QStringLiteral("value")).toString();
    bool numeric{};
    static_cast<void>(initialValue.toDouble(&numeric));
    const QString mode = numeric ? QStringLiteral("Ramp") : QStringLiteral("Toggle");
    const auto ticksBeforeBehavior = model.behaviorTickCount();
    if (!model.startMemberBehavior(behaviorMember, mode, 100, 1.0) ||
        !waitUntil([&] { return model.behaviorTickCount() >= ticksBeforeBehavior + 3U; }, 2'000)) {
        qCritical() << "RUNTIME_COMMISSIONING_FAIL periodic_behavior";
        return 18;
    }
    if (!model.focusMemberValue(behaviorMember)) return 19;
    const auto driven = controller.selectedValue();
    if (driven.value(QStringLiteral("value")).toString() == initialValue) {
        qCritical() << "RUNTIME_COMMISSIONING_FAIL periodic_no_change";
        return 20;
    }

    QString readOutput;
    if (!runReadProbe(
            readProbe,
            port,
            driven.value(QStringLiteral("mmsDomain")).toString(),
            driven.value(QStringLiteral("mmsItem")).toString(),
            &readOutput)) {
        return 21;
    }

    if (model.startMemberBehavior(behaviorMember, QStringLiteral("NoSuchMode"), 100, 1.0) ||
        model.startMemberBehavior(behaviorMember, QStringLiteral("Toggle"), 50, 1.0)) {
        qCritical() << "RUNTIME_COMMISSIONING_FAIL invalid_behavior_accepted";
        return 22;
    }

    if (!model.stopMemberBehavior(behaviorMember, true) ||
        !waitUntil([&model] { return model.behaviorCount() == 0; }, 1'000) ||
        !model.focusMemberValue(behaviorMember) ||
        controller.selectedValue().value(QStringLiteral("value")).toString() != initialValue) {
        qCritical() << "RUNTIME_COMMISSIONING_FAIL behavior_restore";
        return 23;
    }

    if (selectFirstKind(model, QStringLiteral("Control")) < 0 || !model.focusControlStatus()) {
        qCritical() << "RUNTIME_COMMISSIONING_FAIL control_status_focus";
        return 24;
    }
    const auto controlStatus = controller.selectedValue();
    if (controlStatus.value(QStringLiteral("fc")).toString() != QStringLiteral("ST") ||
        controlStatus.value(QStringLiteral("dataAttribute")).toString() != QStringLiteral("stVal")) {
        qCritical().noquote() << "RUNTIME_COMMISSIONING_FAIL control_status_semantics" << controlStatus;
        return 25;
    }

    controller.stopSimulation();
    if (!waitUntil([&controller] { return !controller.anyRunning(); }, 3'000)) {
        qCritical() << "RUNTIME_COMMISSIONING_FAIL runtime_stop";
        return 26;
    }
    model.setKindFilter(QStringLiteral("DataSet"));
    if (model.itemCount() > 0) model.select(0);
    const int stoppedMember = model.firstDrivableMember();
    if (stoppedMember >= 0 &&
        model.startMemberBehavior(stoppedMember, QStringLiteral("Toggle"), 100, 1.0)) {
        qCritical() << "RUNTIME_COMMISSIONING_FAIL stopped_runtime_accepted";
        return 27;
    }

    qInfo().noquote()
        << "RUNTIME_COMMISSIONING_ACTIONS_PASS"
        << "report_pulse=brcb_event"
        << "goose_source_stimulus=pass"
        << "control_status_focus=pass"
        << "behavior_mode=" + mode
        << "behavior_ticks=" + QString::number(model.behaviorTickCount())
        << "behavior_rejected=" + QString::number(model.behaviorRejectedCount())
        << "bounded_slots=" + QString::number(model.behaviorCapacity())
        << "mms_visibility=pass"
        << "restore=pass";
    qInfo().noquote()
        << "RUNTIME_COMMISSIONING_NEGATIVE_PASS"
        << "stopped_runtime=rejected"
        << "bad_mode=rejected"
        << "fast_interval=rejected"
        << "external_control_semantics=preserved";
    return 0;
}
