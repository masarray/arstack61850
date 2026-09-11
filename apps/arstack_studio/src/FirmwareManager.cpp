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

#include <algorithm>

namespace {
constexpr auto kManifestName = "firmware-manifest.json";
constexpr auto kManifestSchema = "arstack.studio.firmware.v1";
constexpr auto kExpectedChip = "esp32p4";
constexpr auto kPreV3Policy = "pre-v3";
constexpr qsizetype kMaxOperationOutput = 65536;
constexpr qsizetype kTrimmedOperationOutput = 49152;

QString normalizedHash(const QString& text) {
    QString hash = text.trimmed().toLower();
    hash.remove(QLatin1Char(' '));
    return hash;
}
} // namespace

FirmwareManager::FirmwareManager(QObject* parent) : QObject(parent) {
    process_.setProcessChannelMode(QProcess::SeparateChannels);

    startupTimer_.setSingleShot(true);
    startupTimer_.setInterval(2500);
    connect(&startupTimer_, &QTimer::timeout, this, [this] {
        if (!busy_ || operation_ == Operation::none || process_.state() == QProcess::NotRunning) return;

        const Operation failedOperation = operation_;
        operation_ = Operation::none;
        busy_ = false;
        flashProgress_ = -1;
        process_.kill();

        if (failedOperation == Operation::reset) {
            setStatus(QStringLiteral("Firmware was written, but the reset tool did not start. Press RESET once or reconnect USB; Studio will verify the board."));
            emit stateChanged();
            emit installationFinished(false);
            return;
        }

        fail(QStringLiteral("Firmware tool did not start within 2.5 seconds."));
        bootloaderHelpNeeded_ = false;
        emit stateChanged();
        emit operationFailed(status_, false);
    });

    connect(&process_, &QProcess::started, this, [this] {
        startupTimer_.stop();
    });
    connect(&process_, &QProcess::errorOccurred,
            this, &FirmwareManager::handleProcessError);
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] {
        const QString text = QString::fromUtf8(process_.readAllStandardOutput());
        appendOperationOutput(text);
        updateProgressFromOutput(text);
        appendLog(text);
    });
    connect(&process_, &QProcess::readyReadStandardError, this, [this] {
        const QString text = QString::fromUtf8(process_.readAllStandardError());
        appendOperationOutput(text);
        updateProgressFromOutput(text);
        appendLog(text);
    });
    connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &FirmwareManager::finishOperation);
    refreshBundle();
}

bool FirmwareManager::parseEsp32P4Revision(const QString& output, int& major, int& minor) {
    static const QRegularExpression expression{
        QStringLiteral(R"(Chip\s+type:\s*ESP32[-_ ]?P4\s*\(revision\s+v(\d+)\.(\d+)\))"),
        QRegularExpression::CaseInsensitiveOption};
    const auto match = expression.match(output);
    if (!match.hasMatch()) {
        major = -1;
        minor = -1;
        return false;
    }
    major = match.captured(1).toInt();
    minor = match.captured(2).toInt();
    return true;
}

