// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QStringList>
#include <QThread>
#include <QVariantMap>

class DeterministicSessionHarness;
class DeviceIoWorker;

struct DeviceIdentity final {
    QString product;
    QString target;
    QString protocolVersion;
    QString deviceId;
    QString firmwareVersion;
    QString bootId;
    QStringList capabilities;

    [[nodiscard]] bool empty() const noexcept {
        return product.isEmpty() && target.isEmpty() && protocolVersion.isEmpty() &&
            deviceId.isEmpty() && firmwareVersion.isEmpty() && bootId.isEmpty() &&
            capabilities.isEmpty();
    }

    friend bool operator==(const DeviceIdentity&, const DeviceIdentity&) = default;
};

class DeviceController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList ports READ ports NOTIFY portsChanged)
    Q_PROPERTY(QString recommendedPort READ recommendedPort NOTIFY discoveryChanged)
    Q_PROPERTY(QString discoveryStatus READ discoveryStatus NOTIFY discoveryChanged)
    Q_PROPERTY(bool discovering READ discovering NOTIFY discoveryChanged)
    Q_PROPERTY(bool deviceVerified READ deviceVerified NOTIFY deviceVerifiedChanged)
    Q_PROPERTY(IdentificationState identificationState READ identificationState NOTIFY identificationStateChanged)
    Q_PROPERTY(int identifyAttempts READ identifyAttempts NOTIFY identificationStateChanged)
    Q_PROPERTY(QString deviceProduct READ deviceProduct NOTIFY deviceIdentityChanged)
    Q_PROPERTY(QString deviceTarget READ deviceTarget NOTIFY deviceIdentityChanged)
    Q_PROPERTY(QString deviceId READ deviceId NOTIFY deviceIdentityChanged)
    Q_PROPERTY(QString protocolVersion READ protocolVersion NOTIFY deviceIdentityChanged)
    Q_PROPERTY(QString firmwareVersion READ firmwareVersion NOTIFY deviceIdentityChanged)
    Q_PROPERTY(QString bootId READ bootId NOTIFY deviceIdentityChanged)
    Q_PROPERTY(QStringList capabilities READ capabilities NOTIFY deviceIdentityChanged)
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
    Q_PROPERTY(QString ptpFollowUpSent READ ptpFollowUpSent NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpPdelayFrames READ ptpPdelayFrames NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpTxFailures READ ptpTxFailures NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpRole READ ptpRole NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpDiscipline READ ptpDiscipline NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpSource READ ptpSource NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpOffsetNs READ ptpOffsetNs NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpPathDelayNs READ ptpPathDelayNs NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpJitterNs READ ptpJitterNs NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpFrequencyPpb READ ptpFrequencyPpb NOTIFY ptpStateChanged)
    Q_PROPERTY(bool ptpGlobalTraceable READ ptpGlobalTraceable NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpMeasuredSmpSynch READ ptpMeasuredSmpSynch NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpRxAnnounce READ ptpRxAnnounce NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpRxSync READ ptpRxSync NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpRxFollowUp READ ptpRxFollowUp NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpRxPdelay READ ptpRxPdelay NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpPdelayRequests READ ptpPdelayRequests NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpAccepted READ ptpAccepted NOTIFY ptpStateChanged)
    Q_PROPERTY(QString ptpRejected READ ptpRejected NOTIFY ptpStateChanged)
    Q_PROPERTY(QString smpSynchMode READ smpSynchMode NOTIFY ptpStateChanged)
    Q_PROPERTY(QString smpSynchValue READ smpSynchValue NOTIFY ptpStateChanged)
    Q_PROPERTY(QString smpSynchSource READ smpSynchSource NOTIFY ptpStateChanged)
    Q_PROPERTY(bool smpSynchSimulated READ smpSynchSimulated NOTIFY ptpStateChanged)
    Q_PROPERTY(bool smpSynchMeasured READ smpSynchMeasured NOTIFY ptpStateChanged)

