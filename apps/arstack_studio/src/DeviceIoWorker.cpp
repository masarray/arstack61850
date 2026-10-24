// SPDX-License-Identifier: GPL-3.0-or-later

#include "DeviceIoWorker.hpp"

#include "DeviceController.hpp"

#include <QDebug>
#include <QElapsedTimer>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QSettings>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace {
constexpr qint32 kBaudRate = 115200;
constexpr qsizetype kMaxRxBytesWithoutLine = 16384;
constexpr qsizetype kMaxCommandBytes = 512;

struct PortCandidate final {
    QString name;
    int confidence{};
};

[[nodiscard]] int portConfidence(const QSerialPortInfo& info) {
    return DeviceIoWorker::portConfidenceForMetadata(
        info.hasVendorIdentifier(),
        info.hasVendorIdentifier() ? info.vendorIdentifier() : 0U,
        info.hasProductIdentifier(),
        info.hasProductIdentifier() ? info.productIdentifier() : 0U,
        info.description(),
        info.manufacturer(),
        info.serialNumber());
}
} // namespace

DeviceIoWorker::DeviceIoWorker(QObject* parent) : QObject(parent) {}

void DeviceIoWorker::initialize() {
    if (initialized_) return;
    initialized_ = true;
    shuttingDown_ = false;

    serial_ = new QSerialPort(this);
    verificationTimer_ = new QTimer(this);
    presenceTimer_ = new QTimer(this);
    heartbeatTimer_ = new QTimer(this);
    writeGapTimer_ = new QTimer(this);
    writeGapTimer_->setSingleShot(true);
    writeGapTimer_->setInterval(serialCommandGapMs());
    connect(writeGapTimer_, &QTimer::timeout, this, &DeviceIoWorker::pumpWriteQueue);

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
        queuePriorityHeartbeat();
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
        if (serialErrorForcesTransportLoss(error)) {
            handleSerialFailure(message);
            return;
        }
        emit transportError(activeGeneration_, message, false);
    });

    presenceTimer_->start();
    refreshPortsInternal(true);
    emit ready(activeGeneration_,
               serial_->thread() == QThread::currentThread() && thread() == QThread::currentThread());
}

bool DeviceIoWorker::adoptGeneration(const quint64 generation, const bool closeOldSession) {
    if (generation == 0) return false;
    if (generation == activeGeneration_) return true;
    if (serial_ != nullptr && serial_->isOpen()) {
        if (!closeOldSession) return false;
        closePortInternal(true);
    }
    activeGeneration_ = generation;
    return true;
}

void DeviceIoWorker::shutdown(const bool requestSvStop, const bool requestPtpStop) {
    if (!initialized_ || shuttingDown_) return;
    shuttingDown_ = true;

    if (verificationTimer_ != nullptr) verificationTimer_->stop();
    if (presenceTimer_ != nullptr) presenceTimer_->stop();
    if (heartbeatTimer_ != nullptr) heartbeatTimer_->stop();
    if (writeGapTimer_ != nullptr) writeGapTimer_->stop();
    heartbeatEnabled_ = false;
    heartbeatPending_ = false;
    probeQueue_.clear();
    genericProbeActive_ = false;

    // Stop both independent publishers before handing the COM port to an
    // installer. Keep SV stop ahead of PTP stop so no new SV frame is sent
    // during grandmaster shutdown. This runs once during teardown only.
    commandQueue_.clear();
    ptpStopPending_ = false;
    svStopPending_ = false;
    if ((requestSvStop || requestPtpStop) && serial_ != nullptr && serial_->isOpen()) {
        const auto sendBoundedStop = [this](const QByteArray& command) {
            const qint64 queued = serial_->write(command);
            if (queued != command.size()) {
                qWarning().noquote() << "Device shutdown: bounded STOP write was rejected.";
                return;
            }
            QElapsedTimer deadline;
            deadline.start();
            while (serial_->bytesToWrite() > 0 && deadline.elapsed() < 120) {
                const int remaining = 120 - static_cast<int>(deadline.elapsed());
                if (!serial_->waitForBytesWritten(remaining)) break;
            }
            if (serial_->bytesToWrite() > 0) {
                qWarning().noquote()
                    << "Device shutdown: STOP bytes remained queued after the bounded drain.";
            }
        };
        if (requestSvStop) sendBoundedStop(QByteArrayLiteral("STOP\n"));
        if (requestPtpStop) sendBoundedStop(QByteArrayLiteral("PTP STOP\n"));
    }

    exclusiveCommandsRemaining_ = 0;
    writeActive_ = false;
    activeCommand_ = {};
    closePortInternal(true);
}

