// SPDX-License-Identifier: GPL-3.0-or-later

#include "SmartSessionController.hpp"

#include "DeviceController.hpp"
#include "FirmwareManager.hpp"
#include "SclProfileModel.hpp"

#include <QRegularExpression>
#include <QVariantMap>

#include <algorithm>
#include <utility>

#ifndef ARSTACK_STUDIO_VERSION
#define ARSTACK_STUDIO_VERSION "0.1.0"
#endif

namespace {
constexpr int kMaxUpdateReconnectAttempts = 6;
}

SmartSessionController::SmartSessionController(QObject* parent) : QObject(parent) {
    discoveryTimer_.setInterval(2500);
    discoveryTimer_.setSingleShot(false);
    connect(&discoveryTimer_, &QTimer::timeout, this, [this] {
        if (!started_ || device_ == nullptr || updateRequested_) return;
        if (!device_->deviceVerified() && !device_->discovering() && !device_->connected()) {
            static_cast<void>(device_->autoDetectAndConnect());
        }
    });

    prepareTimer_.setInterval(650);
    prepareTimer_.setSingleShot(true);
    connect(&prepareTimer_, &QTimer::timeout, this, &SmartSessionController::reconcile);

    reconnectTimer_.setInterval(3000);
    reconnectTimer_.setSingleShot(true);
    connect(&reconnectTimer_, &QTimer::timeout, this, [this] {
        if (device_ == nullptr || !updateRequested_ || updateStage_ != UpdateStage::reconnecting) return;
        if (device_->deviceVerified()) {
            reconcile();
            return;
        }
        if (device_->discovering() || device_->connected()) {
            reconnectTimer_.start();
            return;
        }
        if (updateReconnectAttempts_ >= kMaxUpdateReconnectAttempts) {
            updateRequested_ = false;
            updateStage_ = UpdateStage::idle;
            emit firmwareUpdateFinished(false);
            reconcile();
            return;
        }
        ++updateReconnectAttempts_;
        static_cast<void>(device_->autoDetectAndConnect());
        reconnectTimer_.start();
        reconcile();
    });
}

QObject* SmartSessionController::device() const noexcept { return device_; }
QObject* SmartSessionController::profiles() const noexcept { return profiles_; }
QObject* SmartSessionController::firmware() const noexcept { return firmware_; }
QString SmartSessionController::state() const { return state_; }
QString SmartSessionController::statusText() const { return statusText_; }
bool SmartSessionController::startReady() const noexcept { return startReady_; }
bool SmartSessionController::firmwareUpdateRequired() const noexcept { return firmwareUpdateRequired_; }
bool SmartSessionController::updatingFirmware() const noexcept {
    return updateStage_ == UpdateStage::stopping || updateStage_ == UpdateStage::probing ||
        updateStage_ == UpdateStage::flashing || updateStage_ == UpdateStage::reconnecting;
}
bool SmartSessionController::updateNeedsBootloaderHelp() const noexcept {
    return updateStage_ == UpdateStage::waitingForBootloader;
}
QString SmartSessionController::updateStatus() const {
    if (firmware_ == nullptr) return {};
    switch (updateStage_) {
    case UpdateStage::stopping:
        return QStringLiteral("Stopping SMV output safely…");
    case UpdateStage::probing:
    case UpdateStage::flashing:
        return firmware_->status();
    case UpdateStage::reconnecting:
        return QStringLiteral("Firmware installed. Reconnecting to ESP32-P4… %1/%2")
            .arg(std::min(updateReconnectAttempts_ + 1, kMaxUpdateReconnectAttempts))
            .arg(kMaxUpdateReconnectAttempts);
    case UpdateStage::waitingForBootloader:
        return QStringLiteral("Hold BOOT, press and release RESET, release BOOT, then Retry update.");
    case UpdateStage::idle:
        return {};
    }
    return {};
}
QString SmartSessionController::expectedFirmwareVersion() const { return QStringLiteral(ARSTACK_STUDIO_VERSION); }
QString SmartSessionController::deviceFirmwareVersion() const { return deviceFirmwareVersion_; }

