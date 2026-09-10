// SPDX-License-Identifier: GPL-3.0-or-later

#include "SmartSessionController.hpp"

#include "DeviceController.hpp"
#include "SclProfileModel.hpp"

#include <QVariantMap>

SmartSessionController::SmartSessionController(QObject* parent) : QObject(parent) {
    discoveryTimer_.setInterval(2500);
    discoveryTimer_.setSingleShot(false);
    connect(&discoveryTimer_, &QTimer::timeout, this, [this] {
        if (!started_ || device_ == nullptr) return;
        if (!device_->deviceVerified() && !device_->discovering() && !device_->connected()) {
            static_cast<void>(device_->autoDetectAndConnect());
        }
    });

    prepareTimer_.setInterval(650);
    prepareTimer_.setSingleShot(true);
    connect(&prepareTimer_, &QTimer::timeout, this, &SmartSessionController::reconcile);
}

QObject* SmartSessionController::device() const noexcept { return device_; }
QObject* SmartSessionController::profiles() const noexcept { return profiles_; }
QString SmartSessionController::state() const { return state_; }
QString SmartSessionController::statusText() const { return statusText_; }
bool SmartSessionController::startReady() const noexcept { return startReady_; }
bool SmartSessionController::firmwareUpdateRequired() const noexcept { return firmwareUpdateRequired_; }

void SmartSessionController::setDevice(QObject* object) {
    auto* next = qobject_cast<DeviceController*>(object);
    if (device_ == next) return;
    if (device_ != nullptr) disconnect(device_, nullptr, this, nullptr);
    device_ = next;
    reconnectDeviceSignals();
    emit dependenciesChanged();
    reconcile();
}

void SmartSessionController::setProfiles(QObject* object) {
    auto* next = qobject_cast<SclProfileModel*>(object);
    if (profiles_ == next) return;
    if (profiles_ != nullptr) disconnect(profiles_, nullptr, this, nullptr);
    profiles_ = next;
    reconnectProfileSignals();
    emit dependenciesChanged();
    reconcile();
}

void SmartSessionController::start() {
    if (started_) return;
    started_ = true;
    if (profiles_ != nullptr) static_cast<void>(ensureDefaultProfile());
    discoveryTimer_.start();
    reconcile();
}

void SmartSessionController::reconnectDeviceSignals() {
    if (device_ == nullptr) return;

    connect(device_, &DeviceController::deviceVerifiedChanged, this, [this] {
        if (device_ != nullptr && device_->deviceVerified()) {
            // Allow the already-requested SHOW / PROFILE SHOW replies to settle
            // before changing a stopped profile. This avoids racing a reconnect
            // against a publisher that was already running on the device.
            prepareTimer_.start();
        }
        reconcile();
    });
    connect(device_, &DeviceController::connectedChanged, this, &SmartSessionController::reconcile);
    connect(device_, &DeviceController::discoveryChanged, this, &SmartSessionController::reconcile);
    connect(device_, &DeviceController::portsChanged, this, &SmartSessionController::reconcile);
    connect(device_, &DeviceController::runningChanged, this, &SmartSessionController::reconcile);
    connect(device_, &DeviceController::profileStateChanged, this, &SmartSessionController::reconcile);
}

void SmartSessionController::reconnectProfileSignals() {
    if (profiles_ == nullptr) return;
    connect(profiles_, &SclProfileModel::sourceChanged, this, &SmartSessionController::reconcile);
    connect(profiles_, &SclProfileModel::selectedProfileChanged, this, &SmartSessionController::reconcile);
}

bool SmartSessionController::ensureDefaultProfile() {
    if (profiles_ == nullptr) return false;
    if (profiles_->hasProfiles()) return true;
    return profiles_->loadReferenceTemplate();
}

void SmartSessionController::setPresentation(
    QString state,
    QString status,
    const bool startReady,
    const bool firmwareUpdateRequired) {
    const bool readyNow = startReady;
    const bool changed = state_ != state || statusText_ != status ||
        startReady_ != startReady || firmwareUpdateRequired_ != firmwareUpdateRequired;

    state_ = std::move(state);
    statusText_ = std::move(status);
    startReady_ = startReady;
    firmwareUpdateRequired_ = firmwareUpdateRequired;

    if (changed) emit stateChanged();
    if (readyNow && !wasReady_) emit readyForLiveApply();
    wasReady_ = readyNow;
}

void SmartSessionController::reconcile() {
    if (device_ == nullptr || profiles_ == nullptr) {
        setPresentation(
            QStringLiteral("INITIALIZING"),
            QStringLiteral("Preparing ARStack Studio…"),
            false,
            false);
        return;
    }

    if (!profiles_->hasProfiles() && !ensureDefaultProfile()) {
        setPresentation(
            QStringLiteral("SETUP ERROR"),
            profiles_->fatalError().isEmpty()
                ? QStringLiteral("The built-in 4I+4V profile could not be loaded.")
                : profiles_->fatalError(),
            false,
            false);
        return;
    }

    if (device_->running()) {
        setPresentation(
            QStringLiteral("RUNNING"),
            QStringLiteral("4I + 4V · 4000 samples/s · live value apply"),
            false,
            false);
        return;
    }

    if (device_->discovering() || (device_->connected() && !device_->deviceVerified())) {
        setPresentation(
            QStringLiteral("CONNECTING"),
            device_->discoveryStatus(),
            false,
            false);
        return;
    }

    if (!device_->deviceVerified()) {
        setPresentation(
            device_->ports().isEmpty() ? QStringLiteral("WAITING FOR DEVICE") : QStringLiteral("DEVICE FOUND"),
            device_->ports().isEmpty()
                ? QStringLiteral("Connect ESP32-P4; ARStack Studio will detect it automatically.")
                : QStringLiteral("USB device found. ARStack Studio will connect automatically."),
            false,
            false);
        return;
    }

    if (device_->protocolVersion() != QStringLiteral("1")) {
        setPresentation(
            QStringLiteral("FIRMWARE UPDATE"),
            QStringLiteral("The connected board needs compatible ARStack firmware before injection."),
            false,
            true);
        return;
    }

    const QVariantMap profile = profiles_->selectedProfile();
    const bool deployable = profile.value(QStringLiteral("compatibilityClass")).toString() == QStringLiteral("A") &&
        profile.value(QStringLiteral("deviceSupport")).toString() == QStringLiteral("ready");
    if (!deployable) {
        setPresentation(
            QStringLiteral("PROFILE BLOCKED"),
            QStringLiteral("The selected profile is outside the supported 4I+4V runtime."),
            false,
            false);
        return;
    }

    if (device_->profileDeploying() || prepareTimer_.isActive()) {
        setPresentation(
            QStringLiteral("PREPARING 4I+4V"),
            QStringLiteral("Preparing the default 4I+4V injection profile…"),
            false,
            false);
        return;
    }

    if (!device_->profileArmed()) {
        setPresentation(
            QStringLiteral("PREPARING 4I+4V"),
            QStringLiteral("Synchronizing the default 4I+4V profile…"),
            false,
            false);
        static_cast<void>(device_->deployProfile(profile));
        return;
    }

    setPresentation(
        QStringLiteral("READY"),
        QStringLiteral("4I + 4V · 4000 samples/s · ready to start"),
        true,
        false);
}