void DeviceIoWorker::refreshPorts(const quint64 generation) {
    if (!initialized_ || shuttingDown_) return;
    if (generation != 0 && generation != activeGeneration_) {
        if (serial_ != nullptr && serial_->isOpen()) return;
        activeGeneration_ = generation;
    }
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
    if (forceSignal || changed)
        emit portsObserved(activeGeneration_, ports_, recommendedPort_, highConfidenceCount_);

    // Windows USB bridges can keep QSerialPort::isOpen() true after removal.
    // Reuse the existing 750-ms enumeration; do not poll the board or PTP.
    // Confirm two consecutive missing OS snapshots before retiring COM.
    if (serial_ == nullptr || !serial_->isOpen() || !identityConfirmed_) {
        consecutiveMissingPortPolls_ = 0;
        return;
    }
    const QString ownedPort = serial_->portName().trimmed();
    if (!ownedPort.isEmpty() && ports_.contains(ownedPort, Qt::CaseInsensitive)) {
        consecutiveMissingPortPolls_ = 0;
        return;
    }
    if (consecutiveMissingPortPolls_ < missingPortConfirmationPolls())
        ++consecutiveMissingPortPolls_;
    if (verifiedPortPhysicallyGone(
            serial_->isOpen(), identityConfirmed_, consecutiveMissingPortPolls_)) {
        // Only release the stale serial transport. NEVER issue SV/PTP STOP,
        // firmware reset, or START based on USB enumeration.
        handleSerialFailure(QStringLiteral(
            "Verified USB serial port %1 vanished; releasing stale COM handle for rediscovery.")
            .arg(ownedPort));
    }
}

void DeviceIoWorker::autoDetectAndConnect(const quint64 generation) {
    if (!initialized_ || shuttingDown_ || !adoptGeneration(generation, true)) return;
    if (serial_ != nullptr && serial_->isOpen()) return;

    refreshPortsInternal(true);
    probeQueue_.clear();
    genericProbeActive_ = false;

    // Prefer the port which previously returned a genuine ARStack identity,
    // but never treat its COM number or USB metadata as proof of identity.
    // A stale hint MUST fall through to other currently visible candidates.
    const QSettings settings;
    const QString verifiedPort = settings.value(
        QStringLiteral("device-discovery/lastVerifiedPort")).toString().trimmed();
    probeQueue_ = orderedDiscoveryPorts(
        ports_, verifiedPort, recommendedPort_);
    genericProbeActive_ = !probeQueue_.isEmpty();
    if (genericProbeActive_) tryNextProbe();
    else emit automaticProbeExhausted(activeGeneration_);
}

