// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QSerialPort>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

class DeviceController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList ports READ ports NOTIFY portsChanged)
    Q_PROPERTY(QString recommendedPort READ recommendedPort NOTIFY discoveryChanged)
    Q_PROPERTY(QString discoveryStatus READ discoveryStatus NOTIFY discoveryChanged)
    Q_PROPERTY(bool discovering READ discovering NOTIFY discoveryChanged)
    Q_PROPERTY(bool deviceVerified READ deviceVerified NOTIFY deviceVerifiedChanged)
    Q_PROPERTY(QString deviceProduct READ deviceProduct NOTIFY deviceVerifiedChanged)
    Q_PROPERTY(QString deviceId READ deviceId NOTIFY deviceVerifiedChanged)
    Q_PROPERTY(QString protocolVersion READ protocolVersion NOTIFY deviceVerifiedChanged)
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
    Q_PROPERTY(bool ptpAvailable READ ptpAvailable NOTIFY ptpStateChanged)
    Q_PROPERTY(bool ptpRunning READ ptpRunning NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpStatus READ ptpStatus NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpDomain READ ptpDomain NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpTransportSpecific READ ptpTransportSpecific NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpVlan READ ptpVlan NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpAnnounceSent READ ptpAnnounceSent NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpSyncSent READ ptpSyncSent NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpTxFailures READ ptpTxFailures NOTIFY ptpStateChanged)

public:
    explicit DeviceController(QObject* parent = nullptr);

    [[nodiscard]] QStringList ports() const;
    [[nodiscard]] QString recommendedPort() const;
    [[nodiscard]] QString discoveryStatus() const;
    [[nodiscard]] bool discovering() const noexcept;
    [[nodiscard]] bool deviceVerified() const noexcept;
    [[nodiscard]] QString deviceProduct() const;
    [[nodiscard]] QString deviceId() const;
    [[nodiscard]] QString protocolVersion() const;
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
    [[nodiscard]] bool ptpAvailable() const noexcept;
    [[nodiscard]] bool ptpRunning() const noexcept;
    [[nodiscard]] QString ptpStatus() const;
    [[nodiscard]] QString ptpDomain() const;
    [[nodiscard]] QString ptpTransportSpecific() const;
    [[nodiscard]] QString ptpVlan() const;
    [[nodiscard]] QString ptpAnnounceSent() const;
    [[nodiscard]] QString ptpSyncSent() const;
    [[nodiscard]] QString ptpTxFailures() const;

    Q_INVOKABLE void refreshPorts();
    Q_INVOKABLE bool autoDetectAndConnect();
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
    Q_INVOKABLE bool setCtSaturation(bool enabled, double dcOffsetPercent, double harmonicPercent, int harmonicOrder, double clipPercent);
    Q_INVOKABLE bool deployProfile(const QVariantMap& profile);
    Q_INVOKABLE bool sendPtpShow();
    Q_INVOKABLE bool startPtp();
    Q_INVOKABLE bool stopPtp();
    Q_INVOKABLE bool configurePtp(const QVariantMap& profile);

    // P1.75 laboratory SV synchronization stimulus. These commands are
    // intentionally separate from PTP lock detection: AUTO remains conservative,
    // while 0/1/2 are explicit simulated wire states for relay testing.
    Q_INVOKABLE bool sendSmpSynchShow() {
        return sendCommand(QStringLiteral("PROFILE SHOW"));
    }
    Q_INVOKABLE bool setSmpSynchPolicy(const QString& requestedMode) {
        const QString mode = requestedMode.trimmed().toUpper();
        static const QStringList validModes{
            QStringLiteral("AUTO"), QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2")};
        if (!validModes.contains(mode)) {
            setError(QStringLiteral("smpSynch policy must be AUTO, 0, 1, or 2."));
            return false;
        }
        return sendCommand(QStringLiteral("PROFILE SMPSYNCH %1").arg(mode));
    }

    Q_INVOKABLE void clearLog();

signals:
    void portsChanged();
    void discoveryChanged();
    void deviceVerifiedChanged();
    void connectedChanged();
    void runningChanged();
    void lastErrorChanged();
    void logTextChanged();
    void telemetryChanged();
    void profileStateChanged();
    void ptpStateChanged();
    void deviceMessage(const QString& message);

private:
    bool connectPortInternal(const QString& portName, bool automatic);
    bool tryNextProbe();
    bool sendCommand(const QString& command);
    void markDeviceVerified();
    void setDiscoveryState(const QString& status, bool active);
    void setRunning(bool value);
    void setError(const QString& message);
    void appendLog(const QString& direction, const QString& line);
    void processLine(const QString& rawLine);
    void resetTelemetry();
    static QString cleanLine(const QString& rawLine);
    static QString utf8Hex(const QString& text);
    static QString compactMac(const QString& text);

    QSerialPort serial_;
    QTimer verificationTimer_;
    QStringList ports_;
    QString recommendedPort_;
    QStringList probeQueue_;
    QString discoveryStatus_{QStringLiteral("Looking for an ARStack ESP32-P4 injector...")};
    QString deviceProduct_;
    QString deviceId_;
    QString protocolVersion_;
    QByteArray pendingRx_;
    QString lastError_;
    QString logText_;
    QString fps_{QStringLiteral("—")};
    QString missed_{QStringLiteral("—")};
    QString txFailures_{QStringLiteral("—")};
    QString signalGeneration_{QStringLiteral("—")};
    QString profileGeneration_{QStringLiteral("—")};
    bool running_{false};
    bool discovering_{false};
    bool deviceVerified_{false};
    bool automaticConnection_{false};
    bool genericProbeActive_{false};
    bool profileArmed_{false};
    bool profileDeploying_{false};
    bool ptpAvailable_{false};
    bool ptpRunning_{false};
    double signalFrequencyHz_{50.0};
    QString ptpStatus_{QStringLiteral("Waiting for device")};
    QString ptpDomain_{QStringLiteral("-")};
    QString ptpTransportSpecific_{QStringLiteral("-")};
    QString ptpVlan_{QStringLiteral("-")};
    QString ptpAnnounceSent_{QStringLiteral("-")};
    QString ptpSyncSent_{QStringLiteral("-")};
    QString ptpTxFailures_{QStringLiteral("-")};
};
