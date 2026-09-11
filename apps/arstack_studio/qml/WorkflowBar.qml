// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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

    SmartSessionController {
        id: smartSession
        device: ribbon.device
        profiles: ribbon.profiles
        firmware: FirmwareService
    }

    Component.onCompleted: {
        controller.phasorDockVisible = false
        controller.waveformDockVisible = false
        smartSession.start()
    }

    Connections {
        target: smartSession
        function onReadyForLiveApply() {
            controller.profileDirty = false
            controller.applyAllSignals()
        }
        function onStateChanged() {
            if (smartSession.updatingFirmware || smartSession.updateNeedsBootloaderHelp) {
                if (installDialog.opened)
                    installDialog.close()
                if (updateDialog.opened)
                    updateDialog.close()
                if (!progressDialog.opened)
                    Qt.callLater(progressDialog.open)
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
            if (progressDialog.opened)
                progressDialog.close()

            ribbon.firmwareResultSuccess = success
            if (success) {
                ribbon.updatePromptDeferred = false
                ribbon.installPromptDeferred = false
                ribbon.firmwareResultTitle = "Firmware installed successfully"
                ribbon.firmwareResultMessage = "ESP32-P4 restarted and ARStack firmware was verified. Studio is preparing the 4I+4V injector."
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
            if (!device.deviceVerified)
                ribbon.updatePromptDeferred = false
        }
        function onPortsChanged() {
            if (device.ports.length === 0) {
                ribbon.installPromptDeferred = false
                ribbon.updatePromptDeferred = false
            }
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
        if (smartSession.state === "READY") return "Ready"
        if (smartSession.state === "RUNNING") return "Running"
        if (smartSession.state === "PROFILE BLOCKED") return "Profile unavailable"
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
        if (smartSession.state === "PROFILE BLOCKED" || smartSession.state === "SETUP ERROR") return theme.red
        return theme.muted
    }

    function startReason() {
        if (device.running) return "SMV output is already running."
        if (smartSession.firmwareInstallRequired) return "Install ARStack firmware before starting injection."
        if (smartSession.firmwareUpdateRequired) return "Update firmware before starting injection."
        if (smartSession.updatingFirmware) return "Firmware installation is in progress."
        if (smartSession.state === "CONNECTING" || smartSession.state === "CHECKING DEVICE") return "Studio is identifying the device automatically."
        if (smartSession.state === "PREPARING 4I+4V") return "Preparing the default 4I+4V injection automatically."
        if (smartSession.state === "WAITING FOR DEVICE") return "Connect ESP32-P4; Studio will detect it automatically."
        return smartSession.statusText
    }

    Dialog {
        id: installDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 420
        title: "Set up ESP32-P4"
        closePolicy: Popup.NoAutoClose

        background: Rectangle {
            color: ribbon.theme.surface
            radius: 10
            border.width: 1
            border.color: ribbon.theme.line
        }

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
                text: "ARStack firmware is not installed or is not responding on this board. Studio can install the correct firmware automatically."
                color: ribbon.theme.textSoft
                font.family: ribbon.uiFont
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: "No firmware file, ESP-IDF, Python, or command line is required. Keep the USB cable connected until Studio confirms success."
                color: ribbon.theme.muted
                font.family: ribbon.uiFont
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                CalmButton {
                    theme: ribbon.theme
                    uiFont: ribbon.uiFont
                    text: "Later"
                    onClicked: {
                        ribbon.installPromptDeferred = true
                        installDialog.close()
                    }
                }
                CalmButton {
                    theme: ribbon.theme
                    uiFont: ribbon.uiFont
                    text: "Install firmware"
                    tone: "accent"
                    implicitWidth: 142
                    onClicked: {
                        if (smartSession.beginFirmwareInstall()) {
                            ribbon.installPromptDeferred = false
                            installDialog.close()
                        } else {
                            controller.showMessage("Firmware installation could not start. Open Advanced for recovery details.", true)
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: updateDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 400
        title: "Firmware update"
        closePolicy: Popup.NoAutoClose

        background: Rectangle {
            color: ribbon.theme.surface
            radius: 10
            border.width: 1
            border.color: ribbon.theme.line
        }

        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: "A newer ARStack firmware is included with this Studio build."
                color: ribbon.theme.text
                font.family: ribbon.uiFont
                font.pixelSize: 13
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: "Update now? Studio will install it, restart the board, reconnect, and verify the result automatically."
                color: ribbon.theme.textSoft
                font.family: ribbon.uiFont
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                CalmButton {
                    theme: ribbon.theme
                    uiFont: ribbon.uiFont
                    text: "Later"
                    onClicked: {
                        ribbon.updatePromptDeferred = true
                        updateDialog.close()
                    }
                }
                CalmButton {
                    theme: ribbon.theme
                    uiFont: ribbon.uiFont
                    text: "Update"
                    tone: "accent"
                    implicitWidth: 110
                    onClicked: {
                        if (smartSession.beginFirmwareUpdate()) {
                            ribbon.updatePromptDeferred = false
                            updateDialog.close()
                        } else {
                            controller.showMessage("Firmware update could not start. Open Advanced for recovery help.", true)
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: progressDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 450
        title: smartSession.updateNeedsBootloaderHelp ? "Device needs Download mode" : "Installing firmware"
        closePolicy: Popup.NoAutoClose

        background: Rectangle {
            color: ribbon.theme.surface
            radius: 10
            border.width: 1
            border.color: smartSession.updateNeedsBootloaderHelp ? ribbon.theme.amber : ribbon.theme.line
        }

        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: smartSession.updateNeedsBootloaderHelp
                    ? "Studio cannot reach the ESP32-P4 bootloader yet."
                    : "Keep the USB cable connected. Studio will verify the board before reporting success."
                color: smartSession.updateNeedsBootloaderHelp ? ribbon.theme.amber : ribbon.theme.text
                font.family: ribbon.uiFont
                font.pixelSize: 13
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: smartSession.updateStatus.length > 0 ? smartSession.updateStatus : "Preparing firmware installation…"
                color: ribbon.theme.textSoft
                font.family: ribbon.uiFont
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
            ProgressBar {
                visible: !smartSession.updateNeedsBootloaderHelp
                Layout.fillWidth: true
                from: 0
                to: 100
                indeterminate: smartSession.firmwareProgress < 0
                value: smartSession.firmwareProgress < 0 ? 0 : smartSession.firmwareProgress
            }
            Label {
                visible: !smartSession.updateNeedsBootloaderHelp && smartSession.firmwareProgress >= 0
                Layout.fillWidth: true
                text: smartSession.firmwareProgress + "%"
                color: ribbon.theme.text
                font.family: ribbon.monoFont
                font.pixelSize: 12
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
            }
            RowLayout {
                visible: smartSession.updateNeedsBootloaderHelp
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                CalmButton {
                    theme: ribbon.theme
                    uiFont: ribbon.uiFont
                    text: "Retry"
                    tone: "accent"
                    implicitWidth: 110
                    onClicked: smartSession.retryFirmwareUpdate()
                }
            }
        }
    }

    Dialog {
        id: resultDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 430
        title: ribbon.firmwareResultSuccess ? "Success" : "Firmware result"
        closePolicy: Popup.NoAutoClose

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
                CalmButton {
                    theme: ribbon.theme
                    uiFont: ribbon.uiFont
                    text: "OK"
                    tone: ribbon.firmwareResultSuccess ? "success" : "normal"
                    implicitWidth: 100
                    onClicked: resultDialog.close()
                }
                Item { Layout.fillWidth: true }
            }
        }
    }

    implicitHeight: 88
    color: theme.surface2
    border.color: theme.line

    component ActionButton: CalmButton {
        theme: ribbon.theme
        uiFont: ribbon.uiFont
        implicitHeight: 40
        iconSize: 15
        font.pixelSize: 10
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.topMargin: 8
        anchors.bottomMargin: 8
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            spacing: 8

            Rectangle {
                width: 8
                height: 8
                radius: 4
                color: ribbon.smartStateColor
            }
            Label {
                text: ribbon.displayState
                color: ribbon.smartStateColor
                font.family: ribbon.uiFont
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
            Label {
                visible: !ribbon.compact && smartSession.state !== "READY" && smartSession.state !== "RUNNING"
                text: smartSession.statusText
                color: ribbon.theme.muted
                font.family: ribbon.uiFont
                font.pixelSize: 10
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label {
                visible: !ribbon.compact && (smartSession.state === "READY" || smartSession.state === "RUNNING")
                text: smartSession.state === "RUNNING"
                    ? "4I + 4V · 4000 samples/s · live"
                    : "4I + 4V · 4000 samples/s"
                color: ribbon.theme.muted
                font.family: ribbon.uiFont
                font.pixelSize: 10
                Layout.fillWidth: true
            }
            Item { Layout.fillWidth: ribbon.compact }
            CalmButton {
                theme: ribbon.theme
                uiFont: ribbon.uiFont
                text: "Advanced"
                implicitHeight: 28
                font.pixelSize: 10
                toolTipText: "Firmware, engineering profile, waveform and timing tools"
                onClicked: ribbon.controller.openConfiguration()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6

            ActionButton {
                visible: !smartSession.updatingFirmware && !smartSession.updateNeedsBootloaderHelp
                text: "Balanced"
                iconSource: Qt.resolvedUrl("../assets/lucide/scale.svg")
                toolTipText: "Apply balanced three-phase values"
                onClicked: ribbon.controller.balanced()
            }
            ActionButton {
                visible: !smartSession.updatingFirmware && !smartSession.updateNeedsBootloaderHelp
                text: "Zero"
                iconSource: Qt.resolvedUrl("../assets/lucide/circle-off.svg")
                toolTipText: "Set all current and voltage magnitudes to zero"
                onClicked: ribbon.controller.zeroAll()
            }

            Rectangle {
                visible: !ribbon.compact && !smartSession.updatingFirmware && !smartSession.updateNeedsBootloaderHelp
                width: 1
                height: 28
                color: ribbon.theme.lineSoft
            }

            ActionButton {
                visible: !ribbon.compact && !smartSession.updatingFirmware && !smartSession.updateNeedsBootloaderHelp
                text: "Phasor"
                iconSource: Qt.resolvedUrl("../assets/lucide/panels-top-left.svg")
                tone: ribbon.controller.phasorDockVisible || ribbon.controller.phasorDetached ? "accent" : "neutral"
                onClicked: {
                    ribbon.controller.phasorDetached = false
                    ribbon.controller.phasorDockVisible = !ribbon.controller.phasorDockVisible
                }
            }
            ActionButton {
                visible: !ribbon.compact && !smartSession.updatingFirmware && !smartSession.updateNeedsBootloaderHelp
                text: "Waveform"
                iconSource: Qt.resolvedUrl("../assets/lucide/activity.svg")
                tone: ribbon.controller.waveformDockVisible || ribbon.controller.waveformDetached ? "accent" : "neutral"
                onClicked: {
                    ribbon.controller.waveformDetached = false
                    ribbon.controller.waveformDockVisible = !ribbon.controller.waveformDockVisible
                }
            }

            Item { Layout.fillWidth: true }

            RowLayout {
                visible: smartSession.updatingFirmware
                spacing: 8
                Label {
                    visible: !ribbon.compact
                    text: "Installing firmware"
                    color: ribbon.theme.textSoft
                    font.family: ribbon.uiFont
                    font.pixelSize: 10
                }
                ProgressBar {
                    indeterminate: smartSession.firmwareProgress < 0
                    from: 0
                    to: 100
                    value: smartSession.firmwareProgress < 0 ? 0 : smartSession.firmwareProgress
                    Layout.preferredWidth: ribbon.compact ? 125 : 190
                }
                Label {
                    visible: smartSession.firmwareProgress >= 0
                    text: smartSession.firmwareProgress + "%"
                    color: ribbon.theme.textSoft
                    font.family: ribbon.monoFont
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    Layout.preferredWidth: 36
                    horizontalAlignment: Text.AlignRight
                }
            }

            Label {
                visible: smartSession.updateNeedsBootloaderHelp
                text: ribbon.compact ? "BOOT + RESET" : "Put the board in Download mode"
                color: ribbon.theme.amber
                font.family: ribbon.uiFont
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }
            ActionButton {
                visible: smartSession.updateNeedsBootloaderHelp
                text: "Retry"
                tone: "accent"
                implicitWidth: 96
                toolTipText: smartSession.updateStatus
                onClicked: smartSession.retryFirmwareUpdate()
            }

            ActionButton {
                visible: smartSession.firmwareInstallRequired && !smartSession.updatingFirmware && !smartSession.updateNeedsBootloaderHelp
                text: "Install firmware"
                tone: "accent"
                implicitWidth: 142
                onClicked: {
                    ribbon.installPromptDeferred = false
                    installDialog.open()
                }
            }

            ActionButton {
                visible: smartSession.firmwareUpdateRequired && !smartSession.updatingFirmware && !smartSession.updateNeedsBootloaderHelp
                text: "Update firmware"
                tone: "accent"
                implicitWidth: 136
                onClicked: {
                    ribbon.updatePromptDeferred = false
                    updateDialog.open()
                }
            }

            ActionButton {
                // Keep the primary run affordance stable even when firmware gates execution.
                // Safety still comes from startReady=false; the tooltip explains the blocker.
                visible: !device.running && !smartSession.updatingFirmware && !smartSession.updateNeedsBootloaderHelp
                text: "Start"
                iconSource: Qt.resolvedUrl("../assets/lucide/play.svg")
                tone: "success"
                implicitWidth: 140
                enabled: smartSession.startReady
                toolTipText: ribbon.startReason()
                onClicked: device.start()
            }
            ActionButton {
                visible: device.running && !smartSession.updatingFirmware
                text: "Stop"
                iconSource: Qt.resolvedUrl("../assets/lucide/square.svg")
                tone: "danger"
                implicitWidth: 140
                enabled: device.deviceVerified
                toolTipText: "Stop Sampled Values output"
                onClicked: device.stop()
            }
        }
    }
}
