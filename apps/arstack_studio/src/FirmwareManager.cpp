// SPDX-License-Identifier: GPL-3.0-or-later

#include "FirmwareManager.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>

namespace {
constexpr auto kManifestName = "firmware-manifest.json";
constexpr auto kManifestSchema = "arstack.studio.firmware.v1";
constexpr auto kExpectedChip = "esp32p4";

QString normalizedHash(const QString& text) {
    QString hash = text.trimmed().toLower();
    hash.remove(QLatin1Char(' '));
    return hash;
}
} // namespace

FirmwareManager::FirmwareManager(QObject* parent) : QObject(parent) {
    process_.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] {
        const QString text = QString::fromUtf8(process_.readAllStandardOutput());
        operationOutput_ += text;
        appendLog(text);
    });
    connect(&process_, &QProcess::readyReadStandardError, this, [this] {
        const QString text = QString::fromUtf8(process_.readAllStandardError());
        operationOutput_ += text;
        appendLog(text);
    });
    connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &FirmwareManager::finishOperation);
    refreshBundle();
}

QString FirmwareManager::bundleRoot() const {
    const QString overridePath = qEnvironmentVariable("ARSTACK_STUDIO_FIRMWARE_DIR").trimmed();
    if (!overridePath.isEmpty()) return QDir::cleanPath(overridePath);
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("firmware"));
}

QString FirmwareManager::flasherPath() const {
    const QString overridePath = qEnvironmentVariable("ARSTACK_STUDIO_ESPFLASH").trimmed();
    if (!overridePath.isEmpty()) return QDir::cleanPath(overridePath);
#ifdef Q_OS_WIN
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tools/espflash.exe"));
#else
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tools/espflash"));
#endif
}

void FirmwareManager::refreshBundle() {
    if (busy_) return;
    bundleReady_ = false;
    firmwareVersion_ = QStringLiteral("-");
    expectedProtocol_ = QStringLiteral("-");
    firmwareSha256_.clear();
    firmwareImagePath_.clear();

    const QFileInfo flasherInfo{flasherPath()};
    flasherAvailable_ = flasherInfo.exists() && flasherInfo.isFile() && flasherInfo.isExecutable();
    if (!loadManifest()) {
        emit stateChanged();
        return;
    }
    if (!flasherAvailable_) {
        bundleStatus_ = QStringLiteral("Firmware image is valid, but bundled espflash is missing.");
        emit stateChanged();
        return;
    }
    bundleReady_ = true;
    bundleStatus_ = QStringLiteral("Ready · firmware v%1 · SHA-256 verified").arg(firmwareVersion_);
    emit stateChanged();
}

bool FirmwareManager::loadManifest() {
    const QDir root{bundleRoot()};
    QFile manifest{root.filePath(QString::fromLatin1(kManifestName))};
    if (!manifest.open(QIODevice::ReadOnly)) {
        bundleStatus_ = QStringLiteral("No release firmware package is installed with this build.");
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        bundleStatus_ = QStringLiteral("Firmware manifest is invalid JSON.");
        return false;
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schema")).toString() != QString::fromLatin1(kManifestSchema) ||
        object.value(QStringLiteral("chip")).toString().compare(QString::fromLatin1(kExpectedChip), Qt::CaseInsensitive) != 0) {
        bundleStatus_ = QStringLiteral("Firmware manifest target/schema is incompatible with ARStack Studio.");
        return false;
    }

    const QString imageName = object.value(QStringLiteral("image")).toString().trimmed();
    const QString expectedHash = normalizedHash(object.value(QStringLiteral("sha256")).toString());
    const int protocol = object.value(QStringLiteral("protocol")).toInt(-1);
    const QString version = object.value(QStringLiteral("version")).toString().trimmed();
    const qint64 flashOffset = object.value(QStringLiteral("flashOffset")).toVariant().toLongLong();

    if (imageName.isEmpty() || QFileInfo(imageName).fileName() != imageName ||
        expectedHash.size() != 64 || version.isEmpty() || protocol < 1 || flashOffset != 0) {
        bundleStatus_ = QStringLiteral("Firmware manifest fields are incomplete or unsafe.");
        return false;
    }

    const QString imagePath = root.filePath(imageName);
    QFile image{imagePath};
    if (!image.open(QIODevice::ReadOnly)) {
        bundleStatus_ = QStringLiteral("Firmware image referenced by the manifest is missing.");
        return false;
    }

    QCryptographicHash hasher{QCryptographicHash::Sha256};
    if (!hasher.addData(&image)) {
        bundleStatus_ = QStringLiteral("Unable to hash the firmware image.");
        return false;
    }
    const QString actualHash = QString::fromLatin1(hasher.result().toHex()).toLower();
    if (actualHash != expectedHash) {
        bundleStatus_ = QStringLiteral("Firmware SHA-256 mismatch. Installation is blocked.");
        return false;
    }

    firmwareVersion_ = version;
    expectedProtocol_ = QString::number(protocol);
    firmwareSha256_ = actualHash;
    firmwareImagePath_ = QDir::cleanPath(imagePath);
    return true;
}

