from pathlib import Path

ROOT = Path('.')


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        if new in text:
            return text
        raise SystemExit(f'{label}: expected source block not found')
    return text.replace(old, new, 1)


# --- SmartSessionController.hpp: explicit advisory/update cancellation contract.
path = ROOT / 'apps/arstack_studio/src/SmartSessionController.hpp'
text = path.read_text(encoding='utf-8')
text = replace_once(
    text,
    '    Q_PROPERTY(bool firmwareUpdateRequired READ firmwareUpdateRequired NOTIFY stateChanged)\n',
    '    Q_PROPERTY(bool firmwareUpdateRequired READ firmwareUpdateRequired NOTIFY stateChanged)\n'
    '    Q_PROPERTY(bool firmwareUpdateAvailable READ firmwareUpdateAvailable NOTIFY stateChanged)\n'
    '    Q_PROPERTY(bool firmwareUpdateCanCancel READ firmwareUpdateCanCancel NOTIFY stateChanged)\n',
    'SmartSession advisory properties')
text = replace_once(
    text,
    '    [[nodiscard]] bool firmwareUpdateRequired() const noexcept;\n'
    '    [[nodiscard]] bool firmwareReinstallAvailable() const noexcept;\n',
    '    [[nodiscard]] bool firmwareUpdateRequired() const noexcept;\n'
    '    [[nodiscard]] bool firmwareUpdateAvailable() const noexcept;\n'
    '    [[nodiscard]] bool firmwareUpdateCanCancel() const noexcept;\n'
    '    [[nodiscard]] bool firmwareReinstallAvailable() const noexcept;\n',
    'SmartSession advisory getters')
text = replace_once(
    text,
    '    Q_INVOKABLE bool retryFirmwareUpdate();\n'
    '    Q_INVOKABLE bool retryFirmwareSetup();\n',
    '    Q_INVOKABLE bool retryFirmwareUpdate();\n'
    '    Q_INVOKABLE bool cancelFirmwareUpdate();\n'
    '    Q_INVOKABLE bool retryFirmwareSetup();\n',
    'SmartSession cancel action')
text = replace_once(
    text,
    '    bool firmwareIsCurrent() const;\n'
    '    bool deviceControlAvailable() const noexcept;\n',
    '    bool firmwareIsCurrent() const;\n'
    '    bool firmwareIsCompatible() const;\n'
    '    bool deviceControlAvailable() const noexcept;\n',
    'SmartSession compatibility helper')
text = replace_once(
    text,
    '    bool updateRequested_{false};\n'
    '    bool blankBoardDetected_{false};\n',
    '    bool updateRequested_{false};\n'
    '    bool updateWasOptional_{false};\n'
    '    bool blankBoardDetected_{false};\n',
    'SmartSession optional-operation state')
path.write_text(text, encoding='utf-8')


# --- SmartSessionController.cpp: compatible current firmware remains usable.
path = ROOT / 'apps/arstack_studio/src/SmartSessionController.cpp'
text = path.read_text(encoding='utf-8')
text = replace_once(
    text,
    'bool SmartSessionController::firmwareUpdateRequired() const noexcept { return firmwareUpdateRequired_; }\n'
    'bool SmartSessionController::firmwareReinstallAvailable() const noexcept {\n',
    'bool SmartSessionController::firmwareUpdateRequired() const noexcept { return firmwareUpdateRequired_; }\n'
    'bool SmartSessionController::firmwareUpdateAvailable() const noexcept {\n'
    '    return device_ != nullptr && firmware_ != nullptr && device_->deviceVerified() &&\n'
    '        firmware_->bundleReady() && !firmware_->busy() && !updateRequested_ &&\n'
    '        firmwareIsCompatible() && !firmwareIsCurrent();\n'
    '}\n'
    'bool SmartSessionController::firmwareUpdateCanCancel() const noexcept {\n'
    '    return updateRequested_ && updateWasOptional_ && updateStage_ == UpdateStage::waitingForBootloader &&\n'
    '        firmware_ != nullptr && !firmware_->busy();\n'
    '}\n'
    'bool SmartSessionController::firmwareReinstallAvailable() const noexcept {\n',
    'SmartSession advisory implementations')
