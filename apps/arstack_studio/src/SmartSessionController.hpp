// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QTimer>

class DeviceController;
class SclProfileModel;

class SmartSessionController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* device READ device WRITE setDevice NOTIFY dependenciesChanged)
    Q_PROPERTY(QObject* profiles READ profiles WRITE setProfiles NOTIFY dependenciesChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(bool startReady READ startReady NOTIFY stateChanged)
    Q_PROPERTY(bool firmwareUpdateRequired READ firmwareUpdateRequired NOTIFY stateChanged)

public:
    explicit SmartSessionController(QObject* parent = nullptr);

    [[nodiscard]] QObject* device() const noexcept;
    [[nodiscard]] QObject* profiles() const noexcept;
    [[nodiscard]] QString state() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] bool startReady() const noexcept;
    [[nodiscard]] bool firmwareUpdateRequired() const noexcept;

    void setDevice(QObject* object);
    void setProfiles(QObject* object);

    Q_INVOKABLE void start();
    Q_INVOKABLE void reconcile();

signals:
    void dependenciesChanged();
    void stateChanged();
    void readyForLiveApply();

private:
    void reconnectDeviceSignals();
    void reconnectProfileSignals();
    bool ensureDefaultProfile();
    void setPresentation(QString state, QString status, bool startReady, bool firmwareUpdateRequired);

    DeviceController* device_{nullptr};
    SclProfileModel* profiles_{nullptr};
    QTimer discoveryTimer_;
    QTimer prepareTimer_;
    QString state_{QStringLiteral("WAITING FOR DEVICE")};
    QString statusText_{QStringLiteral("Connect ESP32-P4; ARStack Studio will detect it automatically.")};
    bool startReady_{false};
    bool firmwareUpdateRequired_{false};
    bool started_{false};
    bool wasReady_{false};
};
