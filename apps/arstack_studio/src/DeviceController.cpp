// SPDX-License-Identifier: GPL-3.0-or-later

#include "DeviceController.hpp"

#include <QDateTime>
#include <QRegularExpression>
#include <QSerialPortInfo>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {
constexpr qint32 kBaudRate = 115200;
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

struct PortCandidate {
    QString name;
    int confidence{};
};

[[nodiscard]] int portConfidence(const QSerialPortInfo& info) {
    int score = 0;
    if (info.hasVendorIdentifier() && info.vendorIdentifier() == 0x303AU) score += 100;
    const QString identity = QStringLiteral("%1 %2 %3")
        .arg(info.description(), info.manufacturer(), info.serialNumber()).toLower();
    if (identity.contains(QStringLiteral("esp32-p4"))) score += 90;
    else if (identity.contains(QStringLiteral("esp32"))) score += 65;
    if (identity.contains(QStringLiteral("espressif"))) score += 60;
    if (identity.contains(QStringLiteral("usb jtag")) ||
        identity.contains(QStringLiteral("usb serial"))) score += 15;
    return score;
}

[[nodiscard]] bool finitePositive(const double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}
} // namespace

DeviceController::DeviceController(QObject* parent) : QObject(parent) {
    connect(&serial_, &QSerialPort::readyRead, this, [this] {
        pendingRx_.append(serial_.readAll());
        while (true) {
            const auto newline = pendingRx_.indexOf('\n');
            if (newline < 0) break;
            QByteArray line = pendingRx_.left(newline);
            pendingRx_.remove(0, newline + 1);
            if (!line.isEmpty() && line.endsWith('\r')) line.chop(1);
            processLine(QString::fromUtf8(line));
        }
    });

    connect(&serial_, &QSerialPort::errorOccurred, this, [this](const QSerialPort::SerialPortError error) {
        if (error == QSerialPort::NoError) return;
        if (error == QSerialPort::ResourceError || error == QSerialPort::DeviceNotFoundError) {
            setError(serial_.errorString());
            disconnectPort();
        } else if (serial_.isOpen()) {
            setError(serial_.errorString());
        }
    });

    verificationTimer_.setSingleShot(true);
    verificationTimer_.setInterval(identityRetryIntervalMs());
    connect(&verificationTimer_, &QTimer::timeout, this, [this] {
        if (!serial_.isOpen() || deviceVerified_ ||
            identificationState_ != IdentificationState::Identifying) {
            return;
        }
        if (identityRetryAllowed(identifyAttempts_) && sendIdentifyProbe()) {
            verificationTimer_.start();
            return;
        }
        finishIdentificationTimeout();
    });

    refreshPorts();
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
bool DeviceController::connected() const noexcept { return serial_.isOpen(); }
bool DeviceController::running() const noexcept { return running_; }
QString DeviceController::portName() const { return serial_.portName(); }
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
        QStringLiteral("SESSION-LEASE")};
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
    QList<PortCandidate> candidates;
    for (const auto& info : QSerialPortInfo::availablePorts()) {
        candidates.push_back({info.portName(), portConfidence(info)});
    }
    std::sort(candidates.begin(), candidates.end(), [](const PortCandidate& left, const PortCandidate& right) {
        if (left.confidence != right.confidence) return left.confidence > right.confidence;
        return left.name.localeAwareCompare(right.name) < 0;
    });

    QStringList discovered;
    QStringList highConfidence;
    for (const auto& candidate : candidates) {
        discovered.push_back(candidate.name);
        if (candidate.confidence >= 60) highConfidence.push_back(candidate.name);
    }
    discovered.removeDuplicates();
    const QString recommended = highConfidence.size() == 1 ? highConfidence.front() : QString{};
    const bool portsDidChange = discovered != ports_;
    const bool recommendationChanged = recommended != recommendedPort_;
    ports_ = std::move(discovered);
    recommendedPort_ = recommended;

    if (identificationState_ == IdentificationState::Unidentified &&
        !lastIdentificationPort_.isEmpty() && !ports_.contains(lastIdentificationPort_)) {
        lastIdentificationPort_.clear();
        identifyAttempts_ = 0;
        setIdentificationState(IdentificationState::Idle);
    }

    if (portsDidChange) emit portsChanged();
    if (recommendationChanged) emit discoveryChanged();

    if (!serial_.isOpen() && !discovering_) {
        if (identificationState_ == IdentificationState::Unidentified) {
            setDiscoveryState(
                QStringLiteral("ARStack identity was not received. Retry identification or choose firmware setup explicitly."),
                false);
        } else if (!recommendedPort_.isEmpty()) {
            setDiscoveryState(QStringLiteral("Compatible ESP32-P4 USB device found."), false);
        } else if (highConfidence.size() > 1) {
            setDiscoveryState(QStringLiteral("Multiple Espressif devices found; choose the intended injector."), false);
        } else {
            setDiscoveryState(QStringLiteral("No verified injector found; setpoints remain editable offline."), false);
        }
    }
}

