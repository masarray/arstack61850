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
        if (!started_ || device_ == nullptr || updateRequested_ || blankProbeInFlight_ || blankBoardDetected_) return;
        if (firmware_ != nullptr && firmware_->busy()) return;
        if (!device_->deviceVerified() && !device_->discovering() && !device_->connected()) {
            static_cast<void>(device_->autoDetectAndConnect());
        }
    });

    prepareTimer_.setInterval(650);
    prepareTimer_.setSingleShot(true);
    connect(&prepareTimer_, &QTimer::timeout, this, &SmartSessionController::reconcile);

    blankProbeTimer_.setInterval(450);
    blankProbeTimer_.setSingleShot(true);
    connect(&blankProbeTimer_, &QTimer::timeout, this, [this] {
        if (!started_ || device_ == nullptr || firmware_ == nullptr || updateRequested_ ||
            device_->deviceVerified() || device_->discovering() || device_->connected() ||
            firmware_->busy() || !firmware_->bundleReady() || blankBoardDetected_ || blankProbeInFlight_) {
            return;
        }

        const QString port = device_->recommendedPort().trimmed();
        if (port.isEmpty() || port == blankProbeAttemptedPort_) return;

        blankProbeAttemptedPort_ = port;
        blankBoardPort_ = port;
        blankProbeInFlight_ = true;
        setPresentation(
            QStringLiteral("CHECKING DEVICE"),
            QStringLiteral("Checking whether the connected ESP32-P4 needs ARStack firmware…"),
            false,
            false);

        if (!firmware_->probeTarget(port)) {
            blankProbeInFlight_ = false;
            reconcile();
        }
    });

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
bool SmartSessionController::firmwareInstallRequired() const noexcept {
    return blankBoardDetected_ && !updateRequested_;
}
bool SmartSessionController::updatingFirmware() const noexcept {
    return updateStage_ == UpdateStage::stopping || updateStage_ == UpdateStage::probing ||
        updateStage_ == UpdateStage::flashing || updateStage_ == UpdateStage::reconnecting;
}
bool SmartSessionController::updateNeedsBootloaderHelp() const noexcept {
    return updateStage_ == UpdateStage::waitingForBootloader;
}
int SmartSessionController::firmwareProgress() const noexcept {
    if (firmware_ == nullptr || updateStage_ != UpdateStage::flashing) return -1;
    return firmware_->flashProgress();
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
        return QStringLiteral("Firmware written. Reconnecting and verifying the board… %1/%2")
            .arg(std::min(updateReconnectAttempts_ + 1, kMaxUpdateReconnectAttempts))
            .arg(kMaxUpdateReconnectAttempts);
    case UpdateStage::waitingForBootloader:
        return QStringLiteral("Put the board in Download mode: hold BOOT, press and release RESET, then release BOOT.");
    case UpdateStage::idle:
        return {};
    }
    return {};
}
QString SmartSessionController::firmwareSetupPort() const { return blankBoardPort_; }
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
    clearBlankBoardContext();
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
    clearBlankBoardContext();
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
    maybeScheduleBlankBoardProbe();
}

bool SmartSessionController::beginFirmwareUpdate() {
    if (device_ == nullptr || firmware_ == nullptr || !device_->deviceVerified() ||
        !firmware_->bundleReady() || firmware_->busy()) {
        return false;
    }
    return beginFirmwareOperation(device_->portName());
}

bool SmartSessionController::beginFirmwareInstall() {
    if (device_ == nullptr || firmware_ == nullptr || !blankBoardDetected_ ||
        !firmware_->bundleReady() || firmware_->busy() || blankBoardPort_.isEmpty()) {
        return false;
    }
    return beginFirmwareOperation(blankBoardPort_);
}

