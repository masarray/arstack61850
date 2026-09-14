// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QProcessEnvironment>
#include <QStringList>

class QProcess;
class QTimer;

// Long-lived owner of the external firmware process.
//
// S5 invariant: QProcess and its launch/operation timers are created only after
// this object has moved to its dedicated thread. Every operation/result carries
// the supervisor generation that authorized it so stale process callbacks can
// never mutate a newer device session.
class FirmwareWorker final : public QObject {
    Q_OBJECT

public:
    explicit FirmwareWorker(QObject* parent = nullptr);

public slots:
    void initialize();
    void startOperation(
        quint64 generation,
        const QString& program,
        const QStringList& arguments,
        const QProcessEnvironment& environment,
        int launchTimeoutMs,
        int operationTimeoutMs);
    void cancel(quint64 generation);
    void shutdown();

signals:
    void ready(bool processAffinityValid);
    void operationStarted(quint64 generation);
    void outputReady(quint64 generation, const QString& text);
    void operationFinished(quint64 generation, int exitCode, bool normalExit);
    void operationRejected(quint64 generation, const QString& message);
    void operationLaunchFailed(quint64 generation, const QString& message);
    void operationLaunchTimedOut(quint64 generation);
    void operationTimedOut(quint64 generation);
    void operationCancelled(quint64 generation);

private:
    void finishProcess(int exitCode, bool normalExit);
    void clearOperation();

    QProcess* process_{nullptr};
    QTimer* launchTimer_{nullptr};
    QTimer* operationTimer_{nullptr};
    quint64 generation_{0};
    bool initialized_{false};
    bool shuttingDown_{false};
    bool operationActive_{false};
    bool cancelRequested_{false};
};