void SmartSessionController::setDevice(QObject* object) {
    auto* next = qobject_cast<DeviceController*>(object);
    if (device_ == next) return;
    if (device_ != nullptr) disconnect(device_, nullptr, this, nullptr);
    device_ = next;
    deviceFirmwareVersion_.clear();
    needsProfileSync_ = true;
    profileSyncInFlight_ = false;
    reconnectDeviceSignals();
    emit dependenciesChanged();
    reconcile();
}

void SmartSessionController::setProfiles(QObject* object) {
    auto* next = qobject_cast<SclProfileModel*>(object);
    if (profiles_ == next) return;
    if (profiles_ != nullptr) disconnect(profiles_, nullptr, this, nullptr);
    profiles_ = next;
    needsProfileSync_ = true;
    profileSyncInFlight_ = false;
    reconnectProfileSignals();
    emit dependenciesChanged();
    reconcile();
}

void SmartSessionController::setFirmware(QObject* object) {
    auto* next = qobject_cast<FirmwareManager*>(object);
    if (firmware_ == next) return;
    if (firmware_ != nullptr) disconnect(firmware_, nullptr, this, nullptr);
    firmware_ = next;
    reconnectFirmwareSignals();
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

bool SmartSessionController::beginFirmwareUpdate() {
    if (device_ == nullptr || firmware_ == nullptr || !device_->deviceVerified() ||
        !firmware_->bundleReady() || firmware_->busy()) {
        return false;
    }

    updatePort_ = device_->portName().trimmed();
    if (updatePort_.isEmpty()) return false;

    updateRequested_ = true;
    updateReconnectAttempts_ = 0;
    needsProfileSync_ = true;
    profileSyncInFlight_ = false;

    if (device_->running()) {
        updateStage_ = UpdateStage::stopping;
        setPresentation(
            QStringLiteral("UPDATING FIRMWARE"),
            QStringLiteral("Stopping SMV output before firmware update…"),
            false,
            false);
        if (!device_->stop()) {
            updateRequested_ = false;
            updateStage_ = UpdateStage::idle;
            reconcile();
            return false;
        }
        return true;
    }

    continueFirmwareUpdate();
    return true;
}

bool SmartSessionController::retryFirmwareUpdate() {
    if (firmware_ == nullptr || updatePort_.isEmpty() || firmware_->busy()) return false;
    updateRequested_ = true;
    updateReconnectAttempts_ = 0;
    updateStage_ = UpdateStage::probing;
    if (device_ != nullptr && device_->connected()) device_->disconnectPort();
    setPresentation(
        QStringLiteral("UPDATING FIRMWARE"),
        QStringLiteral("Checking ESP32-P4 bootloader…"),
        false,
        false);
    if (!firmware_->probeTarget(updatePort_)) {
        updateStage_ = UpdateStage::waitingForBootloader;
        reconcile();
        return false;
    }
    return true;
}

void SmartSessionController::continueFirmwareUpdate() {
    if (!updateRequested_ || device_ == nullptr || firmware_ == nullptr) return;
    if (device_->running()) {
        updateStage_ = UpdateStage::stopping;
        return;
    }

    updateStage_ = UpdateStage::probing;
    if (device_->connected()) device_->disconnectPort();
    setPresentation(
        QStringLiteral("UPDATING FIRMWARE"),
        QStringLiteral("Checking ESP32-P4 and preparing firmware…"),
        false,
        false);
    if (!firmware_->probeTarget(updatePort_)) {
        updateStage_ = UpdateStage::waitingForBootloader;
        reconcile();
    }
}

void SmartSessionController::reconnectDeviceSignals() {
    if (device_ == nullptr) return;

    connect(device_, &DeviceController::deviceVerifiedChanged, this, [this] {
        if (device_ != nullptr && device_->deviceVerified()) {
            refreshFirmwareIdentity();
            needsProfileSync_ = true;
            profileSyncInFlight_ = false;
            prepareTimer_.start();
        } else {
            deviceFirmwareVersion_.clear();
            needsProfileSync_ = true;
            profileSyncInFlight_ = false;
        }
        reconcile();
    });
    connect(device_, &DeviceController::connectedChanged, this, &SmartSessionController::reconcile);
    connect(device_, &DeviceController::discoveryChanged, this, &SmartSessionController::reconcile);
    connect(device_, &DeviceController::portsChanged, this, &SmartSessionController::reconcile);
    connect(device_, &DeviceController::logTextChanged, this, [this] {
        refreshFirmwareIdentity();
        reconcile();
    });
    connect(device_, &DeviceController::runningChanged, this, [this] {
        if (updateRequested_ && updateStage_ == UpdateStage::stopping &&
            device_ != nullptr && !device_->running()) {
            QTimer::singleShot(0, this, [this] { continueFirmwareUpdate(); });
        }
        reconcile();
    });
    connect(device_, &DeviceController::profileStateChanged, this, [this] {
        if (device_ != nullptr && profileSyncInFlight_ &&
            device_->profileArmed() && !device_->profileDeploying()) {
            profileSyncInFlight_ = false;
            needsProfileSync_ = false;
        }
        reconcile();
    });
}

void SmartSessionController::reconnectProfileSignals() {
    if (profiles_ == nullptr) return;
    connect(profiles_, &SclProfileModel::sourceChanged, this, [this] {
        needsProfileSync_ = true;
        profileSyncInFlight_ = false;
        QTimer::singleShot(0, this, &SmartSessionController::reconcile);
    });
    connect(profiles_, &SclProfileModel::selectedProfileChanged, this, [this] {
        needsProfileSync_ = true;
        profileSyncInFlight_ = false;
        QTimer::singleShot(0, this, &SmartSessionController::reconcile);
    });
}

void SmartSessionController::reconnectFirmwareSignals() {
    if (firmware_ == nullptr) return;

    connect(firmware_, &FirmwareManager::stateChanged, this, [this] {
        if (!updateRequested_) {
            reconcile();
            return;
        }

        if (updateStage_ == UpdateStage::probing && !firmware_->busy()) {
            if (firmware_->targetVerified()) {
                updateStage_ = UpdateStage::flashing;
                setPresentation(
                    QStringLiteral("UPDATING FIRMWARE"),
                    QStringLiteral("Installing verified ARStack firmware…"),
                    false,
                    false);
                QTimer::singleShot(0, this, [this] {
                    if (!firmware_->installFirmware(updatePort_)) {
                        if (firmware_->bootloaderHelpNeeded()) {
                            updateStage_ = UpdateStage::waitingForBootloader;
                        } else {
                            updateRequested_ = false;
                            updateStage_ = UpdateStage::idle;
                            emit firmwareUpdateFinished(false);
                        }
                        reconcile();
                    }
                });
                return;
            }

            if (firmware_->bootloaderHelpNeeded()) {
                updateStage_ = UpdateStage::waitingForBootloader;
            } else {
                updateRequested_ = false;
                updateStage_ = UpdateStage::idle;
                emit firmwareUpdateFinished(false);
            }
        } else if (updateStage_ == UpdateStage::flashing && !firmware_->busy() &&
                   firmware_->bootloaderHelpNeeded()) {
            updateStage_ = UpdateStage::waitingForBootloader;
        }
        reconcile();
    });

    connect(firmware_, &FirmwareManager::installationFinished, this, [this](const bool resetSucceeded) {
        if (!updateRequested_) return;

        updateReconnectAttempts_ = 0;
        if (resetSucceeded) {
            updateStage_ = UpdateStage::reconnecting;
            setPresentation(
                QStringLiteral("UPDATING FIRMWARE"),
                QStringLiteral("Firmware installed. Reconnecting to ESP32-P4…"),
                false,
                false);
            reconnectTimer_.start();
            return;
        }

        if (firmware_->bootloaderHelpNeeded()) {
            updateStage_ = UpdateStage::waitingForBootloader;
            reconcile();
            return;
        }

        updateStage_ = UpdateStage::reconnecting;
        reconnectTimer_.start();
        reconcile();
    });
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

void SmartSessionController::refreshFirmwareIdentity() {
    if (device_ == nullptr || !device_->deviceVerified()) return;
    const QString log = device_->logText();
    const qsizetype identityPos = log.lastIndexOf(QStringLiteral("ARSTACK identity"), -1, Qt::CaseInsensitive);
    if (identityPos < 0) return;

    qsizetype lineEnd = log.indexOf(QLatin1Char('\n'), identityPos);
    if (lineEnd < 0) lineEnd = log.size();
    const QString identityLine = log.mid(identityPos, lineEnd - identityPos);
    static const QRegularExpression versionExpression{
        QStringLiteral(R"(\bfirmware=([0-9A-Za-z._+\-]+))"),
        QRegularExpression::CaseInsensitiveOption};
    const auto match = versionExpression.match(identityLine);
    deviceFirmwareVersion_ = match.hasMatch() ? match.captured(1) : QString{};
}

bool SmartSessionController::firmwareIsCurrent() const {
    return device_ != nullptr && device_->protocolVersion() == QStringLiteral("1") &&
        !deviceFirmwareVersion_.isEmpty() && deviceFirmwareVersion_ == expectedFirmwareVersion();
}

void SmartSessionController::reconcile() {
    if (device_ == nullptr || profiles_ == nullptr || firmware_ == nullptr) {
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

    if (updateRequested_) {
        if (updateStage_ == UpdateStage::waitingForBootloader) {
            setPresentation(QStringLiteral("UPDATE NEEDS BOOT"), updateStatus(), false, false);
            return;
        }
        if (updateStage_ == UpdateStage::stopping || updateStage_ == UpdateStage::probing ||
            updateStage_ == UpdateStage::flashing ||
            (updateStage_ == UpdateStage::reconnecting && !device_->deviceVerified())) {
            setPresentation(
                QStringLiteral("UPDATING FIRMWARE"),
                updateStatus().isEmpty() ? QStringLiteral("Updating firmware…") : updateStatus(),
                false,
                false);
            return;
        }
    }

    if (device_->discovering() || (device_->connected() && !device_->deviceVerified())) {
        setPresentation(QStringLiteral("CONNECTING"), device_->discoveryStatus(), false, false);
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

    refreshFirmwareIdentity();
    if (!firmwareIsCurrent()) {
        if (updateRequested_ && updateStage_ == UpdateStage::reconnecting) {
            updateRequested_ = false;
            updateStage_ = UpdateStage::idle;
            emit firmwareUpdateFinished(false);
        }
        const QString versionText = deviceFirmwareVersion_.isEmpty()
            ? QStringLiteral("legacy firmware")
            : QStringLiteral("firmware v%1").arg(deviceFirmwareVersion_);
        setPresentation(
            QStringLiteral("FIRMWARE UPDATE"),
            QStringLiteral("%1 detected. ARStack Studio v%2 is ready to update it.")
                .arg(versionText, expectedFirmwareVersion()),
            false,
            true);
        return;
    }

    if (updateRequested_ && updateStage_ == UpdateStage::reconnecting) {
        updateRequested_ = false;
        updateStage_ = UpdateStage::idle;
        emit firmwareUpdateFinished(true);
    }

    if (device_->running()) {
        setPresentation(
            QStringLiteral("RUNNING"),
            QStringLiteral("4I + 4V · 4000 samples/s · live value apply"),
            false,
            false);
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

    if (needsProfileSync_) {
        setPresentation(
            QStringLiteral("PREPARING 4I+4V"),
            QStringLiteral("Synchronizing the default 4I+4V profile…"),
            false,
            false);
        if (!profileSyncInFlight_) {
            profileSyncInFlight_ = device_->deployProfile(profile);
        }
        return;
    }

    if (!device_->profileArmed()) {
        needsProfileSync_ = true;
        profileSyncInFlight_ = false;
        setPresentation(
            QStringLiteral("PREPARING 4I+4V"),
            QStringLiteral("Restoring the default 4I+4V profile…"),
            false,
            false);
        QTimer::singleShot(0, this, &SmartSessionController::reconcile);
        return;
    }

    setPresentation(
        QStringLiteral("READY"),
        QStringLiteral("4I + 4V · 4000 samples/s · ready to start"),
        true,
        false);
}
