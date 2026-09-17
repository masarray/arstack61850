// SPDX-License-Identifier: GPL-3.0-or-later

#include "DeterministicSessionHarness.hpp"

#include "FirmwareManager.hpp"
#include "SclProfileModel.hpp"
#include "SmartSessionController.hpp"
#include "StudioDeviceController.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QStringList>

#ifndef ARSTACK_STUDIO_VERSION
#define ARSTACK_STUDIO_VERSION "0.1.0"
#endif

class DeterministicSessionHarness final {
public:
    static int run(int argc, char* argv[]) {
        QCoreApplication app(argc, argv);

        struct CaseResult {
            const char* name;
            bool passed;
        };

        const CaseResult cases[] = {
            {"current identity -> READY without recovery", currentIdentityNeverRecovers()},
            {"legacy identity -> firmware update", legacyIdentityOffersUpdate()},
            {"same version missing build ID -> READY with update suggestion", sameVersionMissingBuildOffersUpdate()},
            {"same version stale build -> READY with update suggestion", sameVersionStaleBuildOffersUpdate()},
            {"optional bootloader wait -> cancel restores supervisor", optionalBootloaderWaitCanCancel()},
            {"firmware stage projection -> supervisor-owned", firmwareStageProjectionIsExplicit()},
            {"fragmented espflash progress -> monotonic", fragmentedEspflashProgressIsMonotonic()},
            {"trusted ESP32-P4 identity timeout -> firmware required", automaticIdentityTimeoutOffersFirmwareRecovery()},
            {"single visible COM without metadata -> firmware required", singleVisibleTimeoutWithoutRecommendationOffersFirmwareRecovery()},
            {"ambiguous identity timeout -> UNIDENTIFIED", ambiguousIdentityTimeoutStaysUnidentified()},
            {"manual recovery intent survives IDENTIFY -> firmware required", manualRecoveryIntentArmsFirmwareOffer()},
            {"stale generation release -> ignored", staleReleaseGenerationIsIgnored()},
            {"firmware probe before serial release -> blocked", firmwareProbeRequiresReleasedPort()},
            {"profile timeout -> bounded terminal error", profileTimeoutIsTerminal()},
            {"profile rejection -> bounded terminal error", profileRejectionIsTerminal()},
            {"READY unplug/replug COM renumber -> READY stopped", readyReplugRecoversWithoutStart()},
            {"RUNNING unplug/replug -> never auto-restarts", runningReplugNeverAutoStarts()},
            {"wrong device during recovery -> SETUP ERROR", wrongDeviceRecoveryFailsClosed()},
            {"control health misses -> bounded failure", healthTimeoutIsBounded()},
            {"worker teardown -> joins cleanly", workerTeardownCompletes()},
        };

        QStringList failures;
        for (const auto& result : cases) {
            qInfo().noquote()
                << "S8 deterministic session:" << (result.passed ? "PASS" : "FAIL")
                << "·" << result.name;
            if (!result.passed) failures.push_back(QString::fromUtf8(result.name));
        }

        if (!failures.isEmpty()) {
            qCritical().noquote()
                << "S8 deterministic session harness: FAIL ·"
                << failures.join(QStringLiteral("; "));
            return 21;
        }

        qInfo().noquote()
            << "S8 deterministic session harness: PASS · semantic identity/recovery, generation, ownership, profile terminal states, unplug/replug, wrong-device and health boundaries locked";
        return 0;
    }

private:
    struct Fixture {
        SclProfileModel profiles;
        FirmwareManager firmware;
        StudioDeviceController device;
        SmartSessionController session;
        bool profileReady{false};

        Fixture() {
            firmware.firmwareVersion_ = QStringLiteral(ARSTACK_STUDIO_VERSION);
            firmware.firmwareBuildId_ = QStringLiteral("0123456789abcdef");
            firmware.bundleReady_ = true;
            profileReady = profiles.loadReferenceTemplate();
            session.setProfiles(&profiles);
            session.setFirmware(&firmware);
            session.setDevice(&device);
        }

        ~Fixture() {
            firmware.shutdown();
        }
    };

    static void quiesce(SmartSessionController& session, StudioDeviceController& device) {
        session.discoveryTimer_.stop();
        session.prepareTimer_.stop();
        session.reconnectTimer_.stop();
        session.profileSyncTimer_.stop();
        device.controlHealthTimer_.stop();
        device.liveFlushTimer_.stop();
    }

