// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QObject>
#include <QQueue>
#include <QSerialPort>
#include <QStringList>

#include <utility>

class QTimer;

// Long-lived serial transport owner for ARStack Studio.
//
// S4 invariant: QSerialPort and every timer that directly drives serial I/O are
// created only after this object has moved to its dedicated thread.
// S5 invariant: every transport event carries the supervisor generation that
// authorized the session; stale generations are rejected/ignored explicitly.
class DeviceIoWorker final : public QObject {
    Q_OBJECT

public:
    explicit DeviceIoWorker(QObject* parent = nullptr);

    [[nodiscard]] static constexpr int commandQueueCapacity() noexcept { return 64; }
    [[nodiscard]] static constexpr int presencePollIntervalMs() noexcept { return 750; }
    [[nodiscard]] static constexpr int identityMaxAttempts() noexcept { return 3; }
    [[nodiscard]] static constexpr int identityRetryIntervalMs() noexcept { return 650; }
    [[nodiscard]] static constexpr int heartbeatIntervalMs() noexcept { return 700; }

    // Fast discovery policy: USB bridge metadata may prioritize a port, but it
    // never verifies device identity. The opened port must still answer the
    // semantic IDENTIFY contract before Studio can become READY.
    [[nodiscard]] static int portConfidenceForMetadata(
        const bool hasVendorIdentifier,
        const quint16 vendorIdentifier,
        const bool hasProductIdentifier,
        const quint16 productIdentifier,
        const QString& description,
        const QString& manufacturer,
        const QString& serialNumber) {
        int score = 0;
        if (hasVendorIdentifier && vendorIdentifier == 0x303AU) score += 100;

        const QString identity = QStringLiteral("%1 %2 %3")
            .arg(description, manufacturer, serialNumber).toLower();

        // The current ARStack ESP32-P4 board exposes its console through a WCH
        // CH343 bridge on Windows. Treat that bridge as a fast transport
        // candidate, not as proof that the device is ARStack.
        const bool saysCh343 = identity.contains(QStringLiteral("ch343"));
        const bool wchCh343 = hasVendorIdentifier && vendorIdentifier == 0x1A86U &&
            hasProductIdentifier && productIdentifier == 0x55D3U;
        if (wchCh343) score += 95;
        else if (saysCh343) score += 85;

        if (identity.contains(QStringLiteral("esp32-p4"))) score += 90;
        else if (identity.contains(QStringLiteral("esp32"))) score += 65;
        if (identity.contains(QStringLiteral("espressif"))) score += 60;
        if (identity.contains(QStringLiteral("usb jtag")) ||
            identity.contains(QStringLiteral("usb serial")) ||
            identity.contains(QStringLiteral("usb-enhanced-serial"))) {
            score += 15;
        }

        // Windows can expose many virtual Bluetooth COM ports. They are poor
        // automatic injector candidates and must never outrank a physical USB
        // bridge. Manual selection remains available.
        if (identity.contains(QStringLiteral("bluetooth"))) score -= 120;
        return score;
    }

    // S8B Windows ownership contract: access/device/resource/read/write errors
    // are session-fatal and must release the transport instead of leaving a
    // stale COM handle alive. Keep this pure so Windows CI can prove the exact
    // Qt error classification without requiring physical USB hardware.
    [[nodiscard]] static constexpr bool serialErrorForcesTransportLoss(
        const QSerialPort::SerialPortError error) noexcept {
        switch (error) {
        case QSerialPort::ResourceError:
        case QSerialPort::DeviceNotFoundError:
        case QSerialPort::PermissionError:
        case QSerialPort::ReadError:
        case QSerialPort::WriteError:
            return true;
        default:
            return false;
        }
    }

    void enqueueCommands(
        const QStringList& commands,
        const bool quiet,
        const quint64 generation) {
        const bool exclusive = commands.size() > 1 &&
            commands.front() == QStringLiteral("PROFILE BEGIN");
        enqueueCommands(commands, quiet, exclusive, generation);
    }

public slots:
    void initialize();
    void shutdown(bool requestStop);
    void refreshPorts(quint64 generation);
    void autoDetectAndConnect(quint64 generation);
    void connectPort(const QString& portName, quint64 generation);
    void disconnectPort(quint64 generation);
    void confirmIdentity(quint64 generation);
    void enqueueCommands(
        const QStringList& commands,
        bool quiet,
        bool exclusive,
        quint64 generation);
    void setHeartbeatEnabled(bool enabled, quint64 generation);

signals:
    void ready(quint64 generation, bool serialAffinityValid);
    void portsObserved(quint64 generation, const QStringList& ports, const QString& recommendedPort, int highConfidenceCount);
    void openingPort(quint64 generation, const QString& portName, bool automatic);
    void portOpened(quint64 generation, const QString& portName, bool automatic);
    void portOpenFailed(quint64 generation, const QString& portName, const QString& message, bool automatic);
    void portClosed(quint64 generation, const QString& portName);
    void portReleased(quint64 generation, const QString& portName);
    void identificationAttempt(quint64 generation, const QString& portName, int attempt, int maximum);
    void identificationTimedOut(quint64 generation, const QString& portName, int attempts, bool continuingProbe);
    void automaticProbeExhausted(quint64 generation);
    void lineReceived(quint64 generation, const QString& line);
    void commandTransmitted(quint64 generation, const QString& command, bool quiet);
    void commandRejected(quint64 generation, const QString& message);
    void transportError(quint64 generation, const QString& message, bool fatal);

private:
    struct PendingCommand final {
        QString text;
        QByteArray bytes;
        bool quiet{false};
        bool exclusive{false};
    };

    bool adoptGeneration(quint64 generation, bool closeOldSession);
    void refreshPortsInternal(bool forceSignal);
    void tryNextProbe();
    bool openPortInternal(const QString& portName, bool automatic);
    void closePortInternal(bool emitRelease);
    bool sendIdentifyProbe();
    void finishIdentificationTimeout();
    bool enqueueCommandsInternal(
        const QStringList& commands,
        bool quiet,
        bool exclusive,
        bool reportRejection);
    void pumpWriteQueue();
    void completeActiveWrite();
    void processReadyRead();
    void handleSerialFailure(const QString& message);

    QSerialPort* serial_{nullptr};
    QTimer* verificationTimer_{nullptr};
    QTimer* presenceTimer_{nullptr};
    QTimer* heartbeatTimer_{nullptr};

    QStringList ports_;
    QString recommendedPort_;
    QStringList probeQueue_;
    QByteArray pendingRx_;
    QQueue<PendingCommand> commandQueue_;
    PendingCommand activeCommand_;
    QString lastPort_;

    quint64 activeGeneration_{0};
    int highConfidenceCount_{0};
    int identifyAttempts_{0};
    int exclusiveCommandsRemaining_{0};
    bool initialized_{false};
    bool shuttingDown_{false};
    bool automaticConnection_{false};
    bool genericProbeActive_{false};
    bool identityConfirmed_{false};
    bool writeActive_{false};
    bool heartbeatEnabled_{false};
};