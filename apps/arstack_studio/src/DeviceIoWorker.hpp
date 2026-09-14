// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QObject>
#include <QQueue>
#include <QStringList>

#include <utility>

class QSerialPort;
class QTimer;

// Long-lived serial transport owner for ARStack Studio.
//
// S4 invariant: QSerialPort and every timer that directly drives serial I/O are
// created only after this object has moved to its worker thread. The GUI-facing
// DeviceController facade never opens, closes, reads, or writes a serial handle.
class DeviceIoWorker final : public QObject {
    Q_OBJECT

public:
    explicit DeviceIoWorker(QObject* parent = nullptr);

    [[nodiscard]] static constexpr int commandQueueCapacity() noexcept { return 64; }
    [[nodiscard]] static constexpr int presencePollIntervalMs() noexcept { return 750; }
    [[nodiscard]] static constexpr int identityMaxAttempts() noexcept { return 3; }
    [[nodiscard]] static constexpr int identityRetryIntervalMs() noexcept { return 650; }
    [[nodiscard]] static constexpr int heartbeatIntervalMs() noexcept { return 700; }

    // Preserve the facade's two-argument transport call while recognizing the
    // one multi-command transaction that must be exclusive in S4.
    void enqueueCommands(const QStringList& commands, bool quiet) {
        const bool exclusive = commands.size() > 1 &&
            commands.front() == QStringLiteral("PROFILE BEGIN");
        enqueueCommands(commands, quiet, exclusive);
    }

public slots:
    void initialize();
    void shutdown(bool requestStop);
    void refreshPorts();
    void autoDetectAndConnect();
    void connectPort(const QString& portName);
    void disconnectPort();
    void confirmIdentity();
    void enqueueCommands(const QStringList& commands, bool quiet, bool exclusive);
    void setHeartbeatEnabled(bool enabled);

signals:
    void ready(bool serialAffinityValid);
    void portsObserved(const QStringList& ports, const QString& recommendedPort, int highConfidenceCount);
    void openingPort(const QString& portName, bool automatic);
    void portOpened(const QString& portName, bool automatic);
    void portOpenFailed(const QString& portName, const QString& message, bool automatic);
    void portClosed(const QString& portName);
    void portReleased(const QString& portName);
    void identificationAttempt(const QString& portName, int attempt, int maximum);
    void identificationTimedOut(const QString& portName, int attempts, bool continuingProbe);
    void automaticProbeExhausted();
    void lineReceived(const QString& line);
    void commandTransmitted(const QString& command, bool quiet);
    void commandRejected(const QString& message);
    void transportError(const QString& message, bool fatal);

private:
    struct PendingCommand final {
        QString text;
        QByteArray bytes;
        bool quiet{false};
        bool exclusive{false};
    };

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