bool FirmwareManager::probeTarget(const QString& portName) {
    if (busy_) return false;
    refreshBundle();
    if (!flasherAvailable_) {
        fail(QStringLiteral("Bundled espflash executable is unavailable."));
        return false;
    }
    const QString port = portName.trimmed();
    if (port.isEmpty()) {
        fail(QStringLiteral("Select the ESP32-P4 USB serial port first."));
        return false;
    }

    selectedPort_ = port;
    targetVerified_ = false;
    targetChip_ = QStringLiteral("Probing...");
    busy_ = true;
    setStatus(QStringLiteral("Reading target identity from %1...").arg(port));
    emit stateChanged();
    return startEspflash({QStringLiteral("--skip-update-check"), QStringLiteral("board-info")}, Operation::probe);
}

bool FirmwareManager::installFirmware(const QString& portName) {
    if (busy_) return false;
    refreshBundle();
    const QString port = portName.trimmed();
    if (!bundleReady_) {
        fail(bundleStatus_);
        return false;
    }
    if (!targetVerified_ || port.isEmpty() || port != selectedPort_) {
        fail(QStringLiteral("Probe and verify this ESP32-P4 port before flashing."));
        return false;
    }

    busy_ = true;
    setStatus(QStringLiteral("Installing ARStack firmware v%1...").arg(firmwareVersion_));
    emit stateChanged();
    return startEspflash({
        QStringLiteral("--skip-update-check"),
        QStringLiteral("write-bin"),
        QStringLiteral("--chip"),
        QStringLiteral("esp32p4"),
        QStringLiteral("0x0"),
        firmwareImagePath_}, Operation::flash);
}

void FirmwareManager::cancel() {
    if (!busy_) return;
    process_.kill();
    process_.waitForFinished(1200);
    operation_ = Operation::none;
    busy_ = false;
    setStatus(QStringLiteral("Firmware operation cancelled."));
    emit stateChanged();
}

void FirmwareManager::clearLog() {
    if (logText_.isEmpty()) return;
    logText_.clear();
    emit logChanged();
}

bool FirmwareManager::startEspflash(const QStringList& arguments, const Operation operation) {
    if (process_.state() != QProcess::NotRunning) {
        fail(QStringLiteral("Another firmware process is already running."));
        return false;
    }

    operation_ = operation;
    operationOutput_.clear();
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("ESPFLASH_PORT"), selectedPort_);
    environment.insert(QStringLiteral("ESPFLASH_SKIP_UPDATE_CHECK"), QStringLiteral("1"));
    process_.setProcessEnvironment(environment);
    process_.setProgram(flasherPath());
    process_.setArguments(arguments);
    appendLog(QStringLiteral("$ espflash %1\n").arg(arguments.join(QLatin1Char(' '))));
    process_.start();
    if (!process_.waitForStarted(2500)) {
        fail(QStringLiteral("Unable to start bundled espflash."));
        operation_ = Operation::none;
        busy_ = false;
        emit stateChanged();
        return false;
    }
    return true;
}

void FirmwareManager::finishOperation(const int exitCode, const QProcess::ExitStatus exitStatus) {
    const bool success = exitStatus == QProcess::NormalExit && exitCode == 0;
    const Operation completed = operation_;
    operation_ = Operation::none;

    if (completed == Operation::probe) {
        busy_ = false;
        const QString lower = operationOutput_.toLower();
        const bool p4 = success && (lower.contains(QStringLiteral("esp32-p4")) ||
                                    lower.contains(QStringLiteral("esp32p4")));
        targetVerified_ = p4;
        targetChip_ = p4 ? QStringLiteral("ESP32-P4") : QStringLiteral("Unsupported / unknown target");
        setStatus(p4
            ? QStringLiteral("ESP32-P4 verified on %1. Firmware installation is unlocked.").arg(selectedPort_)
            : QStringLiteral("Target verification failed. Only ESP32-P4 is accepted."));
        emit stateChanged();
        return;
    }

    if (completed == Operation::flash) {
        if (!success) {
            busy_ = false;
            fail(QStringLiteral("Firmware flash failed. Device contents were not trusted."));
            emit stateChanged();
            return;
        }
        setStatus(QStringLiteral("Flash completed. Resetting ESP32-P4..."));
        emit stateChanged();
        if (!startEspflash({QStringLiteral("--skip-update-check"), QStringLiteral("reset")}, Operation::reset)) {
            busy_ = false;
            emit installationFinished(false);
        }
        return;
    }

    if (completed == Operation::reset) {
        busy_ = false;
        if (success) {
            targetVerified_ = false;
            targetChip_ = QStringLiteral("Awaiting firmware IDENTIFY");
            setStatus(QStringLiteral("Firmware installed and reset. ARStack Studio will verify IDENTIFY next."));
        } else {
            setStatus(QStringLiteral("Flash succeeded, but automatic reset failed. Replug the board and verify it."));
        }
        emit stateChanged();
        emit installationFinished(success);
    }
}

void FirmwareManager::appendLog(const QString& text) {
    if (text.isEmpty()) return;
    const QString stamped = QStringLiteral("[%1] %2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), text);
    logText_ += stamped;
    constexpr qsizetype kMax = 60000;
    if (logText_.size() > kMax) logText_ = logText_.right(48000);
    emit logChanged();
}

void FirmwareManager::setStatus(const QString& text) {
    if (status_ == text) return;
    status_ = text;
    emit stateChanged();
}

void FirmwareManager::fail(const QString& text) {
    setStatus(text);
    appendLog(QStringLiteral("ERROR: %1\n").arg(text));
}
