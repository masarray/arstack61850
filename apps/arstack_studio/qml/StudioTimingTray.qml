// SPDX-License-Identifier: GPL-3.0-or-later
// Main-window timing workflow. Shares the existing Advanced PTP profile and
// SmartSessionController/DeviceController authority; never invents lock state.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

Rectangle {
    id: tray
    property var theme
    property var controller
    property var device
    property var session
    property string uiFont: "Inter"
    property string monoFont: "Inter"
    property bool compact: false
    property int selectedRoleIndex: offlineSettings.lastRole
    // Offline controls edit only a lab plan, never the real PTP subsystem.
    Settings {
        id: offlineSettings
        category: "offline-ptp-lab"
        property int lastRole: 0
        property int lastSynch: 0
        property bool previewPtp: false
    }
    readonly property bool boardOnline: Boolean(device && device.deviceVerified)
    readonly property bool offlinePtpPreview: !boardOnline && offlineSettings.previewPtp

    readonly property var roleValues: ["SOURCE", "RECEIVER", "MONITOR"]
    readonly property bool busy: Boolean(device && device.ptpStarting)
    readonly property bool ptpReady: Boolean(session && device && device.deviceVerified
                                               && device.ptpAvailable && !session.updatingFirmware)
    readonly property string stateText: !boardOnline
        ? (offlinePtpPreview ? "PTP PREVIEW" : "NO DEVICE")
        : !device.ptpAvailable ? "PTP UNAVAILABLE"
        : busy ? (device.ptpStatus.indexOf("unverified") !== -1 ? "UNVERIFIED" : "PENDING")
        : !device.ptpRunning ? "PTP OFF"
        : device.ptpRole + " RUNNING"
    readonly property color stateColor: !boardOnline ? theme.amber
        : busy ? theme.amber
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
        anchors.margins: tray.compact ? 7 : 9
        spacing: 0

        // The only global PTP/device status lives in the footer.
        // This is an action strip, not a second diagnostic/status card.
        RowLayout {
            Layout.fillWidth: true
            spacing: tray.compact ? 7 : 10
            StudioPtpSwitch {
                id: powerSwitch
                objectName: "studioPtpSwitch"
                theme: tray.theme
                uiFont: tray.uiFont
                checked: tray.boardOnline ? Boolean(tray.device.ptpRunning) : offlineSettings.previewPtp
                enabled: !tray.busy && (!tray.boardOnline || tray.ptpReady)

                onClicked: {
                    // Switch flips locally before asynchronous firmware evidence.
                    // Restore binding immediately, including on rejected requests.
                    const wantsRunning = checked
                    checked = Qt.binding(function() {
                        return tray.boardOnline ? Boolean(tray.device.ptpRunning) : offlineSettings.previewPtp
                    })
                    if (!tray.boardOnline) {
                        offlineSettings.previewPtp = wantsRunning
                        if (tray.controller)
                            tray.controller.showMessage("PTP preview only. Connect ESP32-P4 to transmit.", false)
                    } else if (wantsRunning) {
                        if (tray.controller) tray.controller.startPtpRole(tray.roleValues[tray.selectedRoleIndex])
                    } else if (tray.controller && !tray.controller.requestStopPtp()) {
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
                enabled: !tray.busy && (!tray.boardOnline
                    || (tray.ptpReady && !tray.device.ptpRunning))
                onActivated: {
                    tray.selectedRoleIndex = currentIndex
                    offlineSettings.lastRole = currentIndex
                    currentIndex = Qt.binding(function() {
                        return tray.device && tray.device.ptpRunning
                            ? tray.roleIndex(tray.device.ptpRole) : tray.selectedRoleIndex
                    })
                }

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
                currentIndex: tray.boardOnline
                    ? tray.syncIndex(tray.device.smpSynchMode) : offlineSettings.lastSynch
                enabled: !tray.boardOnline || (tray.session && !tray.session.updatingFirmware)

                onActivated: {
                    const requested = currentIndex
                    offlineSettings.lastSynch = requested
                    if (!tray.boardOnline) {
                        currentIndex = Qt.binding(function() {
                            return tray.boardOnline ? tray.syncIndex(tray.device.smpSynchMode)
                                                    : offlineSettings.lastSynch
                        })
                        if (tray.controller)
                            tray.controller.showMessage("Offline smpSynch plan saved; no SV flag or clock changed.", false)
                        return
                    }
                    const accepted = tray.session && tray.session.requestSmpSynch(["AUTO", "0", "1", "2"][requested])
                    currentIndex = Qt.binding(function() {
                        return tray.boardOnline ? tray.syncIndex(tray.device.smpSynchMode) : offlineSettings.lastSynch
                    })
                    if (!accepted && tray.controller)
                        tray.controller.showMessage("SV smpSynch override rejected; inspect diagnostics.", true)
                }
            }
            CalmButton {
                theme: tray.theme
                uiFont: tray.uiFont
                text: "Details"
                iconSource: Qt.resolvedUrl("../assets/material/settings.svg")
                objectName: "studioTimingDetails"
                implicitWidth: 66

                onClicked: if (tray.controller) tray.controller.openTimingSettings()
            }
        }

    }
}