text = replace_once(
    text,
    'bool SmartSessionController::beginFirmwareUpdate() {\n'
    '    if (device_ == nullptr || firmware_ == nullptr || !device_->deviceVerified() ||\n'
    '        !firmware_->bundleReady() || firmware_->busy()) {\n'
    '        return false;\n'
    '    }\n'
    '    return beginFirmwareOperation(device_->portName());\n'
    '}\n\n'
    'bool SmartSessionController::beginFirmwareReinstall() {\n'
    '    if (!firmwareReinstallAvailable() || device_ == nullptr) return false;\n'
    '    return beginFirmwareOperation(device_->portName());\n'
    '}\n\n'
    'bool SmartSessionController::beginFirmwareInstall() {\n',
    'bool SmartSessionController::beginFirmwareUpdate() {\n'
    '    if (device_ == nullptr || firmware_ == nullptr || !device_->deviceVerified() ||\n'
    '        !firmware_->bundleReady() || firmware_->busy()) {\n'
    '        return false;\n'
    '    }\n'
    '    updateWasOptional_ = firmwareUpdateAvailable() && !firmwareUpdateRequired_;\n'
    '    return beginFirmwareOperation(device_->portName());\n'
    '}\n\n'
    'bool SmartSessionController::beginFirmwareReinstall() {\n'
    '    if (!firmwareReinstallAvailable() || device_ == nullptr) return false;\n'
    '    updateWasOptional_ = true;\n'
    '    return beginFirmwareOperation(device_->portName());\n'
    '}\n\n'
    'bool SmartSessionController::beginFirmwareInstall() {\n'
    '    updateWasOptional_ = false;\n',
    'SmartSession optional update classification')
text = replace_once(
    text,
    'bool SmartSessionController::retryFirmwareUpdate() {\n'
    '    if (device_ == nullptr || firmware_ == nullptr || updatePort_.isEmpty() || firmware_->busy()) return false;\n'
    '    setupError_ = false;\n'
    '    setupErrorStatus_.clear();\n'
    '    blankBoardDetected_ = false;\n'
    '    updateRequested_ = true;\n'
    '    updateReconnectAttempts_ = 0;\n'
    '    pendingReleaseGeneration_ = 0;\n'
    '    continueFirmwareUpdate();\n'
    '    return updateRequested_;\n'
    '}\n\n'
    'bool SmartSessionController::retryFirmwareSetup() {\n',
    'bool SmartSessionController::retryFirmwareUpdate() {\n'
    '    if (device_ == nullptr || firmware_ == nullptr || updatePort_.isEmpty() || firmware_->busy()) return false;\n'
    '    setupError_ = false;\n'
    '    setupErrorStatus_.clear();\n'
    '    blankBoardDetected_ = false;\n'
    '    updateRequested_ = true;\n'
    '    updateReconnectAttempts_ = 0;\n'
    '    pendingReleaseGeneration_ = 0;\n'
    '    continueFirmwareUpdate();\n'
    '    return updateRequested_;\n'
    '}\n\n'
    'bool SmartSessionController::cancelFirmwareUpdate() {\n'
    '    if (!firmwareUpdateCanCancel() || device_ == nullptr) return false;\n'
    '    reconnectTimer_.stop();\n'
    '    updateRequested_ = false;\n'
    '    updateWasOptional_ = false;\n'
    '    updateStage_ = UpdateStage::idle;\n'
    '    updateReconnectAttempts_ = 0;\n'
    '    pendingReleaseGeneration_ = 0;\n'
    '    setupError_ = false;\n'
    '    setupErrorStatus_.clear();\n'
    '    blankBoardDetected_ = false;\n'
    '    blankBoardPort_.clear();\n'
    '    if (portOwner_ == PortOwner::firmwareTool) setPortOwner(PortOwner::none);\n'
    '    if (started_ && !device_->connected() && !device_->discovering()) {\n'
    '        QTimer::singleShot(0, this, [this] {\n'
    '            if (!started_ || updateRequested_ || device_ == nullptr || device_->connected() || device_->discovering()) return;\n'
    '            static_cast<void>(startDeviceDiscovery());\n'
    '            reconcile();\n'
    '        });\n'
    '    } else {\n'
    '        reconcile();\n'
    '    }\n'
    '    return true;\n'
    '}\n\n'
    'bool SmartSessionController::retryFirmwareSetup() {\n',
    'SmartSession cancel firmware operation')