int FirmwareManager::parseFlashProgress(const QString& output) {
    static const QRegularExpression expression{QStringLiteral(R"((\d{1,3})\s*%)")};
    auto matches = expression.globalMatch(output);
    int latest = -1;
    while (matches.hasNext()) {
        const int value = matches.next().captured(1).toInt();
        if (value >= 0 && value <= 100) latest = value;
    }
    return latest;
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
    revisionPolicy_.clear();
    firmwareSha256_.clear();
    firmwareImagePath_.clear();
    flashProgress_ = -1;

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
    bundleStatus_ = QStringLiteral("Ready · firmware v%1 · ESP32-P4 pre-v3 · SHA-256 verified")
        .arg(firmwareVersion_);
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
    const QString revisionPolicy = object.value(QStringLiteral("chipRevisionPolicy")).toString().trimmed().toLower();
    const qint64 flashOffset = object.value(QStringLiteral("flashOffset")).toVariant().toLongLong();

    if (imageName.isEmpty() || QFileInfo(imageName).fileName() != imageName ||
        expectedHash.size() != 64 || version.isEmpty() || protocol < 1 || flashOffset != 0 ||
        revisionPolicy != QString::fromLatin1(kPreV3Policy)) {
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
    revisionPolicy_ = revisionPolicy;
    firmwareSha256_ = actualHash;
    firmwareImagePath_ = QDir::cleanPath(imagePath);
    return true;
}

bool FirmwareManager::probeTarget(const QString& portName) {
    if (busy_) return false;
    if (!bundleReady_ || !flasherAvailable_) refreshBundle();
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
    bootloaderHelpNeeded_ = false;
    flashProgress_ = -1;
    targetChip_ = QStringLiteral("Checking %1...").arg(port);
    busy_ = true;
    cancelRequested_ = false;
    setStatus(QStringLiteral("Checking ESP32-P4 ROM identity on %1...").arg(port));
    emit stateChanged();
    return startEspflash({
        QStringLiteral("board-info"),
        QStringLiteral("--non-interactive")}, Operation::probe);
}

bool FirmwareManager::installFirmware(const QString& portName) {
    if (busy_) return false;
    if (!bundleReady_ || !flasherAvailable_) refreshBundle();
    const QString port = portName.trimmed();
    if (!bundleReady_) {
        fail(bundleStatus_);
        return false;
    }
    if (!targetVerified_ || port.isEmpty() || port != selectedPort_) {
        fail(QStringLiteral("Check and verify a supported ESP32-P4 pre-v3 board before flashing."));
        return false;
    }

    bootloaderHelpNeeded_ = false;
    flashProgress_ = 0;
    busy_ = true;
    cancelRequested_ = false;
    setStatus(QStringLiteral("Installing ARStack firmware v%1 on %2...").arg(firmwareVersion_, port));
    emit stateChanged();
    return startEspflash({
        QStringLiteral("write-bin"),
        QStringLiteral("--chip"),
        QStringLiteral("esp32p4"),
        QStringLiteral("--non-interactive"),
        QStringLiteral("0x0"),
        firmwareImagePath_}, Operation::flash);
}

void FirmwareManager::cancel() {
    if (!busy_) return;
    cancelRequested_ = true;
    startupTimer_.stop();
    setStatus(QStringLiteral("Cancelling firmware operation..."));
    emit stateChanged();

    if (process_.state() == QProcess::NotRunning) {
        const Operation cancelledOperation = operation_;
        cancelRequested_ = false;
        operation_ = Operation::none;
        busy_ = false;
        flashProgress_ = -1;
        setStatus(QStringLiteral("Firmware operation cancelled."));
        emit stateChanged();
        if (cancelledOperation == Operation::reset) emit installationFinished(false);
        else emit operationFailed(status_, false);
        return;
    }
    process_.kill();
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
    environment.insert(QStringLiteral("ESPFLASH_SKIP_UPDATE_CHECK"), QStringLiteral("true"));
    process_.setProcessEnvironment(environment);
    process_.setProgram(flasherPath());
    process_.setArguments(arguments);
    appendLog(QStringLiteral("$ ESPFLASH_PORT=%1 espflash %2\n")
        .arg(selectedPort_, arguments.join(QLatin1Char(' '))));

    process_.start();
    startupTimer_.start();
    return true;
}

void FirmwareManager::handleProcessError(const QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart || operation_ == Operation::none) return;

    startupTimer_.stop();
    const Operation failedOperation = operation_;
    operation_ = Operation::none;
    busy_ = false;
    flashProgress_ = -1;
    cancelRequested_ = false;

    if (failedOperation == Operation::reset) {
        setStatus(QStringLiteral("Firmware was written, but the reset tool could not start. Press RESET once or reconnect USB; Studio will verify the board."));
        emit stateChanged();
        emit installationFinished(false);
        return;
    }

    fail(QStringLiteral("Unable to start bundled espflash: %1").arg(process_.errorString()));
    bootloaderHelpNeeded_ = false;
    emit stateChanged();
    emit operationFailed(status_, false);
}

