// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import ARStack.Studio 1.0

SurfacePanel {
    id: ribbon

    property var controller
    property var device
    property var profiles
    property string uiFont: "Inter"
    property string monoFont: "Inter"
    property bool compact: false
    // Firmware transaction is the single source of truth. UI timers and
    // unrelated PTP/serial errors must not bounce the injection START button.
    // Operator intent may arrive before first profile synchronization ends.
    // One bounded, boot-bound pending click is consumed as soon as the SAME
    // verified board enters READY. Never persist/replay across reconnect.
    property bool startIntentPending: false
    property string startIntentBoot: ""
    property double startIntentDeadline: 0
    readonly property bool startQueued: Boolean(
        startIntentPending || (ribbon.device && ribbon.device.scenarioApplying))
    Timer {
        id: startIntentExpiry
        interval: 6500
        repeat: false
        onTriggered: {
            if (!ribbon.startIntentPending) return
            ribbon.cancelStartIntent()
            controller.showMessage(
                "START not sent: device preparation timed out. Check board/profile state and retry.", true)
        }
    }
    function cancelStartIntent() {
        startIntentPending = false
        startIntentBoot = ""
        startIntentDeadline = 0
        startIntentExpiry.stop()
    }
    function queueFirstStart() {
        // Only a verified, physically connected board may receive deferred
        // START. Never arm for offline, firmware update or reconnect.
        if (!device.deviceVerified || !device.connected ||
            smartSession.updatingFirmware || smartSession.firmwareUpdateRequired ||
            smartSession.firmwareInstallRequired) return false
        const boot = String(device.deviceIdentity ? device.deviceIdentity.bootId : device.bootId)
        if (!boot || boot === "undefined") return false
        startIntentBoot = boot
        startIntentPending = true
        startIntentDeadline = Date.now() + 6500
        startIntentExpiry.restart()
        controller.showMessage("START queued — waiting for 4I+4V profile readiness on this board.", false)
        return true
    }
    function tryConsumeFirstStart() {
        if (!startIntentPending || !smartSession.startReady) return
        const boot = String(device.deviceIdentity ? device.deviceIdentity.bootId : device.bootId)
        if (!device.deviceVerified || !device.connected || boot !== startIntentBoot ||
            Date.now() > startIntentDeadline) {
            cancelStartIntent()
            return
        }
        cancelStartIntent()
        Qt.callLater(function() {
            // Verify the device again AFTER scheduling, never replay a stale
            // intent across a new session, and snapshot the actual GUI now.
            if (device.deviceVerified && device.connected && smartSession.startReady &&
                !device.running && !device.scenarioApplying)
                ribbon.requestStart()
        })
    }
    Connections {
        target: smartSession
        function onStateChanged() { ribbon.tryConsumeFirstStart() }
    }
    Connections {
        target: ribbon.device
        function onRunningChanged() {
            if (ribbon.device.running) controller.pendingApply = false
        }
        function onDeviceVerifiedChanged() {
            if (!ribbon.device.deviceVerified) {
                controller.pendingApply = true
                ribbon.cancelStartIntent()
            }
        }
    }
    property bool updatePromptDeferred: false
    property bool installPromptDeferred: false
    property bool firmwareResultSuccess: false
    property string firmwareResultTitle: ""
    property string firmwareResultMessage: ""

    readonly property alias session: smartSession
    readonly property bool firmwareGateActive:
        smartSession.firmwareInstallRequired || smartSession.firmwareUpdateRequired ||
        smartSession.updatingFirmware || smartSession.updateNeedsBootloaderHelp
    readonly property bool injectionRunning: smartSession.state === "RUNNING"
    readonly property int firmwareStageIndex: {
        switch (smartSession.firmwareUpdateStage) {
        case "prepare":
        case "verify": return 0
        case "write": return 1
        case "reconnect": return 2
        default: return -1
        }
    }
    readonly property var firmwareStages: [
        { title: "Prepare device" },
        { title: "Install firmware" },
        { title: "Verify restart" }
    ]

    function firmwareStageState(index) {
        if (firmwareStageIndex < 0) return "pending"
        if (index < firmwareStageIndex) return "done"
        if (index === firmwareStageIndex) return "active"
        return "pending"
    }

    Settings {
        id: operatorSettings
        category: "operator-ui"
        property string lastEngineeringUrl: ""
        property int lastSelectedIndex: 0
        property bool phasorVisible: false
        property bool waveformVisible: false
        property bool telemetryVisible: true
        property bool phasorDetached: false
        property bool waveformDetached: false
        property bool telemetryExpanded: false
    }

    SmartSessionController {
        id: smartSession
        device: ribbon.device
        profiles: ribbon.profiles
        firmware: FirmwareService
    }

    Component.onCompleted: {
        controller.phasorDockVisible = operatorSettings.phasorVisible
        controller.waveformDockVisible = operatorSettings.waveformVisible
        controller.telemetryDockVisible = operatorSettings.telemetryVisible
        controller.phasorDetached = operatorSettings.phasorDetached
        controller.waveformDetached = operatorSettings.waveformDetached
        controller.telemetryExpanded = operatorSettings.telemetryExpanded

        if (operatorSettings.lastEngineeringUrl.length > 0) {
            if (profiles.loadFile(operatorSettings.lastEngineeringUrl)) {
                profiles.selectStream(operatorSettings.lastSelectedIndex)
                controller.profileDirty = false
                controller.showMessage("Last engineering configuration restored.", false)
            } else {
                operatorSettings.lastEngineeringUrl = ""
                operatorSettings.lastSelectedIndex = 0
                controller.showMessage("The previous engineering file is no longer available; using the built-in 4I+4V profile.", false)
            }
        }
        smartSession.start()
    }

    Connections {
        target: controller
        function onPhasorDockVisibleChanged() { operatorSettings.phasorVisible = controller.phasorDockVisible }
        function onWaveformDockVisibleChanged() { operatorSettings.waveformVisible = controller.waveformDockVisible }
        function onTelemetryDockVisibleChanged() { operatorSettings.telemetryVisible = controller.telemetryDockVisible }
        function onPhasorDetachedChanged() { operatorSettings.phasorDetached = controller.phasorDetached }
        function onWaveformDetachedChanged() { operatorSettings.waveformDetached = controller.waveformDetached }
        function onTelemetryExpandedChanged() { operatorSettings.telemetryExpanded = controller.telemetryExpanded }
    }

    Connections {
        target: profiles
        function onSelectedIndexChanged() {
            if (profiles.selectedIndex >= 0)
                operatorSettings.lastSelectedIndex = profiles.selectedIndex
        }
    }

    Connections {
        target: smartSession
        function onReadyForLiveApply() {
            // The current board SHOW response does not contain per-channel
            // amplitude/phase/quality. Never silently claim readback or
            // overwrite a board with a restored/offline local draft.
            controller.profileDirty = false
            controller.pendingApply = true
            // Fast path: the next START uploads the local setpoints first.
            // No auto-transmit on device discovery/reconnection.
        }
        function onStateChanged() {
            if (ribbon.firmwareGateActive)
                controller.profileDirty = true

            if (smartSession.updatingFirmware || smartSession.updateNeedsBootloaderHelp) {
                if (installDialog.opened) installDialog.close()
                if (updateDialog.opened) updateDialog.close()
                if (!progressDialog.opened) Qt.callLater(progressDialog.open)
                return
            }
            // Firmware readiness is shown in the ribbon. Do not interrupt
            // connection/discovery with unsolicited setup dialogs. If the
            // operator presses Start with an incompatible/unprovisioned board,
            // requestStart() offers the necessary action exactly then.
            // Explicit firmware installation still owns its progress UI.
        }
        function onFirmwareUpdateFinished(success) {
            if (progressDialog.opened) progressDialog.close()
            ribbon.firmwareResultSuccess = success
            if (success) {
                ribbon.updatePromptDeferred = false
                ribbon.installPromptDeferred = false
                ribbon.firmwareResultTitle = "Firmware installed successfully"
                ribbon.firmwareResultMessage = "ESP32-P4 reconnected and verified."
                controller.showMessage("Firmware installed and verified successfully.", false)
            } else {
                ribbon.firmwareResultTitle = "Firmware not verified"
                ribbon.firmwareResultMessage = smartSession.firmwareUpdateResult.length > 0
                    ? smartSession.firmwareUpdateResult
                    : "Studio could not verify the firmware installation. Open Advanced > Firmware > Copy diagnostics."
                controller.showMessage("Firmware installation was not verified.", true)
            }
            // Installation completion is a normal successful transition,
            // not another mandatory acknowledgment dialog. Retain full
            // diagnostics and a modal only when verification failed.
            if (!success) Qt.callLater(resultDialog.open)
        }
    }

    Connections {
        target: device
        function onDeviceVerifiedChanged() {
            if (!device.deviceVerified) ribbon.updatePromptDeferred = false
        }
        function onPortsChanged() {
            if (device.ports.length === 0) {
                ribbon.installPromptDeferred = false
                ribbon.updatePromptDeferred = false
            }
        }
        function onProfileStateChanged() {
            if (!ribbon.firmwareGateActive) return
            controller.profileDirty = true
            Qt.callLater(function() {
                if (ribbon.firmwareGateActive) controller.profileDirty = true
            })
        }
    }

    // Never display green READY immediately after an unsolicited board STOP.
    // The board-reported reason is distinct from the physical PTP role.
    readonly property bool unexpectedSvStop: Boolean(
        ribbon.device && ribbon.device.deviceVerified &&
        !ribbon.device.running && ribbon.device.svStopReason.length > 0 &&
        !ribbon.startQueued)
    readonly property string displayState: {
        if (ribbon.unexpectedSvStop) {
            if (ribbon.device.svStopReason === "LEASE_EXPIRED") return "SV stopped · USB control lease expired"
            if (ribbon.device.svStopReason === "RUNTIME_FAULT") return "SV stopped · board runtime fault"
            return "SV stopped · checking board cause"
        }
        if (smartSession.state === "WAITING FOR DEVICE") return "Connect device"
        if (smartSession.state === "DEVICE FOUND" || smartSession.state === "CONNECTING") return "Connecting"
        if (smartSession.state === "CHECKING DEVICE") return "Checking device"
        if (smartSession.state === "FIRMWARE RECOVERY") return "Firmware status unknown"
        if (smartSession.state === "FIRMWARE UPDATE") return "Firmware update available"
        if (smartSession.state === "UPDATING FIRMWARE") return "Installing firmware"
        if (smartSession.state === "UPDATE NEEDS BOOT") return "Download mode required"
        if (smartSession.state === "PREPARING 4I+4V") return "Preparing"
        if (smartSession.state === "RECOVERING PROFILE") return "Retrying profile"
        if (smartSession.state === "READY") return smartSession.firmwareUpdateAvailable ? "Ready · update available" : "Ready to inject"
        if (smartSession.state === "RUNNING") return "Injection running"
        if (smartSession.state === "PROFILE BLOCKED") return "Profile unavailable"
        if (smartSession.state === "PROFILE SYNC ERROR") return "Profile sync failed"
        if (smartSession.state === "SETUP ERROR") return "Setup issue"
        return smartSession.state
    }

    readonly property color smartStateColor: {
        if (ribbon.unexpectedSvStop) return theme.red
        if (smartSession.state === "RUNNING" || smartSession.state === "READY") return theme.green
        if (smartSession.state === "CONNECTING" || smartSession.state === "CHECKING DEVICE" ||
            smartSession.state === "FIRMWARE RECOVERY" || smartSession.state === "PREPARING 4I+4V" ||
            smartSession.state === "RECOVERING PROFILE" ||
            smartSession.state === "FIRMWARE UPDATE" || smartSession.state === "UPDATING FIRMWARE" ||
            smartSession.state === "UPDATE NEEDS BOOT") return theme.amber
        if (smartSession.state === "DEVICE FOUND") return theme.accent
        if (smartSession.state === "PROFILE BLOCKED" || smartSession.state === "PROFILE SYNC ERROR" ||
            smartSession.state === "SETUP ERROR") return theme.red
        return theme.muted
    }

    function startReason() {
        if (ribbon.injectionRunning) return "SMV output is already running."
        if (smartSession.firmwareInstallRequired) return "ARStack identity not verified. Retry connection or use optional firmware recovery."
        if (smartSession.firmwareUpdateRequired) return "Update firmware before starting injection."
        if (smartSession.updatingFirmware) return "Firmware installation is in progress."
        if (smartSession.updateNeedsBootloaderHelp) return "Complete Download mode recovery before starting injection."
        if (smartSession.state === "CONNECTING" || smartSession.state === "CHECKING DEVICE") return "Studio is identifying the device automatically."
        if (smartSession.state === "PREPARING 4I+4V") return "Studio is preparing the default 4I+4V injection automatically."
        if (smartSession.state === "RECOVERING PROFILE") return smartSession.statusText
        if (smartSession.state === "PROFILE SYNC ERROR") return smartSession.statusText
        if (smartSession.state === "WAITING FOR DEVICE") return "Connect ESP32-P4; Studio will detect it automatically."
        return smartSession.statusText
    }

    function requestStart() {
        if (ribbon.injectionRunning || ribbon.startQueued) return
        if (smartSession.firmwareInstallRequired) {
            ribbon.installPromptDeferred = false
            installDialog.open()
            return
        }
        if (smartSession.firmwareUpdateRequired) {
            ribbon.updatePromptDeferred = false
            updateDialog.open()
            return
        }
        if (smartSession.updatingFirmware || smartSession.updateNeedsBootloaderHelp) {
            controller.showMessage(ribbon.startReason(), false)
            return
        }
        if (smartSession.profileSyncRetryAvailable) {
            if (smartSession.retryProfileSync())
                controller.showMessage("Retrying 4I+4V profile synchronization.", false)
            else
                controller.showMessage(smartSession.statusText, true)
            return
        }
        if (!smartSession.startReady) {
            // A first click during same-device profile preparation is retained
            // briefly and dispatched once READY. Other errors are explained.
            if (smartSession.state === "PREPARING 4I+4V" ||
                smartSession.state === "RECOVERING PROFILE" ||
                smartSession.state === "DEVICE FOUND") {
                if (ribbon.queueFirstStart()) return
            }
            controller.showMessage("START waiting: " + ribbon.startReason(), true)
            return
        }
        // A START always transmits the current 4I+4V table snapshot.
        // A previous run may have cleared pendingApply; a bare START could
        // then repeat old board angles while Studio displays edited values.
        // Validate/flush the visible 4I+4V table before entering the
        // exclusive device START transaction; no stale debounced angle.
        const scenario = controller.startScenarioSnapshot()
        if (!scenario) return
        const accepted = smartSession.requestStartWithScenario(scenario)
        if (!accepted) {
            controller.pendingApply = true
            controller.showMessage("Start rejected. Check device status.", true)
        }
    }

    function requestStop() {
        if (ribbon.startIntentPending) {
            ribbon.cancelStartIntent()
            controller.showMessage("Pending START cancelled.", false)
            return
        }
        // A pending CFG transaction can progress to START: Stop must remain
        // actionable before RUNNING telemetry, not only after it.
        if (!ribbon.injectionRunning && !ribbon.startQueued) return
        if (!smartSession.requestStop()) {
            controller.showMessage("Studio could not cancel the current device session.", true)
            return
        }
    }

    function openEngineeringDialog() {
        if (smartSession.engineeringEditable) engineeringFileDialog.open()
        else controller.showMessage("Stop injection or finish the current device operation before changing the engineering configuration.", true)
    }

    function loadEngineeringFile(url) {
        if (!smartSession.engineeringEditable) {
            controller.showMessage("Stop injection or finish the current device operation before changing the engineering configuration.", true)
            return false
        }
        if (!profiles.loadFile(url)) {
            controller.showMessage(profiles.fatalError || "Unable to load engineering file.", true)
            return false
        }
        operatorSettings.lastEngineeringUrl = url.toString()
        operatorSettings.lastSelectedIndex = profiles.selectedIndex >= 0 ? profiles.selectedIndex : 0
        controller.profileDirty = false
        controller.showMessage(profiles.documentStatus, false)
        return true
    }

    function useBuiltInProfile() {
        if (!smartSession.engineeringEditable) {
            controller.showMessage("Stop injection or finish the current device operation before changing the engineering configuration.", true)
            return
        }
        if (profiles.loadReferenceTemplate()) {
            operatorSettings.lastEngineeringUrl = ""
            operatorSettings.lastSelectedIndex = 0
            controller.profileDirty = false
            controller.showMessage("Built-in 4I+4V / 4000 fps reference profile selected.", false)
        } else {
            controller.showMessage(profiles.fatalError || "Unable to load the built-in profile.", true)
        }
    }

    function resetDockLayout() {
        controller.phasorDetached = false
        controller.waveformDetached = false
        controller.phasorDockVisible = true
        controller.waveformDockVisible = false
        controller.telemetryDockVisible = true
        controller.telemetryExpanded = false
        controller.showMessage("Dock layout reset.", false)
    }

    function openUpdatePrompt() {
        updatePromptDeferred = false
        updateDialog.open()
    }

    function openInstallPrompt() {
        installPromptDeferred = false
        installDialog.open()
    }

    function retryProfileSync() {
        if (smartSession.retryProfileSync())
            controller.showMessage("Retrying 4I+4V profile synchronization.", false)
        else
            controller.showMessage(smartSession.statusText, true)
    }

    FileDialog {
        id: engineeringFileDialog
        title: "Open IEC 61850 engineering configuration"
        nameFilters: [
            "IEC 61850 SCL (*.scd *.cid *.icd *.iid *.ssd *.xml)",
            "All files (*)"
        ]
        onAccepted: ribbon.loadEngineeringFile(selectedFile)
    }

    Dialog {
        id: installDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 408
        padding: 16
        title: "Set up ESP32-P4"
        closePolicy: Popup.NoAutoClose
        background: Rectangle { color: ribbon.theme.surface; radius: 10; border.width: 1; border.color: ribbon.theme.line }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: "No ARStack identity was verified on the manually selected port. This does NOT prove firmware is missing. Retry connection first; optional installation requires a ROM chip check."
                color: ribbon.theme.textSoft
                font.family: ribbon.uiFont
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: "Keep USB connected until verification finishes."
                color: ribbon.theme.muted
                font.family: ribbon.uiFont
                font.pixelSize: 10
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                CalmButton {
                    theme: ribbon.theme; uiFont: ribbon.uiFont; text: "Later"
                    onClicked: { ribbon.installPromptDeferred = true; installDialog.close() }
                }
                CalmButton {
                    theme: ribbon.theme; uiFont: ribbon.uiFont; text: "Install firmware"; tone: "accent"; implicitWidth: 142
                    onClicked: {
                        if (smartSession.beginFirmwareInstall()) {
                            ribbon.installPromptDeferred = false
                            installDialog.close()
                        } else controller.showMessage("Firmware installation could not start. Open Tools > Advanced for recovery details.", true)
                    }
                }
            }
        }
    }

    Dialog {
        id: updateDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 408
        padding: 16
        title: smartSession.firmwareUpdateRequired ? "Firmware update required" : "Firmware update available"
        closePolicy: Popup.NoAutoClose
        background: Rectangle { color: ribbon.theme.surface; radius: 10; border.width: 1; border.color: ribbon.theme.line }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: smartSession.firmwareUpdateRequired
                    ? "Update is required before SV injection."
                    : "Current firmware works. Updating is optional."
                color: ribbon.theme.textSoft
                font.family: ribbon.uiFont
                font.pixelSize: 11
            }
            Label {
                Layout.fillWidth: true
                text: "Keep USB connected during installation."
                color: ribbon.theme.muted
                font.family: ribbon.uiFont
                font.pixelSize: 10
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                CalmButton {
                    theme: ribbon.theme; uiFont: ribbon.uiFont; text: "Later"
                    onClicked: { ribbon.updatePromptDeferred = true; updateDialog.close() }
                }
                CalmButton {
                    theme: ribbon.theme; uiFont: ribbon.uiFont; text: "Update firmware"; tone: "accent"; implicitWidth: 138
                    onClicked: {
                        if (smartSession.beginFirmwareUpdate()) {
                            ribbon.updatePromptDeferred = false
                            updateDialog.close()
                        } else controller.showMessage("Firmware update could not start. Open Tools > Advanced for recovery help.", true)
                    }
                }
            }
        }
    }

    Dialog {
        id: progressDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 448
        closePolicy: Popup.NoAutoClose
        padding: 16
        header: Rectangle {
            implicitHeight: 42
            color: ribbon.theme.surface2
            border.width: 1
            border.color: smartSession.updateNeedsBootloaderHelp ? ribbon.theme.amber : ribbon.theme.line
            Label {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                text: smartSession.updateNeedsBootloaderHelp ? "Download mode required" : "Installing firmware"
                color: smartSession.updateNeedsBootloaderHelp ? ribbon.theme.amber : ribbon.theme.text
                font.family: ribbon.uiFont
                font.pixelSize: 13
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }
        }
        background: Rectangle {
            color: ribbon.theme.surface
            radius: 10
            border.width: 1
            border.color: smartSession.updateNeedsBootloaderHelp ? ribbon.theme.amber : ribbon.theme.line
        }
        contentItem: ColumnLayout {
            spacing: 10

            Label {
                Layout.fillWidth: true
                text: smartSession.updateNeedsBootloaderHelp
                    ? "Enter ESP32-P4 Download mode, then choose Retry."
                    : "Keep USB connected until verification completes."
                color: smartSession.updateNeedsBootloaderHelp ? ribbon.theme.amber : ribbon.theme.textSoft
                font.family: ribbon.uiFont
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: stageColumn.implicitHeight + 12
                radius: 8
                color: ribbon.theme.panelAlt
                border.width: 1
                border.color: ribbon.theme.lineSoft

                ColumnLayout {
                    id: stageColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 6
                    spacing: 0

                    Repeater {
                        model: ribbon.firmwareStages
                        delegate: RowLayout {
                            required property int index
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: 32
                            spacing: 9
                            readonly property string stageState: ribbon.firmwareStageState(index)

                            Rectangle {
                                width: 18
                                height: 18
                                radius: 9
                                color: parent.stageState === "done" ? ribbon.theme.greenSoft
                                     : parent.stageState === "active" ? ribbon.theme.accentSoft
                                     : "transparent"
                                border.width: 1
                                border.color: parent.stageState === "done" ? ribbon.theme.green
                                            : parent.stageState === "active" ? ribbon.theme.accent
                                            : ribbon.theme.line
                                Label {
                                    anchors.centerIn: parent
                                    text: parent.parent.stageState === "done" ? "✓" : parent.parent.stageState === "active" ? "•" : ""
                                    color: parent.parent.stageState === "done" ? ribbon.theme.green : ribbon.theme.accent
                                    font.family: ribbon.uiFont
                                    font.pixelSize: parent.parent.stageState === "active" ? 16 : 11
                                    font.weight: Font.Bold
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                text: modelData.title
                                color: stageState === "pending" ? ribbon.theme.muted : ribbon.theme.text
                                font.family: ribbon.uiFont
                                font.pixelSize: 11
                                font.weight: stageState === "active" ? Font.DemiBold : Font.Medium
                            }
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                text: smartSession.updateStatus.length > 0 ? smartSession.updateStatus : "Preparing…"
                color: ribbon.theme.textSoft
                font.family: ribbon.uiFont
                font.pixelSize: 10
                wrapMode: smartSession.updateNeedsBootloaderHelp ? Text.WordWrap : Text.NoWrap
                maximumLineCount: smartSession.updateNeedsBootloaderHelp ? 3 : 1
                elide: Text.ElideRight
            }

            ColumnLayout {
                visible: !smartSession.updateNeedsBootloaderHelp && ribbon.firmwareStageIndex === 1
                Layout.fillWidth: true
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "Writing firmware"
                        color: ribbon.theme.textSoft
                        font.family: ribbon.uiFont
                        font.pixelSize: 9
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        text: smartSession.firmwareProgress >= 0 ? smartSession.firmwareProgress + "%" : "Starting write…"
                        color: smartSession.firmwareProgress >= 0 ? ribbon.theme.accent : ribbon.theme.muted
                        font.family: ribbon.monoFont
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }
                }

                Rectangle {
                    id: flashTrack
                    Layout.fillWidth: true
                    height: 8
                    radius: 4
                    color: ribbon.theme.lineSoft
                    clip: true
                    Rectangle {
                        visible: smartSession.firmwareProgress >= 0
                        height: parent.height
                        radius: parent.radius
                        color: ribbon.theme.accent
                        width: parent.width * Math.max(0, Math.min(1, smartSession.firmwareProgress / 100.0))
                        Behavior on width { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                    }
                    Rectangle {
                        id: indeterminateFlash
                        visible: smartSession.firmwareProgress < 0
                        height: parent.height
                        width: Math.max(28, parent.width * 0.24)
                        radius: parent.radius
                        color: ribbon.theme.accent
                        opacity: 0.72
                        NumberAnimation on x {
                            running: indeterminateFlash.visible
                            loops: Animation.Infinite
                            from: -indeterminateFlash.width
                            to: flashTrack.width
                            duration: 900
                            easing.type: Easing.InOutQuad
                        }
                    }
                }
            }

            RowLayout {
                visible: smartSession.updateNeedsBootloaderHelp
                Layout.fillWidth: true
                CalmButton {
                    visible: smartSession.firmwareUpdateCanCancel
                    theme: ribbon.theme; uiFont: ribbon.uiFont; text: "Use current firmware"; implicitWidth: 150
                    onClicked: {
                        if (smartSession.cancelFirmwareUpdate()) {
                            progressDialog.close()
                            controller.showMessage("Firmware update skipped. Reconnecting to the compatible firmware already on the board.", false)
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                CalmButton { theme: ribbon.theme; uiFont: ribbon.uiFont; text: "Retry"; tone: "accent"; implicitWidth: 110; onClicked: smartSession.retryFirmwareUpdate() }
            }
        }
    }

    Dialog {
        id: resultDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 408
        closePolicy: Popup.NoAutoClose
        padding: 16
        header: Rectangle {
            implicitHeight: 44
            color: ribbon.theme.surface2
            border.width: 1
            border.color: ribbon.firmwareResultSuccess ? ribbon.theme.green : ribbon.theme.red
            Label {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                text: "ESP32-P4 firmware"
                color: ribbon.firmwareResultSuccess ? ribbon.theme.green : ribbon.theme.text
                font.family: ribbon.uiFont
                font.pixelSize: 13
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }
        }
        background: Rectangle {
            color: ribbon.theme.surface
            radius: 10
            border.width: 1
            border.color: ribbon.firmwareResultSuccess ? ribbon.theme.green : ribbon.theme.red
        }
        contentItem: ColumnLayout {
            spacing: 10
            Label {
                Layout.fillWidth: true
                text: ribbon.firmwareResultSuccess ? "✓" : "!"
                color: ribbon.firmwareResultSuccess ? ribbon.theme.green : ribbon.theme.red
                font.family: ribbon.uiFont
                font.pixelSize: 30
                font.weight: Font.Bold
                horizontalAlignment: Text.AlignHCenter
            }
            Label {
                Layout.fillWidth: true
                text: ribbon.firmwareResultTitle
                color: ribbon.firmwareResultSuccess ? ribbon.theme.green : ribbon.theme.text
                font.family: ribbon.uiFont
                font.pixelSize: 17
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: ribbon.firmwareResultMessage
                color: ribbon.theme.textSoft
                font.family: ribbon.uiFont
                font.pixelSize: 11
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                CalmButton {
                    theme: ribbon.theme
                    uiFont: ribbon.uiFont
                    text: "Copy diagnostics"
                    visible: !ribbon.firmwareResultSuccess
                    implicitWidth: 145
                    onClicked: StudioClipboard.copyText(smartSession.diagnosticsText())
                }
                CalmButton { theme: ribbon.theme; uiFont: ribbon.uiFont; text: "OK"; tone: ribbon.firmwareResultSuccess ? "success" : "normal"; implicitWidth: 100; onClicked: resultDialog.close() }
                Item { Layout.fillWidth: true }
            }
        }
    }

    implicitHeight: 54
    color: "transparent"
    border.width: 0

    ModernRibbon {
        anchors.fill: parent
        theme: ribbon.theme
        controller: ribbon.controller
        workflow: ribbon
        session: smartSession
        device: ribbon.device
        uiFont: ribbon.uiFont
        compact: ribbon.compact
    }
}
