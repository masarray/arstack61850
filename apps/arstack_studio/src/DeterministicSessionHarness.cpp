// SPDX-License-Identifier: GPL-3.0-or-later

#include "DeterministicSessionHarness.hpp"

#include "DeviceIoWorker.hpp"
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
            {"firmware verification failure remains copyable after READY", firmwareFailureRemainsDiagnosable()},
            {"firmware build mismatch explains expected and observed IDs", reconnectBuildMismatchPreservesEvidence()},
            {"fragmented espflash progress -> monotonic", fragmentedEspflashProgressIsMonotonic()},
            {"verified COM hint falls back without bypassing identity", verifiedComOrderingPreservesFallback()},
            {"reconnect focuses verified USB, never blind Bluetooth COM sweeps", fastPhysicalUsbCandidateSelection()},
            {"automatic IDENTIFY timeout never implies missing firmware", automaticIdentityTimeoutOffersFirmwareRecovery()},
            {"single COM semantic timeout stays unverified", singleVisibleTimeoutWithoutRecommendationOffersFirmwareRecovery()},
            {"ambiguous identity timeout -> UNIDENTIFIED", ambiguousIdentityTimeoutStaysUnidentified()},
            {"manual recovery intent stays optional and visible", manualRecoveryIntentArmsFirmwareOffer()},
            {"stale generation release -> ignored", staleReleaseGenerationIsIgnored()},
            {"firmware probe before serial release -> blocked", firmwareProbeRequiresReleasedPort()},
            {"PTP preflight readback guards active board and deferred role commands", ptpBoardRuntimePreflight()},
            {"PTP preflight adopts same physical role with no disruptive STOP", ptpAlreadyActiveMatchingRole()},
            {"PTP stale-role rejection never fabricates OFF", ptpRoleRejectPreservesBoardRunning()},
            {"PTP staged START requires role/config/runtime ACKs", ptpGuidedStartNeedsAcks()},
            {"PTP SOURCE invalid 9-3 profile rejects before serial", ptpSourceProfilePreflight()},

            {"PTP stale role readback cannot bypass ACK", ptpStaleRoleReadbackBlocked()},
            {"PTP runtime requires matching PTP2 role", ptpRuntimeRoleEvidenceRequired()},
            {"PTP RUNNING before PTP2 is bounded success", ptpRuntimeOutOfOrderProof()},
            {"PTP source rejection survives verified safety STOP", ptpSourceReasonSurvivesSafetyStop()},
            {"PTP LINK_DOWN is classified early; MAC INVALID_STATE keeps STOP verification", ptpPhysicalEthernetPreflightAndMacFailure()},
            {"PTP rejected config never starts", ptpGuidedRejectedConfigStops()},
            {"PTP STOP cancels pending start after verified readback", ptpGuidedStopCancelsStart()},
            {"legacy bare SV START refuses stale eight-channel state", legacyBareStartFailsClosed()},
            {"SV commit ACK/readback START chain is generation bound", svTransactionAckChain()},
            {"SV stale ACK and mismatched firmware readback fail closed", svTransactionNegativeAcks()},
            {"RUNNING 4I4V live commit verifies ACK without START or SV stop", svLiveTransactionAckNoRestart()},
            {"RUNNING live rejected readback retains existing SV stream", svLiveTransactionRejectKeepsRunning()},
            {"STOP cancels pending SV commit-to-START chain", svStopCancelsPendingStart()},
            {"ABC live SET encoder remains consistent and bounded", abcSetpointBatchContract()},
            {"PTP STOP full queue uses bounded priority slot", ptpSafetyStopBypassesFullQueue()},
            {"SV STOP remains responsive with full command queue", svPriorityStopBypassesFullQueue()},
            {"control lease heartbeat coalesces outside saturated exclusive queue", heartbeatKeepsBoundedSafetySlotUnderExclusiveLoad()},
            {"board fail-safe STOP is not mislabelled as operator STOP ACK", forcedFirmwareStopRetainsCauseAndIsNotOperatorAck()},
            {"latched board STOP cause overrides READY and survives console truncation", boardStopCausePersistsUntilVerifiedRestart()},
            {"old board SV fault at startup is diagnostic history, not an active alarm", historicSvStopDoesNotTriggerStartupAlarm()},
            {"live SV fault has one active incident and manual STOP stays quiet", liveSvStopIsNewIncidentOnly()},
            {"PTP STOP requires firmware ACK and stopped readback", ptpStopConfirmationCannotBeGuessed()},
            {"PTP STOP timeout blocks restart pending manual recovery", ptpUnverifiedStopBlocksRestart()},
            {"Studio teardown never owns autonomous SV/PTP lifetime", ptpOnlyShutdownIntent()},
            {"PTP role mismatch and new session revoke pending start", ptpGuidedRoleAndGenerationGuards()},
            {"profile timeout -> bounded terminal error", profileTimeoutIsTerminal()},
            {"profile failure waits visible bounded cooldown before retry", profileRetryCooldownIsVisible()},
            {"absolute deadline prevents indefinite PREPARING", profileHardDeadlineTerminates()},
            {"profile rejection -> bounded terminal error", profileRejectionIsTerminal()},
            {"binary stale ACK identity -> fail closed", binaryProfileStaleAckFailsClosed()},
            {"identical canonical PROFILE digest + readback reuses active", canonicalProfileReuseRequiresMatchingReadback()},
            {"supervisor accepts same generation only with verified reuse", matchingProfileReuseTransitionsToReady()},
            {"same COM firmware reboot invalidates old START and profile authority", samePortFirmwareRebootResetsAuthority()},
            {"corrupt console PROFILE command reports cause and blocks START", malformedProfileLineFailsClosed()},
            {"PTP polling suspended while binary PROFILE stage owns serial", ptpRefreshBlockedDuringProfileSync()},
            {"binary PROFILE coalesces PTP/SHOW without internal toast", binaryProfileReservesSerialConsole()},
            {"PTP STOP during exclusive CFG/BIN defers proof, never invents OFF", ptpStopReadbackDeferredDuringExclusive()},
            {"UART paced but STOP remains reserved queue preemption", serialPacingDoesNotRemoveSafetySlot()},
            {"binary progress -> per-step deadline rearmed", binaryProfileProgressRearmsDeadline()},
            {"USB port loss requires two verified misses; replug emits one arrival", usbPhysicalPortLossAndArrivalGate()},
            {"READY unplug/replug COM renumber -> READY stopped", readyReplugRecoversWithoutStart()},
            {"RUNNING unplug/replug -> never auto-restarts", runningReplugNeverAutoStarts()},
            {"wrong device during recovery -> SETUP ERROR", wrongDeviceRecoveryFailsClosed()},
            {"CFG transport exclusivity cannot trigger false health loss", healthProbeDefersDuringScenario()},
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
            QStringLiteral("PROFILE-BINARY-V1"),
            QStringLiteral("LIVE-SETPOINTS"),
            QStringLiteral("SV-LAN-AUTONOMY-V1"),
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

    static bool reconnectBuildMismatchPreservesEvidence() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        auto stale = identity();
        stale.buildId = QStringLiteral("fedcba9876543210");
        seedVerified(fixture, stale, QStringLiteral("COM7"), false);
        auto& session = fixture.session;
        session.started_ = false;
        session.updateRequested_ = true;
        session.updateWasOptional_ = true;
        session.updateStage_ = SmartSessionController::UpdateStage::reconnecting;
        session.reconcile();
        const QString evidence = session.firmwareUpdateResult();
        return !session.updateRequested_ &&
            session.state() == QStringLiteral("READY") &&
            session.firmwareUpdateAvailable() &&
            evidence.contains(QStringLiteral("fedcba9876543210")) &&
            evidence.contains(QStringLiteral("0123456789abcdef")) &&
            evidence.contains(QStringLiteral("Copy diagnostics")) &&
            session.diagnosticsText().contains(evidence);
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

    static bool verifiedComOrderingPreservesFallback() {
        const QStringList visible{QStringLiteral("COM3"), QStringLiteral("COM15"),
                                  QStringLiteral("COM7")};
        const QStringList expected{QStringLiteral("COM15"), QStringLiteral("COM7"),
                                   QStringLiteral("COM3")};
        const QStringList staleExpected{QStringLiteral("COM7"), QStringLiteral("COM3"),
                                        QStringLiteral("COM15")};
        return DeviceIoWorker::orderedDiscoveryPorts(
                   visible, QStringLiteral("COM15"), QStringLiteral("COM7")) == expected &&
               DeviceIoWorker::orderedDiscoveryPorts(
                   visible, QStringLiteral("COM99"), QStringLiteral("COM7")) == staleExpected &&
               DeviceIoWorker::orderedDiscoveryPorts(
                   visible, QString{}, QString{}) == visible &&
               DeviceIoWorker::identityMaxAttempts() >= 5 &&
               DeviceIoWorker::identityRetryIntervalMs() >= 1000;
    }

    static bool fastPhysicalUsbCandidateSelection() {
        const QStringList allPorts{
            QStringLiteral("COM3"), QStringLiteral("COM9"),
            QStringLiteral("COM15"), QStringLiteral("COM24")};
        const QStringList trustedPhysical{QStringLiteral("COM15")};
        const QStringList realOnly{QStringLiteral("COM15")};
        // Restore within one semantic timeout rather than chasing several
        // unrelated generic/virtual ports for 5 * 1100ms EACH.
        return DeviceIoWorker::focusedAutomaticPorts(
                   allPorts, trustedPhysical,
                   QStringLiteral("COM15"), QStringLiteral("COM15")) == realOnly &&
               DeviceIoWorker::focusedAutomaticPorts(
                   allPorts, trustedPhysical,
                   QStringLiteral("COM99"), QStringLiteral("COM15")) == realOnly &&
               DeviceIoWorker::focusedAutomaticPorts(
                   allPorts, QStringList{},
                   QStringLiteral("COM15"), QString{}) == realOnly &&
               // Initial setup without USB metadata is still supported.
               DeviceIoWorker::focusedAutomaticPorts(
                   allPorts, QStringList{}, QString{}, QString{}) == allPorts &&
               // Multiple physically plausible devices require *real*
               // IDENTIFY of each, ordered by verified identity.
               DeviceIoWorker::focusedAutomaticPorts(
                   allPorts,
                   QStringList{QStringLiteral("COM9"), QStringLiteral("COM15")},
                   QStringLiteral("COM15"), QStringLiteral("COM9")) ==
                   QStringList{QStringLiteral("COM15"), QStringLiteral("COM9")};
    }

    static bool automaticIdentityTimeoutOffersFirmwareRecovery() {
        Fixture fixture;
        if (!fixture.profileReady || !seedIdentityTimeout(fixture, false)) return false;
        return fixture.session.state() == QStringLiteral("UNIDENTIFIED") &&
            !fixture.session.firmwareInstallVisible() &&
            !fixture.session.blankBoardDetected_ &&
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

        return session.state() == QStringLiteral("UNIDENTIFIED") &&
            !session.firmwareInstallVisible() &&
            !session.blankBoardDetected_ &&
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
        return fixture.session.state() == QStringLiteral("FIRMWARE RECOVERY") &&
            fixture.session.firmwareInstallVisible() &&
            fixture.session.blankBoardPort_ == QStringLiteral("COM7") &&
            !fixture.session.startReady();
    }

    static bool ptpOnlyShutdownIntent() {
        Fixture fixture;
        auto& device = fixture.device;
        device.running_ = false;
        device.ptpRunning_ = false;
        device.ptpStartStage_ = DeviceController::PtpStartStage::idle;
        if (device.needsPtpStopBeforeShutdown()) return false;
        device.ptpRunning_ = true;
        if (!device.needsPtpStopBeforeShutdown()) return false;
        device.ptpRunning_ = false;
        device.ptpStartStage_ = DeviceController::PtpStartStage::awaiting_start;
        if (!device.needsPtpStopBeforeShutdown()) return false;
        device.ptpStartStage_ = DeviceController::PtpStartStage::awaiting_role_ack;
        return device.needsPtpStopBeforeShutdown();
    }

    static bool legacyBareStartFailsClosed() {
        Fixture f;
        // Regression: neither a future C++ caller nor the legacy supervisor
        // entry point may transmit START without a fresh scenario snapshot.
        f.session.startReady_ = true;
        if (f.session.requestStart()) return false;
        if (f.device.DeviceController::start()) return false;
        return f.device.lastError().contains(QStringLiteral("Bare START refused"));
    }

    static void seedScenarioAck(Fixture& f) {
        seedVerified(f, identity(), QStringLiteral("COM7"), false);
        auto& d = f.device;
        if (d.sessionGeneration_ == 0) d.setSessionGeneration(77);
        d.ioWorkerReady_ = true; // Queued invocation accepted, no physical COM.
        d.scenarioStage_ = DeviceController::ScenarioStage::awaiting_commit;
        d.scenarioToken_ = 4242U;
        d.scenarioHash_ = UINT64_C(0xABCDEF1234567890);
        d.scenarioBoot_ = d.identity_.bootId;
        d.scenarioSessionGeneration_ = d.sessionGeneration_;
        d.scenarioProfileGeneration_ = d.profileGeneration_;
    }

    static bool svTransactionAckChain() {
        Fixture f;
        seedScenarioAck(f);
        auto& d = f.device;
        d.processLine(QStringLiteral(
            "CFG COMMITTED token=4242 hash=ABCDEF1234567890 generation=91 boot=0123456789ABCDEF"));
        if (d.scenarioStage_ != DeviceController::ScenarioStage::awaiting_readback ||
            d.running()) return false;
        d.processLine(QStringLiteral(
            "CFG READBACK token=4242 hash=ABCDEF1234567890 generation=91 boot=0123456789ABCDEF"));
        if (d.scenarioStage_ != DeviceController::ScenarioStage::awaiting_start_ack ||
            d.running()) return false;
        d.processLine(QStringLiteral(
            "START accepted: profile armed token=4242 hash=ABCDEF1234567890"));
        return d.running() && d.scenarioStage_ == DeviceController::ScenarioStage::idle;
    }

    static bool svStopCancelsPendingStart() {
        Fixture f;
        seedScenarioAck(f);
        auto& d = f.device;
        if (!f.session.requestStop()) return false;
        if (d.scenarioStage_ != DeviceController::ScenarioStage::idle || d.running())
            return false;
        d.processLine(QStringLiteral(
            "CFG COMMITTED token=4242 hash=ABCDEF1234567890 generation=91 boot=0123456789ABCDEF"));
        d.processLine(QStringLiteral(
            "START accepted: profile armed token=4242 hash=ABCDEF1234567890"));
        return !d.running() && d.scenarioStage_ == DeviceController::ScenarioStage::idle;
    }

    static bool svLiveTransactionAckNoRestart() {
        Fixture f;
        seedScenarioAck(f);
        auto& d = f.device;
        d.setRunning(true);
        d.scenarioLiveUpdate_ = true;
        int verified = 0;
        QObject::connect(&d, &DeviceController::liveScenarioApplied,
                         &d, [&verified] { ++verified; });
        d.processLine(QStringLiteral(
            "CFG COMMITTED token=4242 hash=ABCDEF1234567890 generation=91 boot=0123456789ABCDEF"));
        if (!d.running() ||
            d.scenarioStage_ != DeviceController::ScenarioStage::awaiting_readback ||
            verified != 0) return false;
        d.processLine(QStringLiteral(
            "CFG READBACK token=4242 hash=ABCDEF1234567890 generation=91 boot=0123456789ABCDEF"));
        return d.running() && !d.scenarioLiveUpdate_ &&
            d.scenarioStage_ == DeviceController::ScenarioStage::idle &&
            verified == 1 &&
            d.logText_.contains(QStringLiteral("mode=LIVE")) &&
            !d.logText_.contains(QStringLiteral("START_ACK_VERIFIED")) &&
            !d.logText_.contains(QStringLiteral("FAIL_SAFE_STOP_REQUEST"));
    }

    static bool svLiveTransactionRejectKeepsRunning() {
        Fixture f;
        seedScenarioAck(f);
        auto& d = f.device;
        d.setRunning(true);
        d.scenarioLiveUpdate_ = true;
        d.processLine(QStringLiteral(
            "CFG COMMITTED token=4242 hash=ABCDEF1234567890 generation=91 boot=0123456789ABCDEF"));
        if (d.scenarioStage_ != DeviceController::ScenarioStage::awaiting_readback)
            return false;
        d.processLine(QStringLiteral(
            "CFG READBACK token=4242 hash=ABCDEF1234567890 generation=92 boot=0123456789ABCDEF"));
        if (!d.running() ||
            d.scenarioStage_ != DeviceController::ScenarioStage::idle ||
            !d.lastError_.contains(QStringLiteral("differs")) ||
            d.logText_.contains(QStringLiteral("FAIL_SAFE_STOP_REQUEST")))
            return false;
        // A stale subsequent START/CFG ACK cannot mutate a live stream.
        d.processLine(QStringLiteral(
            "START accepted: profile armed token=4242 hash=ABCDEF1234567890"));
        return d.running() && d.scenarioStage_ == DeviceController::ScenarioStage::idle;
    }

    static bool svTransactionNegativeAcks() {
        Fixture mismatch;
        seedScenarioAck(mismatch);
        auto& d = mismatch.device;
        d.processLine(QStringLiteral(
            "CFG COMMITTED token=4241 hash=ABCDEF1234567890 generation=91 boot=0123456789ABCDEF"));
        if (d.running() || d.scenarioStage_ != DeviceController::ScenarioStage::idle ||
            !d.lastError_.contains(QStringLiteral("mismatch"))) return false;

        Fixture wrongReadback;
        seedScenarioAck(wrongReadback);
        auto& r = wrongReadback.device;
        r.processLine(QStringLiteral(
            "CFG COMMITTED token=4242 hash=ABCDEF1234567890 generation=91 boot=0123456789ABCDEF"));
        if (r.scenarioStage_ != DeviceController::ScenarioStage::awaiting_readback) return false;
        r.processLine(QStringLiteral(
            "CFG READBACK token=4242 hash=ABCDEF1234567890 generation=92 boot=0123456789ABCDEF"));
        if (r.running() || r.scenarioStage_ != DeviceController::ScenarioStage::idle) return false;

        Fixture negativeAck;
        seedScenarioAck(negativeAck);
        auto& n = negativeAck.device;
        n.processLine(QStringLiteral("CFG REJECT token=4242 code=5"));
        return !n.running() && n.scenarioStage_ == DeviceController::ScenarioStage::idle &&
            n.lastError_.contains(QStringLiteral("rejected"));
    }

    static QVariantMap ptpValidProfile() {
        return {{QStringLiteral("domain"), 0},
                {QStringLiteral("transportSpecific"), 0},
                {QStringLiteral("vlanEnabled"), false},
                {QStringLiteral("vlanId"), 100},
                {QStringLiteral("vlanPriority"), 4},
                {QStringLiteral("announceIntervalMs"), 1000},
                {QStringLiteral("syncIntervalMs"), 1000},
                {QStringLiteral("respondToPeerDelay"), true}};
    }

    static bool setupGuidedPtp(Fixture& f, const QString& role) {
        if (!f.profileReady) return false;
        seedVerified(f, identity(), QStringLiteral("COM7"), false);
        f.device.ptpAvailable_ = true;
        f.device.ioWorkerReady_ = true; // No actual COM opened by this harness.
        if (!f.session.requestStartPtpConfigured(role, ptpValidProfile()) ||
            f.device.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_preflight)
            return false;
        f.device.processLine(ptpStoppedStatus());
        return f.device.ptpStartStage_ == DeviceController::PtpStartStage::awaiting_role_ack;
    }

    static bool ptpSourceProfilePreflight() {
        Fixture f;
        if (!f.profileReady) return false;
        seedVerified(f, identity(), QStringLiteral("COM7"), false);
        f.device.ptpAvailable_ = true;
        f.device.ioWorkerReady_ = true; // Test transport: no external COM.
        auto& d = f.device;
        auto invalid = ptpValidProfile();
        invalid[QStringLiteral("respondToPeerDelay")] = false;
        if (f.session.requestStartPtpConfigured(QStringLiteral("SOURCE"), invalid) ||
            d.ptpStarting() || d.ptpRunning_ ||
            !d.ptpLastStartFailure_.contains(QStringLiteral("hardware peer-delay")) ||
            !d.ptpStatus_.startsWith(QStringLiteral("PTP profile rejected")))
            return false;
        invalid = ptpValidProfile();
        invalid[QStringLiteral("syncIntervalMs")] = 250;
        if (f.session.requestStartPtpConfigured(QStringLiteral("SOURCE"), invalid) ||
            d.ptpStarting() || d.ptpRunning_) return false;
        invalid = ptpValidProfile();
        invalid[QStringLiteral("announceIntervalMs")] = 2000;
        if (f.session.requestStartPtpConfigured(QStringLiteral("SOURCE"), invalid) ||
            d.ptpStarting() || d.ptpRunning_) return false;
        // A valid SOURCE profile must immediately permit a fresh attempt.
        if (!f.session.requestStartPtpConfigured(QStringLiteral("SOURCE"),
                                                  ptpValidProfile()) ||
            d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_preflight ||
            !d.ptpLastStartFailure_.isEmpty()) return false;
        d.processLine(ptpStoppedStatus());
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_role_ack)
            return false;
        return true;
    }

    static QString ptpRoleEvidence(const QString& role) {
        return QStringLiteral("PTP2 role=%1 discipline=UNLOCKED source=NONE offset=NA path=NA "
            "jitter=NA freq=0 global=0 measured=NA rxAnnounce=0 rxSync=0 rxFollowUp=0 "
            "rxPdelay=0 pdelayReq=0 accepted=0 rejected=0").arg(role);
    }

    static bool ptpStaleRoleReadbackBlocked() {
        Fixture f;
        if (!setupGuidedPtp(f, QStringLiteral("RECEIVER"))) return false;
        auto& d = f.device;
        d.processLine(QStringLiteral("PTPROLE role=RECEIVER value=1 running=0"));
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_role_ack) return false;
        if (f.session.requestPtpRefresh()) return false;
        d.processLine(QStringLiteral("PTPROLE accepted"));
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_role_readback)
            return false;
        d.processLine(QStringLiteral("PTPROLE role=RECEIVER value=1 running=0"));
        return d.ptpStartStage_ == DeviceController::PtpStartStage::awaiting_config;
    }

    static bool ptpRuntimeRoleEvidenceRequired() {
        Fixture f;
        if (!setupGuidedPtp(f, QStringLiteral("SOURCE"))) return false;
        auto& d = f.device;
        d.processLine(QStringLiteral("PTPROLE accepted"));
        d.processLine(QStringLiteral("PTPROLE role=SOURCE value=0 running=0"));
        d.processLine(QStringLiteral("PTP configuration accepted"));
        d.processLine(QStringLiteral("PTP start accepted"));
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_runtime) return false;
        d.processLine(QStringLiteral(
            "PTP status=RUNNING Announce=1 Sync=1 FollowUp=1 PdelayFrames=0 TXfail=0"));
        // The first RUNNING readback is not enough without PTP2; it is not
        // an error either. A bounded deadline must trigger safety STOP if
        // the additional role evidence never arrives.
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_runtime ||
            !d.ptpStarting() || !d.ptpRuntimeRunningSeen_ ||
            d.ptpRuntimeRoleSeen_ ||
            f.session.requestStartPtpConfigured(QStringLiteral("SOURCE"), ptpValidProfile()) ||
            d.startPtp() || d.setPtpRole(QStringLiteral("MONITOR"))) return false;
        if (!QMetaObject::invokeMethod(
                &d.ptpStartTimer_, "timeout", Qt::DirectConnection)) return false;
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_stop_ack ||
            !d.ptpStarting()) return false;
        d.processLine(QStringLiteral("PTP stop requested"));
        d.processLine(ptpStoppedStatus());
        if (d.ptpStarting() || d.ptpRunning_) return false;
        if (!f.session.requestStartPtpConfigured(QStringLiteral("SOURCE"), ptpValidProfile()))
            return false;
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_preflight)
            return false;
        d.processLine(ptpStoppedStatus());
        d.processLine(QStringLiteral("PTPROLE accepted"));
        d.processLine(QStringLiteral("PTPROLE role=SOURCE value=0 running=0"));
        d.processLine(QStringLiteral("PTP configuration accepted"));
        d.processLine(QStringLiteral("PTP start accepted"));
        d.processLine(ptpRoleEvidence(QStringLiteral("RECEIVER")));
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_stop_ack ||
            !d.ptpStarting()) return false;
        d.processLine(QStringLiteral("PTP stop requested"));
        d.processLine(ptpStoppedStatus());
        return !d.ptpStarting() && !d.ptpRunning_ &&
            d.ptpStatus_.contains(QStringLiteral("confirmed by board"));
    }

    static bool ptpPhysicalEthernetPreflightAndMacFailure() {
        {
            Fixture f;
            auto& d = f.device;
            d.processLine(QStringLiteral(
                "I ar_smv_eth: PTP ETH driver=STARTED link=DOWN tx_ready=0 start_reason=NONE"));
            if (d.ptpEthernetDriver_ != QStringLiteral("STARTED") ||
                d.ptpEthernetLink_ != QStringLiteral("DOWN") || d.ptpRunning_) return false;
            d.processLine(QStringLiteral(
                "PTP ETH driver=STARTED link=UP tx_ready=1 start_reason=NONE"));
            if (d.ptpEthernetLink_ != QStringLiteral("UP") || d.ptpRunning_) return false;
            d.processLine(QStringLiteral(
                "PTP ETH driver=STOPPED link=DOWN tx_ready=0 start_reason=ETHERNET_DRIVER_NOT_STARTED"));
            if (d.ptpEthernetDriver_ != QStringLiteral("STOPPED") ||
                d.ptpEthernetLink_ != QStringLiteral("DOWN")) return false;
        }
        {
            Fixture f;
            if (!setupGuidedPtp(f, QStringLiteral("SOURCE"))) return false;
            auto& d = f.device;
            d.processLine(QStringLiteral("PTPROLE accepted"));
            d.processLine(QStringLiteral("PTPROLE role=SOURCE value=0 running=0"));
            d.processLine(QStringLiteral("PTP configuration accepted"));
            if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_start)
                return false;
            d.processLine(QStringLiteral(
                "PTP start rejected: source_reason=ETHERNET_LINK_DOWN"));
            // ETH preflight has not launched ANY source runtime, therefore
            // a generic rollback PTP STOP would be unsafe and misleading.
            if (d.ptpStarting() || d.ptpRunning_ ||
                !d.ptpLastStartFailure_.contains(QStringLiteral("link is DOWN")) ||
                !d.ptpStatus_.contains(QStringLiteral("link is DOWN")))
                return false;
            if (d.logText_.contains(QStringLiteral("→ PTP STOP"))) return false;
        }
        {
            Fixture f;
            if (!setupGuidedPtp(f, QStringLiteral("SOURCE"))) return false;
            auto& d = f.device;
            d.processLine(QStringLiteral("PTPROLE accepted"));
            d.processLine(QStringLiteral("PTPROLE role=SOURCE value=0 running=0"));
            d.processLine(QStringLiteral("PTP configuration accepted"));
            d.processLine(QStringLiteral(
                "PTP start rejected: source_reason=ETHERNET_TX_INVALID_STATE"));
            // A source TX error is NOT physical evidence PTP is OFF until
            // its teardown/STOP is acknowledged by the board.
            if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_stop_ack ||
                !d.ptpLastStartFailure_.contains(QStringLiteral("ESP_ERR_INVALID_STATE")) ||
                d.ptpRunning_) return false;
            d.processLine(QStringLiteral("PTP stop requested"));
            d.processLine(ptpStoppedStatus());
            if (d.ptpStarting() || d.ptpRunning_ ||
                !d.ptpLastStartFailure_.contains(QStringLiteral("ESP_ERR_INVALID_STATE")))
                return false;
        }
        return true;
    }

    static bool ptpSourceReasonSurvivesSafetyStop() {
        Fixture f;
        if (!setupGuidedPtp(f, QStringLiteral("SOURCE"))) return false;
        auto& d = f.device;
        d.processLine(QStringLiteral("PTPROLE accepted"));
        d.processLine(QStringLiteral("PTPROLE role=SOURCE value=0 running=0"));
        d.processLine(QStringLiteral("PTP configuration accepted"));
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_start) return false;
        d.processLine(QStringLiteral(
            "PTP start rejected: source_reason=PDELAY_RX_TIMESTAMP_UNAVAILABLE"));
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_stop_ack ||
            !d.ptpLastStartFailure_.contains(QStringLiteral("PDELAY_RX_TIMESTAMP_UNAVAILABLE")))
            return false;
        d.processLine(QStringLiteral("PTP stop requested"));
        d.processLine(ptpStoppedStatus());
        if (d.ptpStarting() || d.ptpRunning_) return false;
        return d.ptpLastStartFailure_.contains(
                   QStringLiteral("PDELAY_RX_TIMESTAMP_UNAVAILABLE")) &&
            d.ptpStatus_.contains(QStringLiteral("confirmed by board"));
    }

    static bool ptpRuntimeOutOfOrderProof() {
        Fixture f;
        if (!setupGuidedPtp(f, QStringLiteral("SOURCE"))) return false;
        auto& d = f.device;
        d.processLine(QStringLiteral("PTPROLE accepted"));
        d.processLine(QStringLiteral("PTPROLE role=SOURCE value=0 running=0"));
        d.processLine(QStringLiteral("PTP configuration accepted"));
        d.processLine(QStringLiteral("PTP start accepted"));
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_runtime)
            return false;
        d.processLine(QStringLiteral(
            "PTP status=RUNNING Announce=1 Sync=1 FollowUp=1 PdelayFrames=0 TXfail=0"));
        // Real firmware sends PTP status before the periodic PTP2 line.
        if (!d.ptpStarting() || !d.ptpRuntimeRunningSeen_ ||
            d.ptpRuntimeRoleSeen_) return false;
        d.processLine(ptpRoleEvidence(QStringLiteral("SOURCE")));
        return !d.ptpStarting() && d.ptpRunning_ &&
            d.ptpStatus_.contains(QStringLiteral("external timing still unqualified"));
    }

    static bool ptpAlreadyActiveMatchingRole() {
        Fixture f;
        if (!f.profileReady) return false;
        seedVerified(f, identity(), QStringLiteral("COM7"), false);
        auto& d = f.device;
        d.ptpAvailable_ = true;
        d.ioWorkerReady_ = true;
        if (!f.session.requestStartPtpConfigured(QStringLiteral("SOURCE"), ptpValidProfile()) ||
            d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_preflight)
            return false;
        d.processLine(ptpRoleEvidence(QStringLiteral("SOURCE")));
        if (!d.ptpStarting()) return false;
        d.processLine(QStringLiteral(
            "PTP status=RUNNING Announce=10 Sync=10 FollowUp=10 PdelayFrames=0 TXfail=0"));
        return !d.ptpStarting() && d.ptpRunning_ &&
            d.ptpLastStartFailure_.isEmpty() &&
            d.ptpStatus_.contains(QStringLiteral("already active")) &&
            d.ptpStartPhase() == QStringLiteral("IDLE");
    }

    static bool ptpRoleRejectPreservesBoardRunning() {
        Fixture f;
        if (!setupGuidedPtp(f, QStringLiteral("SOURCE"))) return false;
        auto& d = f.device;
        d.processLine(QStringLiteral(
            "E ar_smv_profile: PTPROLE rejected: stop PTP before changing operating role"));
        if (d.ptpStarting() || !d.ptpRunning_ ||
            !d.ptpLastStartFailure_.contains(QStringLiteral("PTPROLE rejected")) ||
            !d.ptpStatus_.contains(QStringLiteral("active on board")))
            return false;
        if (!f.session.requestStopPtp()) return false;
        d.processLine(QStringLiteral("PTP stop requested"));
        d.processLine(ptpStoppedStatus());
        return !d.ptpStarting() && !d.ptpRunning_ &&
            d.ptpLastStartFailure_.contains(QStringLiteral("PTPROLE rejected")) &&
            f.session.requestStartPtpConfigured(QStringLiteral("SOURCE"), ptpValidProfile());
    }

    static bool ptpBoardRuntimePreflight() {
        Fixture running;
        if (!running.profileReady) return false;
        seedVerified(running, identity(), QStringLiteral("COM7"), false);
        auto& d = running.device;
        d.ptpAvailable_ = true;
        d.ioWorkerReady_ = true;
        if (!running.session.requestStartPtpConfigured(QStringLiteral("SOURCE"), ptpValidProfile()))
            return false;
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_preflight ||
            !d.ptpStarting()) return false;
        // Stale ROLE and CONFIG chatter may not advance preflight.
        d.processLine(QStringLiteral("PTPROLE accepted"));
        d.processLine(QStringLiteral("PTP configuration accepted"));
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_preflight) return false;
        d.processLine(QStringLiteral(
            "PTP status=RUNNING Announce=1 Sync=1 FollowUp=1 PdelayFrames=0 TXfail=0"));
        if (d.ptpStarting() || !d.ptpRunning_ ||
            !d.ptpLastStartFailure_.contains(QStringLiteral("already RUNNING")) ||
            d.ptpStartPhase() != QStringLiteral("IDLE")) return false;
        // Do not auto-stop a real transmitter merely because GUI has an old
        // local OFF state. Explicit verified STOP is the only recovery.
        if (!d.stopPtp() ||
            d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_stop_ack)
            return false;
        d.processLine(QStringLiteral("PTP stop requested"));
        d.processLine(ptpStoppedStatus());
        if (d.ptpStarting() || d.ptpRunning_) return false;

        Fixture timedOut;
        if (!timedOut.profileReady) return false;
        seedVerified(timedOut, identity(), QStringLiteral("COM7"), false);
        timedOut.device.ptpAvailable_ = true;
        timedOut.device.ioWorkerReady_ = true;
        if (!timedOut.session.requestStartPtpConfigured(QStringLiteral("SOURCE"), ptpValidProfile()))
            return false;
        if (!QMetaObject::invokeMethod(
                &timedOut.device.ptpStartTimer_, "timeout", Qt::DirectConnection))
            return false;
        return !timedOut.device.ptpStarting() &&
               timedOut.device.ptpLastStartFailure_.contains(QStringLiteral("VERIFY_STOPPED")) &&
               !timedOut.device.ptpRunning_;
    }

    static bool ptpGuidedStartNeedsAcks() {
        Fixture f;
        if (!setupGuidedPtp(f, QStringLiteral("RECEIVER"))) return false;
        auto& d = f.device;
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_role_ack ||
            d.ptpRunning_ || !d.ptpStarting()) return false;
        d.processLine(QStringLiteral("PTPROLE accepted"));
        d.processLine(QStringLiteral("PTPROLE role=RECEIVER value=1 running=0"));
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_config ||
            !d.ptpStarting()) return false;
        d.processLine(QStringLiteral("PTP configuration accepted"));
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_start ||
            d.ptpRunning_) return false;
        d.processLine(QStringLiteral("PTP start accepted"));
        // A PTP-only START must NEVER switch the SV 4I+4V publisher on.
        if (d.running_ || d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_runtime) return false;
        d.processLine(ptpRoleEvidence(QStringLiteral("RECEIVER")));
        d.processLine(QStringLiteral(
            "PTP status=RUNNING Announce=1 Sync=1 FollowUp=1 PdelayFrames=0 TXfail=0"));
        return !d.running_ && !d.ptpStarting() && d.ptpRunning_ &&
            d.ptpRole_ == QStringLiteral("RECEIVER") &&
            d.ptpStatus_.contains(QStringLiteral("external timing still unqualified"));
    }

    static bool ptpGuidedRejectedConfigStops() {
        Fixture f;
        if (!setupGuidedPtp(f, QStringLiteral("SOURCE"))) return false;
        auto& d = f.device;
        d.processLine(QStringLiteral("PTPROLE accepted"));
        d.processLine(QStringLiteral("PTPROLE role=SOURCE value=0 running=0"));
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_config) return false;
        d.processLine(QStringLiteral("PTP configuration rejected: invalid profile"));
        if (d.ptpStarting() || d.ptpRunning_ ||
            !d.ptpStatus_.startsWith(QStringLiteral("PTP start failed")) ||
            !d.ptpStatus_.contains(QStringLiteral("invalid profile")) ||
            !d.ptpLastStartFailure_.contains(QStringLiteral("invalid profile")))
            return false;
        // A later STOP readback must never erase the reason an earlier START
        // failed. It is reset only at the next explicit START or new boot.
        if (!f.session.requestStartPtpConfigured(QStringLiteral("SOURCE"), ptpValidProfile()))
            return false;
        return d.ptpLastStartFailure_.isEmpty();
    }

    static QString ptpStoppedStatus() {
        return QStringLiteral(
            "PTP status=STOPPED Announce=0 Sync=0 FollowUp=0 PdelayFrames=0 TXfail=0");
    }

    static bool ptpGuidedStopCancelsStart() {
        Fixture f;
        if (!setupGuidedPtp(f, QStringLiteral("SOURCE"))) return false;
        auto& d = f.device;
        if (!f.session.requestStopPtp() ||
            d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_stop_ack) return false;
        // ACK from now-abandoned transaction cannot trigger CONFIG or START.
        d.processLine(QStringLiteral("PTPROLE role=SOURCE value=0 running=0"));
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_stop_ack) return false;
        d.processLine(QStringLiteral("PTP stop requested"));
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_stop_readback) return false;
        d.processLine(ptpStoppedStatus());
        return !d.ptpStarting() &&
            d.ptpStartStage_ == DeviceController::PtpStartStage::idle &&
            d.ptpStatus_.contains(QStringLiteral("confirmed by board"));
    }

    static bool abcSetpointBatchContract() {
        const QString ia = DeviceController::formatSignalCommand(
            QStringLiteral("Ia"), 1.0, 0.0, 0U, 1000.0, 100.0, 50.0);
        const QString ib = DeviceController::formatSignalCommand(
            QStringLiteral("Ib"), 1.0, -120.0, 0U, 1000.0, 100.0, 50.0);
        const QString ic = DeviceController::formatSignalCommand(
            QStringLiteral("Ic"), 1.0, 120.0, 0U, 1000.0, 100.0, 50.0);
        // Operator table −30/−150/+90 must NEVER be serialized as 0/−120/+120.
        const QString iaOperator = DeviceController::formatSignalCommand(
            QStringLiteral("Ia"), 1.0, -30.0, 0U, 1000.0, 100.0, 50.0);
        const QString ibOperator = DeviceController::formatSignalCommand(
            QStringLiteral("Ib"), 1.0, -150.0, 0U, 1000.0, 100.0, 50.0);
        const QString icOperator = DeviceController::formatSignalCommand(
            QStringLiteral("Ic"), 1.0, 90.0, 0U, 1000.0, 100.0, 50.0);
        const QString badId = DeviceController::formatSignalCommand(
            QStringLiteral("IX"), 1.0, 0.0, 0U, 1000.0, 100.0, 50.0);
        const QString overflow = DeviceController::formatSignalCommand(
            QStringLiteral("Ia"), 1.0e12, 0.0, 0U, 1000.0, 100.0, 50.0);
        const QString dcNegative = DeviceController::formatSignalCommand(
            QStringLiteral("Ia"), -1.0, 0.0, 0U, 1000.0, 100.0, 0.0);
        return ia == QStringLiteral("SET IA 1000 0 0") &&
               ib == QStringLiteral("SET IB 1000 -120000 0") &&
               ic == QStringLiteral("SET IC 1000 120000 0") &&
               iaOperator == QStringLiteral("SET IA 1000 -30000 0") &&
               ibOperator == QStringLiteral("SET IB 1000 -150000 0") &&
               icOperator == QStringLiteral("SET IC 1000 90000 0") &&
               badId.isEmpty() && overflow.isEmpty() &&
               dcNegative == QStringLiteral("SET IA -1000 0 0");
    }

    static bool svPriorityStopBypassesFullQueue() {
        DeviceIoWorker worker;
        for (int i = 0; i < DeviceIoWorker::commandQueueCapacity(); ++i) {
            worker.commandQueue_.enqueue(DeviceIoWorker::PendingCommand{
                QStringLiteral("CFG SET IA 1000 0 0"),
                QByteArrayLiteral("CFG SET IA 1000 0 0\\n"), false, true});
        }
        worker.exclusiveCommandsRemaining_ = DeviceIoWorker::commandQueueCapacity();
        worker.queuePrioritySvStop();
        const bool cancelled = worker.commandQueue_.isEmpty() &&
                               worker.exclusiveCommandsRemaining_ == 0 &&
                               worker.svStopPending_;
        worker.queuePrioritySvStop();
        return cancelled && worker.svStopPending_ && worker.commandQueue_.isEmpty();
    }

    static bool historicSvStopDoesNotTriggerStartupAlarm() {
        Fixture f;
        if (!f.profileReady) return false;
        seedVerified(f, identity(), QStringLiteral("COM15"), false);
        auto& d = f.device;
        d.processLine(QStringLiteral(
            "SVLIFE state=STOPPED stop_cause=RUNTIME_FAULT stop_seq=7 "
            "stop_uptime_ms=1308494 hb_seen=1"));
        if (d.svLastStopCause() != QStringLiteral("RUNTIME_FAULT") ||
            !d.svStopReason().isEmpty() || d.svUnexpectedStopObserved() ||
            d.svUnexpectedStopIncident() != 0U) return false;

        // First SHOW after reconnect may contain the last TX error and/or
        // trailing serial log lines from before the current Studio session.
        d.processLine(QStringLiteral(
            "SVTXFAULT state=STOPPED svlife_stop_seq=7 burst_now=0 "
            "last_stop_seq=3 last_stop_first_error=263 last_stop_last_error=263 "
            "last_stop_link_up=1"));
        d.processLine(QStringLiteral("SV transport fault: 400 consecutive TX failures"));
        d.processLine(QStringLiteral(
            "STOP accepted: SV transmission suppressed by fail-closed runtime"));
        d.processLine(QStringLiteral(
            "state=STOPPED generation=5 signal_frequency=50000 mHz"));
        return !d.running() && !d.svUnexpectedStopObserved() &&
            d.svUnexpectedStopIncident() == 0U &&
            d.svStopReason().isEmpty() &&
            d.svTxFaultDetail().isEmpty() &&
            d.svLastStopCause() == QStringLiteral("RUNTIME_FAULT") &&
            f.session.state() == QStringLiteral("READY");
    }

    static bool liveSvStopIsNewIncidentOnly() {
        Fixture f;
        if (!f.profileReady) return false;
        seedVerified(f, identity(), QStringLiteral("COM15"), false);
        auto& d = f.device;
        d.processLine(QStringLiteral(
            "SVLIFE state=STOPPED stop_cause=RUNTIME_FAULT stop_seq=3"));
        if (d.svUnexpectedStopObserved()) return false;

        // This RUNNING state is seen during the current verified session,
        // then the board stops without an operator STOP request.
        d.setRunning(true);
        d.processLine(QStringLiteral(
            "state=STOPPED generation=8 signal_frequency=50000 mHz"));
        if (d.running() || !d.svUnexpectedStopObserved() ||
            d.svStopReason() != QStringLiteral("AWAITING_BOARD_CAUSE") ||
            d.svUnexpectedStopIncident() != 1U) return false;
        d.processLine(QStringLiteral(
            "SVLIFE state=STOPPED stop_cause=RUNTIME_FAULT stop_seq=4"));
        d.processLine(QStringLiteral(
            "SVTXFAULT state=STOPPED svlife_stop_seq=4 burst_now=0 "
            "last_stop_seq=1 last_stop_first_error=263 last_stop_last_error=263 "
            "last_stop_link_up=1"));
        if (d.svStopReason() != QStringLiteral("RUNTIME_FAULT") ||
            !d.svTxFaultDetail().contains(QStringLiteral("TIMEOUT")) ||
            d.svUnexpectedStopIncident() != 1U) return false;
        d.processLine(QStringLiteral(
            "SVLIFE state=STOPPED stop_cause=RUNTIME_FAULT stop_seq=4"));
        if (d.svUnexpectedStopIncident() != 1U) return false;

        // An actual subsequent successful START clears the presentation, not
        // the board's sticky last-stop cause. Operator STOP is not an incident.
        d.setRunning(true);
        if (d.svUnexpectedStopObserved() || !d.svStopReason().isEmpty() ||
            !d.svTxFaultDetail().isEmpty() ||
            d.svLastStopCause() != QStringLiteral("RUNTIME_FAULT")) return false;
        d.operatorStopPending_ = true;
        d.setRunning(false);
        d.processLine(QStringLiteral(
            "SVLIFE state=STOPPED stop_cause=OPERATOR_STOP stop_seq=5"));
        if (d.svUnexpectedStopObserved() || !d.svStopReason().isEmpty() ||
            d.svUnexpectedStopIncident() != 1U) return false;

        // Even if a new COM session sees the same old fault record, it must
        // not create a new alarm. The next genuine stop increments the ID.
        d.resetTelemetry();
        d.processLine(QStringLiteral(
            "SVLIFE state=STOPPED stop_cause=RUNTIME_FAULT stop_seq=5"));
        if (d.svUnexpectedStopObserved() || !d.svStopReason().isEmpty()) return false;
        d.setRunning(true);
        d.setRunning(false);
        return d.svUnexpectedStopObserved() && d.svUnexpectedStopIncident() == 2U;
    }

    static bool boardStopCausePersistsUntilVerifiedRestart() {
        Fixture f;
        if (!f.profileReady) return false;
        seedVerified(f, identity(), QStringLiteral("COM7"), true);
        auto& d = f.device;
        d.processLine(QStringLiteral(
            "I ar_smv_ctrl: state=STOPPED generation=3 signal_frequency=50000 mHz"));
        if (d.running() || d.svStopReason_ != QStringLiteral("AWAITING_BOARD_CAUSE"))
            return false;
        d.processLine(QStringLiteral(
            "I ar_smv_ctrl: SVLIFE state=STOPPED stop_cause=LEASE_EXPIRED "
            "stop_seq=1 stop_uptime_ms=33000 stop_hb_age_ms=2500 "
            "hb_age_ms=2600 hb_seen=1 lease_ms=2500 watchdog=1"));
        if (d.svStopReason_ != QStringLiteral("LEASE_EXPIRED") ||
            d.svLastStopCause_ != QStringLiteral("LEASE_EXPIRED") ||
            !d.logText_.contains(QStringLiteral("BOARD_STOP_READBACK")))
            return false;
        // Repeated polling of the same event must be idempotent.
        d.processLine(QStringLiteral(
            "SVLIFE state=STOPPED stop_cause=LEASE_EXPIRED stop_seq=1"));
        if (d.svStopReason_ != QStringLiteral("LEASE_EXPIRED"))
            return false;
        // An already restarted board can still return its last-stop history.
        d.processLine(QStringLiteral(
            "SVLIFE state=RUNNING stop_cause=LEASE_EXPIRED stop_seq=1"));
        if (!d.svStopReason_.isEmpty() ||
            d.svLastStopCause_ != QStringLiteral("LEASE_EXPIRED")) return false;
        // Operator STOP must never be reported as spontaneous lease loss.
        d.setRunning(true);
        d.operatorStopPending_ = true;
        d.processLine(QStringLiteral(
            "SVLIFE state=STOPPED stop_cause=OPERATOR_STOP stop_seq=2"));
        return d.svStopReason_.isEmpty();
    }

    static bool forcedFirmwareStopRetainsCauseAndIsNotOperatorAck() {
        Fixture f;
        auto& d = f.device;
        d.processLine(QStringLiteral(
            "STOP accepted: SV transmission suppressed by fail-closed runtime"));
        if (!d.logText_.contains(QStringLiteral("RUNTIME_FAIL_SAFE_STOP_OBSERVED")) ||
            d.logText_.contains(QStringLiteral("STOP_ACK_OBSERVED")))
            return false;
        d.processLine(QStringLiteral(
            "Control session lease expired after 2500 ms; SV transmission stopped"));
        if (!d.logText_.contains(QStringLiteral("FIRMWARE_STOP_CAUSE")) ||
            !d.logText_.contains(QStringLiteral("lease expired")))
            return false;
        d.processLine(QStringLiteral("STOP accepted: SV transmission suppressed"));
        return d.logText_.contains(QStringLiteral("STOP_ACK_OBSERVED"));
    }

    static bool heartbeatKeepsBoundedSafetySlotUnderExclusiveLoad() {
        DeviceIoWorker worker;
        // Normal 64-command FIFO is saturated by an exclusive CFG batch.
        // A 700ms lease heartbeat must remain deliverable without growing
        // this FIFO; safety STOP must still own the highest priority.
        for (int i = 0; i < DeviceIoWorker::commandQueueCapacity(); ++i) {
            worker.commandQueue_.enqueue(DeviceIoWorker::PendingCommand{
                QStringLiteral("CFG SET IA 1000 0 0"),
                QByteArrayLiteral("CFG SET IA 1000 0 0\n"), false, true});
        }
        worker.exclusiveCommandsRemaining_ = DeviceIoWorker::commandQueueCapacity();
        worker.queuePriorityHeartbeat(); // isolated, no live COM needed
        worker.queuePriorityHeartbeat(); // repeated keepalive coalesces
        if (!worker.heartbeatPending_ ||
            worker.commandQueue_.size() != DeviceIoWorker::commandQueueCapacity())
            return false;
        worker.queuePrioritySvStop();
        if (!worker.svStopPending_ || !worker.heartbeatPending_ ||
            !worker.commandQueue_.isEmpty())
            return false; // STOP does not wait behind keepalive
        worker.heartbeatPending_ = false; // explicit session deactivation
        return !worker.heartbeatPending_ && worker.svStopPending_;
    }

    static bool ptpSafetyStopBypassesFullQueue() {
        DeviceIoWorker worker;
        for (int i = 0; i < DeviceIoWorker::commandQueueCapacity(); ++i) {
            worker.commandQueue_.enqueue(DeviceIoWorker::PendingCommand{
                i == 7 ? QStringLiteral("PTP START") : QStringLiteral("PROFILE SHOW"),
                QByteArrayLiteral("PROFILE SHOW\n"), false, false});
        }
        worker.queuePriorityPtpStop(); // no COM: test pure queue mutation only
        const bool reserved = worker.ptpStopPending_ &&
            worker.commandQueue_.size() == DeviceIoWorker::commandQueueCapacity() - 1;
        bool noPendingStart = true;
        for (const auto& cmd : worker.commandQueue_) {
            if (cmd.text == QStringLiteral("PTP START")) noPendingStart = false;
        }
        worker.queuePriorityPtpStop(); // coalesced, never double-allocates
        return reserved && noPendingStart && worker.ptpStopPending_ &&
            worker.commandQueue_.size() == DeviceIoWorker::commandQueueCapacity() - 1;
    }

    static bool ptpStopConfirmationCannotBeGuessed() {
        Fixture f;
        if (!setupGuidedPtp(f, QStringLiteral("RECEIVER"))) return false;
        auto& d = f.device;
        if (!d.stopPtp()) return false;
        d.ptpRunning_ = true; // preserve RUN until the new STOP has proof
        d.processLine(ptpStoppedStatus()); // stale SHOW reply, before STOP ACK
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_stop_ack ||
            !d.ptpRunning_) return false;
        d.processLine(QStringLiteral("PTP stop requested"));
        d.processLine(QStringLiteral(
            "PTP status=RUNNING Announce=1 Sync=1 FollowUp=0 PdelayFrames=0 TXfail=0"));
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_stop_readback ||
            !d.ptpStopPollTimer_.isActive()) return false;
        d.processLine(ptpStoppedStatus());
        return d.ptpStartStage_ == DeviceController::PtpStartStage::idle &&
            !d.ptpStarting() && !d.ptpStopPollTimer_.isActive();
    }

    static bool ptpUnverifiedStopBlocksRestart() {
        Fixture f;
        if (!setupGuidedPtp(f, QStringLiteral("SOURCE"))) return false;
        auto& d = f.device;
        if (!d.stopPtp()) return false;
        d.markPtpStopUnconfirmed(QStringLiteral("Injected STOP verification timeout"));
        const bool blocksRestart = d.ptpStarting() &&
            d.ptpStartStage_ == DeviceController::PtpStartStage::stop_unconfirmed &&
            !f.session.requestStartPtpConfigured(QStringLiteral("SOURCE"), ptpValidProfile());
        if (!blocksRestart || !d.stopPtp()) return false;
        d.processLine(QStringLiteral("PTP stop requested"));
        d.processLine(ptpStoppedStatus());
        return !d.ptpStarting() && d.ptpStartStage_ == DeviceController::PtpStartStage::idle;
    }

    static bool ptpGuidedRoleAndGenerationGuards() {
        Fixture f;
        if (!setupGuidedPtp(f, QStringLiteral("RECEIVER"))) return false;
        auto& d = f.device;
        d.processLine(QStringLiteral("PTPROLE accepted"));
        // Earlier SHOW telemetry must not abort or advance RECEIVER selection.
        d.processLine(QStringLiteral("PTPROLE role=MONITOR value=2 running=0"));
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_role_readback ||
            !d.ptpStarting()) return false;
        d.processLine(QStringLiteral("PTPROLE role=RECEIVER value=1 running=0"));
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_config) return false;
        const quint64 previous = d.sessionGeneration_;
        d.setSessionGeneration(previous + 1);
        d.processLine(QStringLiteral("PTPROLE role=RECEIVER value=1 running=0"));
        return !d.ptpStarting() && d.ptpStartStage_ ==
            DeviceController::PtpStartStage::idle;
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

    static bool profileRetryCooldownIsVisible() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        seedVerified(fixture, identity(), QStringLiteral("COM7"), false);
        auto& session = fixture.session;
        session.needsProfileSync_ = true;
        session.profileSyncStage_ = SmartSessionController::ProfileSyncStage::deploying;
        session.profileSyncAttempts_ = 1;
        fixture.device.profileDeploying_ = false;
        fixture.device.profileArmed_ = false;
        session.handleProfileSyncAttemptFailure(QStringLiteral("timed out at BINCHUNK"));
        const bool waiting = session.profileRetryTimer_.isActive() &&
            session.profileRetryTimer_.interval() ==
                SmartSessionController::profileSyncBackoffMs(1) &&
            session.state() == QStringLiteral("RECOVERING PROFILE") &&
            session.statusText().contains(QStringLiteral("BINCHUNK")) &&
            session.profileSyncAttempts_ == 1 &&
            !session.startReady();
        session.profileRetryTimer_.stop();
        return waiting;
    }

    static bool profileHardDeadlineTerminates() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        seedVerified(fixture, identity(), QStringLiteral("COM7"), false);
        auto& session = fixture.session;
        session.needsProfileSync_ = true;
        session.profileSyncStage_ = SmartSessionController::ProfileSyncStage::deploying;
        session.profileSyncAttempts_ = SmartSessionController::profileSyncMaxAttempts();
        fixture.device.profileDeploying_ = false;
        fixture.device.profileArmed_ = false;
        session.profileSyncTotalTimer_.start();
        // Deterministic simulated timer signal uses production timeout lambda.
        if (!QMetaObject::invokeMethod(&session.profileSyncTotalTimer_,
                                       "timeout", Qt::DirectConnection)) return false;
        return !session.profileSyncTotalTimer_.isActive() &&
            !session.profileRetryTimer_.isActive() &&
            session.profileSyncStage_ == SmartSessionController::ProfileSyncStage::failed &&
            session.state() == QStringLiteral("PROFILE SYNC ERROR") &&
            session.statusText().contains(QStringLiteral("absolute")) &&
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

    static bool binaryProfileStaleAckFailsClosed() {
        // Isolate DeviceController's parser boundary from the supervisor retry
        // policy. Production may immediately schedule a bounded retry after a
        // deployment failure; that must not be mistaken for stale-ACK acceptance.
        Fixture abortFixture;
        if (!abortFixture.profileReady) return false;
        seedVerified(abortFixture, identity(), QStringLiteral("COM7"), false);
        abortFixture.session.profileSyncStage_ =
            SmartSessionController::ProfileSyncStage::failed;
        auto& abortDevice = abortFixture.device;
        const QString abortGeneration = abortDevice.profileGeneration_;

        abortDevice.profileDeploying_ = true;
        abortDevice.profileArmed_ = false;
        abortDevice.binaryProfileStage_ =
            DeviceController::BinaryProfileStage::waiting_abort;
        abortDevice.binaryProfile_.transaction = 42U;
        abortDevice.binaryProfile_.lastObservedTransaction = 42U;
        abortDevice.lastError_.clear();

        const bool abortConsumed = abortDevice.processBinaryProfileLine(
            QStringLiteral("PROFILE BINABORT transaction=41 accepted"));
        const bool abortClosed =
            abortConsumed && !abortDevice.profileArmed_ &&
            abortDevice.profileGeneration_ == abortGeneration &&
            abortDevice.binaryProfileStage_ ==
                DeviceController::BinaryProfileStage::idle &&
            abortDevice.lastError_.contains(
                QStringLiteral("BINABORT"), Qt::CaseInsensitive);

        Fixture commitFixture;
        if (!commitFixture.profileReady) return false;
        seedVerified(commitFixture, identity(), QStringLiteral("COM7"), false);
        commitFixture.session.profileSyncStage_ =
            SmartSessionController::ProfileSyncStage::failed;
        auto& commitDevice = commitFixture.device;
        const QString commitGeneration = commitDevice.profileGeneration_;

        commitDevice.profileDeploying_ = true;
        commitDevice.profileArmed_ = false;
        commitDevice.binaryProfileStage_ =
            DeviceController::BinaryProfileStage::waiting_commit;
        commitDevice.binaryProfile_.transaction = 77U;
        commitDevice.binaryProfile_.svId = QStringLiteral("ARSTACK_SV01");
        commitDevice.binaryProfile_.appId = 0x4000U;
        commitDevice.binaryProfile_.rate = 4000U;
        commitDevice.binaryProfile_.modulus = 4000U;
        commitDevice.lastError_.clear();

        const bool commitConsumed = commitDevice.processBinaryProfileLine(
            QStringLiteral(
                "PROFILE BINCOMMIT transaction=76 committed generation=8 "
                "svID=ARSTACK_SV01 APPID=0x4000 rate=4000 wrap=4000"));
        const bool commitClosed =
            commitConsumed && !commitDevice.profileArmed_ &&
            commitDevice.profileGeneration_ == commitGeneration &&
            commitDevice.binaryProfileStage_ ==
                DeviceController::BinaryProfileStage::idle &&
            commitDevice.lastError_.contains(
                QStringLiteral("BINCOMMIT"), Qt::CaseInsensitive);

        return abortClosed && commitClosed;
    }

    static bool canonicalProfileReuseRequiresMatchingReadback() {
        Fixture good;
        if (!good.profileReady) return false;
        auto updated = identity();
        updated.capabilities.push_back(QStringLiteral("PROFILE-REUSE-V1"));
        seedVerified(good, updated, QStringLiteral("COM7"), false);
        good.session.profileSyncStage_ =
            SmartSessionController::ProfileSyncStage::failed;
        auto& d = good.device;
        d.profileDeploying_ = true;
        d.profileArmed_ = false;
        d.binaryProfileStage_ = DeviceController::BinaryProfileStage::waiting_reuse;
        d.binaryProfile_.svId = QStringLiteral("ARSTACK_SV01");
        d.binaryProfile_.appId = 0x4000;
        d.binaryProfile_.rate = 4000;
        d.binaryProfile_.modulus = 4000;
        d.binaryProfile_.confRev = 1;
        d.binaryProfile_.bytes = QByteArray(96, char(7));
        d.binaryProfile_.recordHash = 0xAABBCCDDEEFF0011ULL;
        d.binaryProfile_.lastObservedTransaction = 5;

        const auto readback = QStringLiteral(
            "PROFILE generation=7 svID=ARSTACK_SV01 APPID=0x4000 rate=4000 "
            "wrap=4000 confRev=1 VLAN=1/0/4 dataSetField=0 sampleRateField=0");
        if (!d.processBinaryProfileLine(readback)) return false;
        if (d.profileArmed() || d.profileWasReused()) return false;
        if (!d.processBinaryProfileLine(QStringLiteral(
                "PROFILE BINIDENTITY generation=7 hash=AABBCCDDEEFF0011 bytes=96"))) return false;
        const bool reused = d.profileArmed() && d.profileWasReused() &&
            !d.profileDeploying() && d.profileGeneration() == QStringLiteral("7");

        Fixture bad;
        if (!bad.profileReady) return false;
        seedVerified(bad, updated, QStringLiteral("COM8"), false);
        bad.session.profileSyncStage_ =
            SmartSessionController::ProfileSyncStage::failed;
        auto& different = bad.device;
        different.profileDeploying_ = true;
        different.profileArmed_ = false;
        different.binaryProfileStage_ = DeviceController::BinaryProfileStage::waiting_reuse;
        different.binaryProfile_.svId = QStringLiteral("ARSTACK_SV01");
        different.binaryProfile_.appId = 0x4000;
        different.binaryProfile_.rate = 4000;
        different.binaryProfile_.modulus = 4000;
        different.binaryProfile_.confRev = 1;
        different.binaryProfile_.bytes = QByteArray(96, char(7));
        different.binaryProfile_.recordHash = 0xAABBCCDDEEFF0011ULL;
        different.binaryProfile_.lastObservedTransaction = 5;
        different.processBinaryProfileLine(readback);
        different.processBinaryProfileLine(QStringLiteral(
            "PROFILE BINIDENTITY generation=6 hash=AABBCCDDEEFF0011 bytes=96"));
        // Same hash but stale generation can NEVER make the profile armed.
        return reused && !different.profileWasReused() && !different.profileArmed();
    }

    static bool matchingProfileReuseTransitionsToReady() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        seedVerified(fixture, identity(), QStringLiteral("COM7"), false);
        auto& session = fixture.session;
        auto& device = fixture.device;
        session.profileSyncBaselineGeneration_ = QStringLiteral("7");
        session.profileSyncStage_ = SmartSessionController::ProfileSyncStage::deploying;
        session.profileSyncAttempts_ = 1;
        session.needsProfileSync_ = true;
        device.profileArmed_ = true;
        device.profileDeploying_ = false;
        device.profileWasReused_ = true;
        session.handleProfileStateChanged();
        session.reconcile();
        const bool ready = !session.needsProfileSync_ &&
            session.profileSyncStage_ == SmartSessionController::ProfileSyncStage::idle &&
            session.state() == QStringLiteral("READY") && session.startReady();
        // An unchanged generation alone without verified reuse is not proof.
        session.needsProfileSync_ = true;
        session.profileSyncStage_ = SmartSessionController::ProfileSyncStage::deploying;
        device.profileWasReused_ = false;
        session.handleProfileStateChanged();
        return ready &&
            session.needsProfileSync_ &&
            session.profileSyncStage_ == SmartSessionController::ProfileSyncStage::deploying;
    }

    static bool samePortFirmwareRebootResetsAuthority() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        const auto oldIdentity = identity();
        seedVerified(fixture, oldIdentity, QStringLiteral("COM7"), true);
        auto& device = fixture.device;
        auto& session = fixture.session;
        device.profileWasReused_ = true;
        session.profileSyncBootId_ = oldIdentity.bootId;
        const QString replacement = QStringLiteral(
            "I (311) ar_smv_ctrl: ARSTACK identity product=SMV-INJECTOR "
            "target=ESP32-P4 protocol=1 device_id=A1B2C3D4E5F6 "
            "firmware=0.1.1 build=0123456789abcdef "
            "boot_id=FEDCBA9876543210 "
            "capabilities=SMV-4I4V,PROFILE,PROFILE-BINARY-V1,"
            "PROFILE-REUSE-V1,LIVE-SETPOINTS,SESSION-LEASE,PTP-P2,SMPSYNCH-AUTO");
        device.processLine(replacement);
        return device.bootId() == QStringLiteral("FEDCBA9876543210") &&
            device.deviceVerified() &&
            !device.running() && !device.svUnexpectedStopObserved() &&
            device.svStopReason().isEmpty() && !device.profileArmed() &&
            !device.profileWasReused() &&
            session.profileSyncBootId_ != oldIdentity.bootId &&
            !session.startReady();
    }

    static bool malformedProfileLineFailsClosed() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        seedVerified(fixture, identity(), QStringLiteral("COM7"), false);
        fixture.session.profileSyncStage_ =
            SmartSessionController::ProfileSyncStage::failed;
        auto& d = fixture.device;
        d.profileDeploying_ = true;
        d.profileArmed_ = false;
        d.binaryProfileStage_ = DeviceController::BinaryProfileStage::waiting_chunk;
        d.binaryProfile_.transaction = 7;
        d.binaryProfile_.offset = 48;
        d.binaryProfile_.bytes = QByteArray(96, char(0x42));
        const auto stage = d.profileDeploymentProgressSummary();
        d.processLine(QStringLiteral("W (12345) ar_smv_ctrl: Unknown command 'PROFW'; type HELP"));
        return stage.contains(QStringLiteral("BINCHUNK")) &&
            stage.contains(QStringLiteral("48/96")) &&
            !d.profileDeploying_ && !d.profileArmed_ &&
            d.lastError_.contains(QStringLiteral("PROFW")) &&
            d.binaryProfileStage_ == DeviceController::BinaryProfileStage::idle &&
            !fixture.session.startReady();
    }

    static bool ptpRefreshBlockedDuringProfileSync() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        seedVerified(fixture, identity(), QStringLiteral("COM7"), false);
        fixture.session.profileSyncStage_ =
            SmartSessionController::ProfileSyncStage::deploying;
        fixture.device.profileDeploying_ = true;
        const bool blocked = !fixture.session.requestPtpRefresh();
        fixture.session.profileSyncStage_ = SmartSessionController::ProfileSyncStage::failed;
        fixture.device.profileDeploying_ = false;
        return blocked;
    }

    static bool binaryProfileReservesSerialConsole() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        seedVerified(fixture, identity(), QStringLiteral("COM7"), false);
        fixture.session.profileSyncStage_ =
            SmartSessionController::ProfileSyncStage::failed;
        auto& d = fixture.device;
        d.profileDeploying_ = true;
        d.profileArmed_ = false;
        d.binaryProfileStage_ = DeviceController::BinaryProfileStage::waiting_chunk;
        const QString initialError = d.lastError_;
        const bool ptpDeferred = d.sendPtpShow();
        const bool showDeferred = d.sendShow();
        const bool coalesced = d.sendPtpShow() && d.sendShow() &&
            d.deferredPtpShow_ && d.deferredShow_ &&
            d.lastError_ == initialError;
        const bool setBlocked = !d.setEnabled(QStringLiteral("IA"), true);
        d.frequencyPending_ = true;
        d.pendingFrequencyHz_ = 60.0;
        const bool deferredMutation = d.flushLiveCommands() && d.frequencyPending_;
        d.liveFlushTimer_.stop();
        // Busy telemetry is normal, but an unsafe mutation remains rejected.
        return ptpDeferred && showDeferred && coalesced && setBlocked &&
            deferredMutation && d.lastError_.contains(QStringLiteral("being prepared")) &&
            !d.lastError_.contains(QStringLiteral("owns the device console"));
    }

    static bool ptpStopReadbackDeferredDuringExclusive() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        seedVerified(fixture, identity(), QStringLiteral("COM7"), false);
        auto& d = fixture.device;
        d.ptpAvailable_ = true;
        d.ptpRunning_ = true;
        d.ioWorkerReady_ = true;
        d.profileDeploying_ = true;
        d.binaryProfileStage_ = DeviceController::BinaryProfileStage::waiting_chunk;
        const QString previousError = d.lastError_;
        if (!d.stopPtp() ||
            d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_stop_ack)
            return false;
        d.processLine(QStringLiteral("PTP stop requested"));
        if (d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_stop_readback ||
            !d.deferredPtpShow_ || !d.ptpRunning_ || d.lastError_ != previousError)
            return false;
        d.processLine(ptpStoppedStatus()); // stale status before fresh readback
        if (!d.ptpRunning_ ||
            d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_stop_readback)
            return false;
        // When the lock is released, exactly one readback is submitted to
        // the existing serial owner; no readback is accepted as STOP proof.
        d.profileDeploying_ = false;
        d.resetBinaryProfileTransfer();
        d.flushDeferredReadbacks();
        if (d.deferredPtpShow_ || !d.ptpRunning_ ||
            d.ptpStartStage_ != DeviceController::PtpStartStage::awaiting_stop_readback)
            return false;
        d.markPtpStopUnconfirmed(QStringLiteral("Injected STOP timeout: Copy diagnostics."));
        return d.ptpRunning_ &&
            d.ptpStartStage_ == DeviceController::PtpStartStage::stop_unconfirmed &&
            d.lastError_.contains(QStringLiteral("Copy diagnostics."));
    }

    static bool serialPacingDoesNotRemoveSafetySlot() {
        return DeviceIoWorker::serialCommandGapMs() >= 10 &&
            DeviceIoWorker::serialCommandGapMs() <= 50 &&
            svPriorityStopBypassesFullQueue() && ptpSafetyStopBypassesFullQueue();
    }

    static bool binaryProfileProgressRearmsDeadline() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        seedVerified(fixture, identity(), QStringLiteral("COM7"), false);
        auto& session = fixture.session;
        auto& device = fixture.device;

        session.profileSyncStage_ = SmartSessionController::ProfileSyncStage::deploying;
        session.profileSyncTimer_.stop();
        emit device.profileDeploymentProgress();
        const bool armed = session.profileSyncTimer_.isActive() &&
            session.profileSyncTimer_.interval() == SmartSessionController::profileSyncTimeoutMs();
        session.profileSyncTimer_.stop();
        session.profileSyncStage_ = SmartSessionController::ProfileSyncStage::idle;
        return armed;
    }

    static bool usbPhysicalPortLossAndArrivalGate() {
        using Worker = DeviceIoWorker;
        using Session = SmartSessionController;
        const QStringList before{QStringLiteral("COM7"), QStringLiteral("COM15")};
        const QStringList unplugged{QStringLiteral("COM7")};
        const QStringList replugged{QStringLiteral("COM7"), QStringLiteral("COM16")};
        const QStringList unchanged{QStringLiteral("COM7"), QStringLiteral("COM15")};
        return Worker::missingPortConfirmationPolls() == 2 &&
            Worker::presencePollIntervalMs() == 750 &&
            !Worker::verifiedPortPhysicallyGone(true, true, 0) &&
            !Worker::verifiedPortPhysicallyGone(true, true, 1) &&
            Worker::verifiedPortPhysicallyGone(true, true, 2) &&
            !Worker::verifiedPortPhysicallyGone(false, true, 2) &&
            !Worker::verifiedPortPhysicallyGone(true, false, 2) &&
            !Session::hasNewlyEnumeratedPort(before, unplugged) &&
            !Session::hasNewlyEnumeratedPort(before, unchanged) &&
            Session::hasNewlyEnumeratedPort(unplugged, replugged) &&
            Session::hasNewlyEnumeratedPort(
                QStringList{QStringLiteral("COM15")},
                QStringList{QStringLiteral("com15"), QStringLiteral("COM16")}) &&
            !Session::hasNewlyEnumeratedPort(
                QStringList{QStringLiteral("COM15")},
                QStringList{QStringLiteral("com15")});
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

    static bool healthProbeDefersDuringScenario() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        seedVerified(fixture, identity(), QStringLiteral("COM7"), false);
        auto& d = fixture.device;
        d.controlResponsive_ = true;
        d.healthProbeOutstanding_ = true;
        d.missedHealthReplies_ = 0;
        d.scenarioStage_ = DeviceController::ScenarioStage::awaiting_commit;
        d.serviceControlHealth();
        d.serviceControlHealth();
        // Known exclusive queue occupancy cannot mimic two missing COM replies.
        const bool quietBusy = d.controlResponsive_ && d.healthProbeOutstanding_ &&
            d.missedHealthReplies_ == 0 && d.connected_;
        d.scenarioStage_ = DeviceController::ScenarioStage::idle;
        d.serviceControlHealth();
        return quietBusy && !d.controlResponsive_ &&
            d.missedHealthReplies_ == 1; // real timeout remains detectable
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

    static bool firmwareFailureRemainsDiagnosable() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        auto& session = fixture.session;
        session.updateWasOptional_ = true;
        session.updateRequested_ = true;
        session.updateStage_ = SmartSessionController::UpdateStage::reconnecting;
        session.latchFirmwareFailure(QStringLiteral(
            "Firmware was written, but reconnect reported a different build."));
        const QString report = session.diagnosticsText();
        return !session.updateRequested_ && !session.setupError_ &&
            session.firmwareUpdateResult().contains(QStringLiteral("different build")) &&
            report.contains(QStringLiteral("Last firmware update result: Firmware was written")) &&
            report.contains(QStringLiteral("Bundled build ID: 0123456789abcdef")) &&
            report.contains(QStringLiteral("Configured SV ID: ARSTACK_SV01")) &&
            report.contains(QStringLiteral("Configured publisher fps: 4000")) &&
            report.contains(QStringLiteral("Exact bundled build match: NOT VERIFIED")) &&
            report.contains(QStringLiteral("Firmware protocol compatible: UNKNOWN"));
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