    static DeviceIdentity identity(
        const QString& deviceId = QStringLiteral("A1B2C3D4E5F6"),
        const QString& firmware = QStringLiteral(ARSTACK_STUDIO_VERSION),
        const QString& bootId = QStringLiteral("0123456789ABCDEF"),
        const QString& buildId = QStringLiteral("0123456789abcdef")) {
        DeviceIdentity result;
        result.product = QStringLiteral("SMV-INJECTOR");
        result.target = QStringLiteral("ESP32-P4");
        result.protocolVersion = QStringLiteral("1");
        result.deviceId = deviceId;
        result.firmwareVersion = firmware;
        result.buildId = buildId;
        result.bootId = bootId;
        result.capabilities = {
            QStringLiteral("SMV-4I4V"),
            QStringLiteral("PROFILE"),
            QStringLiteral("LIVE-SETPOINTS"),
            QStringLiteral("SESSION-LEASE"),
            QStringLiteral("PTP-P2"),
            QStringLiteral("SMPSYNCH-AUTO")};
        return result;
    }

    static void seedVerified(
        Fixture& fixture,
        const DeviceIdentity& identityValue,
        const QString& port,
        const bool running) {
        auto& device = fixture.device;
        auto& session = fixture.session;

        quiesce(session, device);
        session.setupError_ = false;
        session.setupErrorStatus_.clear();
        session.manualRecoveryArmed_ = false;
        session.blankBoardDetected_ = false;
        session.blankBoardPort_.clear();
        session.updateRequested_ = false;
        session.updateWasOptional_ = false;
        session.updateStage_ = SmartSessionController::UpdateStage::idle;
        session.portOwner_ = SmartSessionController::PortOwner::deviceSession;
        session.profileSyncStage_ = SmartSessionController::ProfileSyncStage::idle;
        session.profileSyncAttempts_ = 0;
        session.needsProfileSync_ = false;

        device.ports_ = {port};
        device.recommendedPort_ = port;
        device.portName_ = port;
        device.connected_ = true;
        device.discovering_ = false;
        device.deviceVerified_ = true;
        device.identificationState_ = DeviceController::IdentificationState::Verified;
        device.identifyAttempts_ = 1;
        device.identity_ = identityValue;
        device.running_ = running;
        device.profileDeploying_ = false;
        device.profileArmed_ = true;
        device.profileGeneration_ = QStringLiteral("7");
        device.lastError_.clear();

        emit device.portsChanged();
        emit device.deviceIdentityChanged();
        emit device.identificationStateChanged();
        emit device.connectedChanged();
        emit device.deviceVerifiedChanged();
        emit device.profileStateChanged();
        emit device.runningChanged();

        // deviceVerifiedChanged intentionally re-arms volatile profile sync in
        // production. For these scenarios the harness is modelling the point
        // after that transaction has already been confirmed.
        session.prepareTimer_.stop();
        session.profileSyncTimer_.stop();
        session.profileSyncStage_ = SmartSessionController::ProfileSyncStage::idle;
        session.profileSyncAttempts_ = 0;
        session.profileSyncBaselineGeneration_ = device.profileGeneration_;
        session.profileSyncBootId_ = identityValue.bootId;
        session.profileSyncError_.clear();
        session.needsProfileSync_ = false;
        device.profileDeploying_ = false;
        device.profileArmed_ = true;
        device.controlHealthTimer_.stop();
        session.reconcile();
    }

    static bool seedIdentityTimeout(Fixture& fixture, const bool manualRecovery) {
        auto& device = fixture.device;
        auto& session = fixture.session;

        quiesce(session, device);
        session.clearBlankBoardContext();
        session.manualRecoveryArmed_ = manualRecovery;
        session.portOwner_ = SmartSessionController::PortOwner::deviceSession;
        session.recoveryPending_ = false;

        device.ports_ = {QStringLiteral("COM7")};
        device.recommendedPort_ = QStringLiteral("COM7");
        device.portName_ = QStringLiteral("COM7");
        device.connected_ = true;
        device.discovering_ = false;
        device.deviceVerified_ = false;
        device.identificationState_ = DeviceController::IdentificationState::Identifying;
        device.identifyAttempts_ = 1;
        device.identity_ = {};
        device.running_ = false;
        device.profileArmed_ = false;
        device.profileDeploying_ = false;

        // Drive the real intermediate state first. Explicit recovery intent must
        // survive this normal port-open/semantic-IDENTIFY phase.
        emit device.portsChanged();
        emit device.connectedChanged();
        emit device.identificationStateChanged();
        session.reconcile();
        const bool intentSurvivedIdentifying =
            !manualRecovery || (session.manualRecoveryArmed_ && !session.blankBoardDetected_);

        device.connected_ = false;
        device.identificationState_ = DeviceController::IdentificationState::Unidentified;
        device.identifyAttempts_ = DeviceController::identityMaxAttempts();
        emit device.connectedChanged();
        emit device.identificationStateChanged();
        session.reconcile();
        return intentSurvivedIdentifying;
    }

