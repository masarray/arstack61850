// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SurfacePanel {
    id: ribbon

    property var controller
    property var device
    property var profiles
    property string uiFont: "Inter"
    property string monoFont: "Inter"
    property bool compact: false

    readonly property bool p0FirmwareCompatible: device.deviceVerified && device.protocolVersion === "1"
    readonly property bool startReady:
        p0FirmwareCompatible && profiles.hasProfiles && controller.selectedProfileDeployable &&
        device.profileArmed && !controller.profileDirty && !device.running && !device.profileDeploying

    readonly property string smartState: {
        if (device.running) return "RUNNING"
        if (device.discovering || (device.connected && !device.deviceVerified)) return "CONNECTING"
        if (!device.deviceVerified) return device.ports.length > 0 ? "DEVICE FOUND" : "WAITING FOR DEVICE"
        if (!p0FirmwareCompatible) return "FIRMWARE UPDATE"
        if (device.profileDeploying || controller.profileDirty || !device.profileArmed) return "PREPARING 4I+4V"
        return "READY"
    }

    readonly property color smartStateColor: {
        if (smartState === "RUNNING" || smartState === "READY") return theme.green
        if (smartState === "CONNECTING" || smartState === "PREPARING 4I+4V" || smartState === "FIRMWARE UPDATE") return theme.amber
        if (smartState === "DEVICE FOUND") return theme.accent
        return theme.muted
    }

    function ensureDefaultProfile() {
        if (profiles.referenceTemplateActive)
            return true
        if (!profiles.loadReferenceTemplate()) {
            controller.showMessage(profiles.fatalError || "Unable to load the built-in 4I+4V profile.", true)
            return false
        }
        controller.profileDirty = true
        return true
    }

    function prepareSmartSession() {
        if (!device.deviceVerified || device.protocolVersion !== "1")
            return
        if (!ensureDefaultProfile())
            return
        if (device.running)
            return
        if (!controller.selectedProfileDeployable) {
            controller.showMessage("Built-in 4I+4V profile failed the device compatibility gate.", true)
            return
        }
        if (controller.profileDirty || !device.profileArmed) {
            controller.deploySelectedProfile()
            return
        }
        controller.applyAllSignals()
    }

    function startReason() {
        if (device.running) return "SMV output is already running."
        if (!device.deviceVerified) return device.ports.length > 0
            ? "Device detected. ARStack Studio is connecting automatically."
            : "Connect the ESP32-P4; ARStack Studio will detect it automatically."
        if (!p0FirmwareCompatible) return "Firmware update is required before injection."
        if (device.profileDeploying || controller.profileDirty || !device.profileArmed) return "Preparing the default 4I+4V profile automatically…"
        if (!controller.selectedProfileDeployable) return "The active profile is not compatible with this injector."
        return "Start 4I+4V Sampled Values output."
    }

    Component.onCompleted: {
        ensureDefaultProfile()
        Qt.callLater(device.autoDetectAndConnect)
    }

    Connections {
        target: device
        function onDeviceVerifiedChanged() {
            if (device.deviceVerified)
                prepareTimer.restart()
        }
        function onProfileStateChanged() {
            if (device.profileArmed && !device.profileDeploying) {
                controller.profileDirty = false
                controller.applyAllSignals()
            }
        }
        function onRunningChanged() {
            if (!device.running && device.deviceVerified && !device.profileArmed)
                prepareTimer.restart()
        }
    }

    Timer {
        id: prepareTimer
        interval: 600
        repeat: false
        onTriggered: ribbon.prepareSmartSession()
    }

    // Hot-plug/replug recovery. This never starts output automatically; it only
    // discovers and verifies the device. START remains an explicit operator action.
    Timer {
        interval: 2500
        repeat: true
        running: true
        onTriggered: {
            if (!device.deviceVerified && !device.discovering && !device.connected)
                device.autoDetectAndConnect()
        }
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
                text: ribbon.smartState
                color: ribbon.smartStateColor
                font.family: ribbon.uiFont
                font.pixelSize: 10
                font.weight: Font.Bold
                font.letterSpacing: 0.45
            }
            Label {
                visible: !ribbon.compact
                text: ribbon.smartState === "READY" || ribbon.smartState === "RUNNING"
                    ? "4I + 4V · 4000 samples/s · live value apply"
                    : (device.discoveryStatus.length ? device.discoveryStatus : "ARStack Studio is preparing the injector automatically")
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
                visible: ribbon.smartState === "PREPARING 4I+4V"
                text: "Preparing…"
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
                enabled: ribbon.startReady
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
