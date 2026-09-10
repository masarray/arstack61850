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
            if (smartSession.firmwareUpdateRequired && !ribbon.updatePromptDeferred && !updateDialog.opened)
                Qt.callLater(updateDialog.open)
        }
        function onFirmwareUpdateFinished(success) {
            if (success) {
                ribbon.updatePromptDeferred = false
                controller.showMessage("Firmware updated. Preparing injection…", false)
            } else {
                controller.showMessage("Firmware update did not complete. Open Advanced if recovery help is needed.", true)
            }
        }
    }

    Connections {
        target: device
        function onDeviceVerifiedChanged() {
            if (!device.deviceVerified)
                ribbon.updatePromptDeferred = false
        }
    }

    readonly property string displayState: {
        if (smartSession.state === "WAITING FOR DEVICE") return "Connect device"
        if (smartSession.state === "DEVICE FOUND" || smartSession.state === "CONNECTING") return "Connecting"
        if (smartSession.state === "FIRMWARE UPDATE") return "Firmware update available"
        if (smartSession.state === "UPDATING FIRMWARE") return "Updating firmware"
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
        if (smartSession.state === "CONNECTING" || smartSession.state === "PREPARING 4I+4V" ||
            smartSession.state === "FIRMWARE UPDATE" || smartSession.state === "UPDATING FIRMWARE" ||
            smartSession.state === "UPDATE NEEDS BOOT") return theme.amber
        if (smartSession.state === "DEVICE FOUND") return theme.accent
        if (smartSession.state === "PROFILE BLOCKED" || smartSession.state === "SETUP ERROR") return theme.red
        return theme.muted
    }

    function startReason() {
        if (device.running) return "SMV output is already running."
        if (smartSession.firmwareUpdateRequired) return "Update firmware before starting injection."
        if (smartSession.updatingFirmware) return "Firmware update is in progress."
        if (smartSession.state === "CONNECTING") return "Connecting to the device automatically."
        if (smartSession.state === "PREPARING 4I+4V") return "Preparing the default 4I+4V injection automatically."
        if (smartSession.state === "WAITING FOR DEVICE") return "Connect ESP32-P4; Studio will detect it automatically."
        return smartSession.statusText
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
                text: "Update now? Studio will install it, restart the board, reconnect, and return to Ready automatically."
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
                    text: "Updating firmware"
                    color: ribbon.theme.textSoft
                    font.family: ribbon.uiFont
                    font.pixelSize: 10
                }
                ProgressBar {
                    indeterminate: FirmwareService.flashProgress < 0
                    from: 0
                    to: 100
                    value: FirmwareService.flashProgress < 0 ? 0 : FirmwareService.flashProgress
                    Layout.preferredWidth: ribbon.compact ? 125 : 190
                }
                Label {
                    visible: FirmwareService.flashProgress >= 0
                    text: FirmwareService.flashProgress + "%"
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
                visible: !device.running && !smartSession.firmwareUpdateRequired &&
                    !smartSession.updatingFirmware && !smartSession.updateNeedsBootloaderHelp
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
