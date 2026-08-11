// SPDX-License-Identifier: GPL-3.0-or-later

#include "DeviceController.hpp"

#include <QDateTime>
#include <QRegularExpression>
#include <QSerialPortInfo>

#include <algorithm>
#include <cmath>
#include <limits>

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

    refreshPorts();
}

QStringList DeviceController::ports() const { return ports_; }
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

void DeviceController::refreshPorts() {
    QStringList discovered;
    for (const auto& info : QSerialPortInfo::availablePorts()) {
        discovered.push_back(info.portName());
    }
    discovered.removeDuplicates();
    std::sort(discovered.begin(), discovered.end(), [](const QString& left, const QString& right) {
        return left.localeAwareCompare(right) < 0;
    });
    if (discovered == ports_) return;
    ports_ = std::move(discovered);
    emit portsChanged();
}

bool DeviceController::connectPort(const QString& portName) {
    if (portName.trimmed().isEmpty()) {
        setError(QStringLiteral("Select a serial port first."));
        return false;
    }
    if (serial_.isOpen()) disconnectPort();

    serial_.setPortName(portName.trimmed());
    serial_.setBaudRate(kBaudRate);
    serial_.setDataBits(QSerialPort::Data8);
    serial_.setParity(QSerialPort::NoParity);
    serial_.setStopBits(QSerialPort::OneStop);
    serial_.setFlowControl(QSerialPort::NoFlowControl);

    if (!serial_.open(QIODevice::ReadWrite)) {
        setError(QStringLiteral("Cannot open %1: %2").arg(portName, serial_.errorString()));
        return false;
    }

    pendingRx_.clear();
    lastError_.clear();
    running_ = false;
    profileArmed_ = false;
    profileDeploying_ = false;
    resetTelemetry();
    appendLog(QStringLiteral("•"), QStringLiteral("Connected %1 at 115200 8N1").arg(serial_.portName()));
    emit connectedChanged();
    emit runningChanged();
    emit lastErrorChanged();
    emit profileStateChanged();
    static_cast<void>(sendCommand(QStringLiteral("SHOW")));
    static_cast<void>(sendCommand(QStringLiteral("PROFILE SHOW")));
    return true;
}

void DeviceController::disconnectPort() {
    const bool wasConnected = serial_.isOpen();
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
    if (!finitePositive(hz) || hz > 1000.0) {
        setError(QStringLiteral("Frequency must be within 0..1000 Hz."));
        return false;
    }
    const auto millihertz = std::llround(hz * 1000.0);
    if (millihertz <= 0 || millihertz > std::numeric_limits<quint32>::max()) return false;
    return sendCommand(QStringLiteral("FREQ %1").arg(millihertz));
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
    if (!validIds.contains(id) || !std::isfinite(magnitude) || magnitude < 0.0 || !std::isfinite(phaseDegrees)) {
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
    if (!connected()) {
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

void DeviceController::clearLog() {
    if (logText_.isEmpty()) return;
    logText_.clear();
    emit logTextChanged();
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
