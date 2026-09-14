// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

class DeviceController;
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
    Q_PROPERTY(bool firmwareUpdateRequired READ firmwareUpdateRequired NOTIFY stateChanged)
    Q_PROPERTY(bool firmwareInstallRequired READ firmwareInstallRequired NOTIFY stateChanged)
    Q_PROPERTY(bool firmwareRetryAvailable READ firmwareRetryAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool profileSyncRetryAvailable READ profileSyncRetryAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool updatingFirmware READ updatingFirmware NOTIFY stateChanged)
    Q_PROPERTY(bool updateNeedsBootloaderHelp READ updateNeedsBootloaderHelp NOTIFY stateChanged)
    Q_PROPERTY(int firmwareProgress READ firmwareProgress NOTIFY stateChanged)
    Q_PROPERTY(QString updateStatus READ updateStatus NOTIFY stateChanged)
    Q_PROPERTY(QString firmwareSetupPort READ firmwareSetupPort NOTIFY stateChanged)
    Q_PROPERTY(QString expectedFirmwareVersion READ expectedFirmwareVersion CONSTANT)
    Q_PROPERTY(QString deviceFirmwareVersion READ deviceFirmwareVersion NOTIFY stateChanged)
    Q_PROPERTY(qulonglong sessionGeneration READ sessionGeneration NOTIFY stateChanged)
    Q_PROPERTY(PortOwner portOwner READ portOwner NOTIFY stateChanged)

public:
    enum class PortOwner {
        none,
        deviceSession,
        firmwareTool,
    };
    Q_ENUM(PortOwner)

    explicit SmartSessionController(QObject* parent = nullptr);

    [[nodiscard]] QObject* device() const noexcept;
    [[nodiscard]] QObject* profiles() const noexcept;
    [[nodiscard]] QObject* firmware() const noexcept;
    [[nodiscard]] QString state() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] bool startReady() const noexcept;
    [[nodiscard]] bool firmwareUpdateRequired() const noexcept;
    [[nodiscard]] bool firmwareInstallRequired() const noexcept;
    [[nodiscard]] bool firmwareRetryAvailable() const noexcept;
    [[nodiscard]] bool profileSyncRetryAvailable() const noexcept;
    [[nodiscard]] bool updatingFirmware() const noexcept;
    [[nodiscard]] bool updateNeedsBootloaderHelp() const noexcept;
    [[nodiscard]] int firmwareProgress() const noexcept;
    [[nodiscard]] QString updateStatus() const;
    [[nodiscard]] QString firmwareSetupPort() const;
    [[nodiscard]] QString expectedFirmwareVersion() const;
    [[nodiscard]] QString deviceFirmwareVersion() const;
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

    void setDevice(QObject* object);
    void setProfiles(QObject* object);
    void setFirmware(QObject* object);

    Q_INVOKABLE void start();
    Q_INVOKABLE void reconcile();
    Q_INVOKABLE bool beginFirmwareUpdate();
    Q_INVOKABLE bool beginFirmwareInstall();
    Q_INVOKABLE bool retryFirmwareUpdate();
    Q_INVOKABLE bool retryFirmwareSetup();
    Q_INVOKABLE bool retryIdentification();
    Q_INVOKABLE bool retryProfileSync();

signals:
    void dependenciesChanged();
    void stateChanged();
    void readyForLiveApply();
    void firmwareUpdateFinished(bool success);

private:
    enum class UpdateStage {
        idle,
        stopping,
        releasingPort,
        probing,
        flashing,
        reconnecting,
        waitingForBootloader,
    };
    enum class ProfileSyncStage { idle, deploying, failed };

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
    bool blankBoardDetected_{false};
    bool setupError_{false};
    int updateReconnectAttempts_{0};
    int profileSyncAttempts_{0};
    PortOwner portOwner_{PortOwner::none};
    UpdateStage updateStage_{UpdateStage::idle};
    ProfileSyncStage profileSyncStage_{ProfileSyncStage::idle};
};