public:
    enum class IdentificationState {
        Idle,
        Identifying,
        Verified,
        Unidentified,
    };
    Q_ENUM(IdentificationState)

    explicit DeviceController(QObject* parent = nullptr);
    ~DeviceController() override;

    // Application-lifetime boundary required by AGENTS.md section 11.
    // Returns false if an emergency retirement fallback was required.
    bool shutdown();
    [[nodiscard]] static constexpr int shutdownAckTimeoutMs() noexcept { return 1200; }
    [[nodiscard]] static constexpr int shutdownJoinTimeoutMs() noexcept { return 1200; }
    [[nodiscard]] static constexpr int shutdownRetryTimeoutMs() noexcept { return 800; }
    [[nodiscard]] static constexpr int shutdownForceTimeoutMs() noexcept { return 500; }

    [[nodiscard]] QStringList ports() const;
    [[nodiscard]] QString recommendedPort() const;
    [[nodiscard]] QString discoveryStatus() const;
    [[nodiscard]] bool discovering() const noexcept;
    [[nodiscard]] bool deviceVerified() const noexcept;
    [[nodiscard]] IdentificationState identificationState() const noexcept;
    [[nodiscard]] int identifyAttempts() const noexcept;
    [[nodiscard]] QString deviceProduct() const;
    [[nodiscard]] QString deviceTarget() const;
    [[nodiscard]] QString deviceId() const;
    [[nodiscard]] QString protocolVersion() const;
    [[nodiscard]] QString firmwareVersion() const;
    [[nodiscard]] QString bootId() const;
    [[nodiscard]] QStringList capabilities() const;
    [[nodiscard]] DeviceIdentity deviceIdentity() const;
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
    [[nodiscard]] QString ptpFollowUpSent() const { return ptpFollowUpSent_; }
    [[nodiscard]] QString ptpPdelayFrames() const { return ptpPdelayFrames_; }
    [[nodiscard]] QString ptpTxFailures() const;
    [[nodiscard]] QString ptpRole() const { return ptpRole_; }
    [[nodiscard]] QString ptpDiscipline() const { return ptpDiscipline_; }
    [[nodiscard]] QString ptpSource() const { return ptpSource_; }
    [[nodiscard]] QString ptpOffsetNs() const { return ptpOffsetNs_; }
    [[nodiscard]] QString ptpPathDelayNs() const { return ptpPathDelayNs_; }
    [[nodiscard]] QString ptpJitterNs() const { return ptpJitterNs_; }
    [[nodiscard]] QString ptpFrequencyPpb() const { return ptpFrequencyPpb_; }
    [[nodiscard]] bool ptpGlobalTraceable() const noexcept { return ptpGlobalTraceable_; }
    [[nodiscard]] QString ptpMeasuredSmpSynch() const { return ptpMeasuredSmpSynch_; }
    [[nodiscard]] QString ptpRxAnnounce() const { return ptpRxAnnounce_; }
    [[nodiscard]] QString ptpRxSync() const { return ptpRxSync_; }
    [[nodiscard]] QString ptpRxFollowUp() const { return ptpRxFollowUp_; }
    [[nodiscard]] QString ptpRxPdelay() const { return ptpRxPdelay_; }
    [[nodiscard]] QString ptpPdelayRequests() const { return ptpPdelayRequests_; }
    [[nodiscard]] QString ptpAccepted() const { return ptpAccepted_; }
    [[nodiscard]] QString ptpRejected() const { return ptpRejected_; }
    [[nodiscard]] QString smpSynchMode() const { return smpSynchMode_; }
    [[nodiscard]] QString smpSynchValue() const { return smpSynchValue_; }
    [[nodiscard]] QString smpSynchSource() const { return smpSynchSource_; }
    [[nodiscard]] bool smpSynchSimulated() const noexcept { return smpSynchSimulated_; }
    [[nodiscard]] bool smpSynchMeasured() const noexcept { return smpSynchMeasured_; }
    [[nodiscard]] quint64 sessionGeneration() const noexcept { return sessionGeneration_; }

    [[nodiscard]] bool ioWorkerReady() const noexcept { return ioWorkerReady_; }
    [[nodiscard]] bool ioWorkerAffinityValid() const noexcept { return ioWorkerAffinityValid_; }
    [[nodiscard]] static constexpr int ioCommandQueueCapacity() noexcept { return 64; }
    [[nodiscard]] static constexpr int ioPresencePollIntervalMs() noexcept { return 750; }
    [[nodiscard]] static constexpr bool workerEventIsCurrent(
        const quint64 activeGeneration,
        const quint64 eventGeneration) noexcept {
        return activeGeneration != 0 && eventGeneration == activeGeneration;
    }

    [[nodiscard]] static bool parseIdentityLine(const QString& line, DeviceIdentity& identity);
    [[nodiscard]] static bool identitySupportsCurrentContract(
        const DeviceIdentity& identity,
        const QString& expectedFirmwareVersion);

    [[nodiscard]] static constexpr int identityMaxAttempts() noexcept { return 3; }
    [[nodiscard]] static constexpr int identityRetryIntervalMs() noexcept { return 650; }
    [[nodiscard]] static constexpr bool identityRetryAllowed(const int attemptsSent) noexcept {
        return attemptsSent >= 0 && attemptsSent < identityMaxAttempts();
    }

    void setSessionGeneration(quint64 generation);

    // S7 authority boundary: device/session mutations are public C++ for the
    // SmartSessionController but intentionally absent from the QML meta-object.
    // This prevents aliases or future presentation code from bypassing the
    // supervisor even if a source-policy grep were accidentally evaded.
    void refreshPorts();
    bool autoDetectAndConnect();
    bool connectPort(const QString& portName);
    void disconnectPort();
    bool sendShow();
    virtual bool start();
    bool stop();
    virtual bool zero();
    virtual bool setFrequency(double hz);
    virtual bool setSignal(
        const QString& signalId,
        double magnitude,
        double phaseDegrees,
        quint32 quality,
        double currentCountsPerAmp,
        double voltageCountsPerVolt);
    bool setEnabled(const QString& signalId, bool enabled);
    bool setQuality(const QString& signalId, quint32 quality);
    bool setCtSaturation(bool enabled, double dcOffsetPercent, double harmonicPercent, int harmonicOrder, double clipPercent);
    virtual bool deployProfile(const QVariantMap& profile);

    void abandonProfileDeployment() {
        if (!profileDeploying_) return;
        profileDeploying_ = false;
        profileArmed_ = false;
        emit profileStateChanged();
    }

    bool sendPtpShow();
    bool startPtp();
    bool stopPtp();
    bool configurePtp(const QVariantMap& profile);

    bool setPtpRole(const QString& requestedRole) {
        const QString role = requestedRole.trimmed().toUpper();
        static const QStringList validRoles{
            QStringLiteral("SOURCE"),
            QStringLiteral("RECEIVER"),
            QStringLiteral("MONITOR")};
        if (!validRoles.contains(role)) {
            setError(QStringLiteral("PTP role must be SOURCE, RECEIVER, or MONITOR."));
            return false;
        }
        if (ptpRunning_) {
            setError(QStringLiteral("Stop PTP before changing its operating role."));
            return false;
        }
        return sendCommand(QStringLiteral("PROFILE PTPROLE %1").arg(role));
    }

    bool sendSmpSynchShow() {
        return sendCommand(QStringLiteral("PROFILE SHOW"));
    }
    bool setSmpSynchPolicy(const QString& requestedMode) {
        const QString mode = requestedMode.trimmed().toUpper();
        static const QStringList validModes{
            QStringLiteral("AUTO"), QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2")};
        if (!validModes.contains(mode)) {
            setError(QStringLiteral("smpSynch policy must be AUTO, 0, 1, or 2."));
            return false;
        }
        return sendCommand(QStringLiteral("PROFILE SMPSYNCH %1").arg(mode));
    }

    // Diagnostics are presentation-safe: clearing the local log does not touch
    // serial ownership, output state, firmware or the device protocol.
    Q_INVOKABLE void clearLog();

signals:
    void portsChanged();
    void discoveryChanged();
    void deviceVerifiedChanged();
    void identificationStateChanged();
    void deviceIdentityChanged();
    void connectedChanged();
    void runningChanged();
    void lastErrorChanged();
    void logTextChanged();
    void telemetryChanged();
    void profileStateChanged();
    void ptpStateChanged();
    void deviceMessage(const QString& message);
    void portReleased(quint64 generation, const QString& portName);

protected:
    bool sendQuietCommand(const QString& command);
    bool sendCommandBatch(const QStringList& commands, bool quiet = false);
    void setSessionHeartbeatEnabled(bool enabled);

private:
    friend class DeterministicSessionHarness;

    bool sendCommand(const QString& command);
    void connectWorkerSignals();
    void handlePortSnapshot(const QStringList& ports, const QString& recommendedPort, int highConfidenceCount);
    void handlePortOpened(const QString& portName, bool automatic);
    void handlePortClosed(const QString& portName);
    void applyIdentity(DeviceIdentity identity);
    void clearIdentity();
    void markDeviceVerified();
    void setIdentificationState(IdentificationState state);
    void setDiscoveryState(const QString& status, bool active);
    void setRunning(bool value);
    void setError(const QString& message);
    void appendLog(const QString& direction, const QString& line);
    void processLine(const QString& rawLine);
    void resetTelemetry();
    void resetPtpState();
    static QString cleanLine(const QString& rawLine);
    static QString utf8Hex(const QString& text);
    static QString compactMac(const QString& text);

    DeviceIoWorker* ioWorker_{nullptr};
    QThread ioThread_;
    QStringList ports_;
    QString recommendedPort_;
    QString discoveryStatus_{QStringLiteral("Looking for an ARStack ESP32-P4 injector...")};
    DeviceIdentity identity_;
    QString lastError_;
    QString logText_;
    QString fps_{QStringLiteral("—")};
    QString missed_{QStringLiteral("—")};
    QString txFailures_{QStringLiteral("—")};
    QString signalGeneration_{QStringLiteral("—")};
    QString profileGeneration_{QStringLiteral("—")};
    QString lastIdentificationPort_;
    QString portName_;
    QString pendingConnectPort_;
    quint64 sessionGeneration_{0};
    IdentificationState identificationState_{IdentificationState::Idle};
    int identifyAttempts_{0};
    int highConfidenceCount_{0};
    bool running_{false};
    bool connected_{false};
    bool discovering_{false};
    bool deviceVerified_{false};
    bool profileArmed_{false};
    bool profileDeploying_{false};
    bool ptpAvailable_{false};
    bool ptpRunning_{false};
    bool ioWorkerReady_{false};
    bool ioWorkerAffinityValid_{false};
    bool shuttingDown_{false};
    bool shutdownComplete_{false};
    bool pendingAutoDetect_{false};
    double signalFrequencyHz_{50.0};
    QString ptpStatus_{QStringLiteral("Waiting for device")};
    QString ptpDomain_{QStringLiteral("-")};
    QString ptpTransportSpecific_{QStringLiteral("-")};
    QString ptpVlan_{QStringLiteral("-")};
    QString ptpAnnounceSent_{QStringLiteral("-")};
    QString ptpSyncSent_{QStringLiteral("-")};
    QString ptpFollowUpSent_{QStringLiteral("-")};
    QString ptpPdelayFrames_{QStringLiteral("-")};
    QString ptpTxFailures_{QStringLiteral("-")};
    QString ptpRole_{QStringLiteral("SOURCE")};
    QString ptpDiscipline_{QStringLiteral("UNLOCKED")};
    QString ptpSource_{QStringLiteral("NONE")};
    QString ptpOffsetNs_{QStringLiteral("NA")};
    QString ptpPathDelayNs_{QStringLiteral("NA")};
    QString ptpJitterNs_{QStringLiteral("NA")};
    QString ptpFrequencyPpb_{QStringLiteral("0")};
    QString ptpMeasuredSmpSynch_{QStringLiteral("NA")};
    QString ptpRxAnnounce_{QStringLiteral("0")};
    QString ptpRxSync_{QStringLiteral("0")};
    QString ptpRxFollowUp_{QStringLiteral("0")};
    QString ptpRxPdelay_{QStringLiteral("0")};
    QString ptpPdelayRequests_{QStringLiteral("0")};
    QString ptpAccepted_{QStringLiteral("0")};
    QString ptpRejected_{QStringLiteral("0")};
    QString smpSynchMode_{QStringLiteral("AUTO")};
    QString smpSynchValue_{QStringLiteral("0")};
    QString smpSynchSource_{QStringLiteral("SAFE_DEFAULT")};
    bool ptpGlobalTraceable_{false};
    bool smpSynchSimulated_{false};
    bool smpSynchMeasured_{false};
};
