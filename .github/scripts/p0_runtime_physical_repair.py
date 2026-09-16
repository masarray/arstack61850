from pathlib import Path


def replace(path, old, new, count=1):
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    actual = text.count(old)
    if actual != count:
        raise SystemExit(f"{path}: expected {count} occurrence(s), found {actual}: {old[:120]!r}")
    p.write_text(text.replace(old, new, count), encoding="utf-8")


# P0-A: primary close must force the application event loop to exit.
# The prior lifecycle regression masked the bug by calling exit() itself
# after window.close(), so remove that test-side escape hatch.
replace(
    "apps/arstack_studio/src/main.cpp",
    """        if (event != nullptr && event->type() == QEvent::Close && app_ != nullptr) {
            closeObserved_ = true;
            app_->quit();
        }
""",
    """        if (event != nullptr && event->type() == QEvent::Close && app_ != nullptr) {
            closeObserved_ = true;
            // QCoreApplication::quit() can be ignored by application/window
            // policy. A primary-window close is an explicit process-exit request,
            // so terminate the main event loop deterministically. aboutToQuit()
            // remains the single cleanup boundary for serial/firmware workers.
            QCoreApplication::exit(0);
        }
""",
)

replace(
    "apps/arstack_studio/src/main.cpp",
    """            if (!primaryCloseFilter.closeObserved()) {
                qCritical().noquote() << "Application lifecycle regression: normal primary-window close bypassed the lifetime filter.";
                QCoreApplication::exit(10);
                return;
            }

            QCoreApplication::exit(0);
""",
    """            if (!primaryCloseFilter.closeObserved()) {
                qCritical().noquote() << "Application lifecycle regression: normal primary-window close bypassed the lifetime filter.";
                QCoreApplication::exit(10);
                return;
            }
            // Do not call exit() here. The regression must prove that the real
            // primary-window close path itself terminates the process.
""",
)

replace(
    "apps/arstack_studio/qml/Main.qml",
    """        configurationWindow.hide()
        detachedPhasorWindow.hide()
        detachedWaveformWindow.hide()
        Qt.quit()
""",
    """        configurationWindow.hide()
        detachedPhasorWindow.hide()
        detachedWaveformWindow.hide()
        // C++ owns process termination and bounded worker retirement. Do not
        // race it with a second QML quit path.
""",
)

# P0-B: remember the exact port that actually timed out. A single visible
# serial port is a valid recovery candidate even when Windows supplies weak/no
# USB metadata. Firmware write remains protected by espflash ROM verification.
replace(
    "apps/arstack_studio/src/DeviceController.hpp",
    """    [[nodiscard]] QString recommendedPort() const;
    [[nodiscard]] QString discoveryStatus() const;
""",
    """    [[nodiscard]] QString recommendedPort() const;
    [[nodiscard]] QString recoveryCandidatePort() const;
    [[nodiscard]] QString discoveryStatus() const;
""",
)

replace(
    "apps/arstack_studio/src/DeviceController.hpp",
    """    QString recommendedPort_;
    QString discoveryStatus_{QStringLiteral("Looking for an ARStack ESP32-P4 injector...")};
""",
    """    QString recommendedPort_;
    QString recoveryCandidatePort_;
    QString discoveryStatus_{QStringLiteral("Looking for an ARStack ESP32-P4 injector...")};
""",
)

replace(
    "apps/arstack_studio/src/DeviceController.cpp",
    """QStringList DeviceController::ports() const { return ports_; }
QString DeviceController::recommendedPort() const { return recommendedPort_; }
QString DeviceController::discoveryStatus() const { return discoveryStatus_; }
""",
    """QStringList DeviceController::ports() const { return ports_; }
QString DeviceController::recommendedPort() const { return recommendedPort_; }
QString DeviceController::recoveryCandidatePort() const { return recoveryCandidatePort_; }
QString DeviceController::discoveryStatus() const { return discoveryStatus_; }
""",
)

replace(
    "apps/arstack_studio/src/DeviceController.cpp",
    """        lastIdentificationPort_ = port;
        identifyAttempts_ = attempts;
        if (continuing) {
""",
    """        lastIdentificationPort_ = port;
        identifyAttempts_ = attempts;
        const QString timedOutPort = port.trimmed();
        const bool singleVisiblePort = ports_.size() == 1 &&
            ports_.constFirst().compare(timedOutPort, Qt::CaseInsensitive) == 0;
        const bool recommendedCandidate = !recommendedPort_.isEmpty() &&
            recommendedPort_.compare(timedOutPort, Qt::CaseInsensitive) == 0;
        if (!timedOutPort.isEmpty() && (singleVisiblePort || recommendedCandidate)) {
            recoveryCandidatePort_ = timedOutPort;
        }
        if (continuing) {
""",
)

