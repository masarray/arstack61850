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

    SmartSessionController {
        id: smartSession
        device: ribbon.device
        profiles: ribbon.profiles
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
    }

    readonly property color smartStateColor: {
        if (smartSession.state === "RUNNING" || smartSession.state === "READY") return theme.green
        if (smartSession.state === "CONNECTING" || smartSession.state === "PREPARING 4I+4V" || smartSession.state === "FIRMWARE UPDATE") return theme.amber
        if (smartSession.state === "DEVICE FOUND") return theme.accent
        if (smartSession.state === "PROFILE BLOCKED" || smartSession.state === "SETUP ERROR") return theme.red
        return theme.muted
    }

    function startReason() {
        if (device.running) return "SMV output is already running."
        if (smartSession.firmwareUpdateRequired) return "Firmware update is required before injection."
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
                text: "Advanced…"
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
                onClicked: ribbon.controller.balanced()
            }
            ActionButton {
                text: "Zero"
                iconSource: Qt.resolvedUrl("../assets/lucide/circle-off.svg")
                toolTipText: "Set every current and voltage magnitude to zero"
                onClicked: ribbon.controller.zeroAll()
            }

            Rectangle { width: 1; height: 28; color: ribbon.theme.lineSoft }

            ActionButton {
                visible: !ribbon.compact
                text: "Phasor"
                iconSource: Qt.resolvedUrl("../assets/lucide/panels-top-left.svg")
                tone: ribbon.controller.phasorDockVisible || ribbon.controller.phasorDetached ? "accent" : "neutral"
                onClicked: {
                    ribbon.controller.phasorDetached = false
                    ribbon.controller.phasorDockVisible = !ribbon.controller.phasorDockVisible
                }
            }
            ActionButton {
                visible: !ribbon.compact
                text: "Waveform"
                iconSource: Qt.resolvedUrl("../assets/lucide/activity.svg")
                tone: ribbon.controller.waveformDockVisible || ribbon.controller.waveformDetached ? "accent" : "neutral"
                onClicked: {
                    ribbon.controller.waveformDetached = false
                    ribbon.controller.waveformDockVisible = !ribbon.controller.waveformDockVisible
                }
            }

            Item { Layout.fillWidth: true }

            Label {
                visible: smartSession.state === "CONNECTING" || smartSession.state === "PREPARING 4I+4V"
                text: smartSession.state === "CONNECTING" ? "Connecting…" : "Preparing…"
                color: ribbon.theme.amber
                font.family: ribbon.uiFont
                font.pixelSize: 10
            }

            ActionButton {
                visible: !device.running
                text: "Start"
                iconSource: Qt.resolvedUrl("../assets/lucide/play.svg")
                tone: "success"
                implicitWidth: 132
                enabled: smartSession.startReady
                toolTipText: ribbon.startReason()
                onClicked: device.start()
            }
            ActionButton {
                visible: device.running
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