void FirmwareManager::finishOperation(const int exitCode, const QProcess::ExitStatus exitStatus) {
    startupTimer_.stop();
    const Operation completed = operation_;
    operation_ = Operation::none;
    if (completed == Operation::none) return;

    if (cancelRequested_) {
        cancelRequested_ = false;
        busy_ = false;
        flashProgress_ = -1;
        setStatus(QStringLiteral("Firmware operation cancelled."));
        emit stateChanged();
        if (completed == Operation::reset) emit installationFinished(false);
        else emit operationFailed(status_, false);
        return;
    }

    const bool success = exitStatus == QProcess::NormalExit && exitCode == 0;

    if (completed == Operation::probe) {
        busy_ = false;
        int major = -1;
        int minor = -1;
        const bool p4WithRevision = success && parseEsp32P4Revision(operationOutput_, major, minor);
        const bool anyChipAnswered = success && operationOutput_.contains(
            QRegularExpression{QStringLiteral(R"(Chip\s+type:)"), QRegularExpression::CaseInsensitiveOption});
        const bool supportedRevision = p4WithRevision &&
            revisionPolicy_ == QString::fromLatin1(kPreV3Policy) && major < 3;

        targetVerified_ = supportedRevision;
        bootloaderHelpNeeded_ = !success;
        if (p4WithRevision) {
            targetChip_ = QStringLiteral("ESP32-P4 · revision v%1.%2").arg(major).arg(minor);
        } else if (anyChipAnswered) {
            targetChip_ = QStringLiteral("Different ESP chip detected");
        } else {
            targetChip_ = QStringLiteral("%1 · ROM not responding").arg(selectedPort_);
        }

        if (supportedRevision) {
            setStatus(QStringLiteral("ESP32-P4 revision v%1.%2 verified on %3. Ready to install ARStack firmware.")
                .arg(major).arg(minor).arg(selectedPort_));
        } else if (p4WithRevision) {
            setStatus(QStringLiteral("ESP32-P4 revision v%1.%2 is outside this firmware package (pre-v3). Installation is blocked.")
                .arg(major).arg(minor));
        } else if (anyChipAnswered) {
            setStatus(QStringLiteral("A chip answered on %1, but it is not an ESP32-P4. Installation is blocked.")
                .arg(selectedPort_));
        } else {
            setStatus(QStringLiteral("%1 is visible in Windows, but the ESP32-P4 ROM did not answer. Put the board in Download mode, then retry.")
                .arg(selectedPort_));
        }
        emit stateChanged();
        return;
    }

    if (completed == Operation::flash) {
        if (!success) {
            busy_ = false;
            flashProgress_ = -1;
            bootloaderHelpNeeded_ = true;
            fail(QStringLiteral("Firmware installation failed. Put the board in Download mode and retry. The device is not considered ready."));
            emit stateChanged();
            return;
        }
        if (flashProgress_ != 100) {
            flashProgress_ = 100;
            emit stateChanged();
        }
        bootloaderHelpNeeded_ = false;
        setStatus(QStringLiteral("Flash completed. Resetting ESP32-P4..."));
        emit stateChanged();
        if (!startEspflash({
                QStringLiteral("reset"),
                QStringLiteral("--non-interactive")}, Operation::reset)) {
            busy_ = false;
            emit installationFinished(false);
        }
        return;
    }

    if (completed == Operation::reset) {
        busy_ = false;
        if (success) {
            targetVerified_ = false;
            bootloaderHelpNeeded_ = false;
            targetChip_ = QStringLiteral("Awaiting ARStack firmware");
            setStatus(QStringLiteral("Firmware written successfully. Studio is reconnecting and verifying the board."));
        } else {
            bootloaderHelpNeeded_ = false;
            setStatus(QStringLiteral("Firmware was written, but automatic reset failed. Press RESET once or reconnect USB; Studio will verify the board."));
        }
        emit stateChanged();
        emit installationFinished(success);
    }
}

void FirmwareManager::updateProgressFromOutput(const QString& text) {
    if (operation_ != Operation::flash || text.isEmpty()) return;
    const int latest = parseFlashProgress(text);
    if (latest < 0 || flashProgress_ == latest) return;
    flashProgress_ = std::clamp(latest, 0, 100);
    emit stateChanged();
}

void FirmwareManager::appendOperationOutput(const QString& text) {
    if (text.isEmpty()) return;
    operationOutput_ += text;
    if (operationOutput_.size() > kMaxOperationOutput) {
        operationOutput_ = operationOutput_.right(kTrimmedOperationOutput);
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
