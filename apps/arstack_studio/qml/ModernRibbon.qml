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

    implicitHeight: 54
    radius: 9
    color: theme.chrome
    border.width: 1
    border.color: theme.lineSoft

    function reconnectOrOpenDevice() {
        if (device.deviceVerified)
            controller.openConfiguration()
        else
            session.requestConnect()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 8

        // Product identity is intentionally compact. The command bar is a tool,
        // not a second title bar.
        RowLayout {
            Layout.preferredWidth: bar.compact ? 132 : 158
            Layout.alignment: Qt.AlignVCenter
            spacing: 8

            Rectangle {
                width: 30
                height: 30
                radius: 7
                color: "#0f1821"
                border.width: 1
                border.color: "#27445d"
                Image {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    source: Qt.resolvedUrl("../assets/lucide/radio-tower.svg")
                    sourceSize.width: 32
                    sourceSize.height: 32
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: -1
                Label {
                    Layout.fillWidth: true
                    text: "ARStack"
                    color: bar.theme.text
                    font.family: bar.uiFont
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: "SMV + PTP"
                    color: bar.theme.muted
                    font.family: bar.uiFont
                    font.pixelSize: 8
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
            }
        }

        Rectangle { width: 1; height: 26; color: bar.theme.lineSoft }

        // Primary operator actions. These are the only visually dominant controls.
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

        Rectangle { width: 1; height: 26; color: bar.theme.lineSoft }

        // Engineering actions remain available, but no longer compete with Start/Stop.
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
            text: "Default 4I+4V"
            iconSource: Qt.resolvedUrl("../assets/lucide/panels-top-left.svg")
            enabled: bar.session.engineeringEditable
            toolTipText: "Use the built-in 4I + 4V / 4000 samples/s profile"
            onClicked: bar.workflow.useBuiltInProfile()
        }

        RibbonAction {
            visible: bar.session.firmwareUpdateRequired || bar.session.firmwareUpdateAvailable ||
                     bar.session.firmwareInstallRequired || bar.session.profileSyncRetryAvailable
            theme: bar.theme
            uiFont: bar.uiFont
            tone: "warning"
            text: bar.session.firmwareInstallRequired ? "Install firmware"
                : bar.session.firmwareUpdateRequired ? "Firmware required"
                : bar.session.firmwareUpdateAvailable ? "Update available"
                : "Retry profile"
            toolTipText: bar.session.firmwareInstallRequired ? "Install the bundled ARStack firmware"
                : bar.session.firmwareUpdateRequired ? "Firmware update is required before injection"
                : bar.session.firmwareUpdateAvailable ? "A compatible newer firmware build is available"
                : "Retry the 4I + 4V profile synchronization"
            onClicked: {
                if (bar.session.firmwareInstallRequired) bar.workflow.openInstallPrompt()
                else if (bar.session.firmwareUpdateRequired || bar.session.firmwareUpdateAvailable) bar.workflow.openUpdatePrompt()
                else bar.workflow.retryProfileSync()
            }
        }

        Item { Layout.fillWidth: true; Layout.minimumWidth: 8 }

        // Secondary view controls are deliberately quiet and compact.
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
            text: "Waveform"
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
            text: "Advanced"
            iconOnly: bar.compact
            iconSource: Qt.resolvedUrl("../assets/lucide/settings-2.svg")
            toolTipText: "Advanced device, firmware, waveform and timing controls"
            onClicked: bar.controller.openConfiguration()
        }

        Rectangle {
            Layout.preferredWidth: bar.compact ? 122 : 148
            Layout.preferredHeight: 34
            Layout.alignment: Qt.AlignVCenter
            radius: 17
            color: "#0b1117"
            border.width: 1
            border.color: Qt.rgba(bar.workflow.smartStateColor.r,
                                  bar.workflow.smartStateColor.g,
                                  bar.workflow.smartStateColor.b, 0.65)

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 7

                Rectangle {
                    width: 7
                    height: 7
                    radius: 4
                    color: bar.workflow.smartStateColor
                }

                Label {
                    Layout.fillWidth: true
                    text: bar.workflow.displayState
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