void DeviceIoWorker::connectPort(const QString& portName, const quint64 generation) {
    if (!initialized_ || shuttingDown_ || !adoptGeneration(generation, true)) return;
    probeQueue_.clear();
    genericProbeActive_ = false;
    const QString requested = portName.trimmed();
    if (requested.isEmpty()) {
        emit portOpenFailed(activeGeneration_, {}, QStringLiteral("Select a serial port first."), false);
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
    emit automaticProbeExhausted(activeGeneration_);
}

bool DeviceIoWorker::openPortInternal(const QString& portName, const bool automatic) {
    if (serial_ == nullptr || activeGeneration_ == 0 || portName.trimmed().isEmpty()) return false;
    if (serial_->isOpen()) closePortInternal(true);

    automaticConnection_ = automatic;
    identityConfirmed_ = false;
    identifyAttempts_ = 0;
    consecutiveMissingPortPolls_ = 0;
    pendingRx_.clear();
    if (writeGapTimer_ != nullptr) writeGapTimer_->stop();
    commandQueue_.clear();
    ptpStopPending_ = false;
    svStopPending_ = false;
    exclusiveCommandsRemaining_ = 0;
    writeActive_ = false;
    activeCommand_ = {};
    lastPort_ = portName.trimmed();

    emit openingPort(activeGeneration_, lastPort_, automatic);

    serial_->setPortName(lastPort_);
    serial_->setBaudRate(kBaudRate);
    serial_->setDataBits(QSerialPort::Data8);
    serial_->setParity(QSerialPort::NoParity);
    serial_->setStopBits(QSerialPort::OneStop);
    serial_->setFlowControl(QSerialPort::NoFlowControl);

    if (!serial_->open(QIODevice::ReadWrite)) {
        const QString message = QStringLiteral("Cannot open %1: %2").arg(lastPort_, serial_->errorString());
        emit portOpenFailed(activeGeneration_, lastPort_, message, automatic);
        automaticConnection_ = false;
        return false;
    }

    emit portOpened(activeGeneration_, lastPort_, automatic);
    if (!sendIdentifyProbe()) {
        finishIdentificationTimeout();
        return false;
    }
    verificationTimer_->start();
    return true;
}

void DeviceIoWorker::disconnectPort(const quint64 generation) {
    if (!initialized_ || generation == 0 || generation != activeGeneration_) return;
    probeQueue_.clear();
    genericProbeActive_ = false;
    automaticConnection_ = false;
    closePortInternal(true);
}

void DeviceIoWorker::retireUnresponsivePort(const quint64 generation) {
    if (!initialized_ || shuttingDown_ || generation == 0 ||
        generation != activeGeneration_ || serial_ == nullptr ||
        !serial_->isOpen() || !identityConfirmed_) return;
    // A bounded failure of bidirectional SHOW/telemetry means the old
    // handle may be a zombie even if Windows has already re-enumerated
    // the same COM number. Do not mistake this for operator DISCONNECT.
    handleSerialFailure(QStringLiteral(
        "Verified serial control path stopped responding; retiring stale COM for recovery."));
}

void DeviceIoWorker::closePortInternal(const bool emitRelease) {
    if (serial_ == nullptr) return;
    if (verificationTimer_ != nullptr) verificationTimer_->stop();
    if (heartbeatTimer_ != nullptr) heartbeatTimer_->stop();
    if (writeGapTimer_ != nullptr) writeGapTimer_->stop();
    heartbeatEnabled_ = false;
    heartbeatPending_ = false;
    identityConfirmed_ = false;
    identifyAttempts_ = 0;
    consecutiveMissingPortPolls_ = 0;
    pendingRx_.clear();
    commandQueue_.clear();
    ptpStopPending_ = false;
    svStopPending_ = false;
    exclusiveCommandsRemaining_ = 0;
    writeActive_ = false;
    activeCommand_ = {};

    const quint64 generation = activeGeneration_;
    const bool wasOpen = serial_->isOpen();
    const QString releasedPort = serial_->portName().trimmed().isEmpty() ? lastPort_ : serial_->portName();
    if (wasOpen) serial_->close();
    if (wasOpen) emit portClosed(generation, releasedPort);
    if (emitRelease && wasOpen) emit portReleased(generation, releasedPort);
}

bool DeviceIoWorker::sendIdentifyProbe() {
    if (serial_ == nullptr || !serial_->isOpen() || identityConfirmed_ || activeGeneration_ == 0 ||
        identifyAttempts_ >= identityMaxAttempts()) {
        return false;
    }

    ++identifyAttempts_;
    emit identificationAttempt(
        activeGeneration_, serial_->portName(), identifyAttempts_, identityMaxAttempts());
    return enqueueCommandsInternal(
        {QStringLiteral("IDENTIFY")}, false, false, true);
}

void DeviceIoWorker::finishIdentificationTimeout() {
    if (serial_ == nullptr || identityConfirmed_) return;

    const quint64 generation = activeGeneration_;
    const QString timedOutPort = serial_->portName().trimmed().isEmpty()
        ? lastPort_
        : serial_->portName();
    const int attempts = identifyAttempts_;
    const bool continueProbing = automaticConnection_ && genericProbeActive_ && !probeQueue_.isEmpty();

    closePortInternal(true);
    emit identificationTimedOut(generation, timedOutPort, attempts, continueProbing);

    if (continueProbing) {
        QTimer::singleShot(0, this, &DeviceIoWorker::tryNextProbe);
        return;
    }
    genericProbeActive_ = false;
    automaticConnection_ = false;
}

void DeviceIoWorker::confirmIdentity(const quint64 generation) {
    if (serial_ == nullptr || !serial_->isOpen() || generation != activeGeneration_) return;
    identityConfirmed_ = true;
    consecutiveMissingPortPolls_ = 0;
    automaticConnection_ = false;
    genericProbeActive_ = false;
    probeQueue_.clear();
    if (verificationTimer_ != nullptr) verificationTimer_->stop();
    // Remember a port only AFTER its ARStack identity has been parsed.
    // It is just a priority hint for the next Studio start, never a bypass.
    QSettings settings;
    settings.setValue(
        QStringLiteral("device-discovery/lastVerifiedPort"),
        serial_->portName());
}

void DeviceIoWorker::enqueueCommands(
    const QStringList& commands,
    const bool quiet,
    const bool exclusive,
    const quint64 generation) {
    if (generation == 0 || generation != activeGeneration_) {
        emit commandRejected(generation, QStringLiteral("Stale device-session generation rejected."));
        return;
    }
    static_cast<void>(enqueueCommandsInternal(commands, quiet, exclusive, true));
}

bool DeviceIoWorker::enqueueCommandsInternal(
    const QStringList& commands,
    const bool quiet,
    const bool exclusive,
    const bool reportRejection) {
    if (serial_ == nullptr || !serial_->isOpen()) {
        if (reportRejection) emit commandRejected(activeGeneration_, QStringLiteral("Device is not connected."));
        return false;
    }
    if (commands.isEmpty()) return true;

    if (!exclusive && commands.size() == 1 &&
        commands.constFirst().trimmed() == QStringLiteral("STOP")) {
        // A STOP must not wait behind a saturated configuration queue.
        queuePrioritySvStop();
        return true;
    }
    if (!exclusive && commands.size() == 1 &&
        commands.constFirst().trimmed() == QStringLiteral("PTP STOP")) {
        // One reserved safety slot cannot be rejected by normal queue saturation.
        queuePriorityPtpStop();
        return true;
    }

    if (exclusiveCommandsRemaining_ > 0) {
        if (reportRejection) {
            emit commandRejected(activeGeneration_, exclusive
                ? QStringLiteral("Another exclusive device transaction is already pending.")
                : QStringLiteral("Device configuration is locked while an exclusive profile transaction is pending."));
        }
        return false;
    }

    const int outstanding = commandQueue_.size() + (writeActive_ ? 1 : 0);
    if (commands.size() > commandQueueCapacity() - outstanding) {
        if (reportRejection) {
            emit commandRejected(activeGeneration_, QStringLiteral(
                "Device command queue is full; command batch was rejected without partial enqueue."));
        }
        return false;
    }

    QList<PendingCommand> prepared;
    prepared.reserve(commands.size());
    for (const QString& command : commands) {
        const QString trimmed = command.trimmed();
        if (trimmed.isEmpty() || trimmed.contains(QLatin1Char('\n')) || trimmed.contains(QLatin1Char('\r'))) {
            if (reportRejection) emit commandRejected(activeGeneration_, QStringLiteral("Invalid device command."));
            return false;
        }
        const QByteArray bytes = trimmed.toUtf8() + '\n';
        if (bytes.size() > kMaxCommandBytes) {
            if (reportRejection) emit commandRejected(activeGeneration_, QStringLiteral("Device command exceeds the bounded transport size."));
            return false;
        }
        prepared.push_back(PendingCommand{trimmed, bytes, quiet, exclusive});
    }

    if (exclusive) exclusiveCommandsRemaining_ = commands.size();
    for (auto& command : prepared) commandQueue_.enqueue(std::move(command));
    pumpWriteQueue();
    return true;
}

void DeviceIoWorker::queuePrioritySvStop() {
    // The emergency STOP slot is outside the 64-entry normal queue.
    // Discard remaining batch commands, including an unsent START, so
    // cancellation cannot accidentally complete a stale configuration.
    commandQueue_.clear();
    exclusiveCommandsRemaining_ = (writeActive_ && activeCommand_.exclusive) ? 1 : 0;
    if (!(writeActive_ && activeCommand_.text == QStringLiteral("STOP")))
        svStopPending_ = true;
    pumpWriteQueue();
}

void DeviceIoWorker::queuePriorityHeartbeat() {
    // The 6-second firmware control lease must not be starved by a saturated
    // 64-command queue or an exclusive profile CFG transaction. A single
    // coalesced keepalive uses the same QSerialPort owner and normal pacing;
    // it never bypasses a pending safety STOP or interrupts an active write.
    // A disconnected/stalled serial port still fails closed in firmware.
    if (!(writeActive_ && activeCommand_.text == QStringLiteral("HEARTBEAT")))
        heartbeatPending_ = true;
    pumpWriteQueue();
}

void DeviceIoWorker::queuePriorityPtpStop() {
    // Bounded (64 entries) and infrequent: revoke every unsent PTP START.
    // Preserve all other queued commands; STOP runs after the active write.
    QQueue<PendingCommand> retained;
    while (!commandQueue_.isEmpty()) {
        PendingCommand command = commandQueue_.dequeue();
        if (command.text == QStringLiteral("PTP START")) {
            if (command.exclusive && exclusiveCommandsRemaining_ > 0) {
                --exclusiveCommandsRemaining_;
            }
            continue;
        }
        retained.enqueue(std::move(command));
    }
    commandQueue_ = std::move(retained);
    if (!(writeActive_ && activeCommand_.text == QStringLiteral("PTP STOP"))) {
        ptpStopPending_ = true; // coalesce repeated urgent STOP intents
    }
    pumpWriteQueue();
}

void DeviceIoWorker::pumpWriteQueue() {
    if (serial_ == nullptr || !serial_->isOpen() || writeActive_ ||
        (!svStopPending_ && !ptpStopPending_ && !heartbeatPending_ &&
         commandQueue_.isEmpty())) return;
    // bytesToWrite()==0 proves only host/driver drain, not ESP console line
    // consumption. Throttle ordinary writes; reserved STOPs always bypass.
    if (!svStopPending_ && !ptpStopPending_ && writeGapTimer_ != nullptr &&
        writeGapTimer_->isActive()) return;
    if ((svStopPending_ || ptpStopPending_) && writeGapTimer_ != nullptr)
        writeGapTimer_->stop();

    if (svStopPending_) {
        svStopPending_ = false;
        activeCommand_ = PendingCommand{
            QStringLiteral("STOP"), QByteArrayLiteral("STOP\n"), false, false};
    } else if (ptpStopPending_) {
        ptpStopPending_ = false;
        activeCommand_ = PendingCommand{
            QStringLiteral("PTP STOP"), QByteArrayLiteral("PTP STOP\n"), false, false};
    } else if (heartbeatPending_) {
        heartbeatPending_ = false;
        activeCommand_ = PendingCommand{
            QStringLiteral("HEARTBEAT"), QByteArrayLiteral("HEARTBEAT\n"), true, false};
    } else {
        activeCommand_ = commandQueue_.dequeue();
    }
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
    emit commandTransmitted(activeGeneration_, completed.text, completed.quiet);
    if (svStopPending_ || ptpStopPending_) {
        pumpWriteQueue(); // safety priority never waits for line pacing
    } else if (writeGapTimer_ != nullptr) {
        writeGapTimer_->start();
    } else {
        pumpWriteQueue();
    }
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

        DeviceIdentity identity;
        if (!identityConfirmed_ && DeviceController::parseIdentityLine(line, identity)) {
            confirmIdentity(activeGeneration_);
        }
        emit lineReceived(activeGeneration_, line);
    }
}

