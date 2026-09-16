// SPDX-License-Identifier: GPL-3.0-or-later

#include "DeviceIoWorker.hpp"
#include "SingleInstanceGuard.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>

namespace {
constexpr int kHolderSeconds = 30;
constexpr int kProcessTimeoutMs = 5000;

QString argumentValue(const QStringList& arguments, const QString& name) {
    const qsizetype index = arguments.indexOf(name);
    if (index < 0 || index + 1 >= arguments.size()) return {};
    return arguments.at(index + 1).trimmed();
}

int runLockHolder(const QString& lockPath) {
    if (lockPath.isEmpty()) return 30;
    SingleInstanceGuard guard{lockPath};
    if (!guard.tryAcquire()) return 31;
    std::fputs("LOCKED\n", stdout);
    std::fflush(stdout);
    QThread::sleep(kHolderSeconds);
    return 0;
}

int runLockProbe(const QString& lockPath) {
    if (lockPath.isEmpty()) return 32;
    SingleInstanceGuard guard{lockPath};
    return guard.tryAcquire() ? 0 : SingleInstanceGuard::contentionExitCode();
}

bool waitForHolderReady(QProcess& process) {
    QByteArray output;
    for (int elapsed = 0; elapsed < kProcessTimeoutMs; elapsed += 100) {
        if (process.waitForReadyRead(100)) output += process.readAllStandardOutput();
        if (output.contains("LOCKED")) return true;
        if (process.state() == QProcess::NotRunning) return false;
    }
    output += process.readAllStandardOutput();
    return output.contains("LOCKED");
}

int runProbeProcess(const QString& program, const QString& lockPath) {
    QProcess process;
    process.setProgram(program);
    process.setArguments({QStringLiteral("--lock-probe"), lockPath});
    process.start();
    if (!process.waitForStarted(kProcessTimeoutMs)) return -100;
    if (!process.waitForFinished(kProcessTimeoutMs)) {
        process.kill();
        process.waitForFinished(kProcessTimeoutMs);
        return -101;
    }
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -102;
}

bool serialTransportLossPolicy() {
    using Error = QSerialPort::SerialPortError;
    return DeviceIoWorker::serialErrorForcesTransportLoss(Error::ResourceError) &&
        DeviceIoWorker::serialErrorForcesTransportLoss(Error::DeviceNotFoundError) &&
        DeviceIoWorker::serialErrorForcesTransportLoss(Error::PermissionError) &&
        DeviceIoWorker::serialErrorForcesTransportLoss(Error::ReadError) &&
        DeviceIoWorker::serialErrorForcesTransportLoss(Error::WriteError) &&
        !DeviceIoWorker::serialErrorForcesTransportLoss(Error::NoError) &&
        !DeviceIoWorker::serialErrorForcesTransportLoss(Error::OpenError) &&
        !DeviceIoWorker::serialErrorForcesTransportLoss(Error::TimeoutError) &&
        !DeviceIoWorker::serialErrorForcesTransportLoss(Error::UnknownError);
}

bool fastUsbDiscoveryPolicy() {
    const int ch343Exact = DeviceIoWorker::portConfidenceForMetadata(
        true, 0x1A86U, true, 0x55D3U,
        QStringLiteral("USB-Enhanced-SERIAL CH343"), {}, {});
    const int ch343ByName = DeviceIoWorker::portConfidenceForMetadata(
        false, 0U, false, 0U,
        QStringLiteral("USB-Enhanced-SERIAL CH343"), {}, {});
    const int bluetooth = DeviceIoWorker::portConfidenceForMetadata(
        false, 0U, false, 0U,
        QStringLiteral("Standard Serial over Bluetooth link"), {}, {});
    const int generic = DeviceIoWorker::portConfidenceForMetadata(
        false, 0U, false, 0U, QStringLiteral("USB Serial Port"), {}, {});
    const int espressif = DeviceIoWorker::portConfidenceForMetadata(
        true, 0x303AU, false, 0U, QStringLiteral("USB JTAG/serial debug unit"),
        QStringLiteral("Espressif"), {});

    return ch343Exact >= 60 && ch343ByName >= 60 && espressif >= 60 &&
        ch343Exact > generic && ch343ByName > generic &&
        ch343Exact > bluetooth && bluetooth < generic;
}

bool crossProcessOwnershipAndCrashRecovery() {
    QTemporaryDir temp;
    if (!temp.isValid()) return false;

    const QString lockPath = QDir(temp.path()).filePath(QStringLiteral("windows-ownership.lock"));
    const QString program = QCoreApplication::applicationFilePath();

    QProcess holder;
    holder.setProgram(program);
    holder.setArguments({QStringLiteral("--lock-holder"), lockPath});
    holder.setProcessChannelMode(QProcess::SeparateChannels);
    holder.start();
    if (!holder.waitForStarted(kProcessTimeoutMs) || !waitForHolderReady(holder)) {
        if (holder.state() != QProcess::NotRunning) {
            holder.kill();
            holder.waitForFinished(kProcessTimeoutMs);
        }
        return false;
    }

    const int contenderExit = runProbeProcess(program, lockPath);
    const bool contenderBlocked = contenderExit == SingleInstanceGuard::contentionExitCode();

    // Model a hard process crash rather than a graceful destructor path. The
    // next process must recognize the dead owner and reclaim QLockFile safely.
    holder.kill();
    if (!holder.waitForFinished(kProcessTimeoutMs)) return false;

    const int recoveryExit = runProbeProcess(program, lockPath);
    const bool staleLockRecovered = recoveryExit == 0;

    return contenderBlocked && staleLockRecovered;
}
} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();

    if (arguments.contains(QStringLiteral("--lock-holder"))) {
        return runLockHolder(argumentValue(arguments, QStringLiteral("--lock-holder")));
    }
    if (arguments.contains(QStringLiteral("--lock-probe"))) {
        return runLockProbe(argumentValue(arguments, QStringLiteral("--lock-probe")));
    }

    const bool serialPolicy = serialTransportLossPolicy();
    const bool fastDiscovery = fastUsbDiscoveryPolicy();
    const bool processOwnership = crossProcessOwnershipAndCrashRecovery();

    qInfo().noquote()
        << "S8B Windows ownership:"
        << (serialPolicy ? "PASS" : "FAIL")
        << "· Permission/Resource/DeviceNotFound/Read/Write errors force transport loss";
    qInfo().noquote()
        << "S8B Windows discovery:"
        << (fastDiscovery ? "PASS" : "FAIL")
        << "· CH343/Espressif USB outrank generic and Bluetooth COM ports; IDENTIFY remains authoritative";
    qInfo().noquote()
        << "S8B Windows ownership:"
        << (processOwnership ? "PASS" : "FAIL")
        << "· second process blocked with exit 23 and stale lock reclaimed after hard crash";

    if (!serialPolicy || !fastDiscovery || !processOwnership) {
        qCritical().noquote() << "S8B Windows ownership/discovery integration harness: FAIL";
        return 22;
    }

    qInfo().noquote()
        << "S8B Windows ownership/discovery integration harness: PASS · fast CH343 discovery + OS process lock + crash recovery + serial transport-loss policy locked";
    return 0;
}