bool SmartSessionController::beginFirmwareOperation(const QString& portName) {
    const QString port = portName.trimmed();
    if (device_ == nullptr || firmware_ == nullptr || port.isEmpty() || firmware_->busy()) return false;

    updatePort_ = port;
    updateRequested_ = true;
    updateReconnectAttempts_ = 0;
    needsProfileSync_ = true;
    profileSyncInFlight_ = false;
    blankProbeTimer_.stop();
    blankProbeInFlight_ = false;

    if (device_->running()) {
        updateStage_ = UpdateStage::stopping;
        setPresentation(
            QStringLiteral("UPDATING FIRMWARE"),
            QStringLiteral("Stopping SMV output before firmware installation…"),
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
        QStringLiteral("Checking ESP32-P4 Download mode…"),
        false,
        false);
    if (!firmware_->probeTarget(updatePort_)) {
        updateRequested_ = false;
        updateStage_ = UpdateStage::idle;
        emit firmwareUpdateFinished(false);
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
        QStringLiteral("Verifying ESP32-P4 before firmware installation…"),
        false,
        false);
    if (!firmware_->probeTarget(updatePort_)) {
        updateRequested_ = false;
        updateStage_ = UpdateStage::idle;
        emit firmwareUpdateFinished(false);
        reconcile();
    }
}

void SmartSessionController::reconnectDeviceSignals() {
    if (device_ == nullptr) return;

    connect(device_, &DeviceController::deviceVerifiedChanged, this, [this] {
        if (device_ != nullptr && device_->deviceVerified()) {
            clearBlankBoardContext();
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
    connect(device_, &DeviceController::connectedChanged, this, [this] {
        reconcile();
        maybeScheduleBlankBoardProbe();
    });
    connect(device_, &DeviceController::discoveryChanged, this, [this] {
        reconcile();
        maybeScheduleBlankBoardProbe();
    });
    connect(device_, &DeviceController::portsChanged, this, [this] {
        if (device_ == nullptr) return;
        if (!blankBoardPort_.isEmpty() && !device_->ports().contains(blankBoardPort_)) {
            clearBlankBoardContext();
        } else if (!blankProbeAttemptedPort_.isEmpty() && !device_->ports().contains(blankProbeAttemptedPort_)) {
            blankProbeAttemptedPort_.clear();
        }
        reconcile();
        maybeScheduleBlankBoardProbe();
    });
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
        if (blankProbeInFlight_) {
            if (firmware_->busy()) {
                reconcile();
                return;
            }

            blankProbeInFlight_ = false;
            blankBoardDetected_ = firmware_->targetVerified() &&
                firmware_->selectedPort() == blankBoardPort_;
            if (!blankBoardDetected_) blankBoardPort_.clear();
            reconcile();
            return;
        }

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
        } else if (updateStage_ == UpdateStage::flashing && !firmware_->busy()) {
            if (firmware_->bootloaderHelpNeeded()) {
                updateStage_ = UpdateStage::waitingForBootloader;
            } else {
                updateRequested_ = false;
                updateStage_ = UpdateStage::idle;
                emit firmwareUpdateFinished(false);
            }
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
                QStringLiteral("Firmware written. Reconnecting to ESP32-P4…"),
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

void SmartSessionController::maybeScheduleBlankBoardProbe() {
    if (!started_ || device_ == nullptr || firmware_ == nullptr || updateRequested_ ||
        blankProbeInFlight_ || blankBoardDetected_ || blankProbeTimer_.isActive() ||
        device_->deviceVerified() || device_->discovering() || device_->connected() ||
        firmware_->busy() || !firmware_->bundleReady()) {
        return;
    }

    const QString port = device_->recommendedPort().trimmed();
    if (port.isEmpty() || port == blankProbeAttemptedPort_) return;
    blankProbeTimer_.start();
}

void SmartSessionController::clearBlankBoardContext() {
    blankProbeTimer_.stop();
    blankProbeInFlight_ = false;
    blankBoardDetected_ = false;
    blankBoardPort_.clear();
    blankProbeAttemptedPort_.clear();
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
                updateStatus().isEmpty() ? QStringLiteral("Installing firmware…") : updateStatus(),
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
        if (blankProbeInFlight_) {
            setPresentation(
                QStringLiteral("CHECKING DEVICE"),
                QStringLiteral("Checking the ESP32-P4 firmware state…"),
                false,
                false);
            return;
        }

        if (blankBoardDetected_) {
            setPresentation(
                QStringLiteral("FIRMWARE REQUIRED"),
                QStringLiteral("ESP32-P4 detected on %1. ARStack firmware is not installed or is not responding.")
                    .arg(blankBoardPort_),
                false,
                false);
            return;
        }

        maybeScheduleBlankBoardProbe();
        setPresentation(
            device_->ports().isEmpty() ? QStringLiteral("WAITING FOR DEVICE") : QStringLiteral("DEVICE FOUND"),
            device_->ports().isEmpty()
                ? QStringLiteral("Connect ESP32-P4; ARStack Studio will detect it automatically.")
                : QStringLiteral("USB device found. ARStack Studio is identifying it automatically."),
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
        clearBlankBoardContext();
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
