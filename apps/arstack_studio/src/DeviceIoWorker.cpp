// SPDX-License-Identifier: GPL-3.0-or-later

#include "DeviceIoWorker.hpp"

#include "DeviceController.hpp"

#include <QSerialPort>
#include <QSerialPortInfo>
#include <QThread>
#include <QTimer>

#include <algorithm>

namespace {
constexpr qint32 kBaudRate = 115200;
constexpr qsizetype kMaxRxBytesWithoutLine = 16384;
constexpr qsizetype kMaxCommandBytes = 512;

struct PortCandidate final {
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
} // namespace

DeviceIoWorker::DeviceIoWorker(QObject* parent) : QObject(parent) {}

void DeviceIoWorker::initialize() {
    if (initialized_) return;
    initialized_ = true;
    shuttingDown_ = false;

    // S4 construction rule: these objects are deliberately created here,
    // after DeviceIoWorker has moved to its dedicated thread.
    serial_ = new QSerialPort(this);
    verificationTimer_ = new QTimer(this);
    presenceTimer_ = new QTimer(this);
    heartbeatTimer_ = new QTimer(this);

    verificationTimer_->setSingleShot(true);
    verificationTimer_->setInterval(identityRetryIntervalMs());
    connect(verificationTimer_, &QTimer::timeout, this, [this] {
        if (serial_ == nullptr || !serial_->isOpen() || identityConfirmed_) return;
        if (identifyAttempts_ < identityMaxAttempts() && sendIdentifyProbe()) {
            verificationTimer_->start();
            return;
        }
        finishIdentificationTimeout();
    });

    presenceTimer_->setSingleShot(false);
    presenceTimer_->setInterval(presencePollIntervalMs());
    connect(presenceTimer_, &QTimer::timeout, this, [this] {
        refreshPortsInternal(false);
    });

    heartbeatTimer_->setSingleShot(false);
    heartbeatTimer_->setInterval(heartbeatIntervalMs());
    connect(heartbeatTimer_, &QTimer::timeout, this, [this] {
        if (!heartbeatEnabled_ || serial_ == nullptr || !serial_->isOpen() || !identityConfirmed_) {
            heartbeatTimer_->stop();
            return;
        }
        // Heartbeat is observational/safety maintenance and must never break an
        // exclusive profile transaction. If the exclusive gate is active this
        // tick is simply coalesced away; the next timer tick retries.
        static_cast<void>(enqueueCommandsInternal(
            {QStringLiteral("HEARTBEAT")}, true, false, false));
    });

    connect(serial_, &QSerialPort::readyRead, this, &DeviceIoWorker::processReadyRead);
    connect(serial_, &QSerialPort::bytesWritten, this, [this](qint64) {
        if (serial_ != nullptr && writeActive_ && serial_->bytesToWrite() == 0) {
            completeActiveWrite();
        }
    });
    connect(serial_, &QSerialPort::errorOccurred, this, [this](const QSerialPort::SerialPortError error) {
        if (error == QSerialPort::NoError || serial_ == nullptr || !serial_->isOpen()) return;
        const QString message = serial_->errorString();
        switch (error) {
        case QSerialPort::ResourceError:
        case QSerialPort::DeviceNotFoundError:
        case QSerialPort::PermissionError:
        case QSerialPort::ReadError:
        case QSerialPort::WriteError:
            handleSerialFailure(message);
            return;
        default:
            emit transportError(message, false);
            return;
        }
    });

    presenceTimer_->start();
    refreshPortsInternal(true);
    emit ready(serial_->thread() == QThread::currentThread() && thread() == QThread::currentThread());
}

void DeviceIoWorker::shutdown(const bool requestStop) {
    if (!initialized_ || shuttingDown_) return;
    shuttingDown_ = true;

    if (verificationTimer_ != nullptr) verificationTimer_->stop();
    if (presenceTimer_ != nullptr) presenceTimer_->stop();
    if (heartbeatTimer_ != nullptr) heartbeatTimer_->stop();
    heartbeatEnabled_ = false;
    probeQueue_.clear();
    genericProbeActive_ = false;

    // Graceful application shutdown is the only place this worker performs a
    // short bounded wait. Normal UI/device operations never block the GUI.
    if (requestStop && serial_ != nullptr && serial_->isOpen()) {
        const QByteArray stopBytes{"STOP\n"};
        if (serial_->write(stopBytes) == stopBytes.size()) {
            static_cast<void>(serial_->waitForBytesWritten(120));
        }
    }

    commandQueue_.clear();
    exclusiveCommandsRemaining_ = 0;
    writeActive_ = false;
    activeCommand_ = {};
    closePortInternal(true);
}

void DeviceIoWorker::refreshPorts() {
    if (!initialized_ || shuttingDown_) return;
    refreshPortsInternal(true);
}

void DeviceIoWorker::refreshPortsInternal(const bool forceSignal) {
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
    const bool changed = discovered != ports_ || recommended != recommendedPort_ ||
        highConfidence.size() != highConfidenceCount_;

    ports_ = std::move(discovered);
    recommendedPort_ = recommended;
    highConfidenceCount_ = highConfidence.size();
    if (forceSignal || changed) {
        emit portsObserved(ports_, recommendedPort_, highConfidenceCount_);
    }
}

void DeviceIoWorker::autoDetectAndConnect() {
    if (!initialized_ || shuttingDown_) return;
    if (serial_ != nullptr && serial_->isOpen()) return;

    refreshPortsInternal(true);
    probeQueue_.clear();
    genericProbeActive_ = false;

    if (!recommendedPort_.isEmpty()) {
        if (!openPortInternal(recommendedPort_, true)) emit automaticProbeExhausted();
        return;
    }
    if (ports_.isEmpty()) {
        emit automaticProbeExhausted();
        return;
    }

    probeQueue_ = ports_;
    genericProbeActive_ = true;
    tryNextProbe();
}

void DeviceIoWorker::connectPort(const QString& portName) {
    if (!initialized_ || shuttingDown_) return;
    probeQueue_.clear();
    genericProbeActive_ = false;
    const QString requested = portName.trimmed();
    if (requested.isEmpty()) {
        emit portOpenFailed({}, QStringLiteral("Select a serial port first."), false);
        return;
    }
    static_cast<void>(openPortInternal(requested, false));
}

void DeviceIoWorker::tryNextProbe() {
    if (shuttingDown_) return;
    while (!probeQueue_.isEmpty()) {
        const QString next = probeQueue_.takeFirst();
        if (openPortInternal(next, true)) return;
    }
    genericProbeActive_ = false;
    emit automaticProbeExhausted();
}

bool DeviceIoWorker::openPortInternal(const QString& portName, const bool automatic) {
    if (serial_ == nullptr || portName.trimmed().isEmpty()) return false;
    if (serial_->isOpen()) closePortInternal(true);

    automaticConnection_ = automatic;
    identityConfirmed_ = false;
    identifyAttempts_ = 0;
    pendingRx_.clear();
    commandQueue_.clear();
    exclusiveCommandsRemaining_ = 0;
    writeActive_ = false;
    activeCommand_ = {};
    lastPort_ = portName.trimmed();

    emit openingPort(lastPort_, automatic);

    serial_->setPortName(lastPort_);
    serial_->setBaudRate(kBaudRate);
    serial_->setDataBits(QSerialPort::Data8);
    serial_->setParity(QSerialPort::NoParity);
    serial_->setStopBits(QSerialPort::OneStop);
    serial_->setFlowControl(QSerialPort::NoFlowControl);

    if (!serial_->open(QIODevice::ReadWrite)) {
        const QString message = QStringLiteral("Cannot open %1: %2").arg(lastPort_, serial_->errorString());
        emit portOpenFailed(lastPort_, message, automatic);
        automaticConnection_ = false;
        return false;
    }

    emit portOpened(lastPort_, automatic);
    if (!sendIdentifyProbe()) {
        finishIdentificationTimeout();
        return false;
    }
    verificationTimer_->start();
    return true;
}

void DeviceIoWorker::disconnectPort() {
    if (!initialized_) return;
    probeQueue_.clear();
    genericProbeActive_ = false;
    automaticConnection_ = false;
    closePortInternal(true);
}

void DeviceIoWorker::closePortInternal(const bool emitRelease) {
    if (serial_ == nullptr) return;
    if (verificationTimer_ != nullptr) verificationTimer_->stop();
    if (heartbeatTimer_ != nullptr) heartbeatTimer_->stop();
    heartbeatEnabled_ = false;
    identityConfirmed_ = false;
    identifyAttempts_ = 0;
    pendingRx_.clear();
    commandQueue_.clear();
    exclusiveCommandsRemaining_ = 0;
    writeActive_ = false;
    activeCommand_ = {};

    const bool wasOpen = serial_->isOpen();
    const QString releasedPort = serial_->portName().trimmed().isEmpty() ? lastPort_ : serial_->portName();
    if (wasOpen) serial_->close();
    if (wasOpen) emit portClosed(releasedPort);
    if (emitRelease && wasOpen) emit portReleased(releasedPort);
}

bool DeviceIoWorker::sendIdentifyProbe() {
    if (serial_ == nullptr || !serial_->isOpen() || identityConfirmed_ ||
        identifyAttempts_ >= identityMaxAttempts()) {
        return false;
    }

    ++identifyAttempts_;
    emit identificationAttempt(serial_->portName(), identifyAttempts_, identityMaxAttempts());
    return enqueueCommandsInternal(
        {QStringLiteral("IDENTIFY")}, false, false, true);
}

void DeviceIoWorker::finishIdentificationTimeout() {
    if (serial_ == nullptr || identityConfirmed_) return;

    const QString timedOutPort = serial_->portName().trimmed().isEmpty()
        ? lastPort_
        : serial_->portName();
    const int attempts = identifyAttempts_;
    const bool continueProbing = automaticConnection_ && genericProbeActive_ && !probeQueue_.isEmpty();

    closePortInternal(true);
    emit identificationTimedOut(timedOutPort, attempts, continueProbing);

    if (continueProbing) {
        QTimer::singleShot(0, this, &DeviceIoWorker::tryNextProbe);
        return;
    }
    genericProbeActive_ = false;
    automaticConnection_ = false;
}

void DeviceIoWorker::confirmIdentity() {
    if (serial_ == nullptr || !serial_->isOpen()) return;
    identityConfirmed_ = true;
    automaticConnection_ = false;
    genericProbeActive_ = false;
    probeQueue_.clear();
    if (verificationTimer_ != nullptr) verificationTimer_->stop();
}

void DeviceIoWorker::enqueueCommands(
    const QStringList& commands,
    const bool quiet,
    const bool exclusive) {
    static_cast<void>(enqueueCommandsInternal(commands, quiet, exclusive, true));
}

bool DeviceIoWorker::enqueueCommandsInternal(
    const QStringList& commands,
    const bool quiet,
    const bool exclusive,
    const bool reportRejection) {
    if (serial_ == nullptr || !serial_->isOpen()) {
        if (reportRejection) emit commandRejected(QStringLiteral("Device is not connected."));
        return false;
    }
    if (commands.isEmpty()) return true;

    if (exclusiveCommandsRemaining_ > 0) {
        if (reportRejection) {
            emit commandRejected(exclusive
                ? QStringLiteral("Another exclusive device transaction is already pending.")
                : QStringLiteral("Device configuration is locked while an exclusive profile transaction is pending."));
        }
        return false;
    }

    const int outstanding = commandQueue_.size() + (writeActive_ ? 1 : 0);
    if (commands.size() > commandQueueCapacity() - outstanding) {
        if (reportRejection) {
            emit commandRejected(QStringLiteral(
                "Device command queue is full; command batch was rejected without partial enqueue."));
        }
        return false;
    }

    QList<PendingCommand> prepared;
    prepared.reserve(commands.size());
    for (const QString& command : commands) {
        const QString trimmed = command.trimmed();
        if (trimmed.isEmpty() || trimmed.contains(QLatin1Char('\n')) || trimmed.contains(QLatin1Char('\r'))) {
            if (reportRejection) emit commandRejected(QStringLiteral("Invalid device command."));
            return false;
        }
        const QByteArray bytes = trimmed.toUtf8() + '\n';
        if (bytes.size() > kMaxCommandBytes) {
            if (reportRejection) emit commandRejected(QStringLiteral("Device command exceeds the bounded transport size."));
            return false;
        }
        prepared.push_back(PendingCommand{trimmed, bytes, quiet, exclusive});
    }

    if (exclusive) exclusiveCommandsRemaining_ = commands.size();
    for (auto& command : prepared) commandQueue_.enqueue(std::move(command));
    pumpWriteQueue();
    return true;
}

void DeviceIoWorker::pumpWriteQueue() {
    if (serial_ == nullptr || !serial_->isOpen() || writeActive_ || commandQueue_.isEmpty()) return;

    activeCommand_ = commandQueue_.dequeue();
    const qint64 accepted = serial_->write(activeCommand_.bytes);
    if (accepted != activeCommand_.bytes.size()) {
        const QString detail = accepted < 0
            ? serial_->errorString()
            : QStringLiteral("Serial write accepted only %1/%2 bytes.")
                .arg(accepted)
                .arg(activeCommand_.bytes.size());
        writeActive_ = false;
        activeCommand_ = {};
        handleSerialFailure(detail);
        return;
    }
    writeActive_ = true;
    if (serial_->bytesToWrite() == 0) completeActiveWrite();
}

void DeviceIoWorker::completeActiveWrite() {
    if (!writeActive_) return;
    const PendingCommand completed = activeCommand_;
    writeActive_ = false;
    activeCommand_ = {};
    if (completed.exclusive && exclusiveCommandsRemaining_ > 0) {
        --exclusiveCommandsRemaining_;
    }
    emit commandTransmitted(completed.text, completed.quiet);
    pumpWriteQueue();
}

void DeviceIoWorker::processReadyRead() {
    if (serial_ == nullptr || !serial_->isOpen()) return;
    pendingRx_.append(serial_->readAll());
    if (pendingRx_.size() > kMaxRxBytesWithoutLine && pendingRx_.indexOf('\n') < 0) {
        handleSerialFailure(QStringLiteral("Serial receive line exceeded the bounded input size."));
        return;
    }

    while (serial_ != nullptr && serial_->isOpen()) {
        const qsizetype newline = pendingRx_.indexOf('\n');
        if (newline < 0) break;
        QByteArray raw = pendingRx_.left(newline);
        pendingRx_.remove(0, newline + 1);
        if (!raw.isEmpty() && raw.endsWith('\r')) raw.chop(1);
        const QString line = QString::fromUtf8(raw);

        // Use the one existing semantic parser to stop the handshake timer in
        // the transport thread before a GUI-thread callback can race its expiry.
        DeviceIdentity identity;
        if (!identityConfirmed_ && DeviceController::parseIdentityLine(line, identity)) {
            confirmIdentity();
        }
        emit lineReceived(line);
    }
}

void DeviceIoWorker::handleSerialFailure(const QString& message) {
    const QString detail = message.trimmed().isEmpty()
        ? QStringLiteral("Serial transport failed.")
        : message.trimmed();
    emit transportError(detail, true);
    probeQueue_.clear();
    genericProbeActive_ = false;
    automaticConnection_ = false;
    closePortInternal(true);
}

void DeviceIoWorker::setHeartbeatEnabled(const bool enabled) {
    heartbeatEnabled_ = enabled;
    if (heartbeatTimer_ == nullptr) return;

    if (!enabled || serial_ == nullptr || !serial_->isOpen() || !identityConfirmed_) {
        heartbeatTimer_->stop();
        return;
    }

    static_cast<void>(enqueueCommandsInternal(
        {QStringLiteral("HEARTBEAT")}, true, false, false));
    if (!heartbeatTimer_->isActive()) heartbeatTimer_->start();
}
