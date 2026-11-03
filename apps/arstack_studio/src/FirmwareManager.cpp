// SPDX-License-Identifier: GPL-3.0-or-later

#include "FirmwareManager.hpp"

#include "FirmwareWorker.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSemaphore>
#include <QSharedPointer>

#include <algorithm>

namespace {
constexpr auto kManifestName = "firmware-manifest.json";
constexpr auto kManifestSchema = "arstack.studio.firmware.v1";
constexpr auto kExpectedChip = "esp32p4";
constexpr auto kPreV3Policy = "pre-v3";
constexpr auto kV3Policy = "v3.x";
constexpr auto kCatalogName = "firmware-catalog.json";
constexpr auto kCatalogSchema = "arstack.studio.firmware.catalog.v2";
constexpr qint64 kMaxFirmwareSize = 16 * 1024 * 1024;
constexpr qsizetype kMaxOperationOutput = 65536;
constexpr qsizetype kTrimmedOperationOutput = 49152;
constexpr qsizetype kMaxProgressOutputTail = 2048;
constexpr qsizetype kTrimmedProgressOutputTail = 1024;

QString normalizedHash(const QString& text) {
    QString hash = text.trimmed().toLower();
    hash.remove(QLatin1Char(' '));
    return hash;
}

QString normalizedTerminalProgress(QString text) {
    // espflash renders progress as terminal output. QProcess can deliver ANSI
    // control sequences, carriage-return rewrites and arbitrary chunk splits.
    // Strip presentation controls here; the full operation log remains intact.
    static const QRegularExpression ansiCsi{
        QStringLiteral(R"(\x1B\[[0-?]*[ -/]*[@-~])")};
    text.remove(ansiCsi);
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text.remove(QLatin1Char('\b'));
    return text;
}
} // namespace

