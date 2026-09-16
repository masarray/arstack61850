from pathlib import Path

controller = Path('apps/arstack_studio/src/SmartSessionController.cpp')
text = controller.read_text(encoding='utf-8')
old = '''    // Preserve explicit recovery intent while the selected port moves through
    // CONNECTING/IDENTIFYING. Only the terminal Unidentified state can turn
    // that intent into a firmware offer. Automatic discovery always clears the
    // flag in startDeviceDiscovery(), so a transient handshake failure alone
    // can never imply blank firmware.
    if (device_->identificationState() != DeviceController::IdentificationState::Unidentified) {
        blankBoardDetected_ = false;
        blankBoardPort_.clear();
        return;
    }

    if (!manualRecoveryArmed_ || recoveryPending_) {
        blankBoardDetected_ = false;
        blankBoardPort_.clear();
        return;
    }

    const QString recommended = device_->recommendedPort().trimmed();
    if (recommended.isEmpty() || !device_->ports().contains(recommended)) {
        blankBoardDetected_ = false;
        blankBoardPort_.clear();
        return;
    }

    blankBoardPort_ = recommended;
    blankBoardDetected_ = true;
'''
new = '''    // A terminal semantic-identification timeout on the *single* high-confidence
    // Espressif/ESP32 candidate is a safe legacy/blank-firmware recovery signal.
    // This does not authorize a write: FirmwareManager still releases the serial
    // session and requires espflash ROM identity + ESP32-P4 pre-v3 verification
    // before write-bin can start. Generic or ambiguous serial ports remain closed.
    if (device_->identificationState() != DeviceController::IdentificationState::Unidentified) {
        blankBoardDetected_ = false;
        blankBoardPort_.clear();
        return;
    }

    if (recoveryPending_) {
        blankBoardDetected_ = false;
        blankBoardPort_.clear();
        return;
    }

    const QString recommended = device_->recommendedPort().trimmed();
    if (recommended.isEmpty() || !device_->ports().contains(recommended)) {
        blankBoardDetected_ = false;
        blankBoardPort_.clear();
        return;
    }

    blankBoardPort_ = recommended;
    blankBoardDetected_ = true;
'''
if old not in text:
    raise SystemExit('SmartSession recovery block did not match expected source')
text = text.replace(old, new, 1)
old_status = '''                QStringLiteral("Manual recovery selected for %1 after bounded semantic identification failed. Studio will verify chip and revision before any firmware write.")
                    .arg(blankBoardPort_),'''
new_status = '''                QStringLiteral("ESP32-P4 candidate %1 did not answer the current semantic identity after bounded attempts. Studio can install the bundled firmware after ROM chip/revision verification.")
                    .arg(blankBoardPort_),'''
if old_status not in text:
    raise SystemExit('Firmware-required presentation did not match expected source')
text = text.replace(old_status, new_status, 1)
controller.write_text(text, encoding='utf-8')

harness = Path('apps/arstack_studio/src/DeterministicSessionHarness.cpp')
text = harness.read_text(encoding='utf-8')
old_case = '{"auto identity timeout -> UNIDENTIFIED", automaticIdentityTimeoutStaysUnidentified()},'
new_case = '{"trusted ESP32-P4 identity timeout -> firmware required", automaticIdentityTimeoutOffersFirmwareRecovery()},\n            {"ambiguous identity timeout -> UNIDENTIFIED", ambiguousIdentityTimeoutStaysUnidentified()},'
if old_case not in text:
    raise SystemExit('Harness auto-timeout case did not match expected source')
text = text.replace(old_case, new_case, 1)
old_fn = '''    static bool automaticIdentityTimeoutStaysUnidentified() {
        Fixture fixture;
        if (!fixture.profileReady || !seedIdentityTimeout(fixture, false)) return false;
        return fixture.session.state() == QStringLiteral("UNIDENTIFIED") &&
            !fixture.session.firmwareInstallVisible() &&
            !fixture.session.blankBoardDetected_ &&
            !fixture.session.startReady();
    }
'''
new_fn = '''    static bool automaticIdentityTimeoutOffersFirmwareRecovery() {
        Fixture fixture;
        if (!fixture.profileReady || !seedIdentityTimeout(fixture, false)) return false;
        return fixture.session.state() == QStringLiteral("FIRMWARE REQUIRED") &&
            fixture.session.firmwareInstallVisible() &&
            fixture.session.blankBoardDetected_ &&
            fixture.session.blankBoardPort_ == QStringLiteral("COM7") &&
            !fixture.session.startReady();
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
'''
if old_fn not in text:
    raise SystemExit('Harness auto-timeout function did not match expected source')
text = text.replace(old_fn, new_fn, 1)
harness.write_text(text, encoding='utf-8')
