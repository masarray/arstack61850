// SPDX-License-Identifier: GPL-3.0-or-later
// Main-window timing workflow. Shares the existing Advanced PTP profile and
// SmartSessionController/DeviceController authority; never invents lock state.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: tray
    property var theme
    property var controller
    property var device
    property var session
    property string uiFont: "Inter"
    property string monoFont: "Inter"
    property bool compact: false
    property int selectedRoleIndex: 0
    readonly property var roleValues: ["SOURCE", "RECEIVER", "MONITOR"]
    readonly property bool busy: Boolean(device && device.ptpStarting)
    readonly property bool ptpReady: Boolean(session && device && device.deviceVerified
                                               && device.ptpAvailable && !session.updatingFirmware)
    readonly property string stateText: !device || !device.deviceVerified ? "NO DEVICE"
        : !device.ptpAvailable ? "PTP UNAVAILABLE"
        : busy ? (device.ptpStatus.indexOf("unverified") !== -1 ? "UNVERIFIED" : "PENDING")
        : !device.ptpRunning ? "PTP OFF"
        : device.ptpRole + " RUNNING"
    readonly property color stateColor: busy ? theme.amber
        : device && device.ptpRunning ? theme.green : theme.muted

    function roleIndex(role) {
        if (role === "RECEIVER") return 1
        if (role === "MONITOR") return 2
        return 0
    }

    function syncIndex(mode) {
        if (mode === "FORCE_0") return 1
        if (mode === "FORCE_1_LOCAL") return 2
        if (mode === "FORCE_2_GLOBAL") return 3
        return 0
    }

    color: theme.panelAlt
    radius: theme.panelRadius
    border.width: 1
    border.color: theme.line

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: tray.compact ? 9 : 11
        spacing: tray.compact ? 5 : 7

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Rectangle { width: 3; height: 14; radius: 2; color: tray.theme.accent }
            Label {
                text: "TIME SYNC  /  SV SIMULATION"
                color: tray.theme.textSoft
                font.family: tray.uiFont
                font.pixelSize: 10
                font.weight: Font.DemiBold
                font.letterSpacing: 0.45
            }
            Item { Layout.fillWidth: true }
            StateBadge {
                theme: tray.theme
                monoFont: tray.monoFont
                state: tray.stateText
                stateColor: tray.stateColor
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: tray.compact ? 7 : 10
            StudioPtpSwitch {
                id: powerSwitch
                objectName: "studioPtpSwitch"
                theme: tray.theme
                uiFont: tray.uiFont
                checked: Boolean(tray.device && tray.device.ptpRunning)
                enabled: tray.ptpReady && !tray.busy
                ToolTip.visible: hovered
                ToolTip.text: "Start/stop PTP with verified board feedback. Does not establish SV sample-phase lock."
                onClicked: {
                    // Switch flips locally before asynchronous firmware evidence.
                    // Restore binding immediately, including on rejected requests.
                    const wantsRunning = checked
                    checked = Qt.binding(function() { return Boolean(tray.device && tray.device.ptpRunning) })
                    if (wantsRunning) {
                        if (tray.controller) tray.controller.startPtpRole(tray.roleValues[tray.selectedRoleIndex])
                    } else if (tray.session && !tray.session.requestStopPtp() && tray.controller) {
                        tray.controller.showMessage("PTP stop was not accepted; inspect diagnostics.", true)
                    }
                }
            }
            StudioDarkComboBox {
                id: roleCombo
                objectName: "studioPtpRole"
                theme: tray.theme
                uiFont: tray.uiFont
                Layout.fillWidth: true
                Layout.minimumWidth: 118
                model: ["Source", "Follower", "Monitor"]
                currentIndex: tray.device && tray.device.ptpRunning
                    ? tray.roleIndex(tray.device.ptpRole) : tray.selectedRoleIndex
                enabled: tray.ptpReady && !tray.busy && !tray.device.ptpRunning
                onActivated: tray.selectedRoleIndex = currentIndex
                ToolTip.visible: hovered
                ToolTip.text: "Source transmits PTP; Follower discovers an external GM; Monitor receives without disciplining."
            }
            Label {
                text: "smpSynch"
                color: tray.theme.muted
                font.family: tray.uiFont
                font.pixelSize: 9
            }
            StudioDarkComboBox {
                id: syncCombo
                objectName: "studioSmpSynchMode"
                theme: tray.theme
                uiFont: tray.uiFont
                Layout.fillWidth: true
                Layout.minimumWidth: 100
                model: ["AUTO", "0 · No sync", "1 · Local (sim)", "2 · Global (sim)"]
                currentIndex: tray.syncIndex(tray.device ? tray.device.smpSynchMode : "AUTO")
                enabled: tray.session && tray.device && tray.device.deviceVerified && !tray.session.updatingFirmware
                ToolTip.visible: hovered
                ToolTip.text: "1/2 change SV metadata only; they do not synchronize the sample clock."
                onActivated: {
                    const choice = ["AUTO", "0", "1", "2"][currentIndex]
                    const accepted = tray.session && tray.session.requestSmpSynch(choice)
                    currentIndex = Qt.binding(function() {
                        return tray.syncIndex(tray.device ? tray.device.smpSynchMode : "AUTO")
                    })
                    if (!accepted && tray.controller)
                        tray.controller.showMessage("SV smpSynch override rejected; inspect diagnostics.", true)
                }
            }
            CalmButton {
                theme: tray.theme
                uiFont: tray.uiFont
                text: "Details"
                objectName: "studioTimingDetails"
                implicitWidth: 66
                toolTipText: "PTP settings, timing evidence and diagnostics"
                onClicked: if (tray.controller) tray.controller.openTimingSettings()
            }
        }
        Label {
            Layout.fillWidth: true
            text: !tray.device || !tray.device.deviceVerified
                ? "Connect an ESP32-P4 to enable PTP. Generated waveform is a setpoint preview."
                : tray.device.smpSynchSimulated
                    ? "SV smpSynch=" + tray.device.smpSynchValue + " · SIMULATED metadata — sample clock not proven locked."
                    : tray.device.smpSynchMeasured
                        ? "SV smpSynch=" + tray.device.smpSynchValue + " · board-reported evidence; sample phase needs independent verification."
                        : "SV smpSynch=" + tray.device.smpSynchValue + " · unqualified. PTP traffic alone does not prove SV synchronization."
            color: tray.device && tray.device.smpSynchSimulated ? tray.theme.amber : tray.theme.muted
            font.family: tray.uiFont
            font.pixelSize: tray.compact ? 9 : 10
            elide: Text.ElideRight
            ToolTip.visible: evidenceHover.hovered
            ToolTip.text: text
            HoverHandler { id: evidenceHover }
        }
    }
}