FirmwareManager::FirmwareManager(QObject* parent) : QObject(parent) {
    worker_ = new FirmwareWorker;
    worker_->moveToThread(&workerThread_);
    workerThread_.setObjectName(QStringLiteral("ARStackFirmwareWorker"));
    connect(&workerThread_, &QThread::started, worker_, &FirmwareWorker::initialize);
    connect(&workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
    connectWorkerSignals();
    workerThread_.start();
    refreshBundle();
}

FirmwareManager::~FirmwareManager() {
    static_cast<void>(shutdown());
}

void FirmwareManager::connectWorkerSignals() {
    connect(worker_, &FirmwareWorker::ready, this, [this](const bool affinityValid) {
        workerReady_ = true;
        workerAffinityValid_ = affinityValid;
        emit stateChanged();
    });

    connect(worker_, &FirmwareWorker::outputReady, this,
            [this](const quint64 generation, const QString& text) {
        if (!workerEventIsCurrent(activeOperationGeneration_, generation)) return;
        appendOperationOutput(text);
        updateProgressFromOutput(text);
        appendLog(text);
    });

    connect(worker_, &FirmwareWorker::operationFinished, this,
            [this](const quint64 generation, const int exitCode, const bool normalExit) {
        if (!workerEventIsCurrent(activeOperationGeneration_, generation)) return;
        finishOperation(exitCode, normalExit);
    });

    connect(worker_, &FirmwareWorker::operationRejected, this,
            [this](const quint64 generation, const QString& message) {
        if (!workerEventIsCurrent(activeOperationGeneration_, generation)) return;
        handleOperationRejected(message);
    });

    connect(worker_, &FirmwareWorker::operationLaunchFailed, this,
            [this](const quint64 generation, const QString& message) {
        if (!workerEventIsCurrent(activeOperationGeneration_, generation)) return;
        handleLaunchFailure(message, false);
    });

    connect(worker_, &FirmwareWorker::operationLaunchTimedOut, this,
            [this](const quint64 generation) {
        if (!workerEventIsCurrent(activeOperationGeneration_, generation)) return;
        handleLaunchFailure({}, true);
    });

    connect(worker_, &FirmwareWorker::operationTimedOut, this,
            [this](const quint64 generation) {
        if (!workerEventIsCurrent(activeOperationGeneration_, generation)) return;
        handleOperationTimeout();
    });

    connect(worker_, &FirmwareWorker::operationCancelled, this,
            [this](const quint64 generation) {
        if (!workerEventIsCurrent(activeOperationGeneration_, generation)) return;
        const Operation cancelledOperation = operation_;
        operation_ = Operation::none;
        activeOperationGeneration_ = 0;
        cancelRequested_ = false;
        busy_ = false;
        flashProgress_ = -1;
        progressOutputTail_.clear();
        setStatus(QStringLiteral("Firmware operation cancelled."));
        emit stateChanged();
        if (cancelledOperation == Operation::reset) emit installationFinished(false);
        else emit operationFailed(status_, false);
    });
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

bool FirmwareManager::supportsEsp32P4Revision(const int major, const int minor) noexcept {
    return major >= 0 && major < 3 && minor >= 0;
}

bool FirmwareManager::revisionInRange(const int major, const int minor,
                                      const int minRevision, const int maxRevision) noexcept {
    if (major < 0 || minor < 0 || minor >= 100 || major > 99 ||
        minRevision < 0 || maxRevision < minRevision) return false;
    const int revision = major * 100 + minor;
    return revision >= minRevision && revision <= maxRevision;
}

int FirmwareManager::parseFlashProgress(const QString& output) {
    // Keep this parser pure so deterministic fixtures exercise the exact
    // production token rules. Stream reassembly remains bounded in
    // updateProgressFromOutput(). espflash has used both percentage output and
    // indicatif-style "current/total segment ..." progress bars.
    const QString normalized = normalizedTerminalProgress(output);
    int latest = -1;

    static const QRegularExpression percentExpression{
        QStringLiteral(R"((?<![\d.])(\d{1,3})\s*%)")};
    auto percentMatches = percentExpression.globalMatch(normalized);
    while (percentMatches.hasNext()) {
        const int value = percentMatches.next().captured(1).toInt();
        if (value >= 0 && value <= 100) latest = value;
    }

    static const QRegularExpression segmentExpression{
        QStringLiteral(R"((?<!\d)(\d{1,9})\s*/\s*(\d{1,9})\s+segment\b)"),
        QRegularExpression::CaseInsensitiveOption};
    auto segmentMatches = segmentExpression.globalMatch(normalized);
    while (segmentMatches.hasNext()) {
        const auto match = segmentMatches.next();
        bool currentOk = false;
        bool totalOk = false;
        const qint64 current = match.captured(1).toLongLong(&currentOk);
        const qint64 total = match.captured(2).toLongLong(&totalOk);
        if (!currentOk || !totalOk || total <= 0 || current < 0 || current > total) continue;
        const int value = static_cast<int>((current * 100 + total / 2) / total);
        latest = std::clamp(value, 0, 100);
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
    if (busy_ || shuttingDown_) return;
    bundleReady_ = false;
    firmwareVersion_ = QStringLiteral("-");
    firmwareBuildId_.clear();
    expectedProtocol_ = QStringLiteral("-");
    revisionPolicy_.clear();
    firmwareSha256_.clear();
    firmwareImagePath_.clear();
    revisionImages_.clear();
    revisionCatalogAvailable_ = false;
    targetVerified_ = false;
    flashProgress_ = -1;
    progressOutputTail_.clear();

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
    bundleStatus_ = revisionCatalogAvailable_
        ? QStringLiteral("Ready · pre-v3 + v3.2 preview · firmware v%1 · build %2 · SHA-256 verified")
              .arg(firmwareVersion_, firmwareBuildId_)
        : QStringLiteral("Ready · firmware v%1 · build %2 · ESP32-P4 pre-v3 · PTP-P2 · SHA-256 verified")
              .arg(firmwareVersion_, firmwareBuildId_);
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
    const QString sourceCommit = object.value(QStringLiteral("sourceCommit")).toString().trimmed().toLower();
    const QString buildId = sourceCommit.left(16);
    const QString revisionPolicy = object.value(QStringLiteral("chipRevisionPolicy")).toString().trimmed().toLower();
    const qint64 flashOffset = object.value(QStringLiteral("flashOffset")).toVariant().toLongLong();
    const QJsonArray capabilities = object.value(QStringLiteral("capabilities")).toArray();
    const auto hasCapability = [&capabilities](const QString& wanted) {
        for (const auto& value : capabilities) {
            if (value.toString().compare(wanted, Qt::CaseInsensitive) == 0) return true;
        }
        return false;
    };
    const bool productionCapabilities =
        hasCapability(QStringLiteral("SMV-4I4V")) &&
        hasCapability(QStringLiteral("PROFILE")) &&
        hasCapability(QStringLiteral("PROFILE-BINARY-V1")) &&
        hasCapability(QStringLiteral("LIVE-SETPOINTS")) &&
        hasCapability(QStringLiteral("SV-LAN-AUTONOMY-V1")) &&
        hasCapability(QStringLiteral("PTP-P2")) &&
        hasCapability(QStringLiteral("SMPSYNCH-AUTO"));

    if (imageName.isEmpty() || QFileInfo(imageName).fileName() != imageName ||
        expectedHash.size() != 64 || version.isEmpty() || protocol < 1 || flashOffset != 0 ||
        !QRegularExpression(QStringLiteral("^[0-9a-f]{16}$")).match(buildId).hasMatch() ||
        !QRegularExpression(QStringLiteral("^[0-9a-f]{40}$")).match(sourceCommit).hasMatch() ||
        revisionPolicy != QString::fromLatin1(kPreV3Policy) || !productionCapabilities) {
        bundleStatus_ = QStringLiteral("Firmware manifest fields/capabilities are incomplete or unsafe.");
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
    firmwareBuildId_ = buildId;
    expectedProtocol_ = QString::number(protocol);
    revisionPolicy_ = revisionPolicy;
    firmwareSha256_ = actualHash;
    firmwareImagePath_ = QDir::cleanPath(imagePath);
    return loadRevisionCatalog(root, imageName, actualHash, sourceCommit);
}

bool FirmwareManager::loadRevisionCatalog(
    const QDir& root, const QString& legacyImage,
    const QString& legacyHash, const QString& sourceCommit) {
    const QFileInfo catalogInfo{root.filePath(QString::fromLatin1(kCatalogName))};
    if (!catalogInfo.exists()) return true; // Immutable historical v1 bundles remain pre-v3 only.
    QFile catalog{catalogInfo.absoluteFilePath()};
    if (catalogInfo.isSymLink() || catalogInfo.size() <= 0 || catalogInfo.size() > 65536 ||
        !catalog.open(QIODevice::ReadOnly)) {
        bundleStatus_ = QStringLiteral("Firmware catalog could not be read safely.");
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(catalog.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        bundleStatus_ = QStringLiteral("Firmware catalog is invalid JSON.");
        return false;
    }
    const QJsonObject object = document.object();
    const QJsonArray images = object.value(QStringLiteral("images")).toArray();
    if (object.value(QStringLiteral("schema")).toString() != QString::fromLatin1(kCatalogSchema) ||
        object.value(QStringLiteral("chip")).toString() != QString::fromLatin1(kExpectedChip) ||
        object.value(QStringLiteral("sourceCommit")).toString() != sourceCommit ||
        object.value(QStringLiteral("version")).toString() != firmwareVersion_ ||
        object.value(QStringLiteral("protocol")).toInt(-1) != expectedProtocol_.toInt() ||
        images.size() != 2) {
        bundleStatus_ = QStringLiteral("Firmware catalog identity/schema or image count is incompatible.");
        return false;
    }
    QVector<RevisionImage> validated;
    const QString canonicalRoot = QFileInfo(root.absolutePath()).canonicalFilePath();
    if (canonicalRoot.isEmpty()) {
        bundleStatus_ = QStringLiteral("Firmware directory could not be resolved.");
        return false;
    }
    for (const auto& value : images) {
        if (!value.isObject()) {
            bundleStatus_ = QStringLiteral("Firmware catalog contains an invalid image entry.");
            return false;
        }
        const QJsonObject entry = value.toObject();
        const QString policy = entry.value(QStringLiteral("policy")).toString();
        const QString name = entry.value(QStringLiteral("image")).toString();
        const QString hash = normalizedHash(entry.value(QStringLiteral("sha256")).toString());
        const int minRevision = entry.value(QStringLiteral("minRevision")).toInt(-1);
        const int maxRevision = entry.value(QStringLiteral("maxRevision")).toInt(-1);
        // Qualification is intentionally limited to proven v1.x and the v3.2
        // engineering-preview target. Do not silently permit future revisions.
        const bool legacy = policy == QString::fromLatin1(kPreV3Policy) &&
            minRevision == 100 && maxRevision == 199;
        const bool v3 = policy == QString::fromLatin1(kV3Policy) &&
            minRevision == 302 && maxRevision == 302;
        const QFileInfo imageInfo{root.filePath(name)};
        if ((!legacy && !v3) || name.isEmpty() || QFileInfo(name).fileName() != name ||
            !QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(hash).hasMatch() ||
            imageInfo.isSymLink() || !imageInfo.isFile() || imageInfo.size() <= 0 ||
            imageInfo.size() > kMaxFirmwareSize ||
            imageInfo.canonicalPath() != canonicalRoot ||
            (legacy && (name != legacyImage || hash != legacyHash))) {
            bundleStatus_ = QStringLiteral("Firmware catalog has an unsafe or incompatible image entry.");
            return false;
        }
        for (const RevisionImage& earlier : validated) {
            if (earlier.policy == policy || earlier.imagePath == imageInfo.canonicalFilePath() ||
                !(maxRevision < earlier.minRevision || minRevision > earlier.maxRevision)) {
                bundleStatus_ = QStringLiteral("Firmware catalog contains duplicate or overlapping images.");
                return false;
            }
        }
        QFile imageFile{imageInfo.canonicalFilePath()};
        QCryptographicHash hasher{QCryptographicHash::Sha256};
        if (!imageFile.open(QIODevice::ReadOnly) || !hasher.addData(&imageFile) ||
            QString::fromLatin1(hasher.result().toHex()) != hash) {
            bundleStatus_ = QStringLiteral("Firmware catalog SHA-256 verification failed.");
            return false;
        }
        validated.push_back({policy, imageInfo.canonicalFilePath(), hash, minRevision, maxRevision});
    }
    if (validated.size() != 2 || validated[0].policy == validated[1].policy) {
        bundleStatus_ = QStringLiteral("Firmware catalog must contain two disjoint revision policies.");
        return false;
    }
    revisionImages_ = std::move(validated);
    revisionCatalogAvailable_ = true;
    return true;
}

bool FirmwareManager::selectFirmwareForRevision(const int major, const int minor) {
    if (!revisionCatalogAvailable_) return supportsEsp32P4Revision(major, minor);
    const RevisionImage* selected = nullptr;
    for (const RevisionImage& image : revisionImages_) {
        if (!revisionInRange(major, minor, image.minRevision, image.maxRevision)) continue;
        if (selected != nullptr) return false; // Multiple matches must never be resolved by array order.
        selected = &image;
    }
    if (selected == nullptr) return false;
    revisionPolicy_ = selected->policy;
    firmwareImagePath_ = selected->imagePath;
    firmwareSha256_ = selected->sha256;
    return true;
}

bool FirmwareManager::probeTarget(const QString& portName) {
    if (busy_ || shuttingDown_) return false;
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
    if (sessionGeneration_ == 0) {
        fail(QStringLiteral("Firmware operation has no active supervisor generation."));
        return false;
    }

    selectedPort_ = port;
    targetVerified_ = false;
    bootloaderHelpNeeded_ = false;
    flashProgress_ = -1;
    progressOutputTail_.clear();
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
    if (busy_ || shuttingDown_) return false;
    if (!bundleReady_ || !flasherAvailable_) refreshBundle();
    const QString port = portName.trimmed();
    if (!bundleReady_) {
        fail(bundleStatus_);
        return false;
    }
    if (!targetVerified_ || port.isEmpty() || port != selectedPort_) {
        fail(QStringLiteral("Identify and verify a supported ESP32-P4 board before installing firmware."));
        return false;
    }
    // Re-hash immediately before the write. A file replaced after preflight
    // must never be trusted because the catalog was valid earlier.
    QFile image{firmwareImagePath_};
    QCryptographicHash hasher{QCryptographicHash::Sha256};
    if (!image.open(QIODevice::ReadOnly) || !hasher.addData(&image) ||
        QString::fromLatin1(hasher.result().toHex()) != firmwareSha256_) {
        targetVerified_ = false;
        fail(QStringLiteral("Firmware image changed after verification. Installation is blocked."));
        return false;
    }
    if (sessionGeneration_ == 0) {
        fail(QStringLiteral("Firmware operation has no active supervisor generation."));
        return false;
    }

    bootloaderHelpNeeded_ = false;
    // Do not invent 0%. Progress remains unknown until espflash emits real
    // write telemetry. The UI renders this as an indeterminate write state.
    flashProgress_ = -1;
    progressOutputTail_.clear();
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
    if (!busy_ || shuttingDown_ || worker_ == nullptr || activeOperationGeneration_ == 0) return;
    cancelRequested_ = true;
    setStatus(QStringLiteral("Cancelling firmware operation..."));
    emit stateChanged();
    const quint64 generation = activeOperationGeneration_;
    QMetaObject::invokeMethod(
        worker_,
        [worker = worker_, generation] { worker->cancel(generation); },
        Qt::QueuedConnection);
}

bool FirmwareManager::shutdown() {
    if (shuttingDown_) return !workerThread_.isRunning();
    shuttingDown_ = true;
    operation_ = Operation::none;
    activeOperationGeneration_ = 0;
    busy_ = false;
    cancelRequested_ = true;
    progressOutputTail_.clear();

    if (worker_ == nullptr || !workerThread_.isRunning()) {
        worker_ = nullptr;
        workerReady_ = false;
        workerAffinityValid_ = false;
        return true;
    }
    if (QThread::currentThread() == &workerThread_) {
        worker_->shutdown();
        workerThread_.quit();
        return false;
    }

    auto acknowledged = QSharedPointer<QSemaphore>::create();
    FirmwareWorker* const worker = worker_;
    const bool queued = QMetaObject::invokeMethod(
        worker,
        [worker, acknowledged] {
            worker->shutdown();
            acknowledged->release();
        },
        Qt::QueuedConnection);
    bool graceful = queued && acknowledged->tryAcquire(1, shutdownAckTimeoutMs());
    if (!graceful) {
        qCritical().noquote()
            << "Firmware worker shutdown acknowledgement timed out; entering bounded retirement fallback.";
    }

    workerThread_.quit();
    bool joined = workerThread_.wait(shutdownJoinTimeoutMs());
    if (!joined) {
        graceful = false;
        qCritical().noquote()
            << "Firmware worker missed primary join deadline; retrying bounded retirement.";
        workerThread_.requestInterruption();
        workerThread_.quit();
        joined = workerThread_.wait(shutdownRetryTimeoutMs());
    }
    if (!joined) {
        graceful = false;
        qCritical().noquote()
            << "Firmware worker still alive; emergency terminate fallback engaged.";
        workerThread_.terminate();
        joined = workerThread_.wait(shutdownForceTimeoutMs());
    }

    worker_ = nullptr;
    workerReady_ = false;
    workerAffinityValid_ = false;
    if (!joined) qCritical().noquote() << "Firmware worker could not be retired before exit.";
    return graceful && joined;
}

void FirmwareManager::clearLog() {
    if (logText_.isEmpty()) return;
    logText_.clear();
    emit logChanged();
}

bool FirmwareManager::startEspflash(const QStringList& arguments, const Operation operation) {
    if (shuttingDown_ || worker_ == nullptr || !workerThread_.isRunning() ||
        !workerReady_ || !workerAffinityValid() || sessionGeneration_ == 0) {
        fail(QStringLiteral("Firmware worker is unavailable or not affinity-safe."));
        busy_ = false;
        return false;
    }

    operation_ = operation;
    activeOperationGeneration_ = sessionGeneration_;
    operationOutput_.clear();
    if (operation == Operation::flash) progressOutputTail_.clear();
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("ESPFLASH_PORT"), selectedPort_);
    environment.insert(QStringLiteral("ESPFLASH_SKIP_UPDATE_CHECK"), QStringLiteral("true"));
    appendLog(QStringLiteral("$ ESPFLASH_PORT=%1 espflash %2\n")
        .arg(selectedPort_, arguments.join(QLatin1Char(' '))));

    const quint64 generation = activeOperationGeneration_;
    const QString program = flasherPath();
    const int timeout = operationTimeoutMs(operation);
    QMetaObject::invokeMethod(
        worker_,
        [worker = worker_, generation, program, arguments, environment, timeout] {
            worker->startOperation(
                generation,
                program,
                arguments,
                environment,
                FirmwareManager::launchTimeoutMs(),
                timeout);
        },
        Qt::QueuedConnection);
    return true;
}

int FirmwareManager::operationTimeoutMs(const Operation operation) const noexcept {
    switch (operation) {
    case Operation::probe:
        return probeTimeoutMs();
    case Operation::flash:
        return flashTimeoutMs();
    case Operation::reset:
        return resetTimeoutMs();
    case Operation::none:
        return 1;
    }
    return 1;
}

void FirmwareManager::handleOperationRejected(const QString& message) {
    const Operation rejected = operation_;
    operation_ = Operation::none;
    activeOperationGeneration_ = 0;
    busy_ = false;
    flashProgress_ = -1;
    progressOutputTail_.clear();
    cancelRequested_ = false;

    if (rejected == Operation::reset) {
        bootloaderHelpNeeded_ = false;
        setStatus(QStringLiteral("Firmware was written, but the reset tool could not start. Press RESET once or reconnect USB; Studio will verify the board."));
        emit stateChanged();
        emit installationFinished(false);
        return;
    }

    bootloaderHelpNeeded_ = false;
    fail(message.isEmpty() ? QStringLiteral("Firmware worker rejected the operation.") : message);
    emit stateChanged();
    emit operationFailed(status_, false);
}

void FirmwareManager::handleLaunchFailure(const QString& message, const bool timeout) {
    const Operation failedOperation = operation_;
    operation_ = Operation::none;
    activeOperationGeneration_ = 0;
    busy_ = false;
    flashProgress_ = -1;
    progressOutputTail_.clear();
    cancelRequested_ = false;

    if (failedOperation == Operation::reset) {
        bootloaderHelpNeeded_ = false;
        setStatus(timeout
            ? QStringLiteral("Firmware was written, but the reset tool did not launch. Press RESET once or reconnect USB; Studio will verify the board.")
            : QStringLiteral("Firmware was written, but the reset tool could not launch. Press RESET once or reconnect USB; Studio will verify the board."));
        emit stateChanged();
        emit installationFinished(false);
        return;
    }

    bootloaderHelpNeeded_ = false;
    fail(timeout
        ? QStringLiteral("Bundled firmware tool did not launch within 15 seconds. Windows security may be delaying or blocking it.")
        : QStringLiteral("Unable to launch bundled firmware tool: %1").arg(message));
    emit stateChanged();
    emit operationFailed(status_, false);
}

void FirmwareManager::handleOperationTimeout() {
    const Operation timedOut = operation_;
    operation_ = Operation::none;
    activeOperationGeneration_ = 0;
    busy_ = false;
    flashProgress_ = -1;
    progressOutputTail_.clear();
    cancelRequested_ = false;

    if (timedOut == Operation::reset) {
        bootloaderHelpNeeded_ = false;
        setStatus(QStringLiteral("Firmware was written, but automatic reset timed out. Press RESET once or reconnect USB; Studio will verify the board."));
        emit stateChanged();
        emit installationFinished(false);
        return;
    }

    bootloaderHelpNeeded_ = true;
    if (timedOut == Operation::probe) {
        fail(QStringLiteral("ESP32-P4 did not answer the ROM check within 30 seconds. Put the board in Download mode and retry."));
    } else {
        fail(QStringLiteral("Firmware writing did not complete within 180 seconds. The device is not considered ready; put it in Download mode and retry."));
    }
    emit stateChanged();
    emit operationFailed(status_, true);
}

void FirmwareManager::finishOperation(const int exitCode, const bool normalExit) {
    const Operation completed = operation_;
    operation_ = Operation::none;
    activeOperationGeneration_ = 0;
    if (completed == Operation::none || shuttingDown_) return;

    if (cancelRequested_) {
        cancelRequested_ = false;
        busy_ = false;
        flashProgress_ = -1;
        progressOutputTail_.clear();
        setStatus(QStringLiteral("Firmware operation cancelled."));
        emit stateChanged();
        if (completed == Operation::reset) emit installationFinished(false);
        else emit operationFailed(status_, false);
        return;
    }

    const bool success = normalExit && exitCode == 0;

    if (completed == Operation::probe) {
        busy_ = false;
        int major = -1;
        int minor = -1;
        const bool p4WithRevision = success && parseEsp32P4Revision(operationOutput_, major, minor);
        const bool anyChipAnswered = success && operationOutput_.contains(
            QRegularExpression{QStringLiteral(R"(Chip\s+type:)"), QRegularExpression::CaseInsensitiveOption});
        const bool supportedRevision = p4WithRevision && selectFirmwareForRevision(major, minor);

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
            setStatus(QStringLiteral("ESP32-P4 v%1.%2 verified. Matching %3 firmware selected.")
                .arg(major).arg(minor).arg(revisionPolicy_));
        } else if (p4WithRevision) {
            setStatus(QStringLiteral("ESP32-P4 v%1.%2 is not supported by this firmware package. Installation is blocked.")
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
            progressOutputTail_.clear();
            bootloaderHelpNeeded_ = true;
            fail(QStringLiteral("Firmware installation failed. Put the board in Download mode and retry. The device is not considered ready."));
            emit stateChanged();
            emit operationFailed(status_, true);
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
        progressOutputTail_.clear();
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

    // QProcess signal boundaries are not protocol boundaries. Preserve a small
    // rolling suffix so tokens such as "6" + "4%" are reconstructed without
    // allowing terminal output to grow an unbounded presentation buffer.
    progressOutputTail_ += text;
    if (progressOutputTail_.size() > kMaxProgressOutputTail) {
        progressOutputTail_ = progressOutputTail_.right(kTrimmedProgressOutputTail);
    }

    const int latest = parseFlashProgress(progressOutputTail_);
    // espflash can repaint older terminal lines; operator progress must never
    // move backwards. Completion still owns the authoritative 100% transition.
    if (latest < 0 || latest <= flashProgress_) return;
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
