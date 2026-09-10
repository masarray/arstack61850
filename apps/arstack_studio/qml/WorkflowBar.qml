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
                controller.showMessage("Firmware updated. ARStack Studio is preparing 4I+4V injection.", false)
            } else {
                controller.showMessage("Firmware update did not complete. Open Advanced only if recovery help is needed.", true)
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

    Dialog {
        id: updateDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 430
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
                text: smartSession.deviceFirmwareVersion.length
                    ? "ESP32-P4 is running ARStack firmware v" + smartSession.deviceFirmwareVersion + "."
                    : "ESP32-P4 is running legacy ARStack firmware."
                color: ribbon.theme.text
                font.family: ribbon.uiFont
                font.pixelSize: 13
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: "Update to v" + smartSession.expectedFirmwareVersion + " now? The firmware is already included with ARStack Studio."
                color: ribbon.theme.textSoft
                font.family: ribbon.uiFont
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: "SMV output will be stopped safely if required. Studio will flash, reset, reconnect, and prepare the default 4I+4V profile automatically."
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
                        ribbon.updatePromptDeferred = true
                        updateDialog.close()
                    }
                }
                CalmButton {
                    theme: ribbon.theme
                    uiFont: ribbon.uiFont
                    text: "Update now"
                    tone: "accent"
                    implicitWidth: 118
                    onClicked: {
                        if (smartSession.beginFirmwareUpdate()) {
                            ribbon.updatePromptDeferred = false
                            updateDialog.close()
                        } else {
                            controller.showMessage("Firmware update could not start. Open Advanced for recovery details.", true)
                        }
                    }
                }
            }
        }
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
        if (smartSession.firmwareUpdateRequired) return "Firmware update is required before injection."
        if (smartSession.updatingFirmware) return "Firmware update is in progress."
        if (smartSession.state === "CONNECTING") return "ARStack Studio is connecting automatically."
        if (smartSession.state === "PREPARING 4I+4V") return "Preparing the default 4I+4V profile automatically…"
        if (smartSession.state === "WAITING FOR DEVICE") return "Connect the ESP32-P4; ARStack Studio will detect it automatically."
        return smartSession.statusText
    }

    implicitHeight: 94
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
        anchors.leftMargin: 11
        anchors.rightMargin: 11
        anchors.topMargin: 8
        anchors.bottomMargin: 8
        spacing: 7

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
                text: smartSession.state
                color: ribbon.smartStateColor
                font.family: ribbon.uiFont
                font.pixelSize: 10
                font.weight: Font.Bold
                font.letterSpacing: 0.45
            }
            Label {
                visible: !ribbon.compact
                text: smartSession.statusText
                color: ribbon.theme.muted
                font.family: ribbon.uiFont
                font.pixelSize: 9
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Item { Layout.fillWidth: ribbon.compact }
            CalmButton {
                theme: ribbon.theme
                uiFont: ribbon.uiFont
                text: "Advanced"
                implicitHeight: 28
                font.pixelSize: 9
                toolTipText: "Firmware, SCL, waveform stress, PTP and diagnostics"
                onClicked: ribbon.controller.openConfiguration()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6

            ActionButton {
                text: "Balanced"
                iconSource: Qt.resolvedUrl("../assets/lucide/scale.svg")
                toolTipText: "Apply balanced three-phase values"
                enabled: !smartSession.updatingFirmware
                onClicked: ribbon.controller.balanced()
            }
            ActionButton {
                text: "Zero"
                iconSource: Qt.resolvedUrl("../assets/lucide/circle-off.svg")
                toolTipText: "Set every current and voltage magnitude to zero"
                enabled: !smartSession.updatingFirmware
                onClicked: ribbon.controller.zeroAll()
            }

            Rectangle { width: 1; height: 28; color: ribbon.theme.lineSoft }

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
                ProgressBar {
                    id: firmwareProgress
                    indeterminate: FirmwareService.flashProgress < 0
                    from: 0
                    to: 100
                    value: FirmwareService.flashProgress < 0 ? 0 : FirmwareService.flashProgress
                    Layout.preferredWidth: ribbon.compact ? 125 : 190
                    Layout.alignment: Qt.AlignVCenter
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
                text: "BOOT + RESET required"
                color: ribbon.theme.amber
                font.family: ribbon.uiFont
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }
            ActionButton {
                visible: smartSession.updateNeedsBootloaderHelp
                text: "Retry update"
                tone: "accent"
                implicitWidth: 118
                toolTipText: smartSession.updateStatus
                onClicked: smartSession.retryFirmwareUpdate()
            }

            ActionButton {
                visible: smartSession.firmwareUpdateRequired && !smartSession.updatingFirmware && !smartSession.updateNeedsBootloaderHelp
                text: "Update firmware"
                tone: "accent"
                implicitWidth: 132
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
                implicitWidth: 132
                enabled: smartSession.startReady
                toolTipText: ribbon.startReason()
                onClicked: device.start()
            }
            ActionButton {
                visible: device.running && !smartSession.updatingFirmware
                text: "Stop"
                iconSource: Qt.resolvedUrl("../assets/lucide/square.svg")
                tone: "danger"
                implicitWidth: 132
                enabled: device.deviceVerified
                toolTipText: "Stop Sampled Values output"
                onClicked: device.stop()
            }
        }
    }
}