text = replace_once(
    text,
    'void SmartSessionController::latchFirmwareFailure(QString message) {\n'
    '    reconnectTimer_.stop();\n'
    '    updateRequested_ = false;\n'
    '    updateStage_ = UpdateStage::idle;\n'
    '    updateReconnectAttempts_ = 0;\n'
    '    pendingReleaseGeneration_ = 0;\n'
    '    blankBoardDetected_ = false;\n'
    '    if (portOwner_ == PortOwner::firmwareTool) setPortOwner(PortOwner::none);\n'
    '    setupError_ = true;\n'
    '    message = message.trimmed();\n'
    '    setupErrorStatus_ = message.isEmpty()\n'
    '        ? QStringLiteral("Firmware setup did not complete. Retry explicitly when the board is ready.")\n'
    '        : std::move(message);\n'
    '}\n',
    'void SmartSessionController::latchFirmwareFailure(QString message) {\n'
    '    reconnectTimer_.stop();\n'
    '    const bool optionalOperation = updateWasOptional_;\n'
    '    updateRequested_ = false;\n'
    '    updateWasOptional_ = false;\n'
    '    updateStage_ = UpdateStage::idle;\n'
    '    updateReconnectAttempts_ = 0;\n'
    '    pendingReleaseGeneration_ = 0;\n'
    '    blankBoardDetected_ = false;\n'
    '    if (portOwner_ == PortOwner::firmwareTool) setPortOwner(PortOwner::none);\n'
    '    message = message.trimmed();\n'
    '    setupError_ = !optionalOperation;\n'
    '    setupErrorStatus_ = optionalOperation ? QString{} : (message.isEmpty()\n'
    '        ? QStringLiteral("Firmware setup did not complete. Retry explicitly when the board is ready.")\n'
    '        : std::move(message));\n'
    '    if (optionalOperation && started_ && device_ != nullptr && !device_->connected() && !device_->discovering()) {\n'
    '        QTimer::singleShot(0, this, [this] {\n'
    '            if (!started_ || updateRequested_ || device_ == nullptr || device_->connected() || device_->discovering()) return;\n'
    '            static_cast<void>(startDeviceDiscovery());\n'
    '            reconcile();\n'
    '        });\n'
    '    }\n'
    '}\n',
    'SmartSession optional update failure recovery')
text = replace_once(
    text,
    'bool SmartSessionController::firmwareIsCurrent() const {\n'
    '    return device_ != nullptr && device_->deviceVerified() &&\n'
    '        DeviceController::identitySupportsCurrentContract(\n'
    '            device_->deviceIdentity(), expectedFirmwareVersion(), expectedFirmwareBuildId());\n'
    '}\n\n'
    'bool SmartSessionController::deviceControlAvailable() const noexcept {\n'
    '    return device_ != nullptr && device_->deviceVerified() && firmwareIsCurrent() &&\n',
    'bool SmartSessionController::firmwareIsCurrent() const {\n'
    '    return device_ != nullptr && device_->deviceVerified() &&\n'
    '        DeviceController::identitySupportsCurrentContract(\n'
    '            device_->deviceIdentity(), expectedFirmwareVersion(), expectedFirmwareBuildId());\n'
    '}\n\n'
    'bool SmartSessionController::firmwareIsCompatible() const {\n'
    '    return device_ != nullptr && device_->deviceVerified() &&\n'
    '        DeviceController::identitySupportsCurrentContract(\n'
    '            device_->deviceIdentity(), expectedFirmwareVersion(), QString{});\n'
    '}\n\n'
    'bool SmartSessionController::deviceControlAvailable() const noexcept {\n'
    '    return device_ != nullptr && device_->deviceVerified() && firmwareIsCompatible() &&\n',
    'SmartSession compatible-runtime gate')
