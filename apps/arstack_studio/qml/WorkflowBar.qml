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
        case "prepare": return 0
        case "verify": return 1
        case "write": return 2
        case "reconnect": return 3
        default: return -1
        }
    }
    readonly property var firmwareStages: [
        { title: "Prepare session", detail: "Stop output and release the serial port" },
        { title: "Verify board", detail: "Confirm ESP32-P4 ROM identity" },
        { title: "Write & restart", detail: "Program the verified image and reset the board" },
        { title: "Reconnect & verify", detail: "Confirm the new ARStack semantic identity" }
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
            controller.profileDirty = false
            controller.applyAllSignals()
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
            if (smartSession.firmwareInstallRequired && !ribbon.installPromptDeferred &&
                    !installDialog.opened && !progressDialog.opened) {
                Qt.callLater(installDialog.open)
                return
            }
            if (smartSession.firmwareUpdateRequired && !ribbon.updatePromptDeferred &&
                    !updateDialog.opened && !progressDialog.opened) {
                Qt.callLater(updateDialog.open)
            }
        }
        function onFirmwareUpdateFinished(success) {
            if (progressDialog.opened) progressDialog.close()
            ribbon.firmwareResultSuccess = success
            if (success) {
                ribbon.updatePromptDeferred = false
                ribbon.installPromptDeferred = false
                ribbon.firmwareResultTitle = "Firmware installed successfully"
                ribbon.firmwareResultMessage = "ESP32-P4 restarted, reconnected, and ARStack firmware was verified. Studio is preparing the 4I+4V injector."
                controller.showMessage("Firmware installed and verified successfully.", false)
            } else {
                ribbon.firmwareResultTitle = "Firmware installation not completed"
                ribbon.firmwareResultMessage = FirmwareService.status.length > 0
                    ? FirmwareService.status
                    : "Studio could not verify the firmware installation. Keep the board connected and try again."
                controller.showMessage("Firmware installation was not verified.", true)
            }
            Qt.callLater(resultDialog.open)
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

    readonly property string displayState: {
        if (smartSession.state === "WAITING FOR DEVICE") return "Connect device"
        if (smartSession.state === "DEVICE FOUND" || smartSession.state === "CONNECTING") return "Connecting"
        if (smartSession.state === "CHECKING DEVICE") return "Checking device"
        if (smartSession.state === "FIRMWARE REQUIRED") return "Firmware required"
        if (smartSession.state === "FIRMWARE UPDATE") return "Firmware update available"
        if (smartSession.state === "UPDATING FIRMWARE") return "Installing firmware"
        if (smartSession.state === "UPDATE NEEDS BOOT") return "Download mode required"
        if (smartSession.state === "PREPARING 4I+4V") return "Preparing"
        if (smartSession.state === "READY") return smartSession.firmwareUpdateAvailable ? "Ready · update available" : "Ready to inject"
        if (smartSession.state === "RUNNING") return "Injection running"
        if (smartSession.state === "PROFILE BLOCKED") return "Profile unavailable"
        if (smartSession.state === "PROFILE SYNC ERROR") return "Profile sync failed"
        if (smartSession.state === "SETUP ERROR") return "Setup issue"
        return smartSession.state
    }

    readonly property color smartStateColor: {
        if (smartSession.state === "RUNNING" || smartSession.state === "READY") return theme.green
        if (smartSession.state === "CONNECTING" || smartSession.state === "CHECKING DEVICE" ||
            smartSession.state === "FIRMWARE REQUIRED" || smartSession.state === "PREPARING 4I+4V" ||
            smartSession.state === "FIRMWARE UPDATE" || smartSession.state === "UPDATING FIRMWARE" ||
            smartSession.state === "UPDATE NEEDS BOOT") return theme.amber
        if (smartSession.state === "DEVICE FOUND") return theme.accent
        if (smartSession.state === "PROFILE BLOCKED" || smartSession.state === "PROFILE SYNC ERROR" ||
            smartSession.state === "SETUP ERROR") return theme.red
        return theme.muted
    }

    function startReason() {
        if (ribbon.injectionRunning) return "SMV output is already running."
        if (smartSession.firmwareInstallRequired) return "Install ARStack firmware before starting injection."
        if (smartSession.firmwareUpdateRequired) return "Update firmware before starting injection."
        if (smartSession.updatingFirmware) return "Firmware installation is in progress."
        if (smartSession.updateNeedsBootloaderHelp) return "Complete Download mode recovery before starting injection."
        if (smartSession.state === "CONNECTING" || smartSession.state === "CHECKING DEVICE") return "Studio is identifying the device automatically."
        if (smartSession.state === "PREPARING 4I+4V") return "Studio is preparing the default 4I+4V injection automatically."
        if (smartSession.state === "PROFILE SYNC ERROR") return smartSession.statusText
        if (smartSession.state === "WAITING FOR DEVICE") return "Connect ESP32-P4; Studio will detect it automatically."
        return smartSession.statusText
    }

    function requestStart() {
        if (ribbon.injectionRunning) return
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
            controller.showMessage(ribbon.startReason(), false)
            return
        }
        if (!smartSession.requestStart())
            controller.showMessage(smartSession.statusText, true)
    }

    function requestStop() {
        if (ribbon.injectionRunning && !smartSession.requestStop())
            controller.showMessage("Studio could not stop the current device session.", true)
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
        width: 420
        title: "Set up ESP32-P4"
        closePolicy: Popup.NoAutoClose
        background: Rectangle { color: ribbon.theme.surface; radius: 10; border.width: 1; border.color: ribbon.theme.line }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: "ESP32-P4 detected"
                color: ribbon.theme.text
                font.family: ribbon.uiFont
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                text: "ARStack firmware is not installed or is not responding. Studio can install the correct firmware automatically."
                color: ribbon.theme.textSoft
                font.family: ribbon.uiFont
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: "No firmware file, ESP-IDF, Python, or command line is required. Keep USB connected until Studio confirms success."
                color: ribbon.theme.muted
                font.family: ribbon.uiFont
                font.pixelSize: 10
                wrapMode: Text.WordWrap
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
        width: 410
        title: smartSession.firmwareUpdateRequired ? "Firmware update required" : "Firmware update available"
        closePolicy: Popup.NoAutoClose
        background: Rectangle { color: ribbon.theme.surface; radius: 10; border.width: 1; border.color: ribbon.theme.line }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: smartSession.firmwareUpdateRequired ? "Update firmware to start injection" : "A newer firmware build is available"
                color: ribbon.theme.text
                font.family: ribbon.uiFont
                font.pixelSize: 14
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: smartSession.firmwareUpdateRequired
                    ? "This ESP32-P4 does not satisfy the current runtime contract. Update is required before injection."
                    : "The connected firmware is compatible and can be used now. A newer bundled build is available; update only when convenient. Studio will flash, restart, reconnect, and verify it automatically."
                color: ribbon.theme.textSoft
                font.family: ribbon.uiFont
                font.pixelSize: 11
                wrapMode: Text.WordWrap
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
        width: 500
        closePolicy: Popup.NoAutoClose
        padding: 18
        header: Rectangle {
            implicitHeight: 46
            color: ribbon.theme.surface2
            border.width: 1
            border.color: smartSession.updateNeedsBootloaderHelp ? ribbon.theme.amber : ribbon.theme.line
            Label {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                text: smartSession.updateNeedsBootloaderHelp ? "Device needs Download mode" : "Firmware update"
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
            spacing: 13

            Label {
                Layout.fillWidth: true
                text: smartSession.updateNeedsBootloaderHelp
                    ? "Studio cannot reach the ESP32-P4 ROM bootloader yet."
                    : "Keep USB connected. Studio reports success only after the board restarts and reconnects with the expected ARStack identity."
                color: smartSession.updateNeedsBootloaderHelp ? ribbon.theme.amber : ribbon.theme.text
                font.family: ribbon.uiFont
                font.pixelSize: 12
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: stageColumn.implicitHeight + 18
                radius: 8
                color: ribbon.theme.panelAlt
                border.width: 1
                border.color: ribbon.theme.lineSoft

                ColumnLayout {
                    id: stageColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 9
                    spacing: 2

                    Repeater {
                        model: ribbon.firmwareStages
                        delegate: RowLayout {
                            required property int index
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: 38
                            spacing: 10
                            readonly property string stageState: ribbon.firmwareStageState(index)

                            Rectangle {
                                width: 20
                                height: 20
                                radius: 10
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

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.title
                                    color: stageState === "pending" ? ribbon.theme.muted : ribbon.theme.text
                                    font.family: ribbon.uiFont
                                    font.pixelSize: 10
                                    font.weight: stageState === "active" ? Font.DemiBold : Font.Medium
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.detail
                                    color: ribbon.theme.muted
                                    font.family: ribbon.uiFont
                                    font.pixelSize: 9
                                    elide: Text.ElideRight
                                }
                            }

                            Label {
                                visible: index === 2 && stageState === "active" && smartSession.firmwareProgress >= 0
                                text: smartSession.firmwareProgress + "%"
                                color: ribbon.theme.accent
                                font.family: ribbon.monoFont
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                text: smartSession.updateStatus.length > 0 ? smartSession.updateStatus : "Preparing firmware installation…"
                color: ribbon.theme.textSoft
                font.family: ribbon.uiFont
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }

            ColumnLayout {
                visible: !smartSession.updateNeedsBootloaderHelp && ribbon.firmwareStageIndex === 2
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
        width: 430
        closePolicy: Popup.NoAutoClose
        padding: 18
        header: Rectangle {
            implicitHeight: 44
            color: ribbon.theme.surface2
            border.width: 1
            border.color: ribbon.firmwareResultSuccess ? ribbon.theme.green : ribbon.theme.red
            Label {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                text: ribbon.firmwareResultSuccess ? "Firmware verified" : "Firmware result"
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
            spacing: 12
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
                Item { Layout.fillWidth: true }
                CalmButton { theme: ribbon.theme; uiFont: ribbon.uiFont; text: "OK"; tone: ribbon.firmwareResultSuccess ? "success" : "normal"; implicitWidth: 100; onClicked: resultDialog.close() }
                Item { Layout.fillWidth: true }
            }
        }
    }

    implicitHeight: 62
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
