// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "DeviceController.hpp"

#include <QMetaObject>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

class DeterministicSessionHarness;
class FirmwareManager;
class SclProfileModel;

class SmartSessionController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* device READ device WRITE setDevice NOTIFY dependenciesChanged)
    Q_PROPERTY(QObject* profiles READ profiles WRITE setProfiles NOTIFY dependenciesChanged)
    Q_PROPERTY(QObject* firmware READ firmware WRITE setFirmware NOTIFY dependenciesChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(bool startReady READ startReady NOTIFY stateChanged)
    Q_PROPERTY(bool canDeployProfile READ canDeployProfile NOTIFY stateChanged)
    Q_PROPERTY(bool liveControlReady READ liveControlReady NOTIFY stateChanged)
    Q_PROPERTY(bool engineeringEditable READ engineeringEditable NOTIFY stateChanged)
    Q_PROPERTY(bool firmwareUpdateRequired READ firmwareUpdateRequired NOTIFY stateChanged)
    Q_PROPERTY(bool firmwareUpdateAvailable READ firmwareUpdateAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool firmwareUpdateCanCancel READ firmwareUpdateCanCancel NOTIFY stateChanged)
    Q_PROPERTY(bool firmwareReinstallAvailable READ firmwareReinstallAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool firmwareInstallRequired READ firmwareInstallVisible NOTIFY stateChanged)
    Q_PROPERTY(bool firmwareRetryAvailable READ firmwareRetryAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool profileSyncRetryAvailable READ profileSyncRetryAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool updatingFirmware READ updatingFirmware NOTIFY stateChanged)
    Q_PROPERTY(bool updateNeedsBootloaderHelp READ updateNeedsBootloaderHelp NOTIFY stateChanged)
    Q_PROPERTY(bool recoveryPending READ recoveryPending NOTIFY stateChanged)
    Q_PROPERTY(int firmwareProgress READ firmwareProgress NOTIFY stateChanged)
    Q_PROPERTY(QString firmwareUpdateStage READ firmwareUpdateStage NOTIFY stateChanged)
    Q_PROPERTY(QString updateStatus READ updateStatus NOTIFY stateChanged)
    Q_PROPERTY(QString firmwareSetupPort READ firmwareSetupPort NOTIFY stateChanged)
    Q_PROPERTY(QString expectedFirmwareVersion READ expectedFirmwareVersion CONSTANT)
    Q_PROPERTY(QString expectedFirmwareBuildId READ expectedFirmwareBuildId NOTIFY stateChanged)
    Q_PROPERTY(QString deviceFirmwareVersion READ deviceFirmwareVersion NOTIFY stateChanged)
    Q_PROPERTY(QString deviceFirmwareBuildId READ deviceFirmwareBuildId NOTIFY stateChanged)
    Q_PROPERTY(qulonglong sessionGeneration READ sessionGeneration NOTIFY stateChanged)
    Q_PROPERTY(PortOwner portOwner READ portOwner NOTIFY stateChanged)

public:
    enum class PortOwner {
        none,
        deviceSession,
        firmwareTool,
    };
    Q_ENUM(PortOwner)

    enum class UpdateStage {
        idle,
        stopping,
        releasingPort,
        probing,
        flashing,
        reconnecting,
        waitingForBootloader,
    };
    Q_ENUM(UpdateStage)

    explicit SmartSessionController(QObject* parent = nullptr);
    ~SmartSessionController() override;

    void shutdown();

    [[nodiscard]] QObject* device() const noexcept;
    [[nodiscard]] QObject* profiles() const noexcept;
    [[nodiscard]] QObject* firmware() const noexcept;
    [[nodiscard]] QString state() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] bool startReady() const noexcept;
    [[nodiscard]] bool canDeployProfile() const noexcept;
    [[nodiscard]] bool liveControlReady() const noexcept;
    [[nodiscard]] bool engineeringEditable() const noexcept;
    [[nodiscard]] bool firmwareUpdateRequired() const noexcept;
    [[nodiscard]] bool firmwareUpdateAvailable() const noexcept;
    [[nodiscard]] bool firmwareUpdateCanCancel() const noexcept;
    [[nodiscard]] bool firmwareReinstallAvailable() const noexcept;
    [[nodiscard]] bool firmwareInstallRequired() const noexcept;
    [[nodiscard]] bool firmwareInstallVisible() const noexcept {
        return !recoveryPending_ && firmwareInstallRequired();
    }
    [[nodiscard]] bool firmwareRetryAvailable() const noexcept;
    [[nodiscard]] bool profileSyncRetryAvailable() const noexcept;
    [[nodiscard]] bool updatingFirmware() const noexcept;
    [[nodiscard]] bool updateNeedsBootloaderHelp() const noexcept;
    [[nodiscard]] bool recoveryPending() const noexcept { return recoveryPending_; }
    [[nodiscard]] int firmwareProgress() const noexcept;
    [[nodiscard]] QString firmwareUpdateStage() const {
        switch (updateStage_) {
        case UpdateStage::stopping:
        case UpdateStage::releasingPort:
            return QStringLiteral("prepare");
        case UpdateStage::probing:
        case UpdateStage::waitingForBootloader:
            return QStringLiteral("verify");
        case UpdateStage::flashing:
            return QStringLiteral("write");
        case UpdateStage::reconnecting:
            return QStringLiteral("reconnect");
        case UpdateStage::idle:
            return QStringLiteral("idle");
        }
        return QStringLiteral("idle");
    }
    [[nodiscard]] QString updateStatus() const;
    [[nodiscard]] QString firmwareSetupPort() const;
    [[nodiscard]] QString expectedFirmwareVersion() const;
    [[nodiscard]] QString expectedFirmwareBuildId() const;
    [[nodiscard]] QString deviceFirmwareVersion() const;
    [[nodiscard]] QString deviceFirmwareBuildId() const;
    [[nodiscard]] quint64 sessionGeneration() const noexcept { return sessionGeneration_; }
    [[nodiscard]] PortOwner portOwner() const noexcept { return portOwner_; }

    [[nodiscard]] static QString chooseRecoveryPort(
        const QString& recommendedPort,
        const QStringList& visiblePorts);

    [[nodiscard]] static constexpr int profileSyncMaxAttempts() noexcept { return 2; }
    [[nodiscard]] static constexpr int profileSyncTimeoutMs() noexcept { return 2500; }
    [[nodiscard]] static constexpr bool profileSyncRetryAllowed(const int attemptsStarted) noexcept {
        return attemptsStarted >= 0 && attemptsStarted < profileSyncMaxAttempts();
    }
    [[nodiscard]] static bool profileGenerationAdvanced(
        const QString& baseline,
        const QString& observed) noexcept;

    [[nodiscard]] static constexpr int firmwareStopAckTimeoutMs() noexcept { return 3500; }
    [[nodiscard]] static constexpr int firmwareReleaseAckTimeoutMs() noexcept { return 2000; }

    [[nodiscard]] static constexpr bool generationIsCurrent(
        const quint64 activeGeneration,
        const quint64 eventGeneration) noexcept {
        return activeGeneration != 0 && eventGeneration == activeGeneration;
    }
    [[nodiscard]] static quint64 nextSessionGeneration(quint64 current) noexcept;
    [[nodiscard]] static constexpr bool firmwareOwnershipValid(
        const PortOwner owner,
        const bool serialConnected) noexcept {
        return owner == PortOwner::firmwareTool && !serialConnected;
    }
    [[nodiscard]] static bool recoveryIdentityMatches(
        const QString& expectedDeviceId,
        const QString& observedDeviceId) noexcept {
        const QString expected = expectedDeviceId.trimmed();
        const QString observed = observedDeviceId.trimmed();
        return !expected.isEmpty() && !observed.isEmpty() &&
            expected.compare(observed, Qt::CaseInsensitive) == 0;
    }

    void setDevice(QObject* object);
    void setProfiles(QObject* object);
    void setFirmware(QObject* object);

    Q_INVOKABLE void start();
    Q_INVOKABLE void reconcile();

    // S7: QML submits operator intent only. The supervisor owns every gate that
    // can mutate device connection, output, live values, profile state or PTP.
    Q_INVOKABLE bool requestStart();
    Q_INVOKABLE bool requestStop();
    Q_INVOKABLE bool requestConnect();
    Q_INVOKABLE void requestRefreshPorts();
    Q_INVOKABLE bool requestConnectPort(const QString& portName);
    Q_INVOKABLE bool requestDisconnect();
    Q_INVOKABLE bool requestProfileSync();
    Q_INVOKABLE bool requestSetFrequency(double hz);
    Q_INVOKABLE bool requestSetSignal(
        const QString& signalId,
        double magnitude,
        double phaseDegrees,
        quint32 quality,
        double currentCountsPerAmp,
        double voltageCountsPerVolt);
    Q_INVOKABLE bool requestSetEnabled(const QString& signalId, const bool enabled) {
        return liveControlReady() && device_ != nullptr && device_->setEnabled(signalId, enabled);
    }
    Q_INVOKABLE bool requestSetQuality(const QString& signalId, quint32 quality);
    Q_INVOKABLE bool requestSetCtSaturation(
        bool enabled,
        double dcOffsetPercent,
        double harmonicPercent,
        int harmonicOrder,
        double clipPercent);
    Q_INVOKABLE bool requestZero();
    Q_INVOKABLE bool requestPtpRefresh();
    Q_INVOKABLE bool requestPtpRole(const QString& role);
    Q_INVOKABLE bool requestSmpSynch(const QString& mode);
    Q_INVOKABLE bool requestConfigurePtp(const QVariantMap& profile);
    Q_INVOKABLE bool requestStartPtp();
    Q_INVOKABLE bool requestStopPtp();

    Q_INVOKABLE bool beginFirmwareUpdate();
    Q_INVOKABLE bool beginFirmwareReinstall();
    Q_INVOKABLE bool beginFirmwareInstall();
    Q_INVOKABLE bool retryFirmwareUpdate();
    Q_INVOKABLE bool cancelFirmwareUpdate();
    Q_INVOKABLE bool retryFirmwareSetup();
    Q_INVOKABLE bool retryIdentification();
    Q_INVOKABLE bool retryProfileSync();