old_block = '''    refreshFirmwareIdentity();
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
        const QString observedBuild = deviceFirmwareBuildId_.isEmpty()
            ? QStringLiteral("legacy/no build ID")
            : deviceFirmwareBuildId_;
        const QString packageBuild = expectedFirmwareBuildId().isEmpty()
            ? QStringLiteral("unknown")
            : expectedFirmwareBuildId();
        setPresentation(
            QStringLiteral("FIRMWARE UPDATE"),
            QStringLiteral("%1 build %2 detected. Studio package contains v%3 build %4; update is required before injection.")
                .arg(versionText, observedBuild, expectedFirmwareVersion(), packageBuild),
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
'''
new_block = '''    refreshFirmwareIdentity();

    // A completed write must verify the exact bundled build. This remains
    // fail-closed even though a pre-existing compatible build may be used.
    if (updateRequested_ && updateStage_ == UpdateStage::reconnecting) {
        if (!firmwareIsCurrent()) {
            const QString observed = deviceFirmwareVersion_.isEmpty()
                ? QStringLiteral("legacy/unknown firmware")
                : QStringLiteral("firmware v%1").arg(deviceFirmwareVersion_);
            latchFirmwareFailure(QStringLiteral(
                "Firmware was written, but reconnect verification reported %1 instead of the exact bundled build for v%2.")
                .arg(observed, expectedFirmwareVersion()));
            emit firmwareUpdateFinished(false);
            reconcile();
            return;
        }
        updateRequested_ = false;
        updateWasOptional_ = false;
        updateStage_ = UpdateStage::idle;
        clearBlankBoardContext();
        emit firmwareUpdateFinished(true);
    }

    // Runtime compatibility and package freshness are intentionally separate.
    // Same-version firmware with the required protocol/capabilities remains
    // usable; build provenance mismatch is an advisory update, not a lockout.
    if (!firmwareIsCompatible()) {
        const QString versionText = deviceFirmwareVersion_.isEmpty()
            ? QStringLiteral("legacy firmware")
            : QStringLiteral("firmware v%1").arg(deviceFirmwareVersion_);
        setPresentation(
            QStringLiteral("FIRMWARE UPDATE"),
            QStringLiteral("%1 does not satisfy the current protocol/capability contract. Update is required before injection.")
                .arg(versionText),
            false,
            true);
        return;
    }
'''
text = replace_once(text, old_block, new_block, 'SmartSession reconcile firmware gate')
text = replace_once(
    text,
    '    if (device_->running()) {\n'
    '        setPresentation(\n'
    '            QStringLiteral("RUNNING"),\n'
    '            QStringLiteral("4I + 4V · 4000 samples/s · live value apply"),\n'
    '            false,\n'
    '            false);\n',
    '    if (device_->running()) {\n'
    '        setPresentation(\n'
    '            QStringLiteral("RUNNING"),\n'
    '            firmwareUpdateAvailable()\n'
    '                ? QStringLiteral("4I + 4V · 4000 samples/s · live value apply · firmware update available")\n'
    '                : QStringLiteral("4I + 4V · 4000 samples/s · live value apply"),\n'
    '            false,\n'
    '            false);\n',
    'SmartSession running advisory status')
text = replace_once(
    text,
    '    setPresentation(\n'
    '        QStringLiteral("READY"),\n'
    '        QStringLiteral("4I + 4V · 4000 samples/s · ready to start"),\n'
    '        true,\n'
    '        false);\n',
    '    setPresentation(\n'
    '        QStringLiteral("READY"),\n'
    '        firmwareUpdateAvailable()\n'
    '            ? QStringLiteral("4I + 4V · 4000 samples/s · ready · firmware update available")\n'
    '            : QStringLiteral("4I + 4V · 4000 samples/s · ready to start"),\n'
    '        true,\n'
    '        false);\n',
    'SmartSession ready advisory status')
path.write_text(text, encoding='utf-8')


# --- WorkflowBar.qml: suggestions do not gate Start; Download-mode wait is cancellable.
path = ROOT / 'apps/arstack_studio/qml/WorkflowBar.qml'
text = path.read_text(encoding='utf-8')
text = replace_once(
    text,
    '        if (smartSession.state === "READY") return "Ready to inject"\n',
    '        if (smartSession.state === "READY") return smartSession.firmwareUpdateAvailable ? "Ready · update available" : "Ready to inject"\n',
    'Workflow display advisory')
text = replace_once(
    text,
    '        title: "Firmware update required"\n',
    '        title: smartSession.firmwareUpdateRequired ? "Firmware update required" : "Firmware update available"\n',
    'Workflow update dialog title')
text = replace_once(
    text,
    '                text: "Update firmware to start injection"\n',
    '                text: smartSession.firmwareUpdateRequired ? "Update firmware to start injection" : "A newer firmware build is available"\n',
    'Workflow update dialog heading')
