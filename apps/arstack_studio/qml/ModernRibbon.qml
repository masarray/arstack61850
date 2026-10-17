// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: bar

    property var theme
    property var controller
    property var workflow
    property var session
    property var device
    property string uiFont: "Inter"
    property bool compact: false

    implicitHeight: 76
    radius: 0
    color: "transparent"
    border.width: 0

    function reconnectOrOpenDevice() {
        if (device.deviceVerified)
            controller.openConfiguration()
        else
            session.requestConnect()
    }

    function conciseState() {
        if (!device.deviceVerified && controller.offlinePreviewRunning) return "Offline preview"
        if (session.state === "RUNNING") return "Running"
        if (session.state === "READY") return "Ready"
        if (session.state === "WAITING FOR DEVICE") return "Offline"
        if (session.state === "DEVICE FOUND" || session.state === "CONNECTING" || session.state === "CHECKING DEVICE") return "Connecting"
        if (session.state === "FIRMWARE REQUIRED") return "Firmware required"
        if (session.state === "UPDATING FIRMWARE") return "Updating"
        if (session.state === "UPDATE NEEDS BOOT") return "Bootloader"
        if (session.state === "PREPARING 4I+4V") return "Preparing"
        if (session.state === "PROFILE BLOCKED" || session.state === "PROFILE SYNC ERROR" || session.state === "SETUP ERROR") return "Attention"
        return workflow.displayState
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 6

        RowLayout {
            Layout.preferredWidth: bar.compact ? 148 : 174
            Layout.alignment: Qt.AlignVCenter
            spacing: 8

            Rectangle {
                width: 38
                height: 38
                radius: 9
                color: "#0d151d"
                border.width: 1
                border.color: bar.theme.lineSoft
                Image {
                    anchors.centerIn: parent
                    width: 25
                    height: 25
                    source: Qt.resolvedUrl("../assets/material/smv_injector.svg")
                    sourceSize.width: 50
                    sourceSize.height: 50
                }
            }

            Label {
                Layout.fillWidth: true
                text: "SMV Injector"
                color: bar.theme.text
                font.family: bar.uiFont
                font.pixelSize: bar.compact ? 12 : 14
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
        }

        Rectangle { width: 1; height: 42; color: bar.theme.lineSoft }

        RibbonAction {
            theme: bar.theme
            uiFont: bar.uiFont
            text: bar.workflow.startQueued ? "Starting" : "Start"
            tone: !bar.device.deviceVerified ? "accent"
                : bar.session.startReady ? "success" : "neutral"
            iconSource: Qt.resolvedUrl("../assets/material/play_arrow.svg")
            enabled: !bar.workflow.injectionRunning && !bar.workflow.startQueued && !bar.controller.offlinePreviewRunning
                && !bar.session.updatingFirmware && !bar.session.updateNeedsBootloaderHelp
            toolTipText: !enabled && bar.device.deviceVerified ? bar.workflow.startReason() : ""
            onClicked: {
                if (!bar.device.deviceVerified) {
                    // The footer already identifies preview-only operation.
                    bar.controller.offlinePreviewRunning = true
                } else bar.workflow.requestStart()
            }
        }

        RibbonAction {
            theme: bar.theme
            uiFont: bar.uiFont
            text: "Stop"
            tone: bar.workflow.injectionRunning ? "danger" : "neutral"
            iconSource: Qt.resolvedUrl("../assets/material/stop.svg")
            enabled: (bar.workflow.injectionRunning || bar.controller.offlinePreviewRunning)
                && !bar.session.updatingFirmware
            toolTipText: ""
            onClicked: {
                if (!bar.device.deviceVerified) bar.controller.offlinePreviewRunning = false
                else bar.workflow.requestStop()
            }
        }

        Rectangle { width: 1; height: 42; color: bar.theme.lineSoft }

        RibbonAction {
            theme: bar.theme
            uiFont: bar.uiFont
            text: bar.compact ? "SCL" : "Open SCL"
            iconSource: Qt.resolvedUrl("../assets/material/folder_open.svg")
            enabled: bar.session.engineeringEditable && !bar.workflow.startQueued
            toolTipText: ""
            onClicked: bar.workflow.openEngineeringDialog()
        }

        RibbonAction {
            visible: !bar.compact
            theme: bar.theme
            uiFont: bar.uiFont
            text: "4I+4V"
            iconSource: Qt.resolvedUrl("../assets/material/dashboard.svg")
            enabled: bar.session.engineeringEditable && !bar.workflow.startQueued
            toolTipText: ""
            onClicked: bar.workflow.useBuiltInProfile()
        }

        RibbonAction {
            visible: bar.session.firmwareUpdateRequired || bar.session.firmwareInstallRequired || bar.session.profileSyncRetryAvailable
            theme: bar.theme
            uiFont: bar.uiFont
            tone: "warning"
            text: bar.session.firmwareInstallRequired ? "Install firmware"
                : bar.session.firmwareUpdateRequired ? "Firmware required"
                : "Retry profile"
            toolTipText: bar.session.firmwareInstallRequired ? "Install the bundled ARStack firmware"
                : bar.session.firmwareUpdateRequired ? "Firmware update is required before injection"
                : "Retry the 4I + 4V profile synchronization"
            onClicked: {
                if (bar.session.firmwareInstallRequired) bar.workflow.openInstallPrompt()
                else if (bar.session.firmwareUpdateRequired) bar.workflow.openUpdatePrompt()
                else bar.workflow.retryProfileSync()
            }
        }

        Item { Layout.fillWidth: true; Layout.minimumWidth: 8 }

        RibbonAction {
            theme: bar.theme
            uiFont: bar.uiFont
            text: "Phasor"
            iconSource: Qt.resolvedUrl("../assets/material/tune.svg")
            checkable: true
            checked: bar.controller.phasorDockVisible || bar.controller.phasorDetached
            tone: checked ? "accent" : "neutral"
            toolTipText: ""
            onClicked: {
                if (bar.controller.phasorDetached) bar.controller.phasorDetached = false
                bar.controller.phasorDockVisible = checked
            }
        }

        RibbonAction {
            theme: bar.theme
            uiFont: bar.uiFont
            text: "Wave"
            iconSource: Qt.resolvedUrl("../assets/material/monitor_heart.svg")
            visible: !bar.compact
            checkable: true
            checked: bar.controller.waveformDockVisible || bar.controller.waveformDetached
            tone: checked ? "accent" : "neutral"
            toolTipText: ""
            onClicked: {
                if (bar.controller.waveformDetached) bar.controller.waveformDetached = false
                bar.controller.waveformDockVisible = checked
            }
        }

        RibbonAction {
            theme: bar.theme
            uiFont: bar.uiFont
            text: "Advanced"
            iconOnly: false
            iconSource: Qt.resolvedUrl("../assets/material/settings.svg")
            tone: "neutral"
            toolTipText: ""
            onClicked: bar.controller.openConfiguration()
        }

        Rectangle {
            Layout.preferredWidth: bar.compact ? 84 : 108
            Layout.preferredHeight: 36
            Layout.alignment: Qt.AlignVCenter
            radius: 15
            color: "#0a1016"
            border.width: 1
            border.color: Qt.rgba(bar.workflow.smartStateColor.r,
                                  bar.workflow.smartStateColor.g,
                                  bar.workflow.smartStateColor.b, 0.48)

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 9
                anchors.rightMargin: 9
                spacing: 6

                Rectangle {
                    width: 6
                    height: 6
                    radius: 3
                    color: bar.workflow.smartStateColor
                }

                Label {
                    Layout.fillWidth: true
                    text: bar.conciseState()
                    color: bar.workflow.smartStateColor
                    font.family: bar.uiFont
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
            }

            MouseArea {
                id: stateMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                enabled: !bar.device.discovering
                onClicked: bar.reconnectOrOpenDevice()
            }
            ToolTip.visible: stateMouse.containsMouse
            ToolTip.text: bar.device.deviceVerified ? "Device details" : "Reconnect"
            ToolTip.delay: 900
        }
    }
}
