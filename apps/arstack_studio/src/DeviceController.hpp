// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QSerialPort>
#include <QStringList>
#include <QVariantMap>

class DeviceController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList ports READ ports NOTIFY portsChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString portName READ portName NOTIFY connectedChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logTextChanged)
    Q_PROPERTY(QString fps READ fps NOTIFY telemetryChanged)
    Q_PROPERTY(QString missed READ missed NOTIFY telemetryChanged)
    Q_PROPERTY(QString txFailures READ txFailures NOTIFY telemetryChanged)
    Q_PROPERTY(QString signalGeneration READ signalGeneration NOTIFY telemetryChanged)
    Q_PROPERTY(QString profileGeneration READ profileGeneration NOTIFY profileStateChanged)
    Q_PROPERTY(bool profileArmed READ profileArmed NOTIFY profileStateChanged)
    Q_PROPERTY(bool profileDeploying READ profileDeploying NOTIFY profileStateChanged)

public:
    explicit DeviceController(QObject* parent = nullptr);

    [[nodiscard]] QStringList ports() const;
    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] QString portName() const;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] QString logText() const;
    [[nodiscard]] QString fps() const;
    [[nodiscard]] QString missed() const;
    [[nodiscard]] QString txFailures() const;
    [[nodiscard]] QString signalGeneration() const;
    [[nodiscard]] QString profileGeneration() const;
    [[nodiscard]] bool profileArmed() const noexcept;
    [[nodiscard]] bool profileDeploying() const noexcept;

    Q_INVOKABLE void refreshPorts();
    Q_INVOKABLE bool connectPort(const QString& portName);
    Q_INVOKABLE void disconnectPort();
    Q_INVOKABLE bool sendShow();
    Q_INVOKABLE bool start();
    Q_INVOKABLE bool stop();
    Q_INVOKABLE bool zero();
    Q_INVOKABLE bool setFrequency(double hz);
    Q_INVOKABLE bool setSignal(
        const QString& signalId,
        double magnitude,
        double phaseDegrees,
        quint32 quality,
        double currentCountsPerAmp,
        double voltageCountsPerVolt);
    Q_INVOKABLE bool setEnabled(const QString& signalId, bool enabled);
    Q_INVOKABLE bool setQuality(const QString& signalId, quint32 quality);
    Q_INVOKABLE bool deployProfile(const QVariantMap& profile);
    Q_INVOKABLE void clearLog();

signals:
    void portsChanged();
    void connectedChanged();
    void runningChanged();
    void lastErrorChanged();
    void logTextChanged();
    void telemetryChanged();
    void profileStateChanged();
    void deviceMessage(const QString& message);

private:
    bool sendCommand(const QString& command);
    void setRunning(bool value);
    void setError(const QString& message);
    void appendLog(const QString& direction, const QString& line);
    void processLine(const QString& rawLine);
    void resetTelemetry();
    static QString cleanLine(const QString& rawLine);
    static QString utf8Hex(const QString& text);
    static QString compactMac(const QString& text);

    QSerialPort serial_;
    QStringList ports_;
    QByteArray pendingRx_;
    QString lastError_;
    QString logText_;
    QString fps_{QStringLiteral("—")};
    QString missed_{QStringLiteral("—")};
    QString txFailures_{QStringLiteral("—")};
    QString signalGeneration_{QStringLiteral("—")};
    QString profileGeneration_{QStringLiteral("—")};
    bool running_{false};
    bool profileArmed_{false};
    bool profileDeploying_{false};
};