text = replace_once(
    text,
    '                text: "This ESP32-P4 is connected, but its ARStack firmware is older than this Studio build. Update now? Studio will flash, restart, reconnect, and verify it automatically."\n',
    '                text: smartSession.firmwareUpdateRequired\n'
    '                    ? "This ESP32-P4 does not satisfy the current runtime contract. Update is required before injection."\n'
    '                    : "The connected firmware is compatible and can be used now. A newer bundled build is available; update only when convenient. Studio will flash, restart, reconnect, and verify it automatically."\n',
    'Workflow update dialog message')
text = replace_once(
    text,
    '            RowLayout {\n'
    '                visible: smartSession.updateNeedsBootloaderHelp\n'
    '                Layout.fillWidth: true\n'
    '                Item { Layout.fillWidth: true }\n'
    '                CalmButton { theme: ribbon.theme; uiFont: ribbon.uiFont; text: "Retry"; tone: "accent"; implicitWidth: 110; onClicked: smartSession.retryFirmwareUpdate() }\n'
    '            }\n',
    '            RowLayout {\n'
    '                visible: smartSession.updateNeedsBootloaderHelp\n'
    '                Layout.fillWidth: true\n'
    '                CalmButton {\n'
    '                    visible: smartSession.firmwareUpdateCanCancel\n'
    '                    theme: ribbon.theme; uiFont: ribbon.uiFont; text: "Use current firmware"; implicitWidth: 150\n'
    '                    onClicked: {\n'
    '                        if (smartSession.cancelFirmwareUpdate()) {\n'
    '                            progressDialog.close()\n'
    '                            controller.showMessage("Firmware update skipped. Reconnecting to the compatible firmware already on the board.", false)\n'
    '                        }\n'
    '                    }\n'
    '                }\n'
    '                Item { Layout.fillWidth: true }\n'
    '                CalmButton { theme: ribbon.theme; uiFont: ribbon.uiFont; text: "Retry"; tone: "accent"; implicitWidth: 110; onClicked: smartSession.retryFirmwareUpdate() }\n'
    '            }\n',
    'Workflow bootloader cancel action')
path.write_text(text, encoding='utf-8')


# --- ModernRibbon.qml: surface advisory update as a non-blocking attention action.
path = ROOT / 'apps/arstack_studio/qml/ModernRibbon.qml'
text = path.read_text(encoding='utf-8')
text = replace_once(
    text,
    '                    visible: bar.session.firmwareUpdateRequired || bar.session.firmwareInstallRequired || bar.session.profileSyncRetryAvailable\n',
    '                    visible: bar.session.firmwareUpdateRequired || bar.session.firmwareUpdateAvailable || bar.session.firmwareInstallRequired || bar.session.profileSyncRetryAvailable\n',
    'Ribbon advisory group visibility')
text = replace_once(
    text,
    '                        visible: bar.session.firmwareUpdateRequired\n'
    '                        theme: bar.theme; uiFont: bar.uiFont; text: "Update firmware"; tone: "accent"\n',
    '                        visible: bar.session.firmwareUpdateRequired || bar.session.firmwareUpdateAvailable\n'
    '                        theme: bar.theme; uiFont: bar.uiFont\n'
    '                        text: bar.session.firmwareUpdateRequired ? "Update firmware" : "Update available"\n'
    '                        tone: bar.session.firmwareUpdateRequired ? "accent" : "neutral"\n',
    'Ribbon advisory update action')
path.write_text(text, encoding='utf-8')