    static void simulateDisconnect(Fixture& fixture) {
        auto& device = fixture.device;
        auto& session = fixture.session;

        quiesce(session, device);
        device.connected_ = false;
        device.discovering_ = false;
        device.deviceVerified_ = false;
        device.identificationState_ = DeviceController::IdentificationState::Idle;
        device.identifyAttempts_ = 0;
        device.identity_ = {};
        device.running_ = false;
        device.profileArmed_ = false;
        device.profileDeploying_ = false;
        device.ports_.clear();
        device.recommendedPort_.clear();

        emit device.runningChanged();
        emit device.profileStateChanged();
        emit device.deviceIdentityChanged();
        emit device.identificationStateChanged();
        emit device.deviceVerifiedChanged();
        emit device.connectedChanged();
        emit device.portsChanged();
        session.prepareTimer_.stop();
        session.profileSyncTimer_.stop();
        session.reconcile();
    }

    static bool currentIdentityNeverRecovers() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        seedVerified(fixture, identity(), QStringLiteral("COM7"), false);
        return fixture.session.state() == QStringLiteral("READY") &&
            fixture.session.startReady() &&
            fixture.session.firmwareReinstallAvailable() &&
            !fixture.session.firmwareInstallVisible() &&
            !fixture.session.firmwareUpdateRequired();
    }

    static bool legacyIdentityOffersUpdate() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        seedVerified(
            fixture,
            identity(QStringLiteral("A1B2C3D4E5F6"), QStringLiteral("0.0.9")),
            QStringLiteral("COM7"),
            false);
        return fixture.session.state() == QStringLiteral("FIRMWARE UPDATE") &&
            fixture.session.firmwareUpdateRequired() &&
            !fixture.session.startReady();
    }

    static bool sameVersionMissingBuildOffersUpdate() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        auto stale = identity();
        stale.buildId.clear();
        seedVerified(fixture, stale, QStringLiteral("COM7"), false);
        return fixture.session.state() == QStringLiteral("READY") &&
            !fixture.session.firmwareUpdateRequired() &&
            fixture.session.firmwareUpdateAvailable() &&
            !fixture.session.firmwareReinstallAvailable() &&
            fixture.session.startReady();
    }

    static bool sameVersionStaleBuildOffersUpdate() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        auto stale = identity();
        stale.buildId = QStringLiteral("fedcba9876543210");
        seedVerified(fixture, stale, QStringLiteral("COM7"), false);
        return fixture.session.state() == QStringLiteral("READY") &&
            !fixture.session.firmwareUpdateRequired() &&
            fixture.session.firmwareUpdateAvailable() &&
            !fixture.session.firmwareReinstallAvailable() &&
            fixture.session.startReady();
    }

    static bool optionalBootloaderWaitCanCancel() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        auto stale = identity();
        stale.buildId = QStringLiteral("fedcba9876543210");
        seedVerified(fixture, stale, QStringLiteral("COM7"), false);
        auto& session = fixture.session;
        auto& device = fixture.device;

        session.started_ = false; // deterministic: exercise state recovery without real COM discovery.
        session.updateRequested_ = true;
        session.updateWasOptional_ = true;
        session.updateStage_ = SmartSessionController::UpdateStage::waitingForBootloader;
        session.portOwner_ = SmartSessionController::PortOwner::firmwareTool;
        device.connected_ = false;
        device.deviceVerified_ = false;

        const bool cancelled = session.cancelFirmwareUpdate();
        return cancelled && !session.updateRequested_ && !session.updateWasOptional_ &&
            session.updateStage_ == SmartSessionController::UpdateStage::idle &&
            session.portOwner_ == SmartSessionController::PortOwner::none &&
            !session.setupError_;
    }

    static bool firmwareStageProjectionIsExplicit() {
        Fixture fixture;
        auto& session = fixture.session;
        session.updateStage_ = SmartSessionController::UpdateStage::idle;
        const bool idle = session.firmwareUpdateStage() == QStringLiteral("idle");
        session.updateStage_ = SmartSessionController::UpdateStage::stopping;
        const bool stopping = session.firmwareUpdateStage() == QStringLiteral("prepare");
        session.updateStage_ = SmartSessionController::UpdateStage::releasingPort;
        const bool releasing = session.firmwareUpdateStage() == QStringLiteral("prepare");
        session.updateStage_ = SmartSessionController::UpdateStage::probing;
        const bool probing = session.firmwareUpdateStage() == QStringLiteral("verify");
        session.updateStage_ = SmartSessionController::UpdateStage::flashing;
        const bool flashing = session.firmwareUpdateStage() == QStringLiteral("write");
        session.updateStage_ = SmartSessionController::UpdateStage::reconnecting;
        const bool reconnecting = session.firmwareUpdateStage() == QStringLiteral("reconnect");
        session.updateStage_ = SmartSessionController::UpdateStage::waitingForBootloader;
        const bool bootloader = session.firmwareUpdateStage() == QStringLiteral("verify");
        return idle && stopping && releasing && probing && flashing && reconnecting && bootloader;
    }

    static bool fragmentedEspflashProgressIsMonotonic() {
        Fixture fixture;
        auto& firmware = fixture.firmware;
        firmware.operation_ = FirmwareManager::Operation::flash;
        firmware.flashProgress_ = -1;
        firmware.progressOutputTail_.clear();

        firmware.updateProgressFromOutput(QString::fromLatin1("\x1B[2K\r[00:00:01] [================] 4"));
        const bool remainsUnknown = firmware.flashProgress_ == -1;
        firmware.updateProgressFromOutput(QStringLiteral("2/100 segment 0x0\r"));
        const bool reconstructsSplitCounter = firmware.flashProgress_ == 42;
        firmware.updateProgressFromOutput(QStringLiteral("[00:00:02] [========] 21/100 segment 0x0\r"));
        const bool neverMovesBackward = firmware.flashProgress_ == 42;
        firmware.updateProgressFromOutput(QStringLiteral("[00:00:03] [========================================] 100/100 segment 0x0\r"));
        const bool reachesCompletion = firmware.flashProgress_ == 100;

        return remainsUnknown && reconstructsSplitCounter && neverMovesBackward && reachesCompletion &&
            FirmwareManager::parseFlashProgress(QStringLiteral("Writing 64%\r")) == 64 &&
            FirmwareManager::parseFlashProgress(QStringLiteral("[00:00:02] [================] 17/20 segment 0x10000")) == 85 &&
            FirmwareManager::parseFlashProgress(QStringLiteral("21/0 segment 0x0")) == -1;
    }

    static bool automaticIdentityTimeoutOffersFirmwareRecovery() {
        Fixture fixture;
        if (!fixture.profileReady || !seedIdentityTimeout(fixture, false)) return false;
        return fixture.session.state() == QStringLiteral("FIRMWARE REQUIRED") &&
            fixture.session.firmwareInstallVisible() &&
            fixture.session.blankBoardDetected_ &&
            fixture.session.blankBoardPort_ == QStringLiteral("COM7") &&
            !fixture.session.startReady();
    }

    static bool singleVisibleTimeoutWithoutRecommendationOffersFirmwareRecovery() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        auto& device = fixture.device;
        auto& session = fixture.session;
        quiesce(session, device);
        session.clearBlankBoardContext();
        session.portOwner_ = SmartSessionController::PortOwner::deviceSession;
        session.recoveryPending_ = false;

        device.ports_ = {QStringLiteral("COM7")};
        device.recommendedPort_.clear();
        device.recoveryCandidatePort_ = QStringLiteral("COM7");
        device.portName_ = QStringLiteral("COM7");
        device.connected_ = false;
        device.discovering_ = false;
        device.deviceVerified_ = false;
        device.identificationState_ = DeviceController::IdentificationState::Unidentified;
        device.identifyAttempts_ = DeviceController::identityMaxAttempts();
        device.identity_ = {};
        emit device.portsChanged();
        emit device.identificationStateChanged();
        session.reconcile();

        return session.state() == QStringLiteral("FIRMWARE REQUIRED") &&
            session.firmwareInstallVisible() &&
            session.blankBoardDetected_ &&
            session.blankBoardPort_ == QStringLiteral("COM7") &&
            !session.startReady();
    }

    static bool ambiguousIdentityTimeoutStaysUnidentified() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        auto& device = fixture.device;
        auto& session = fixture.session;
        quiesce(session, device);
        session.clearBlankBoardContext();
        session.portOwner_ = SmartSessionController::PortOwner::deviceSession;
        session.recoveryPending_ = false;

        device.ports_ = {QStringLiteral("COM7"), QStringLiteral("COM8")};
        device.recommendedPort_.clear();
        device.portName_ = QStringLiteral("COM7");
        device.connected_ = false;
        device.discovering_ = false;
        device.deviceVerified_ = false;
        device.identificationState_ = DeviceController::IdentificationState::Unidentified;
        device.identifyAttempts_ = DeviceController::identityMaxAttempts();
        device.identity_ = {};
        emit device.portsChanged();
        emit device.identificationStateChanged();
        session.reconcile();

        return session.state() == QStringLiteral("UNIDENTIFIED") &&
            !session.firmwareInstallVisible() &&
            !session.blankBoardDetected_ &&
            !session.startReady();
    }

    static bool manualRecoveryIntentArmsFirmwareOffer() {
        Fixture fixture;
        if (!fixture.profileReady || !seedIdentityTimeout(fixture, true)) return false;
        return fixture.session.state() == QStringLiteral("FIRMWARE REQUIRED") &&
            fixture.session.firmwareInstallVisible() &&
            fixture.session.blankBoardPort_ == QStringLiteral("COM7") &&
            !fixture.session.startReady();
    }

    static bool staleReleaseGenerationIsIgnored() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        auto& session = fixture.session;
        auto& device = fixture.device;
        const quint64 current = session.sessionGeneration_ == 0 ? 1 : session.sessionGeneration_;
        session.sessionGeneration_ = current;
        device.setSessionGeneration(current);
        session.updateRequested_ = true;
        session.updateStage_ = SmartSessionController::UpdateStage::releasingPort;
        session.portOwner_ = SmartSessionController::PortOwner::deviceSession;
        session.pendingReleaseGeneration_ = current;

        emit device.portReleased(current - 1, QStringLiteral("COM7"));
        const bool staleIgnored =
            session.pendingReleaseGeneration_ == current &&
            session.portOwner_ == SmartSessionController::PortOwner::deviceSession &&
            session.updateStage_ == SmartSessionController::UpdateStage::releasingPort;

        emit device.portReleased(current, QStringLiteral("COM7"));
        const bool currentAccepted =
            session.pendingReleaseGeneration_ == 0 &&
            session.portOwner_ == SmartSessionController::PortOwner::none;

        session.updateRequested_ = false;
        session.updateStage_ = SmartSessionController::UpdateStage::idle;
        return staleIgnored && currentAccepted;
    }

    static bool firmwareProbeRequiresReleasedPort() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        auto& session = fixture.session;
        auto& device = fixture.device;
        device.connected_ = true;
        session.updateRequested_ = true;
        session.updatePort_ = QStringLiteral("COM7");
        session.portOwner_ = SmartSessionController::PortOwner::deviceSession;
        session.updateStage_ = SmartSessionController::UpdateStage::releasingPort;
        const bool blocked = !session.startFirmwareProbe();
        session.updateRequested_ = false;
        session.updateStage_ = SmartSessionController::UpdateStage::idle;
        return blocked && session.portOwner_ == SmartSessionController::PortOwner::deviceSession;
    }

    static bool profileTimeoutIsTerminal() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        seedVerified(fixture, identity(), QStringLiteral("COM7"), false);
        auto& session = fixture.session;
        auto& device = fixture.device;

        session.needsProfileSync_ = true;
        session.profileSyncStage_ = SmartSessionController::ProfileSyncStage::deploying;
        session.profileSyncAttempts_ = 1;
        device.profileDeploying_ = false;
        device.profileArmed_ = false;
        session.handleProfileSyncAttemptFailure(QStringLiteral("first timeout"));
        const bool oneRetryRemains =
            session.profileSyncStage_ == SmartSessionController::ProfileSyncStage::idle &&
            session.profileSyncAttempts_ == 1;

        session.profileSyncStage_ = SmartSessionController::ProfileSyncStage::deploying;
        session.profileSyncAttempts_ = SmartSessionController::profileSyncMaxAttempts();
        device.profileDeploying_ = false;
        session.handleProfileSyncAttemptFailure(QStringLiteral("second timeout"));
        session.reconcile();

        return oneRetryRemains &&
            session.profileSyncStage_ == SmartSessionController::ProfileSyncStage::failed &&
            session.state() == QStringLiteral("PROFILE SYNC ERROR") &&
            session.profileSyncRetryAvailable() &&
            !session.startReady();
    }

    static bool profileRejectionIsTerminal() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        seedVerified(fixture, identity(), QStringLiteral("COM7"), false);
        auto& session = fixture.session;
        auto& device = fixture.device;

        session.needsProfileSync_ = true;
        session.profileSyncStage_ = SmartSessionController::ProfileSyncStage::deploying;
        session.profileSyncAttempts_ = SmartSessionController::profileSyncMaxAttempts();
        device.profileDeploying_ = false;
        device.profileArmed_ = false;
        device.lastError_ = QStringLiteral("Device rejected the profile.");
        session.handleProfileStateChanged();
        session.reconcile();

        return session.profileSyncStage_ == SmartSessionController::ProfileSyncStage::failed &&
            session.state() == QStringLiteral("PROFILE SYNC ERROR") &&
            session.statusText().contains(QStringLiteral("rejected"), Qt::CaseInsensitive) &&
            !session.startReady();
    }

    static bool readyReplugRecoversWithoutStart() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        const auto board = identity();
        seedVerified(fixture, board, QStringLiteral("COM7"), false);
        if (fixture.session.state() != QStringLiteral("READY")) return false;

        simulateDisconnect(fixture);
        const bool disconnected = fixture.session.recoveryPending_ &&
            !fixture.session.startReady() &&
            fixture.session.state() != QStringLiteral("READY") &&
            fixture.session.state() != QStringLiteral("RUNNING");

        seedVerified(fixture, board, QStringLiteral("COM11"), false);
        return disconnected &&
            !fixture.session.recoveryPending_ &&
            fixture.session.state() == QStringLiteral("READY") &&
            !fixture.device.running() &&
            fixture.device.portName() == QStringLiteral("COM11");
    }

    static bool runningReplugNeverAutoStarts() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        const auto board = identity();
        seedVerified(fixture, board, QStringLiteral("COM7"), true);
        if (fixture.session.state() != QStringLiteral("RUNNING")) return false;

        simulateDisconnect(fixture);
        if (fixture.session.state() == QStringLiteral("RUNNING") || fixture.device.running()) return false;

        seedVerified(fixture, board, QStringLiteral("COM12"), false);
        return fixture.session.state() == QStringLiteral("READY") &&
            fixture.session.startReady() &&
            !fixture.device.running();
    }

    static bool wrongDeviceRecoveryFailsClosed() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        seedVerified(fixture, identity(QStringLiteral("A1B2C3D4E5F6")), QStringLiteral("COM7"), false);
        simulateDisconnect(fixture);
        if (!fixture.session.recoveryPending_) return false;

        seedVerified(
            fixture,
            identity(QStringLiteral("001122334455"), QStringLiteral(ARSTACK_STUDIO_VERSION), QStringLiteral("FEDCBA9876543210")),
            QStringLiteral("COM14"),
            false);

        return fixture.session.setupError_ &&
            fixture.session.state() == QStringLiteral("SETUP ERROR") &&
            fixture.session.statusText().contains(QStringLiteral("different ARStack injector"), Qt::CaseInsensitive) &&
            !fixture.session.startReady();
    }

    static bool healthTimeoutIsBounded() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        seedVerified(fixture, identity(), QStringLiteral("COM7"), false);
        auto& device = fixture.device;

        bool failed = false;
        QObject::connect(&device, &StudioDeviceController::controlHealthFailed, &device, [&failed] {
            failed = true;
        });

        device.controlHealthTimer_.stop();
        device.controlResponsive_ = true;
        device.healthProbeOutstanding_ = true;
        device.missedHealthReplies_ = 0;
        device.serviceControlHealth();
        const bool degraded =
            !device.controlResponsive_ && device.missedHealthReplies_ == 1;
        device.serviceControlHealth();

        return degraded && failed &&
            device.missedHealthReplies_ == StudioDeviceController::controlHealthMaxMisses() &&
            !device.controlResponsive_ &&
            !device.healthProbeOutstanding_;
    }

    static bool workerTeardownCompletes() {
        bool profileReady = false;
        {
            Fixture fixture;
            profileReady = fixture.profileReady;
        }
        return profileReady;
    }
};

int runDeterministicSessionHarness(int argc, char* argv[]) {
    return DeterministicSessionHarness::run(argc, argv);
}