bool DeviceController::connectPort(const QString& portName) {
    return connectPortInternal(portName, false);
}

bool DeviceController::autoDetectAndConnect() {
    if (deviceVerified_) return true;
    refreshPorts();
    probeQueue_.clear();
    genericProbeActive_ = false;
    if (!recommendedPort_.isEmpty()) {
        setDiscoveryState(
            QStringLiteral("Compatible device found. Verifying ARStack identity..."), true);
        return connectPortInternal(recommendedPort_, true);
    }
    if (ports_.isEmpty()) return false;

    probeQueue_ = ports_;
    genericProbeActive_ = true;
    return tryNextProbe();
}

bool DeviceController::tryNextProbe() {
    while (!probeQueue_.isEmpty()) {
        const QString port = probeQueue_.takeFirst();
        setDiscoveryState(QStringLiteral("Checking connected devices for an ARStack injector..."), true);
        if (connectPortInternal(port, true)) return true;
    }
    genericProbeActive_ = false;
    setDiscoveryState(QStringLiteral("No ARStack injector answered; manual port selection remains available."), false);
    return false;
}

bool DeviceController::connectPortInternal(const QString& portName, const bool automatic) {
    const QString requestedPort = portName.trimmed();
    if (requestedPort.isEmpty()) {
        setError(QStringLiteral("Select a serial port first."));
        return false;
    }
    if (serial_.isOpen()) disconnectPort();

    if (!automatic) {
        probeQueue_.clear();
        genericProbeActive_ = false;
    }
    automaticConnection_ = automatic;
    deviceVerified_ = false;
    identifyAttempts_ = 0;
    lastIdentificationPort_ = requestedPort;
    clearIdentity();
    setDiscoveryState(QStringLiteral("Verifying ARStack injector identity..."), true);

    serial_.setPortName(requestedPort);
    serial_.setBaudRate(kBaudRate);
    serial_.setDataBits(QSerialPort::Data8);
    serial_.setParity(QSerialPort::NoParity);
    serial_.setStopBits(QSerialPort::OneStop);
    serial_.setFlowControl(QSerialPort::NoFlowControl);

    if (!serial_.open(QIODevice::ReadWrite)) {
        automaticConnection_ = false;
        setIdentificationState(IdentificationState::Idle);
        if (!automatic) {
            setDiscoveryState(QStringLiteral("The selected serial port could not be opened."), false);
            setError(QStringLiteral("Cannot open %1: %2").arg(requestedPort, serial_.errorString()));
        }
        return false;
    }

    pendingRx_.clear();
    lastError_.clear();
    running_ = false;
    profileArmed_ = false;
    profileDeploying_ = false;
    resetTelemetry();
    appendLog(QStringLiteral("•"), QStringLiteral("Connected %1 at 115200 8N1").arg(serial_.portName()));
    setIdentificationState(IdentificationState::Identifying);
    emit connectedChanged();
    emit runningChanged();
    emit lastErrorChanged();
    emit profileStateChanged();

    if (!sendIdentifyProbe()) {
        finishIdentificationTimeout();
        return false;
    }
    verificationTimer_.start();
    return true;
}

