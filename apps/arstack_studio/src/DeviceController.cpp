// SPDX-License-Identifier: GPL-3.0-or-later

#include "DeviceController.hpp"

#include "DeviceIoWorker.hpp"

#include <QDateTime>
#include <QMetaObject>
#include <QRegularExpression>

#include <cmath>
#include <limits>
#include <utility>

namespace {
const QRegularExpression kAnsiExpression{QStringLiteral("\\x1B\\[[0-9;]*m")};
const QRegularExpression kStateExpression{
    QStringLiteral("state=(RUNNING|STOPPED).*generation=(\\d+).*signal_frequency=(\\d+)\\s*mHz"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kTimingExpression{
    QStringLiteral("samples=(\\d+)\\s+\\(~(\\d+)\\s+fps\\).*?MC ok=(\\d+) fail=(\\d+).*?missed=(\\d+).*?signal_gen=(\\d+)"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kSignalGenerationExpression{
    QStringLiteral("Live signal generation\\s+(\\d+)\\s+committed"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kProfileCommittedExpression{
    QStringLiteral("PROFILE committed generation=(\\d+)\\s+svID=(\\S+)\\s+APPID=0x([0-9A-Fa-f]+)\\s+rate=(\\d+)\\s+wrap=(\\d+)"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kProfileArmedExpression{
    QStringLiteral("PROFILE armed generation=(\\d+)\\s+svID=(\\S+)\\s+APPID=0x([0-9A-Fa-f]+)\\s+rate=(\\d+)\\s+wrap=(\\d+)"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kIdentityExpression{
    QStringLiteral(
        "ARSTACK identity product=([A-Z0-9_-]+) target=([A-Z0-9_-]+) protocol=(\\d+) "
        "device_id=([A-Fa-f0-9]{12}) firmware=([0-9A-Za-z._+\\-]+) "
        "(?:boot_id=([A-Fa-f0-9]{16}) )?capabilities=([A-Z0-9_,.\\-]+)"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kPtpStatusExpression{
    QStringLiteral("PTP status=(RUNNING|STOPPED) Announce=(\\d+) Sync=(\\d+) FollowUp=(\\d+) PdelayFrames=(\\d+) TXfail=(\\d+)"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kPtpConfigExpression{
    QStringLiteral("PTP config domain=(\\d+) transportSpecific=0x([0-9A-Fa-f]+) VLAN=(\\S+) Announce=(\\d+) ms Sync=(\\d+) ms Pdelay=(ON|OFF)"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kPtpRoleExpression{
    QStringLiteral("PTPROLE role=(SOURCE|RECEIVER|MONITOR) value=(\\d+) running=([01])"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kPtpMeasuredStateExpression{
    QStringLiteral(
        "PTP2 role=(SOURCE|RECEIVER|MONITOR) discipline=(UNLOCKED|ACQUIRING|LOCKED|HOLDOVER|FAULT) "
        "source=(\\S+) offset=(NA|-?\\d+) path=(NA|-?\\d+) jitter=(NA|-?\\d+) freq=(-?\\d+) "
        "global=([01]) measured=(NA|[012]) rxAnnounce=(\\d+) rxSync=(\\d+) rxFollowUp=(\\d+) "
        "rxPdelay=(\\d+) pdelayReq=(\\d+) accepted=(\\d+) rejected=(\\d+)"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kSmpSynchExpression{
    QStringLiteral(
        "SMPSYNCH mode=([A-Z0-9_]+) advertised=([0-2]) source=([A-Z0-9_]+) simulated=([01]) measured=([01])"),
    QRegularExpression::CaseInsensitiveOption};

[[nodiscard]] bool finitePositive(const double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}
} // namespace

DeviceController::DeviceController(QObject* parent) : QObject(parent) {
    ioWorker_ = new DeviceIoWorker;
    ioWorker_->moveToThread(&ioThread_);
    ioThread_.setObjectName(QStringLiteral("ARStackDeviceIo"));

    connect(&ioThread_, &QThread::started, ioWorker_, &DeviceIoWorker::initialize);
    connect(&ioThread_, &QThread::finished, ioWorker_, &QObject::deleteLater);
    connectWorkerSignals();
    ioThread_.start();
}

DeviceController::~DeviceController() {
    if (ioWorker_ == nullptr || !ioThread_.isRunning()) return;

    const bool requestStop = running_;
    if (QThread::currentThread() == &ioThread_) {
        ioWorker_->shutdown(requestStop);
    } else {
        QMetaObject::invokeMethod(
            ioWorker_,
            [worker = ioWorker_, requestStop] { worker->shutdown(requestStop); },
            Qt::BlockingQueuedConnection);
    }
    ioThread_.quit();
    ioThread_.wait();
}

void DeviceController::connectWorkerSignals() {
    connect(ioWorker_, &DeviceIoWorker::ready, this,
            [this](const quint64, const bool affinityValid) {
        ioWorkerReady_ = true;
        ioWorkerAffinityValid_ = affinityValid;

        const quint64 generation = sessionGeneration_;
        if (!pendingConnectPort_.isEmpty() && generation != 0) {
            const QString requested = std::exchange(pendingConnectPort_, QString{});
            pendingAutoDetect_ = false;
            QMetaObject::invokeMethod(
                ioWorker_,
                [worker = ioWorker_, requested, generation] {
                    worker->connectPort(requested, generation);
                },
                Qt::QueuedConnection);
            return;
        }
        if (pendingAutoDetect_ && generation != 0) {
            pendingAutoDetect_ = false;
            QMetaObject::invokeMethod(
                ioWorker_,
                [worker = ioWorker_, generation] { worker->autoDetectAndConnect(generation); },
                Qt::QueuedConnection);
        }
    });

    connect(ioWorker_, &DeviceIoWorker::portsObserved, this,
            [this](const quint64 generation, const QStringList& ports,
                   const QString& recommended, const int highConfidenceCount) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        handlePortSnapshot(ports, recommended, highConfidenceCount);
    });

    connect(ioWorker_, &DeviceIoWorker::openingPort, this,
            [this](const quint64 generation, const QString& port, const bool automatic) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        Q_UNUSED(port)
        setDiscoveryState(
            automatic
                ? QStringLiteral("Checking connected device for an ARStack injector...")
                : QStringLiteral("Verifying ARStack injector identity..."),
            true);
    });

    connect(ioWorker_, &DeviceIoWorker::portOpened, this,
            [this](const quint64 generation, const QString& port, const bool automatic) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        handlePortOpened(port, automatic);
    });

    connect(ioWorker_, &DeviceIoWorker::portOpenFailed, this,
            [this](const quint64 generation, const QString& port,
                   const QString& message, const bool automatic) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        Q_UNUSED(port)
        if (!automatic) {
            setIdentificationState(IdentificationState::Idle);
            setDiscoveryState(QStringLiteral("The selected serial port could not be opened."), false);
            setError(message);
        }
    });

    connect(ioWorker_, &DeviceIoWorker::portClosed, this,
            [this](const quint64 generation, const QString& port) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        handlePortClosed(port);
    });

    connect(ioWorker_, &DeviceIoWorker::portReleased, this,
            [this](const quint64 generation, const QString& port) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        emit portReleased(generation, port);
    });

    connect(ioWorker_, &DeviceIoWorker::identificationAttempt, this,
            [this](const quint64 generation, const QString& port,
                   const int attempt, const int maximum) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        lastIdentificationPort_ = port;
        identifyAttempts_ = attempt;
        setIdentificationState(IdentificationState::Identifying);
        setDiscoveryState(
            QStringLiteral("Verifying ARStack semantic identity… attempt %1/%2")
                .arg(attempt)
                .arg(maximum),
            true);
    });

    connect(ioWorker_, &DeviceIoWorker::identificationTimedOut, this,
            [this](const quint64 generation, const QString& port,
                   const int attempts, const bool continuing) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        lastIdentificationPort_ = port;
        identifyAttempts_ = attempts;
        if (continuing) {
            setIdentificationState(IdentificationState::Identifying);
            setDiscoveryState(QStringLiteral("Checking connected devices for an ARStack injector..."), true);
            return;
        }
        setIdentificationState(IdentificationState::Unidentified);
        setDiscoveryState(
            QStringLiteral("No ARStack semantic identity received from %1 after %2 bounded attempts.")
                .arg(port.isEmpty() ? QStringLiteral("the selected port") : port)
                .arg(attempts),
            false);
    });

    connect(ioWorker_, &DeviceIoWorker::automaticProbeExhausted, this,
            [this](const quint64 generation) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        if (identificationState_ != IdentificationState::Unidentified) {
            identifyAttempts_ = 0;
            setIdentificationState(IdentificationState::Idle);
        }
        setDiscoveryState(
            ports_.isEmpty()
                ? QStringLiteral("No verified injector found; setpoints remain editable offline.")
                : QStringLiteral("No ARStack injector answered; manual port selection remains available."),
            false);
    });

    connect(ioWorker_, &DeviceIoWorker::lineReceived, this,
            [this](const quint64 generation, const QString& line) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        processLine(line);
    });

    connect(ioWorker_, &DeviceIoWorker::commandTransmitted, this,
            [this](const quint64 generation, const QString& command, const bool quiet) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        if (!quiet) appendLog(QStringLiteral("→"), command);
    });

    connect(ioWorker_, &DeviceIoWorker::commandRejected, this,
            [this](const quint64 generation, const QString& message) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        if (profileDeploying_) {
            profileDeploying_ = false;
            profileArmed_ = false;
            emit profileStateChanged();
        }
        setError(message);
    });

    connect(ioWorker_, &DeviceIoWorker::transportError, this,
            [this](const quint64 generation, const QString& message, const bool fatal) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        Q_UNUSED(fatal)
        setError(message);
    });
}

QStringList DeviceController::ports() const { return ports_; }
QString DeviceController::recommendedPort() const { return recommendedPort_; }
QString DeviceController::discoveryStatus() const { return discoveryStatus_; }
bool DeviceController::discovering() const noexcept { return discovering_; }
bool DeviceController::deviceVerified() const noexcept { return deviceVerified_; }
DeviceController::IdentificationState DeviceController::identificationState() const noexcept { return identificationState_; }
int DeviceController::identifyAttempts() const noexcept { return identifyAttempts_; }
QString DeviceController::deviceProduct() const { return identity_.product; }
QString DeviceController::deviceTarget() const { return identity_.target; }
QString DeviceController::deviceId() const { return identity_.deviceId; }
QString DeviceController::protocolVersion() const { return identity_.protocolVersion; }
QString DeviceController::firmwareVersion() const { return identity_.firmwareVersion; }
QString DeviceController::bootId() const { return identity_.bootId; }
QStringList DeviceController::capabilities() const { return identity_.capabilities; }
DeviceIdentity DeviceController::deviceIdentity() const { return identity_; }
bool DeviceController::connected() const noexcept { return connected_; }
bool DeviceController::running() const noexcept { return running_; }
QString DeviceController::portName() const { return portName_; }
QString DeviceController::lastError() const { return lastError_; }
QString DeviceController::logText() const { return logText_; }
QString DeviceController::fps() const { return fps_; }
QString DeviceController::missed() const { return missed_; }
QString DeviceController::txFailures() const { return txFailures_; }
QString DeviceController::signalGeneration() const { return signalGeneration_; }
QString DeviceController::profileGeneration() const { return profileGeneration_; }
bool DeviceController::profileArmed() const noexcept { return profileArmed_; }
bool DeviceController::profileDeploying() const noexcept { return profileDeploying_; }
bool DeviceController::ptpAvailable() const noexcept { return ptpAvailable_; }
bool DeviceController::ptpRunning() const noexcept { return ptpRunning_; }
QString DeviceController::ptpStatus() const { return ptpStatus_; }
QString DeviceController::ptpDomain() const { return ptpDomain_; }
QString DeviceController::ptpTransportSpecific() const { return ptpTransportSpecific_; }
QString DeviceController::ptpVlan() const { return ptpVlan_; }
QString DeviceController::ptpAnnounceSent() const { return ptpAnnounceSent_; }
QString DeviceController::ptpSyncSent() const { return ptpSyncSent_; }
QString DeviceController::ptpTxFailures() const { return ptpTxFailures_; }

void DeviceController::setSessionGeneration(const quint64 generation) {
    if (generation == 0 || generation == sessionGeneration_) return;
    sessionGeneration_ = generation;
    pendingAutoDetect_ = false;
    pendingConnectPort_.clear();
}

bool DeviceController::parseIdentityLine(const QString& line, DeviceIdentity& identity) {
    const auto match = kIdentityExpression.match(line.trimmed());
    if (!match.hasMatch()) return false;

    DeviceIdentity parsed;
    parsed.product = match.captured(1).toUpper();
    parsed.target = match.captured(2).toUpper();
    parsed.protocolVersion = match.captured(3);
    parsed.deviceId = match.captured(4).toUpper();
    parsed.firmwareVersion = match.captured(5);
    parsed.bootId = match.captured(6).toUpper();
    parsed.capabilities = match.captured(7).split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString& capability : parsed.capabilities) capability = capability.trimmed().toUpper();
    parsed.capabilities.removeDuplicates();

    bool protocolOk = false;
    static_cast<void>(parsed.protocolVersion.toUInt(&protocolOk));
    if (!protocolOk || parsed.product != QStringLiteral("SMV-INJECTOR") ||
        parsed.target != QStringLiteral("ESP32-P4") || parsed.capabilities.isEmpty()) {
        return false;
    }

    identity = std::move(parsed);
    return true;
}

bool DeviceController::identitySupportsCurrentContract(
    const DeviceIdentity& identity,
    const QString& expectedFirmwareVersion) {
    static const QStringList requiredCapabilities{
        QStringLiteral("SMV-4I4V"),
        QStringLiteral("LIVE-SETPOINTS"),
        QStringLiteral("SESSION-LEASE"),
        QStringLiteral("PTP-P2"),
        QStringLiteral("SMPSYNCH-AUTO")};
    if (identity.product != QStringLiteral("SMV-INJECTOR") ||
        identity.target != QStringLiteral("ESP32-P4") ||
        identity.protocolVersion != QStringLiteral("1") ||
        identity.firmwareVersion != expectedFirmwareVersion ||
        identity.bootId.size() != 16) {
        return false;
    }
    for (const QString& required : requiredCapabilities) {
        if (!identity.capabilities.contains(required, Qt::CaseInsensitive)) return false;
    }
    return true;
}

void DeviceController::refreshPorts() {
    if (!ioWorkerReady_ || ioWorker_ == nullptr || sessionGeneration_ == 0) return;
    const quint64 generation = sessionGeneration_;
    QMetaObject::invokeMethod(
        ioWorker_,
        [worker = ioWorker_, generation] { worker->refreshPorts(generation); },
        Qt::QueuedConnection);
}

bool DeviceController::autoDetectAndConnect() {
    if (deviceVerified_) return true;
    if (connected_ || discovering_ || sessionGeneration_ == 0) return false;

    pendingConnectPort_.clear();
    pendingAutoDetect_ = !ioWorkerReady_;
    setDiscoveryState(QStringLiteral("Looking for an ARStack ESP32-P4 injector..."), true);

    if (ioWorkerReady_ && ioWorker_ != nullptr) {
        const quint64 generation = sessionGeneration_;
        QMetaObject::invokeMethod(
            ioWorker_,
            [worker = ioWorker_, generation] { worker->autoDetectAndConnect(generation); },
            Qt::QueuedConnection);
    }
    return true;
}

bool DeviceController::connectPort(const QString& portName) {
    const QString requested = portName.trimmed();
    if (requested.isEmpty()) {
        setError(QStringLiteral("Select a serial port first."));
        return false;
    }
    if (sessionGeneration_ == 0) {
        setError(QStringLiteral("Device connection has no active supervisor generation."));
        return false;
    }

    pendingAutoDetect_ = false;
    setDiscoveryState(QStringLiteral("Verifying ARStack injector identity..."), true);
    if (!ioWorkerReady_) {
        pendingConnectPort_ = requested;
        return true;
    }

    const quint64 generation = sessionGeneration_;
    QMetaObject::invokeMethod(
        ioWorker_,
        [worker = ioWorker_, requested, generation] {
            worker->connectPort(requested, generation);
        },
        Qt::QueuedConnection);
    return true;
}

void DeviceController::disconnectPort() {
    pendingAutoDetect_ = false;
    pendingConnectPort_.clear();
    if (!ioWorkerReady_ || ioWorker_ == nullptr || sessionGeneration_ == 0) return;
    const quint64 generation = sessionGeneration_;
    QMetaObject::invokeMethod(
        ioWorker_,
        [worker = ioWorker_, generation] { worker->disconnectPort(generation); },
        Qt::QueuedConnection);
}

void DeviceController::handlePortSnapshot(
    const QStringList& ports,
    const QString& recommendedPort,
    const int highConfidenceCount) {
    const bool portsDidChange = ports != ports_;
    const bool recommendationChanged = recommendedPort != recommendedPort_;
    ports_ = ports;
    recommendedPort_ = recommendedPort;
    highConfidenceCount_ = highConfidenceCount;

    if (identificationState_ == IdentificationState::Unidentified &&
        !lastIdentificationPort_.isEmpty() && !ports_.contains(lastIdentificationPort_)) {
        lastIdentificationPort_.clear();
        identifyAttempts_ = 0;
        setIdentificationState(IdentificationState::Idle);
    }

    if (portsDidChange) emit portsChanged();
    if (recommendationChanged) emit discoveryChanged();

    if (!connected_ && !discovering_) {
        if (identificationState_ == IdentificationState::Unidentified) {
            setDiscoveryState(
                QStringLiteral("ARStack identity was not received. Retry identification or choose firmware setup explicitly."),
                false);
        } else if (!recommendedPort_.isEmpty()) {
            setDiscoveryState(QStringLiteral("Compatible ESP32-P4 USB device found."), false);
        } else if (highConfidenceCount_ > 1) {
            setDiscoveryState(QStringLiteral("Multiple Espressif devices found; choose the intended injector."), false);
        } else {
            setDiscoveryState(QStringLiteral("No verified injector found; setpoints remain editable offline."), false);
        }
    }
}

void DeviceController::handlePortOpened(const QString& portName, const bool automatic) {
    Q_UNUSED(automatic)
    const bool wasVerified = deviceVerified_;
    connected_ = true;
    portName_ = portName;
    lastIdentificationPort_ = portName;
    identifyAttempts_ = 0;
    deviceVerified_ = false;
    clearIdentity();
    running_ = false;
    profileArmed_ = false;
    profileDeploying_ = false;
    resetPtpState();
    resetTelemetry();

    if (!lastError_.isEmpty()) {
        lastError_.clear();
        emit lastErrorChanged();
    }

    appendLog(QStringLiteral("•"), QStringLiteral("Connected %1 at 115200 8N1").arg(portName_));
    setIdentificationState(IdentificationState::Identifying);
    emit connectedChanged();
    emit runningChanged();
    emit profileStateChanged();
    emit ptpStateChanged();
    if (wasVerified) emit deviceVerifiedChanged();
}

void DeviceController::handlePortClosed(const QString& portName) {
    const bool wasConnected = connected_;
    const bool wasVerified = deviceVerified_;
    connected_ = false;
    if (!portName.isEmpty()) portName_ = portName;
    running_ = false;
    profileDeploying_ = false;
    profileArmed_ = false;
    deviceVerified_ = false;
    clearIdentity();
    identifyAttempts_ = 0;
    setIdentificationState(IdentificationState::Idle);
    discovering_ = false;
    resetPtpState();
    resetTelemetry();

    emit runningChanged();
    emit profileStateChanged();
    emit discoveryChanged();
    emit ptpStateChanged();
    if (wasVerified) emit deviceVerifiedChanged();
    if (wasConnected) {
        appendLog(QStringLiteral("•"), QStringLiteral("Device disconnected"));
        emit connectedChanged();
    }
}

bool DeviceController::sendShow() { return sendCommand(QStringLiteral("SHOW")); }
bool DeviceController::start() { return sendCommand(QStringLiteral("START")); }
bool DeviceController::stop() { return sendCommand(QStringLiteral("STOP")); }
bool DeviceController::zero() { return sendCommand(QStringLiteral("ZERO")); }

bool DeviceController::setFrequency(const double hz) {
    if (!std::isfinite(hz) || hz < 0.0 || hz > 1000.0) {
        setError(QStringLiteral("Frequency must be within 0..1000 Hz (0 = DC)."));
        return false;
    }
    const auto millihertz = std::llround(hz * 1000.0);
    if (millihertz < 0 || millihertz > std::numeric_limits<quint32>::max()) return false;
    if (!sendCommand(QStringLiteral("FREQ %1").arg(millihertz))) return false;
    signalFrequencyHz_ = hz;
    return true;
}

bool DeviceController::setSignal(
    const QString& signalId,
    const double magnitude,
    const double phaseDegrees,
    const quint32 quality,
    const double currentCountsPerAmp,
    const double voltageCountsPerVolt) {
    const QString id = signalId.trimmed().toUpper();
    static const QStringList validIds{
        QStringLiteral("IA"), QStringLiteral("IB"), QStringLiteral("IC"), QStringLiteral("IN"),
        QStringLiteral("UA"), QStringLiteral("UB"), QStringLiteral("UC"), QStringLiteral("UN")};
    if (!validIds.contains(id) || !std::isfinite(magnitude) ||
        (signalFrequencyHz_ > 0.0 && magnitude < 0.0) || !std::isfinite(phaseDegrees)) {
        setError(QStringLiteral("Invalid signal setpoint."));
        return false;
    }
    if (!finitePositive(currentCountsPerAmp) || !finitePositive(voltageCountsPerVolt)) {
        setError(QStringLiteral("Engineering scaling must be positive."));
        return false;
    }

    const double scale = id.startsWith(QLatin1Char('I')) ? currentCountsPerAmp : voltageCountsPerVolt;
    const auto counts = std::llround(magnitude * scale);
    const auto phaseMdeg = std::llround(phaseDegrees * 1000.0);
    if (counts < std::numeric_limits<qint32>::min() || counts > std::numeric_limits<qint32>::max() ||
        phaseMdeg < std::numeric_limits<qint32>::min() || phaseMdeg > std::numeric_limits<qint32>::max()) {
        setError(QStringLiteral("Setpoint exceeds firmware wire range."));
        return false;
    }

    return sendCommand(QStringLiteral("SET %1 %2 %3 %4")
        .arg(id)
        .arg(counts)
        .arg(phaseMdeg)
        .arg(static_cast<qulonglong>(quality)));
}

bool DeviceController::setEnabled(const QString& signalId, const bool enabled) {
    return sendCommand(QStringLiteral("ENABLE %1 %2")
        .arg(signalId.trimmed().toUpper())
        .arg(enabled ? 1 : 0));
}

bool DeviceController::setQuality(const QString& signalId, const quint32 quality) {
    return sendCommand(QStringLiteral("QUALITY %1 %2")
        .arg(signalId.trimmed().toUpper())
        .arg(static_cast<qulonglong>(quality)));
}

bool DeviceController::deployProfile(const QVariantMap& profile) {
    if (!deviceVerified_) {
        setError(QStringLiteral("Connect the ESP32-P4 before deployment."));
        return false;
    }
    if (running_) {
        setError(QStringLiteral("Stop the publisher before changing profile identity/layout."));
        return false;
    }
    if (profile.value(QStringLiteral("compatibilityClass")).toString() != QStringLiteral("A") ||
        profile.value(QStringLiteral("deviceSupport")).toString() != QStringLiteral("ready")) {
        setError(QStringLiteral("Only Class A profiles supported by the current ESP32-P4 layout can be deployed."));
        return false;
    }

    const QString svId = profile.value(QStringLiteral("svId")).toString();
    const QString dataSet = profile.value(QStringLiteral("dataSetReference")).toString();
    const QString mac = compactMac(profile.value(QStringLiteral("destinationMac")).toString());
    const auto appId = profile.value(QStringLiteral("appId")).toUInt();
    const bool vlanPresent = profile.value(QStringLiteral("vlanPresent")).toBool();
    const auto vlanId = profile.value(QStringLiteral("vlanId")).toUInt();
    const auto pcp = profile.value(QStringLiteral("vlanPriority")).toUInt();
    const auto confRev = profile.value(QStringLiteral("confRev")).toULongLong();
    const auto rate = profile.value(QStringLiteral("publisherRate")).toULongLong();
    const auto modulus = profile.value(QStringLiteral("counterModulus")).toUInt();
    const auto noAsdu = profile.value(QStringLiteral("nofASDU")).toUInt();
    const bool includeDataSet = profile.value(QStringLiteral("includeDataSet")).toBool();
    const bool includeSampleRate = profile.value(QStringLiteral("includeSampleRate")).toBool();

    const QString idHex = utf8Hex(svId);
    const QString dataSetHex = includeDataSet ? utf8Hex(dataSet) : QStringLiteral("-");
    if (svId.isEmpty() || idHex.isEmpty() || idHex.size() > 180 || dataSetHex.size() > 170 ||
        mac.size() != 12 || appId == 0U || appId > 65535U || vlanId > 4095U || pcp > 7U ||
        rate == 0U || rate > 65535U || modulus == 0U || modulus > 65535U || noAsdu != 1U) {
        setError(QStringLiteral("Compiled profile exceeds the current bounded device bridge."));
        return false;
    }

    unsigned flags = 0U;
    if (includeDataSet) flags |= 0x1U;
    if (includeSampleRate) flags |= 0x2U;

    const QStringList commands{
        QStringLiteral("PROFILE BEGIN"),
        QStringLiteral("PROFILE ID %1").arg(idHex),
        QStringLiteral("PROFILE DATASET %1").arg(dataSetHex),
        QStringLiteral("PROFILE L2 %1 %2 %3 %4 %5")
            .arg(appId).arg(mac).arg(vlanPresent ? 1 : 0).arg(vlanId).arg(pcp),
        QStringLiteral("PROFILE SV %1 %2 %3 %4 %5")
            .arg(confRev).arg(rate).arg(modulus).arg(noAsdu).arg(flags),
        QStringLiteral("PROFILE COMMIT"),
        QStringLiteral("PROFILE SHOW"),
    };

    profileDeploying_ = true;
    profileArmed_ = false;
    emit profileStateChanged();
    if (!sendCommandBatch(commands)) {
        profileDeploying_ = false;
        emit profileStateChanged();
        return false;
    }
    return true;
}

bool DeviceController::setCtSaturation(
    const bool enabled,
    const double dcOffsetPercent,
    const double harmonicPercent,
    const int harmonicOrder,
    const double clipPercent) {
    if (enabled && signalFrequencyHz_ == 0.0) {
        setError(QStringLiteral("CT saturation shaping requires AC mode."));
        return false;
    }
    if (!std::isfinite(dcOffsetPercent) || dcOffsetPercent < -300.0 || dcOffsetPercent > 300.0 ||
        !std::isfinite(harmonicPercent) || harmonicPercent < 0.0 || harmonicPercent > 300.0 ||
        harmonicOrder < 2 || harmonicOrder > 63 || !std::isfinite(clipPercent) ||
        clipPercent < 1.0 || clipPercent > 1000.0) {
        setError(QStringLiteral("CT saturation parameters are outside the bounded lab range."));
        return false;
    }
    return sendCommand(QStringLiteral("SHAPE CT %1 %2 %3 %4 %5")
        .arg(enabled ? 1 : 0)
        .arg(std::llround(dcOffsetPercent * 10.0))
        .arg(std::llround(harmonicPercent * 10.0))
        .arg(harmonicOrder)
        .arg(std::llround(clipPercent * 10.0)));
}

bool DeviceController::sendPtpShow() { return sendCommand(QStringLiteral("PTP SHOW")); }
bool DeviceController::startPtp() { return sendCommand(QStringLiteral("PTP START")); }
bool DeviceController::stopPtp() { return sendCommand(QStringLiteral("PTP STOP")); }

bool DeviceController::configurePtp(const QVariantMap& profile) {
    if (!deviceVerified_ || ptpRunning_) {
        setError(ptpRunning_
            ? QStringLiteral("Stop PTP Lab TX before applying expert timing settings.")
            : QStringLiteral("Verify the ESP32-P4 before configuring PTP."));
        return false;
    }
    const auto domain = profile.value(QStringLiteral("domain")).toUInt();
    const auto transport = profile.value(QStringLiteral("transportSpecific")).toUInt();
    const bool vlanEnabled = profile.value(QStringLiteral("vlanEnabled")).toBool();
    const auto vlanId = profile.value(QStringLiteral("vlanId")).toUInt();
    const auto pcp = profile.value(QStringLiteral("vlanPriority")).toUInt();
    const auto announceMs = profile.value(QStringLiteral("announceIntervalMs")).toUInt();
    const auto syncMs = profile.value(QStringLiteral("syncIntervalMs")).toUInt();
    const bool pdelay = profile.value(QStringLiteral("respondToPeerDelay")).toBool();
    if (domain > 255U || transport > 15U || (vlanEnabled && (vlanId == 0U || vlanId > 4094U)) ||
        pcp > 7U || announceMs < 100U || announceMs > 10000U ||
        syncMs < 20U || syncMs > 5000U) {
        setError(QStringLiteral("PTP expert profile is outside the firmware safety bounds."));
        return false;
    }
    return sendCommand(QStringLiteral("PTP CONFIG %1 %2 %3 %4 %5 %6 %7 %8")
        .arg(domain).arg(transport).arg(vlanEnabled ? 1 : 0).arg(vlanId).arg(pcp)
        .arg(announceMs).arg(syncMs).arg(pdelay ? 1 : 0));
}

void DeviceController::clearLog() {
    if (logText_.isEmpty()) return;
    logText_.clear();
    emit logTextChanged();
}

bool DeviceController::sendCommand(const QString& command) {
    return sendCommandBatch({command}, false);
}

bool DeviceController::sendQuietCommand(const QString& command) {
    return sendCommandBatch({command}, true);
}

bool DeviceController::sendCommandBatch(const QStringList& commands, const bool quiet) {
    if (!connected_ || !ioWorkerReady_ || ioWorker_ == nullptr || sessionGeneration_ == 0) {
        setError(QStringLiteral("Device is not connected."));
        return false;
    }
    const QStringList copy = commands;
    const quint64 generation = sessionGeneration_;
    QMetaObject::invokeMethod(
        ioWorker_,
        [worker = ioWorker_, copy, quiet, generation] {
            worker->enqueueCommands(copy, quiet, generation);
        },
        Qt::QueuedConnection);
    return true;
}

void DeviceController::setSessionHeartbeatEnabled(const bool enabled) {
    if (!ioWorkerReady_ || ioWorker_ == nullptr || sessionGeneration_ == 0) return;
    const quint64 generation = sessionGeneration_;
    QMetaObject::invokeMethod(
        ioWorker_,
        [worker = ioWorker_, enabled, generation] {
            worker->setHeartbeatEnabled(enabled, generation);
        },
        Qt::QueuedConnection);
}

void DeviceController::setRunning(const bool value) {
    if (running_ == value) return;
    running_ = value;
    emit runningChanged();
}

void DeviceController::setIdentificationState(const IdentificationState state) {
    if (identificationState_ == state) return;
    identificationState_ = state;
    emit identificationStateChanged();
}

void DeviceController::setDiscoveryState(const QString& status, const bool active) {
    if (discoveryStatus_ == status && discovering_ == active) return;
    discoveryStatus_ = status;
    discovering_ = active;
    emit discoveryChanged();
}

void DeviceController::applyIdentity(DeviceIdentity identity) {
    if (identity_ == identity) return;
    identity_ = std::move(identity);
    emit deviceIdentityChanged();
}

void DeviceController::clearIdentity() {
    if (identity_.empty()) return;
    identity_ = {};
    emit deviceIdentityChanged();
}

void DeviceController::markDeviceVerified() {
    if (deviceVerified_) return;
    deviceVerified_ = true;
    lastIdentificationPort_ = portName_;
    setIdentificationState(IdentificationState::Verified);
    setDiscoveryState(QStringLiteral("ARStack ESP32-P4 identity verified."), false);
    emit deviceVerifiedChanged();
    emit deviceMessage(QStringLiteral("ESP32-P4 recognized. Device is ready."));

    if (ioWorkerReady_ && ioWorker_ != nullptr && sessionGeneration_ != 0) {
        const quint64 generation = sessionGeneration_;
        QMetaObject::invokeMethod(
            ioWorker_,
            [worker = ioWorker_, generation] { worker->confirmIdentity(generation); },
            Qt::QueuedConnection);
    }
    static_cast<void>(sendShow());
    static_cast<void>(sendCommand(QStringLiteral("PROFILE SHOW")));
    static_cast<void>(sendPtpShow());
}

void DeviceController::setError(const QString& message) {
    if (message.isEmpty() || lastError_ == message) return;
    lastError_ = message;
    appendLog(QStringLiteral("!"), message);
    emit lastErrorChanged();
    emit deviceMessage(message);
}

void DeviceController::appendLog(const QString& direction, const QString& line) {
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    logText_.append(QStringLiteral("[%1] %2 %3\n").arg(stamp, direction, line));
    constexpr qsizetype kMaxChars = 60000;
    constexpr qsizetype kKeepChars = 48000;
    if (logText_.size() > kMaxChars) logText_ = logText_.right(kKeepChars);
    emit logTextChanged();
}

void DeviceController::processLine(const QString& rawLine) {
    const QString line = cleanLine(rawLine);
    if (line.isEmpty()) return;
    appendLog(QStringLiteral("←"), line);

    if (line.contains(QStringLiteral("START accepted"), Qt::CaseInsensitive)) setRunning(true);
    if (line.contains(QStringLiteral("STOP accepted"), Qt::CaseInsensitive)) setRunning(false);

    auto match = kStateExpression.match(line);
    if (match.hasMatch()) {
        setRunning(match.captured(1).compare(QStringLiteral("RUNNING"), Qt::CaseInsensitive) == 0);
        signalGeneration_ = match.captured(2);
        emit telemetryChanged();
    }

    match = kTimingExpression.match(line);
    if (match.hasMatch()) {
        fps_ = match.captured(2);
        txFailures_ = match.captured(4);
        missed_ = match.captured(5);
        signalGeneration_ = match.captured(6);
        emit telemetryChanged();
    }

    match = kSignalGenerationExpression.match(line);
    if (match.hasMatch()) {
        signalGeneration_ = match.captured(1);
        emit telemetryChanged();
    }

    match = kProfileCommittedExpression.match(line);
    if (match.hasMatch()) {
        profileGeneration_ = match.captured(1);
        profileDeploying_ = false;
        profileArmed_ = true;
        emit profileStateChanged();
        emit deviceMessage(QStringLiteral("SCL profile deployed and armed."));
    }

    match = kProfileArmedExpression.match(line);
    if (match.hasMatch()) {
        profileGeneration_ = match.captured(1);
        profileDeploying_ = false;
        profileArmed_ = true;
        emit profileStateChanged();
    }

    DeviceIdentity parsedIdentity;
    if (parseIdentityLine(line, parsedIdentity)) {
        applyIdentity(std::move(parsedIdentity));
        markDeviceVerified();
    }

    match = kPtpStatusExpression.match(line);
    if (match.hasMatch()) {
        ptpAvailable_ = true;
        ptpRunning_ = match.captured(1).compare(QStringLiteral("RUNNING"), Qt::CaseInsensitive) == 0;
        ptpAnnounceSent_ = match.captured(2);
        ptpSyncSent_ = match.captured(3);
        ptpFollowUpSent_ = match.captured(4);
        ptpPdelayFrames_ = match.captured(5);
        ptpTxFailures_ = match.captured(6);
        ptpStatus_ = ptpRunning_ ? QStringLiteral("Lab TX active") : QStringLiteral("Lab TX stopped");
        emit ptpStateChanged();
    }

    match = kPtpConfigExpression.match(line);
    if (match.hasMatch()) {
        ptpAvailable_ = true;
        ptpDomain_ = match.captured(1);
        ptpTransportSpecific_ = QStringLiteral("0x%1").arg(match.captured(2).toUpper());
        ptpVlan_ = match.captured(3);
        emit ptpStateChanged();
    }

    match = kPtpRoleExpression.match(line);
    if (match.hasMatch()) {
        ptpAvailable_ = true;
        ptpRole_ = match.captured(1).toUpper();
        ptpRunning_ = match.captured(3) == QStringLiteral("1");
        ptpDiscipline_ = QStringLiteral("UNLOCKED");
        ptpSource_ = QStringLiteral("NONE");
        ptpOffsetNs_ = QStringLiteral("NA");
        ptpPathDelayNs_ = QStringLiteral("NA");
        ptpJitterNs_ = QStringLiteral("NA");
        ptpFrequencyPpb_ = QStringLiteral("0");
        ptpGlobalTraceable_ = false;
        ptpMeasuredSmpSynch_ = QStringLiteral("NA");
        ptpRxAnnounce_ = QStringLiteral("0");
        ptpRxSync_ = QStringLiteral("0");
        ptpRxFollowUp_ = QStringLiteral("0");
        ptpRxPdelay_ = QStringLiteral("0");
        ptpPdelayRequests_ = QStringLiteral("0");
        ptpAccepted_ = QStringLiteral("0");
        ptpRejected_ = QStringLiteral("0");
        ptpStatus_ = ptpRunning_ ? QStringLiteral("Timing active") : QStringLiteral("Timing stopped");
        emit ptpStateChanged();
    }

    match = kPtpMeasuredStateExpression.match(line);
    if (match.hasMatch()) {
        ptpAvailable_ = true;
        ptpRole_ = match.captured(1).toUpper();
        ptpDiscipline_ = match.captured(2).toUpper();
        ptpSource_ = match.captured(3);
        ptpOffsetNs_ = match.captured(4);
        ptpPathDelayNs_ = match.captured(5);
        ptpJitterNs_ = match.captured(6);
        ptpFrequencyPpb_ = match.captured(7);
        ptpGlobalTraceable_ = match.captured(8) == QStringLiteral("1");
        ptpMeasuredSmpSynch_ = match.captured(9).toUpper();
        ptpRxAnnounce_ = match.captured(10);
        ptpRxSync_ = match.captured(11);
        ptpRxFollowUp_ = match.captured(12);
        ptpRxPdelay_ = match.captured(13);
        ptpPdelayRequests_ = match.captured(14);
        ptpAccepted_ = match.captured(15);
        ptpRejected_ = match.captured(16);
        emit ptpStateChanged();
    }

    match = kSmpSynchExpression.match(line);
    if (match.hasMatch()) {
        smpSynchMode_ = match.captured(1).toUpper();
        smpSynchValue_ = match.captured(2);
        smpSynchSource_ = match.captured(3).toUpper();
        smpSynchSimulated_ = match.captured(4) == QStringLiteral("1");
        smpSynchMeasured_ = match.captured(5) == QStringLiteral("1");
        emit ptpStateChanged();
    }

    if (line.contains(QStringLiteral("PTP unavailable"), Qt::CaseInsensitive)) {
        resetPtpState();
        ptpStatus_ = QStringLiteral("Not enabled in firmware");
        emit ptpStateChanged();
    }
    if (line.contains(QStringLiteral("PTP configuration accepted"), Qt::CaseInsensitive)) {
        emit deviceMessage(QStringLiteral("PTP expert profile accepted."));
        static_cast<void>(sendPtpShow());
    }

    if (line.contains(QStringLiteral("PROFILE commit rejected"), Qt::CaseInsensitive) ||
        line.contains(QStringLiteral("PROFILE rejected"), Qt::CaseInsensitive)) {
        profileDeploying_ = false;
        profileArmed_ = false;
        emit profileStateChanged();
        setError(QStringLiteral("Device rejected the profile."));
    }
}

void DeviceController::resetTelemetry() {
    fps_ = QStringLiteral("—");
    missed_ = QStringLiteral("—");
    txFailures_ = QStringLiteral("—");
    signalGeneration_ = QStringLiteral("—");
    emit telemetryChanged();
}

void DeviceController::resetPtpState() {
    ptpAvailable_ = false;
    ptpRunning_ = false;
    ptpStatus_ = QStringLiteral("Waiting for device");
    ptpDomain_ = QStringLiteral("-");
    ptpTransportSpecific_ = QStringLiteral("-");
    ptpVlan_ = QStringLiteral("-");
    ptpAnnounceSent_ = QStringLiteral("-");
    ptpSyncSent_ = QStringLiteral("-");
    ptpFollowUpSent_ = QStringLiteral("-");
    ptpPdelayFrames_ = QStringLiteral("-");
    ptpTxFailures_ = QStringLiteral("-");
    ptpRole_ = QStringLiteral("SOURCE");
    ptpDiscipline_ = QStringLiteral("UNLOCKED");
    ptpSource_ = QStringLiteral("NONE");
    ptpOffsetNs_ = QStringLiteral("NA");
    ptpPathDelayNs_ = QStringLiteral("NA");
    ptpJitterNs_ = QStringLiteral("NA");
    ptpFrequencyPpb_ = QStringLiteral("0");
    ptpGlobalTraceable_ = false;
    ptpMeasuredSmpSynch_ = QStringLiteral("NA");
    ptpRxAnnounce_ = QStringLiteral("0");
    ptpRxSync_ = QStringLiteral("0");
    ptpRxFollowUp_ = QStringLiteral("0");
    ptpRxPdelay_ = QStringLiteral("0");
    ptpPdelayRequests_ = QStringLiteral("0");
    ptpAccepted_ = QStringLiteral("0");
    ptpRejected_ = QStringLiteral("0");
    smpSynchMode_ = QStringLiteral("AUTO");
    smpSynchValue_ = QStringLiteral("0");
    smpSynchSource_ = QStringLiteral("SAFE_DEFAULT");
    smpSynchSimulated_ = false;
    smpSynchMeasured_ = false;
}

QString DeviceController::cleanLine(const QString& rawLine) {
    QString line = rawLine;
    line.remove(kAnsiExpression);
    return line.trimmed();
}

QString DeviceController::utf8Hex(const QString& text) {
    return QString::fromLatin1(text.toUtf8().toHex());
}

QString DeviceController::compactMac(const QString& text) {
    QString out;
    out.reserve(12);
    for (const auto ch : text) {
        if (ch.isDigit() || (ch.toUpper() >= QLatin1Char('A') && ch.toUpper() <= QLatin1Char('F'))) {
            out.append(ch.toUpper());
        }
    }
    return out;
}