void DeviceIoWorker::handleSerialFailure(const QString& message) {
    const QString detail = message.trimmed().isEmpty()
        ? QStringLiteral("Serial transport failed.")
        : message.trimmed();
    const bool hadVerifiedIdentity = identityConfirmed_;
    const quint64 lostGeneration = activeGeneration_;
    const QString lostPort = serial_ != nullptr ? serial_->portName() : lastPort_;
    emit transportError(lostGeneration, detail, true);
    probeQueue_.clear();
    genericProbeActive_ = false;
    automaticConnection_ = false;
    // Close and release the handle BEFORE asking the GUI for new discovery.
    closePortInternal(true);
    if (hadVerifiedIdentity && lostGeneration != 0U)
        emit unexpectedPortLoss(lostGeneration, lostPort);
}

void DeviceIoWorker::setHeartbeatEnabled(const bool enabled, const quint64 generation) {
    if (generation == 0 || generation != activeGeneration_) return;
    heartbeatEnabled_ = enabled;
    if (heartbeatTimer_ == nullptr) return;

    if (!enabled || serial_ == nullptr || !serial_->isOpen() || !identityConfirmed_) {
        heartbeatPending_ = false;
        heartbeatTimer_->stop();
        return;
    }

    queuePriorityHeartbeat();
    if (!heartbeatTimer_->isActive()) heartbeatTimer_->start();
}
