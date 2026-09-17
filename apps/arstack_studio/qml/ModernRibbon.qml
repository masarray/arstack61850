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

    implicitHeight: 52
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
            Layout.preferredWidth: bar.compact ? 112 : 132
            Layout.alignment: Qt.AlignVCenter
            spacing: 8

            Rectangle {
                width: 28
                height: 28
                radius: 7
                color: "#0d151d"
                border.width: 1
                border.color: bar.theme.lineSoft
                Image {
                    anchors.centerIn: parent
                    width: 15
                    height: 15
                    source: Qt.resolvedUrl("../assets/lucide/radio-tower.svg")
                    sourceSize.width: 30
                    sourceSize.height: 30
                }
            }

            Label {
                Layout.fillWidth: true
                text: "SMV Injector"
                color: bar.theme.text
                font.family: bar.uiFont
                font.pixelSize: 11
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
        }

        Rectangle { width: 1; height: 24; color: bar.theme.lineSoft }

        RibbonAction {
            theme: bar.theme
            uiFont: bar.uiFont
            text: "Start"
            tone: bar.session.startReady ? "success" : "neutral"
            iconSource: Qt.resolvedUrl("../assets/lucide/play.svg")
            enabled: !bar.workflow.injectionRunning && !bar.session.updatingFirmware && !bar.session.updateNeedsBootloaderHelp
            toolTipText: bar.workflow.startReason()
            onClicked: bar.workflow.requestStart()
        }

        RibbonAction {
            theme: bar.theme
            uiFont: bar.uiFont
            text: "Stop"
            tone: bar.workflow.injectionRunning ? "danger" : "neutral"
            iconSource: Qt.resolvedUrl("../assets/lucide/square.svg")
            enabled: bar.workflow.injectionRunning && !bar.session.updatingFirmware
            toolTipText: "Stop Sampled Values output"
            onClicked: bar.workflow.requestStop()
        }

        Rectangle { width: 1; height: 24; color: bar.theme.lineSoft }

        RibbonAction {
            theme: bar.theme
            uiFont: bar.uiFont
            text: bar.compact ? "SCL" : "Open SCL"
            iconSource: Qt.resolvedUrl("../assets/lucide/folder-open.svg")
            enabled: bar.session.engineeringEditable
            toolTipText: "Open IEC 61850 engineering configuration"
            onClicked: bar.workflow.openEngineeringDialog()
        }

        RibbonAction {
            visible: !bar.compact
            theme: bar.theme
            uiFont: bar.uiFont
            text: "4I+4V"
            iconSource: Qt.resolvedUrl("../assets/lucide/panels-top-left.svg")
            enabled: bar.session.engineeringEditable
            toolTipText: "Use the built-in 4I + 4V / 4000 samples/s profile"
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
            checkable: true
            checked: bar.controller.phasorDockVisible || bar.controller.phasorDetached
            tone: checked ? "accent" : "neutral"
            toolTipText: "Show or hide phasor preview"
            onClicked: {
                if (bar.controller.phasorDetached) bar.controller.phasorDetached = false
                bar.controller.phasorDockVisible = checked
            }
        }

        RibbonAction {
            theme: bar.theme
            uiFont: bar.uiFont
            text: "Wave"
            visible: !bar.compact
            checkable: true
            checked: bar.controller.waveformDockVisible || bar.controller.waveformDetached
            tone: checked ? "accent" : "neutral"
            toolTipText: "Show or hide waveform preview"
            onClicked: {
                if (bar.controller.waveformDetached) bar.controller.waveformDetached = false
                bar.controller.waveformDockVisible = checked
            }
        }

        RibbonAction {
            theme: bar.theme
            uiFont: bar.uiFont
            text: bar.compact ? "" : "Advanced"
            iconOnly: bar.compact
            iconSource: Qt.resolvedUrl("../assets/lucide/settings-2.svg")
            tone: "neutral"
            toolTipText: bar.session.firmwareUpdateAvailable
                ? "Advanced controls · optional firmware update available"
                : "Advanced device, firmware, waveform and timing controls"
            onClicked: bar.controller.openConfiguration()
        }

        Rectangle {
            Layout.preferredWidth: bar.compact ? 92 : 112
            Layout.preferredHeight: 30
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
            ToolTip.text: bar.device.deviceVerified
                ? bar.device.portName + " · ESP32-P4\n" + bar.session.statusText
                : bar.session.statusText
            ToolTip.delay: 420
        }
    }
}