replace(
    "apps/arstack_studio/src/DeviceController.cpp",
    """    ports_ = ports;
    recommendedPort_ = recommendedPort;
    highConfidenceCount_ = highConfidenceCount;

    if (identificationState_ == IdentificationState::Unidentified &&
""",
    """    ports_ = ports;
    recommendedPort_ = recommendedPort;
    highConfidenceCount_ = highConfidenceCount;
    if (!recoveryCandidatePort_.isEmpty() && !ports_.contains(recoveryCandidatePort_)) {
        recoveryCandidatePort_.clear();
    }

    if (identificationState_ == IdentificationState::Unidentified &&
""",
)

replace(
    "apps/arstack_studio/src/DeviceController.cpp",
    """    portName_ = portName;
    lastIdentificationPort_ = portName;
    identifyAttempts_ = 0;
""",
    """    portName_ = portName;
    lastIdentificationPort_ = portName;
    recoveryCandidatePort_.clear();
    identifyAttempts_ = 0;
""",
)

replace(
    "apps/arstack_studio/src/DeviceController.cpp",
    """void DeviceController::markDeviceVerified() {
    if (deviceVerified_) return;
    deviceVerified_ = true;
    lastIdentificationPort_ = portName_;
""",
    """void DeviceController::markDeviceVerified() {
    if (deviceVerified_) return;
    deviceVerified_ = true;
    recoveryCandidatePort_.clear();
    lastIdentificationPort_ = portName_;
""",
)

replace(
    "apps/arstack_studio/src/SmartSessionController.cpp",
    """    const QString recommended = recommendedPort.trimmed();
    if (!recommended.isEmpty()) return recommended;
    if (visiblePorts.size() != 1) return {};
    return visiblePorts.front().trimmed();
""",
    """    const QString recommended = recommendedPort.trimmed();
    if (!recommended.isEmpty() && visiblePorts.contains(recommended, Qt::CaseInsensitive)) {
        return recommended;
    }
    if (visiblePorts.size() != 1) return {};
    return visiblePorts.front().trimmed();
""",
)

replace(
    "apps/arstack_studio/src/SmartSessionController.cpp",
    """    const QString recommended = device_->recommendedPort().trimmed();
    if (recommended.isEmpty() || !device_->ports().contains(recommended)) {
        blankBoardDetected_ = false;
        blankBoardPort_.clear();
        return;
    }

    blankBoardPort_ = recommended;
    blankBoardDetected_ = true;
""",
    """    QString recoveryPort = device_->recoveryCandidatePort().trimmed();
    if (recoveryPort.isEmpty()) {
        recoveryPort = chooseRecoveryPort(device_->recommendedPort(), device_->ports());
    }
    if (recoveryPort.isEmpty() || !device_->ports().contains(recoveryPort, Qt::CaseInsensitive)) {
        blankBoardDetected_ = false;
        blankBoardPort_.clear();
        return;
    }

    blankBoardPort_ = recoveryPort;
    blankBoardDetected_ = true;
""",
)

replace(
    "apps/arstack_studio/src/SmartSessionController.cpp",
    """                QStringLiteral("No ARStack semantic identity was received after %1 bounded attempts. Retry identification, or use Tools > Advanced > Verify selected port to enter explicit firmware recovery.")
                    .arg(device_->identifyAttempts()),
""",
    """                QStringLiteral("No ARStack semantic identity was received after %1 bounded attempts and the visible serial devices are ambiguous. Disconnect unrelated serial devices or select the intended ESP32-P4 port, then retry.")
                    .arg(device_->identifyAttempts()),
""",
)

replace(
    "apps/arstack_studio/src/DeterministicSessionHarness.cpp",
    """            {"trusted ESP32-P4 identity timeout -> firmware required", automaticIdentityTimeoutOffersFirmwareRecovery()},
            {"ambiguous identity timeout -> UNIDENTIFIED", ambiguousIdentityTimeoutStaysUnidentified()},
""",
    """            {"trusted ESP32-P4 identity timeout -> firmware required", automaticIdentityTimeoutOffersFirmwareRecovery()},
            {"single visible COM without metadata -> firmware required", singleVisibleTimeoutWithoutRecommendationOffersFirmwareRecovery()},
            {"ambiguous identity timeout -> UNIDENTIFIED", ambiguousIdentityTimeoutStaysUnidentified()},
""",
)

marker = """    static bool ambiguousIdentityTimeoutStaysUnidentified() {
"""
insertion = """    static bool singleVisibleTimeoutWithoutRecommendationOffersFirmwareRecovery() {
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

"""
p = Path("apps/arstack_studio/src/DeterministicSessionHarness.cpp")
text = p.read_text(encoding="utf-8")
if text.count(marker) != 1:
    raise SystemExit("DeterministicSessionHarness.cpp: ambiguous case marker mismatch")
p.write_text(text.replace(marker, insertion + marker, 1), encoding="utf-8")
