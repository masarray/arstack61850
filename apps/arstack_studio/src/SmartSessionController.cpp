// SPDX-License-Identifier: GPL-3.0-or-later

#include "SmartSessionController.hpp"

#include "DeviceController.hpp"
#include "FirmwareManager.hpp"
#include "SclProfileModel.hpp"

#include <QVariantMap>

#include <algorithm>
#include <utility>

#ifndef ARSTACK_STUDIO_VERSION
#define ARSTACK_STUDIO_VERSION "0.1.0"
#endif

namespace {
constexpr int kMaxUpdateReconnectAttempts = 6;
}

QString SmartSessionController::chooseRecoveryPort(
    const QString& recommendedPort,
    const QStringList& visiblePorts) {
    const QString recommended = recommendedPort.trimmed();
    if (!recommended.isEmpty()) return recommended;
    if (visiblePorts.size() != 1) return {};
    return visiblePorts.front().trimmed();
}

bool SmartSessionController::profileGenerationAdvanced(
    const QString& baseline,
    const QString& observed) noexcept {
    bool observedOk = false;
    const qulonglong observedGeneration = observed.trimmed().toULongLong(&observedOk);
    if (!observedOk) return false;

    bool baselineOk = false;
    const qulonglong baselineGeneration = baseline.trimmed().toULongLong(&baselineOk);
    if (!baselineOk) return true;
    return observedGeneration != baselineGeneration;
}

