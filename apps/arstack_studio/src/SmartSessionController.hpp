// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QTimer>

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
    Q_PROPERTY(bool updatingFirmware READ updatingFirmware NOTIFY stateChanged)
    Q_PROPERTY(bool updateNeedsBootloaderHelp READ updateNeedsBootloaderHelp NOTIFY stateChanged)
    Q_PROPERTY(QString updateStatus READ updateStatus NOTIFY stateChanged)
    Q_PROPERTY(QString expectedFirmwareVersion READ expectedFirmwareVersion CONSTANT)
    Q_PROPERTY(QString deviceFirmwareVersion READ deviceFirmwareVersion NOTIFY stateChanged)

public:
    explicit SmartSessionController(QObject* parent = nullptr);

    [[nodiscard]] QObject* device() const noexcept;
    [[nodiscard]] QObject* profiles() const noexcept;
    [[nodiscard]] QObject* firmware() const noexcept;
    [[nodiscard]] QString state() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] bool startReady() const noexcept;
    [[nodiscard]] bool firmwareUpdateRequired() const noexcept;
    [[nodiscard]] bool updatingFirmware() const noexcept;
    [[nodiscard]] bool updateNeedsBootloaderHelp() const noexcept;
    [[nodiscard]] QString updateStatus() const;
    [[nodiscard]] QString expectedFirmwareVersion() const;
    [[nodiscard]] QString deviceFirmwareVersion() const;

    void setDevice(QObject* object);
    void setProfiles(QObject* object);
    void setFirmware(QObject* object);

    Q_INVOKABLE void start();
    Q_INVOKABLE void reconcile();
    Q_INVOKABLE bool beginFirmwareUpdate();
    Q_INVOKABLE bool retryFirmwareUpdate();

signals:
    void dependenciesChanged();
    void stateChanged();
    void readyForLiveApply();
    void firmwareUpdateFinished(bool success);

private:
    enum class UpdateStage { idle, stopping, probing, flashing, reconnecting, waitingForBootloader };

    void reconnectDeviceSignals();
    void reconnectProfileSignals();
    void reconnectFirmwareSignals();
    bool ensureDefaultProfile();
    void setPresentation(QString state, QString status, bool startReady, bool firmwareUpdateRequired);
    void refreshFirmwareIdentity();
    void continueFirmwareUpdate();
    bool firmwareIsCurrent() const;

    DeviceController* device_{nullptr};
    SclProfileModel* profiles_{nullptr};
    FirmwareManager* firmware_{nullptr};
    QTimer discoveryTimer_;
    QTimer prepareTimer_;
    QTimer reconnectTimer_;
    QString state_{QStringLiteral("WAITING FOR DEVICE")};
    QString statusText_{QStringLiteral("Connect ESP32-P4; ARStack Studio will detect it automatically.")};
    QString deviceFirmwareVersion_;
    QString updatePort_;
    bool startReady_{false};
    bool firmwareUpdateRequired_{false};
    bool started_{false};
    bool wasReady_{false};
    bool needsProfileSync_{true};
    bool profileSyncInFlight_{false};
    bool updateRequested_{false};
    UpdateStage updateStage_{UpdateStage::idle};
};
