// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ARStack.Studio 1.0

// Read-only projection of the real serial connection and session supervisor.
// Do not infer a hardware fault from latched firmware STOP history.
Item {
    id: indicator

    required property var device
    required property var session
    required property var theme
    property string uiFont: "Inter"
    property bool compact: false

    readonly property string supervisorState: session ? String(session.state) : ""
    readonly property bool portOpen: Boolean(device && device.connected)
    readonly property bool verified: Boolean(device && device.deviceVerified)
    readonly property string stateKey: {
        if (!portOpen)
            return device && device.discovering ? "connecting" : "disconnected"
        if (!verified)
            return (supervisorState === "SETUP ERROR" ||
                    (device.identificationState === DeviceController.Unidentified &&
                     !device.discovering)) ? "error" : "connecting"
        if (!device.running && device.svUnexpectedStopObserved)
            return "error"
        if (supervisorState === "SETUP ERROR" ||
            supervisorState === "PROFILE SYNC ERROR" ||
            supervisorState === "PROFILE BLOCKED")
            return "error"
        if (device.running) return "running"
        if (supervisorState === "READY") return "ready"
        return "preparing"
    }
    readonly property color stateColor: {
        switch (stateKey) {
        case "connecting": return theme.accent
        case "preparing": return theme.amber
        case "ready":
        case "running": return theme.green
        case "error": return theme.red
        default: return theme.muted2
        }
    }
    readonly property string stateCaption: {
        switch (stateKey) {
        case "connecting": return "CONNECTING"
        case "preparing": return "PREPARING"
        case "ready": return "READY"
        case "running": return "RUNNING"
        case "error": return "ERROR"
        default: return "DISCONNECTED"
        }
    }
    readonly property string portCaption:
        portOpen && device.portName.length ? device.portName : "BOARD"
    readonly property string detail: {
        if (stateKey === "error") {
            if (device.svUnexpectedStopObserved)
                return "SV stopped in this Studio session: " +
                    (device.svTxFaultDetail || device.svStopReason) +
                    ". See diagnostics."
            return "Board connected, but verification or setup needs attention: " +
                supervisorState
        }
        if (stateKey === "ready") return "ESP32-P4 verified; ready for an operator START"
        if (stateKey === "running") return "SV transmitter running (board reported; wire still requires independent capture)"
        if (stateKey === "preparing") return "ESP32-P4 connected and verified; preparing injection profile"
        if (stateKey === "connecting") return "Searching for or verifying the serial connection"
        return "No verified ESP32-P4 connection; offline edit is available"
    }

    implicitHeight: 20
    implicitWidth: compact ? 180 : 210

    RowLayout {
        anchors.fill: parent
        spacing: 6
        Rectangle {
            Layout.preferredWidth: 14
            Layout.preferredHeight: 14
            radius: 7
            color: "transparent"
            border.width: 1
            border.color: Qt.rgba(indicator.stateColor.r, indicator.stateColor.g,
                                  indicator.stateColor.b, 0.44)
            Rectangle {
                anchors.centerIn: parent
                width: 8
                height: 8
                radius: 4
                color: indicator.stateColor
                Behavior on color { ColorAnimation { duration: 150 } }
            }
        }
        Label {
            text: indicator.portCaption
            color: indicator.verified ? indicator.theme.textSoft : indicator.theme.muted
            font.family: indicator.uiFont
            font.pixelSize: 10
            font.weight: Font.Medium
            Layout.maximumWidth: indicator.compact ? 56 : 75
            elide: Text.ElideRight
        }
        Label {
            text: indicator.stateCaption
            color: indicator.stateColor
            font.family: indicator.uiFont
            font.pixelSize: 9
            font.weight: Font.DemiBold
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
    }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        ToolTip.visible: containsMouse
        ToolTip.delay: 750
        ToolTip.text: indicator.detail
    }
}