# --- FirmwarePanel.qml: allow optional update from Advanced without calling it required.
path = ROOT / 'apps/arstack_studio/qml/FirmwarePanel.qml'
text = path.read_text(encoding='utf-8')
text = replace_once(
    text,
    '                    text: panel.session && panel.session.firmwareUpdateRequired\n'
    '                        ? "Update firmware"\n'
    '                        : "Reinstall current"\n'
    '                    visible: panel.session &&\n'
    '                             (panel.session.firmwareUpdateRequired || panel.session.firmwareReinstallAvailable)\n'
    '                    enabled: visible && !panel.firmware.busy\n'
    '                    tone: panel.session && panel.session.firmwareUpdateRequired ? "accent" : "normal"\n'
    '                    onClicked: {\n'
    '                        if (panel.session.firmwareUpdateRequired)\n'
    '                            panel.session.beginFirmwareUpdate()\n'
    '                        else\n'
    '                            panel.session.beginFirmwareReinstall()\n'
    '                    }\n',
    '                    text: panel.session && (panel.session.firmwareUpdateRequired || panel.session.firmwareUpdateAvailable)\n'
    '                        ? (panel.session.firmwareUpdateRequired ? "Update firmware" : "Update available")\n'
    '                        : "Reinstall current"\n'
    '                    visible: panel.session &&\n'
    '                             (panel.session.firmwareUpdateRequired || panel.session.firmwareUpdateAvailable || panel.session.firmwareReinstallAvailable)\n'
    '                    enabled: visible && !panel.firmware.busy\n'
    '                    tone: panel.session && panel.session.firmwareUpdateRequired ? "accent" : "normal"\n'
    '                    onClicked: {\n'
    '                        if (panel.session.firmwareUpdateRequired || panel.session.firmwareUpdateAvailable)\n'
    '                            panel.session.beginFirmwareUpdate()\n'
    '                        else\n'
    '                            panel.session.beginFirmwareReinstall()\n'
    '                    }\n',
    'FirmwarePanel advisory action')
text = replace_once(
    text,
    '                            : "When Studio reports Firmware required or Firmware update, use that guided action from the main workflow. It will stop output, obtain an acknowledged serial release, verify the ESP32-P4, then write."\n',
    '                            : (panel.session && panel.session.firmwareUpdateAvailable\n'
    '                                ? "The board firmware is compatible and remains usable. A newer build is available as an optional guided update."\n'
    '                                : "When Studio reports Firmware required, use the guided action. It will stop output, obtain an acknowledged serial release, verify the ESP32-P4, then write.")\n',
    'FirmwarePanel guidance text')
path.write_text(text, encoding='utf-8')


# --- DeterministicSessionHarness.cpp: lock the exact regression.
path = ROOT / 'apps/arstack_studio/src/DeterministicSessionHarness.cpp'
text = path.read_text(encoding='utf-8')
text = replace_once(
    text,
    '            {"same version missing build ID -> firmware update", sameVersionMissingBuildOffersUpdate()},\n'
    '            {"same version stale build -> firmware update", sameVersionStaleBuildOffersUpdate()},\n',
    '            {"same version missing build ID -> READY with update suggestion", sameVersionMissingBuildOffersUpdate()},\n'
    '            {"same version stale build -> READY with update suggestion", sameVersionStaleBuildOffersUpdate()},\n'
    '            {"optional bootloader wait -> cancel restores supervisor", optionalBootloaderWaitCanCancel()},\n',
    'Harness advisory cases')
text = replace_once(
    text,
    '        session.updateRequested_ = false;\n'
    '        session.updateStage_ = SmartSessionController::UpdateStage::idle;\n',
    '        session.updateRequested_ = false;\n'
    '        session.updateWasOptional_ = false;\n'
    '        session.updateStage_ = SmartSessionController::UpdateStage::idle;\n',
    'Harness seed optional reset')
old_missing = '''    static bool sameVersionMissingBuildOffersUpdate() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        auto stale = identity();
        stale.buildId.clear();
        seedVerified(fixture, stale, QStringLiteral("COM7"), false);
        return fixture.session.state() == QStringLiteral("FIRMWARE UPDATE") &&
            fixture.session.firmwareUpdateRequired() &&
            !fixture.session.firmwareReinstallAvailable() &&
            !fixture.session.startReady();
    }

    static bool sameVersionStaleBuildOffersUpdate() {
        Fixture fixture;
        if (!fixture.profileReady) return false;
        auto stale = identity();
        stale.buildId = QStringLiteral("fedcba9876543210");
        seedVerified(fixture, stale, QStringLiteral("COM7"), false);
        return fixture.session.state() == QStringLiteral("FIRMWARE UPDATE") &&
            fixture.session.firmwareUpdateRequired() &&
            !fixture.session.firmwareReinstallAvailable() &&
            !fixture.session.startReady();
    }
'''
new_missing = '''    static bool sameVersionMissingBuildOffersUpdate() {
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
'''
text = replace_once(text, old_missing, new_missing, 'Harness advisory behavior')
path.write_text(text, encoding='utf-8')

print('P0 firmware advisory/cancel fix applied successfully')