signals:
    void dependenciesChanged();
    void stateChanged();
    void readyForLiveApply();
    void firmwareUpdateFinished(bool success);

private:
    friend class DeterministicSessionHarness;

    enum class ProfileSyncStage { idle, deploying, failed };

    class FirmwareHandoffWatchdog final {
    public:
        explicit FirmwareHandoffWatchdog(SmartSessionController* owner) : owner_(owner) {
            timer_.setSingleShot(true);
            QObject::connect(owner_, &SmartSessionController::stateChanged, owner_, [this] { synchronize(); });
            QObject::connect(&timer_, &QTimer::timeout, owner_, [this] { expire(); });
        }
        void shutdown() { timer_.stop(); owner_ = nullptr; }

    private:
        [[nodiscard]] bool waitingForAck() const noexcept {
            if (owner_ == nullptr || !owner_->updateRequested_) return false;
            if (owner_->updateStage_ == UpdateStage::stopping) return true;
            return owner_->updateStage_ == UpdateStage::releasingPort && owner_->pendingReleaseGeneration_ != 0;
        }
        [[nodiscard]] int timeoutForStage(const UpdateStage stage) const noexcept {
            return stage == UpdateStage::stopping ? firmwareStopAckTimeoutMs() : firmwareReleaseAckTimeoutMs();
        }
        void synchronize() {
            if (!waitingForAck()) {
                timer_.stop(); armedGeneration_ = 0; armedStage_ = UpdateStage::idle; return;
            }
            if (timer_.isActive() && armedGeneration_ == owner_->sessionGeneration_ && armedStage_ == owner_->updateStage_) return;
            armedGeneration_ = owner_->sessionGeneration_;
            armedStage_ = owner_->updateStage_;
            timer_.start(timeoutForStage(armedStage_));
        }
        void expire() {
            if (!waitingForAck() || armedGeneration_ == 0 || owner_->sessionGeneration_ != armedGeneration_ || owner_->updateStage_ != armedStage_) {
                synchronize(); return;
            }
            const QString reason = armedStage_ == UpdateStage::stopping
                ? QStringLiteral("Timed out waiting for the injector to confirm STOP. Firmware access was not started; retry explicitly after checking the device connection.")
                : QStringLiteral("Timed out waiting for the serial worker to acknowledge port release. Firmware access was blocked to prevent COM-port contention.");
            owner_->latchFirmwareFailure(reason);
            emit owner_->firmwareUpdateFinished(false);
            owner_->reconcile();
        }
        SmartSessionController* owner_{nullptr};
        QTimer timer_;
        quint64 armedGeneration_{0};
        UpdateStage armedStage_{UpdateStage::idle};
    };

    class DeviceRecoveryMonitor final {
    public:
        explicit DeviceRecoveryMonitor(SmartSessionController* owner) : owner_(owner) {
            QObject::connect(owner_, &SmartSessionController::dependenciesChanged, owner_, [this] { bindDevice(); });
        }
        void shutdown() {
            QObject::disconnect(verifiedConnection_);
            QObject::disconnect(identificationConnection_);
            device_ = nullptr;
            owner_ = nullptr;
        }

    private:
        void bindDevice() {
            QObject::disconnect(verifiedConnection_);
            QObject::disconnect(identificationConnection_);
            device_ = owner_ != nullptr ? owner_->device_ : nullptr;
            wasVerified_ = device_ != nullptr && device_->deviceVerified();
            if (wasVerified_) {
                lastVerifiedDeviceId_ = device_->deviceId().trimmed();
                expectedRecoveryDeviceId_ = lastVerifiedDeviceId_;
            }
            if (device_ == nullptr) return;
            verifiedConnection_ = QObject::connect(device_, &DeviceController::deviceVerifiedChanged, owner_, [this] { handleVerificationChange(); });
            identificationConnection_ = QObject::connect(device_, &DeviceController::identificationStateChanged, owner_, [this] { handleIdentificationChange(); });
        }
        void setRecoveryPending(const bool pending) {
            if (owner_ == nullptr || owner_->recoveryPending_ == pending) return;
            owner_->recoveryPending_ = pending;
            emit owner_->stateChanged();
        }
        void handleVerificationChange() {
            if (owner_ == nullptr || device_ == nullptr) return;
            if (device_->deviceVerified()) {
                const QString observed = device_->deviceId().trimmed();
                if (owner_->recoveryPending_ && !expectedRecoveryDeviceId_.isEmpty() &&
                    !SmartSessionController::recoveryIdentityMatches(expectedRecoveryDeviceId_, observed)) {
                    owner_->blankBoardDetected_ = false;
                    owner_->blankBoardPort_.clear();
                    owner_->setupError_ = true;
                    owner_->setupErrorStatus_ = QStringLiteral("A different ARStack injector (%1) appeared while Studio was recovering device %2. Automatic recovery was blocked; reconnect the intended injector or retry explicitly.")
                        .arg(observed.isEmpty() ? QStringLiteral("unknown") : observed, expectedRecoveryDeviceId_);
                    device_->disconnectPort();
                    owner_->reconcile();
                    return;
                }
                if (!observed.isEmpty()) {
                    lastVerifiedDeviceId_ = observed;
                    expectedRecoveryDeviceId_ = observed;
                }
                setRecoveryPending(false);
                wasVerified_ = true;
                return;
            }
            if (wasVerified_ && !owner_->updateRequested_) {
                expectedRecoveryDeviceId_ = lastVerifiedDeviceId_;
                setRecoveryPending(!expectedRecoveryDeviceId_.isEmpty());
                owner_->blankBoardDetected_ = false;
                owner_->blankBoardPort_.clear();
                owner_->resetProfileSync(true);
            }
            wasVerified_ = false;
        }
        void handleIdentificationChange() {
            if (owner_ == nullptr || device_ == nullptr || !owner_->recoveryPending_ ||
                device_->identificationState() != DeviceController::IdentificationState::Unidentified) return;
            owner_->blankBoardDetected_ = false;
            owner_->blankBoardPort_.clear();
            owner_->setupError_ = true;
            owner_->setupErrorStatus_ = QStringLiteral("The previously verified injector did not answer semantic identity after bounded retries. Firmware absence was not inferred; retry identification after USB settles.");
            owner_->reconcile();
        }
        SmartSessionController* owner_{nullptr};
        DeviceController* device_{nullptr};
        QMetaObject::Connection verifiedConnection_;
        QMetaObject::Connection identificationConnection_;
        QString lastVerifiedDeviceId_;
        QString expectedRecoveryDeviceId_;
        bool wasVerified_{false};
    };

    void reconnectDeviceSignals();
    void reconnectProfileSignals();
    void reconnectFirmwareSignals();
    void refreshRecoveryOfferFromIdentity();
    void clearBlankBoardContext();
    void latchFirmwareFailure(QString message);
    bool beginFirmwareOperation(const QString& portName);
    bool ensureDefaultProfile();
    void setPresentation(QString state, QString status, bool startReady, bool firmwareUpdateRequired);
    void refreshFirmwareIdentity();
    void continueFirmwareUpdate();
    bool startFirmwareProbe();
    bool startDeviceDiscovery();
    quint64 advanceSessionGeneration();
    void setPortOwner(PortOwner owner);
    bool firmwareIsCurrent() const;
    bool firmwareIsCompatible() const;
    bool deviceControlAvailable() const noexcept;
    void resetProfileSync(bool requireSync);
    bool beginProfileSync(const QVariantMap& profile);
    void handleProfileStateChanged();
    void handleProfileSyncAttemptFailure(QString reason);
    void latchProfileSyncFailure(QString reason);

    DeviceController* device_{nullptr};
    SclProfileModel* profiles_{nullptr};
    FirmwareManager* firmware_{nullptr};
    QTimer discoveryTimer_;
    QTimer prepareTimer_;
    QTimer reconnectTimer_;
    QTimer profileSyncTimer_;
    QString state_{QStringLiteral("WAITING FOR DEVICE")};
    QString statusText_{QStringLiteral("Connect ESP32-P4; ARStack Studio will detect it automatically.")};
    QString deviceFirmwareVersion_;
    QString deviceFirmwareBuildId_;
    QString updatePort_;
    QString blankBoardPort_;
    QString setupErrorStatus_;
    QString profileSyncBaselineGeneration_;
    QString profileSyncBootId_;
    QString profileSyncError_;
    quint64 sessionGeneration_{0};
    quint64 pendingReleaseGeneration_{0};
    bool startReady_{false};
    bool firmwareUpdateRequired_{false};
    bool started_{false};
    bool wasReady_{false};
    bool needsProfileSync_{true};
    bool updateRequested_{false};
    bool updateWasOptional_{false};
    bool blankBoardDetected_{false};
    bool manualRecoveryArmed_{false};
    bool setupError_{false};
    bool recoveryPending_{false};
    bool shuttingDown_{false};
    int updateReconnectAttempts_{0};
    int profileSyncAttempts_{0};
    PortOwner portOwner_{PortOwner::none};
    UpdateStage updateStage_{UpdateStage::idle};
    ProfileSyncStage profileSyncStage_{ProfileSyncStage::idle};
    FirmwareHandoffWatchdog firmwareHandoffWatchdog_{this};
    DeviceRecoveryMonitor deviceRecoveryMonitor_{this};
};