SmartSessionController::SmartSessionController(QObject* parent) : QObject(parent) {
    discoveryTimer_.setInterval(2500);
    discoveryTimer_.setSingleShot(false);
    connect(&discoveryTimer_, &QTimer::timeout, this, [this] {
        if (!started_ || device_ == nullptr || updateRequested_ || blankBoardDetected_ || setupError_) return;
        if (firmware_ != nullptr && firmware_->busy()) return;
        if (device_->identificationState() == DeviceController::IdentificationState::Unidentified) return;
        if (!device_->deviceVerified() && !device_->discovering() && !device_->connected()) {
            static_cast<void>(device_->autoDetectAndConnect());
        }
    });

    prepareTimer_.setInterval(650);
    prepareTimer_.setSingleShot(true);
    connect(&prepareTimer_, &QTimer::timeout, this, &SmartSessionController::reconcile);

    profileSyncTimer_.setInterval(profileSyncTimeoutMs());
    profileSyncTimer_.setSingleShot(true);
    connect(&profileSyncTimer_, &QTimer::timeout, this, [this] {
        if (profileSyncStage_ != ProfileSyncStage::deploying) return;
        handleProfileSyncAttemptFailure(QStringLiteral(
            "Timed out waiting for the ESP32-P4 to arm a new profile generation."));
        reconcile();
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
            latchFirmwareFailure(QStringLiteral(
                "Firmware was written, but Studio could not reconnect and verify the ESP32-P4. Reconnect USB and retry firmware setup."));
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
bool SmartSessionController::firmwareRetryAvailable() const noexcept {
    return setupError_ && !updatePort_.trimmed().isEmpty() && firmware_ != nullptr && !firmware_->busy();
}
bool SmartSessionController::profileSyncRetryAvailable() const noexcept {
    return profileSyncStage_ == ProfileSyncStage::failed && device_ != nullptr &&
        device_->deviceVerified() && !device_->running();
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
    profileSyncBootId_.clear();
    resetProfileSync(true);
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
    resetProfileSync(true);
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

    QTimer::singleShot(0, this, [this] {
        if (!started_ || device_ == nullptr || updateRequested_) return;
        if (!device_->deviceVerified() && !device_->discovering() && !device_->connected()) {
            static_cast<void>(device_->autoDetectAndConnect());
        }
        refreshRecoveryOfferFromIdentity();
        reconcile();
    });
}

bool SmartSessionController::beginFirmwareUpdate() {
    if (device_ == nullptr || firmware_ == nullptr || !device_->deviceVerified() ||
        !firmware_->bundleReady() || firmware_->busy()) {
        return false;
    }
    return beginFirmwareOperation(device_->portName());
}

bool SmartSessionController::beginFirmwareInstall() {
    refreshRecoveryOfferFromIdentity();
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
    profileSyncBootId_.clear();
    resetProfileSync(true);
    setupError_ = false;
    setupErrorStatus_.clear();

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
    if (device_ == nullptr || firmware_ == nullptr || updatePort_.isEmpty() || firmware_->busy()) return false;
    setupError_ = false;
    setupErrorStatus_.clear();
    blankBoardDetected_ = false;
    updateRequested_ = true;
    updateReconnectAttempts_ = 0;
    updateStage_ = UpdateStage::probing;
    setPresentation(
        QStringLiteral("UPDATING FIRMWARE"),
        QStringLiteral("Checking ESP32-P4 Download mode…"),
        false,
        false);
    continueFirmwareUpdate();
    return updateRequested_;
}

bool SmartSessionController::retryFirmwareSetup() {
    if (!firmwareRetryAvailable()) return false;
    return retryFirmwareUpdate();
}

bool SmartSessionController::retryIdentification() {
    if (!started_ || device_ == nullptr || updateRequested_ || device_->connected() ||
        device_->discovering() || (firmware_ != nullptr && firmware_->busy())) {
        return false;
    }

    clearBlankBoardContext();
    const bool started = device_->autoDetectAndConnect();
    reconcile();
    return started;
}

bool SmartSessionController::retryProfileSync() {
    if (!profileSyncRetryAvailable()) return false;
    resetProfileSync(true);
    QTimer::singleShot(0, this, &SmartSessionController::reconcile);
    return true;
}

void SmartSessionController::continueFirmwareUpdate() {
    if (!updateRequested_ || device_ == nullptr || firmware_ == nullptr) return;
    if (device_->running()) {
        updateStage_ = UpdateStage::stopping;
        return;
    }

    updateStage_ = UpdateStage::probing;
    setPresentation(
        QStringLiteral("UPDATING FIRMWARE"),
        QStringLiteral("Verifying ESP32-P4 before firmware installation…"),
        false,
        false);

    // S4 serial close is asynchronous. Never let espflash race the worker's
    // COM handle: wait for DeviceController::portReleased before ROM probing.
    // S5 will add the explicit PortOwner/session-generation protocol.
    if (device_->connected()) {
        device_->disconnectPort();
        return;
    }

    if (!firmware_->probeTarget(updatePort_)) {
        latchFirmwareFailure(firmware_->status());
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
            profileSyncBootId_.clear();
            resetProfileSync(true);
            prepareTimer_.start();
        } else {
            deviceFirmwareVersion_.clear();
            profileSyncBootId_.clear();
            resetProfileSync(true);
        }
        reconcile();
    });
    connect(device_, &DeviceController::identificationStateChanged, this, [this] {
        refreshRecoveryOfferFromIdentity();
        reconcile();
    });
    connect(device_, &DeviceController::deviceIdentityChanged, this, [this] {
        refreshFirmwareIdentity();
        emit stateChanged();
        reconcile();
    });
    connect(device_, &DeviceController::connectedChanged, this, [this] {
        refreshRecoveryOfferFromIdentity();
        reconcile();
    });
    connect(device_, &DeviceController::discoveryChanged, this, [this] {
        refreshRecoveryOfferFromIdentity();
        reconcile();
    });
    connect(device_, &DeviceController::portsChanged, this, [this] {
        refreshRecoveryOfferFromIdentity();
        reconcile();
    });
    connect(device_, &DeviceController::runningChanged, this, [this] {
        if (updateRequested_ && updateStage_ == UpdateStage::stopping &&
            device_ != nullptr && !device_->running()) {
            QTimer::singleShot(0, this, [this] { continueFirmwareUpdate(); });
        }
        reconcile();
    });
    connect(device_, &DeviceController::portReleased, this, [this](const QString&) {
        if (updateRequested_ && updateStage_ == UpdateStage::probing &&
            firmware_ != nullptr && !firmware_->busy()) {
            QTimer::singleShot(0, this, [this] { continueFirmwareUpdate(); });
        }
    });
    connect(device_, &DeviceController::profileStateChanged, this, [this] {
        handleProfileStateChanged();
        reconcile();
    });
}

void SmartSessionController::reconnectProfileSignals() {
    if (profiles_ == nullptr) return;
    connect(profiles_, &SclProfileModel::sourceChanged, this, [this] {
        resetProfileSync(true);
        QTimer::singleShot(0, this, &SmartSessionController::reconcile);
    });
    connect(profiles_, &SclProfileModel::selectedProfileChanged, this, [this] {
        resetProfileSync(true);
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
                            latchFirmwareFailure(firmware_->status());
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
                latchFirmwareFailure(firmware_->status());
                emit firmwareUpdateFinished(false);
            }
        } else if (updateStage_ == UpdateStage::flashing && !firmware_->busy() &&
                   firmware_->bootloaderHelpNeeded()) {
            updateStage_ = UpdateStage::waitingForBootloader;
        }
        reconcile();
    });

    connect(firmware_, &FirmwareManager::operationFailed, this,
            [this](const QString& message, const bool bootloaderHelpNeeded) {
        if (!updateRequested_) {
            reconcile();
            return;
        }
        if (bootloaderHelpNeeded) {
            updateStage_ = UpdateStage::waitingForBootloader;
        } else {
            latchFirmwareFailure(message);
            emit firmwareUpdateFinished(false);
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

void SmartSessionController::refreshRecoveryOfferFromIdentity() {
    if (device_ == nullptr || updateRequested_ || setupError_ || device_->deviceVerified() ||
        device_->identificationState() != DeviceController::IdentificationState::Unidentified) {
        if (!updateRequested_ && (device_ == nullptr ||
            device_->identificationState() != DeviceController::IdentificationState::Unidentified)) {
            blankBoardDetected_ = false;
            blankBoardPort_.clear();
        }
        return;
    }

    // Normal startup stops here: USB descriptor confidence is enough to offer
    // recovery, but never enough to classify ROM silicon or authorize a write.
    // beginFirmwareInstall() is the explicit boundary that runs probeTarget().
    const QString recommended = device_->recommendedPort().trimmed();
    if (recommended.isEmpty() || !device_->ports().contains(recommended)) {
        blankBoardDetected_ = false;
        blankBoardPort_.clear();
        return;
    }

    blankBoardPort_ = recommended;
    blankBoardDetected_ = true;
}

void SmartSessionController::clearBlankBoardContext() {
    blankBoardDetected_ = false;
    blankBoardPort_.clear();
    setupError_ = false;
    setupErrorStatus_.clear();
}

void SmartSessionController::latchFirmwareFailure(QString message) {
    reconnectTimer_.stop();
    updateRequested_ = false;
    updateStage_ = UpdateStage::idle;
    updateReconnectAttempts_ = 0;
    blankBoardDetected_ = false;
    setupError_ = true;
    message = message.trimmed();
    setupErrorStatus_ = message.isEmpty()
        ? QStringLiteral("Firmware setup did not complete. Retry explicitly when the board is ready.")
        : std::move(message);
}

void SmartSessionController::resetProfileSync(const bool requireSync) {
    profileSyncTimer_.stop();
    profileSyncStage_ = ProfileSyncStage::idle;
    profileSyncAttempts_ = 0;
    profileSyncBaselineGeneration_.clear();
    profileSyncError_.clear();
    needsProfileSync_ = requireSync;
}

bool SmartSessionController::beginProfileSync(const QVariantMap& profile) {
    if (device_ == nullptr || !device_->deviceVerified() || device_->running() ||
        profileSyncStage_ == ProfileSyncStage::failed ||
        !profileSyncRetryAllowed(profileSyncAttempts_)) {
        return false;
    }

    const QString currentBootId = device_->bootId().trimmed();
    if (profileSyncBootId_ == currentBootId && !currentBootId.isEmpty()) {
        profileSyncBaselineGeneration_ = device_->profileGeneration().trimmed();
    } else {
        // A profile generation only has meaning inside one firmware boot. Do
        // not compare a fresh boot against a generation cached from an older
        // boot/session; the first armed generation on this boot is authoritative.
        profileSyncBaselineGeneration_.clear();
        profileSyncBootId_ = currentBootId;
    }

    ++profileSyncAttempts_;
    profileSyncStage_ = ProfileSyncStage::deploying;
    profileSyncError_.clear();

    const bool accepted = device_->deployProfile(profile);
    if (!accepted) {
        if (profileSyncStage_ == ProfileSyncStage::deploying) {
            handleProfileSyncAttemptFailure(
                device_->lastError().isEmpty()
                    ? QStringLiteral("Studio could not send the profile transaction to the ESP32-P4.")
                    : device_->lastError());
        }
        return false;
    }

    if (profileSyncStage_ == ProfileSyncStage::deploying) profileSyncTimer_.start();
    return true;
}

void SmartSessionController::handleProfileStateChanged() {
    if (device_ == nullptr || profileSyncStage_ != ProfileSyncStage::deploying) return;
    if (device_->profileDeploying()) return;

    if (device_->profileArmed()) {
        const QString observedGeneration = device_->profileGeneration().trimmed();
        if (!profileGenerationAdvanced(profileSyncBaselineGeneration_, observedGeneration)) {
            // Ignore an out-of-order/stale arm indication. The bounded timer
            // remains authoritative and will retry/fail if no new generation
            // arrives for this transaction.
            return;
        }

        profileSyncTimer_.stop();
        profileSyncStage_ = ProfileSyncStage::idle;
        profileSyncAttempts_ = 0;
        profileSyncBaselineGeneration_ = observedGeneration;
        profileSyncBootId_ = device_->bootId().trimmed();
        profileSyncError_.clear();
        needsProfileSync_ = false;
        return;
    }

    handleProfileSyncAttemptFailure(
        device_->lastError().isEmpty()
            ? QStringLiteral("The ESP32-P4 rejected the profile transaction.")
            : device_->lastError());
}

void SmartSessionController::handleProfileSyncAttemptFailure(QString reason) {
    profileSyncTimer_.stop();
    if (profileSyncStage_ == ProfileSyncStage::failed) return;

    reason = reason.trimmed();
    if (reason.isEmpty()) reason = QStringLiteral("Profile synchronization did not complete.");

    if (profileSyncRetryAllowed(profileSyncAttempts_)) {
        // Retire DeviceController's pending marker before retrying. Otherwise a
        // silent peer can leave profileDeploying=true forever even though the
        // supervisor transaction already timed out.
        profileSyncStage_ = ProfileSyncStage::idle;
        profileSyncError_ = reason;
        if (device_ != nullptr && device_->profileDeploying()) device_->abandonProfileDeployment();
        QTimer::singleShot(0, this, &SmartSessionController::reconcile);
        return;
    }

    latchProfileSyncFailure(reason);
    if (device_ != nullptr && device_->profileDeploying()) device_->abandonProfileDeployment();
}

void SmartSessionController::latchProfileSyncFailure(QString reason) {
    profileSyncTimer_.stop();
    profileSyncStage_ = ProfileSyncStage::failed;
    needsProfileSync_ = true;
    reason = reason.trimmed();
    if (reason.isEmpty()) reason = QStringLiteral("Profile synchronization did not complete.");
    profileSyncError_ = QStringLiteral(
        "%1 Studio stopped after %2 bounded attempts; Start remains inhibited. Retry profile synchronization or reconnect the device.")
        .arg(reason)
        .arg(profileSyncMaxAttempts());
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
    deviceFirmwareVersion_ =
        (device_ != nullptr && device_->deviceVerified()) ? device_->firmwareVersion() : QString{};
}

bool SmartSessionController::firmwareIsCurrent() const {
    return device_ != nullptr && device_->deviceVerified() &&
        DeviceController::identitySupportsCurrentContract(
            device_->deviceIdentity(), expectedFirmwareVersion());
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
        refreshRecoveryOfferFromIdentity();

        if (blankBoardDetected_) {
            setPresentation(
                QStringLiteral("FIRMWARE REQUIRED"),
                QStringLiteral("ARStack identity was not received from %1 after bounded retries. If this is the intended ESP32-P4, Studio will verify chip and revision before writing firmware.")
                    .arg(blankBoardPort_),
                false,
                false);
            return;
        }

        if (setupError_) {
            setPresentation(
                QStringLiteral("SETUP ERROR"),
                setupErrorStatus_.isEmpty()
                    ? QStringLiteral("Studio could not identify a supported ESP32-P4 on the connected serial device.")
                    : setupErrorStatus_,
                false,
                false);
            return;
        }

        if (device_->identificationState() == DeviceController::IdentificationState::Unidentified) {
            setPresentation(
                QStringLiteral("UNIDENTIFIED"),
                QStringLiteral("No ARStack semantic identity was received after %1 bounded attempts. Retry identification or select the intended device manually.")
                    .arg(device_->identifyAttempts()),
                false,
                false);
            return;
        }

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
            const QString observed = deviceFirmwareVersion_.isEmpty()
                ? QStringLiteral("legacy/unknown firmware")
                : QStringLiteral("firmware v%1").arg(deviceFirmwareVersion_);
            latchFirmwareFailure(QStringLiteral(
                "Firmware was written, but reconnect verification reported %1 instead of the current semantic identity contract for v%2. Retry firmware setup explicitly.")
                .arg(observed, expectedFirmwareVersion()));
            emit firmwareUpdateFinished(false);
            setPresentation(QStringLiteral("SETUP ERROR"), setupErrorStatus_, false, false);
            return;
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

    if (profileSyncStage_ == ProfileSyncStage::failed) {
        setPresentation(
            QStringLiteral("PROFILE SYNC ERROR"),
            profileSyncError_.isEmpty()
                ? QStringLiteral("Profile synchronization failed after bounded retries. Retry profile synchronization or reconnect the device.")
                : profileSyncError_,
            false,
            false);
        return;
    }

    if (profileSyncStage_ == ProfileSyncStage::deploying ||
        device_->profileDeploying() || prepareTimer_.isActive()) {
        setPresentation(
            QStringLiteral("PREPARING 4I+4V"),
            profileSyncStage_ == ProfileSyncStage::deploying
                ? QStringLiteral("Synchronizing the default 4I+4V profile · attempt %1/%2…")
                    .arg(profileSyncAttempts_)
                    .arg(profileSyncMaxAttempts())
                : QStringLiteral("Preparing the default 4I+4V injection profile…"),
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
        static_cast<void>(beginProfileSync(profile));
        return;
    }

    if (!device_->profileArmed()) {
        resetProfileSync(true);
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