void DeviceController::disconnectPort() {
    const bool wasConnected = serial_.isOpen();
    verificationTimer_.stop();
    if (serial_.isOpen()) serial_.close();
    pendingRx_.clear();
    if (running_) {
        running_ = false;
        emit runningChanged();
    }
    if (profileDeploying_ || profileArmed_) {
        profileDeploying_ = false;
        profileArmed_ = false;
        emit profileStateChanged();
    }
    resetTelemetry();
    const bool wasVerified = deviceVerified_;
    deviceVerified_ = false;
    clearIdentity();
    discovering_ = false;
    automaticConnection_ = false;
    identifyAttempts_ = 0;
    lastIdentificationPort_.clear();
    setIdentificationState(IdentificationState::Idle);
    ptpAvailable_ = false;
    ptpRunning_ = false;
    ptpStatus_ = QStringLiteral("Waiting for device");
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

    profileDeploying_ = true;
    profileArmed_ = false;
    emit profileStateChanged();

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

    for (const auto& command : commands) {
        if (!sendCommand(command)) {
            profileDeploying_ = false;
            emit profileStateChanged();
            return false;
        }
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

bool DeviceController::sendIdentifyProbe() {
    if (!serial_.isOpen() || !identityRetryAllowed(identifyAttempts_)) return false;
    ++identifyAttempts_;
    emit identificationStateChanged();
    setDiscoveryState(
        QStringLiteral("Verifying ARStack semantic identity… attempt %1/%2")
            .arg(identifyAttempts_)
            .arg(identityMaxAttempts()),
        true);
    return sendCommand(QStringLiteral("IDENTIFY"));
}

bool DeviceController::sendCommand(const QString& command) {
    if (!serial_.isOpen()) {
        setError(QStringLiteral("Device is not connected."));
        return false;
    }
    const QByteArray bytes = command.toUtf8() + '\n';
    if (serial_.write(bytes) < 0) {
        setError(serial_.errorString());
        return false;
    }
    appendLog(QStringLiteral("→"), command);
    return true;
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

void DeviceController::finishIdentificationTimeout() {
    if (deviceVerified_) return;

    const QString timedOutPort = serial_.portName().trimmed().isEmpty()
        ? lastIdentificationPort_
        : serial_.portName();
    const int attempts = identifyAttempts_;
    const bool wasAutomatic = automaticConnection_;
    const bool continueProbing = wasAutomatic && genericProbeActive_ && !probeQueue_.isEmpty();

    disconnectPort();
    lastIdentificationPort_ = timedOutPort;
    identifyAttempts_ = attempts;
    setIdentificationState(IdentificationState::Unidentified);
    setDiscoveryState(
        QStringLiteral("No ARStack semantic identity received from %1 after %2 bounded attempts.")
            .arg(timedOutPort.isEmpty() ? QStringLiteral("the selected port") : timedOutPort)
            .arg(attempts),
        false);

    if (continueProbing) {
        QTimer::singleShot(0, this, [this] { static_cast<void>(tryNextProbe()); });
        return;
    }
    genericProbeActive_ = false;
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
    verificationTimer_.stop();
    deviceVerified_ = true;
    automaticConnection_ = false;
    genericProbeActive_ = false;
    probeQueue_.clear();
    lastIdentificationPort_ = serial_.portName();
    setIdentificationState(IdentificationState::Verified);
    setDiscoveryState(QStringLiteral("ARStack ESP32-P4 identity verified."), false);
    emit deviceVerifiedChanged();
    emit deviceMessage(QStringLiteral("ESP32-P4 recognized. Device is ready."));
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

    if (line.contains(QStringLiteral("PTP unavailable"), Qt::CaseInsensitive)) {
        ptpAvailable_ = false;
        ptpRunning_ = false;
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
