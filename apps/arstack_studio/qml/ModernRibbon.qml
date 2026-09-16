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
    property int currentTab: 0

    implicitHeight: compact ? 70 : 96
    color: theme.surface2
    border.width: 1
    border.color: theme.line

    function chooseTab(index) { currentTab = index }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 2

            Repeater {
                model: ["Home", "Injection", "View", "Device"]
                delegate: Button {
                    required property int index
                    required property string modelData
                    implicitHeight: 28
                    implicitWidth: Math.max(70, tabText.implicitWidth + 24)
                    hoverEnabled: true
                    onClicked: bar.chooseTab(index)
                    contentItem: Text {
                        id: tabText
                        text: modelData
                        color: bar.currentTab === index ? bar.theme.text : bar.theme.muted
                        font.family: bar.uiFont
                        font.pixelSize: 10
                        font.weight: bar.currentTab === index ? Font.DemiBold : Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 5
                        color: parent.hovered ? bar.theme.raisedHover : "transparent"
                        Rectangle {
                            visible: bar.currentTab === index
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 2
                            radius: 1
                            color: bar.theme.accent
                        }
                    }
                }
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                implicitWidth: Math.max(142, stateText.implicitWidth + 34)
                implicitHeight: 24
                radius: 6
                color: "#0d141c"
                border.width: 1
                border.color: bar.workflow.smartStateColor
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 9
                    anchors.rightMargin: 9
                    spacing: 7
                    Rectangle { width: 7; height: 7; radius: 4; color: bar.workflow.smartStateColor }
                    Label {
                        id: stateText
                        Layout.fillWidth: true
                        text: bar.workflow.displayState
                        color: bar.workflow.smartStateColor
                        font.family: bar.uiFont
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: bar.theme.lineSoft }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 0
                visible: bar.currentTab === 0

                RibbonGroup {
                    theme: bar.theme; uiFont: bar.uiFont; title: "Session"
                    RibbonAction {
                        theme: bar.theme; uiFont: bar.uiFont; large: !bar.compact
                        text: "Start"; tone: bar.session.startReady ? "success" : "neutral"
                        iconSource: Qt.resolvedUrl("../assets/lucide/play.svg")
                        enabled: !bar.workflow.injectionRunning && !bar.session.updatingFirmware && !bar.session.updateNeedsBootloaderHelp
                        toolTipText: bar.workflow.startReason()
                        onClicked: bar.workflow.requestStart()
                    }
                    RibbonAction {
                        theme: bar.theme; uiFont: bar.uiFont; large: !bar.compact
                        text: "Stop"; tone: bar.workflow.injectionRunning ? "danger" : "neutral"
                        iconSource: Qt.resolvedUrl("../assets/lucide/square.svg")
                        enabled: bar.workflow.injectionRunning && !bar.session.updatingFirmware
                        toolTipText: "Stop Sampled Values output"
                        onClicked: bar.workflow.requestStop()
                    }
                }

                RibbonGroup {
                    theme: bar.theme; uiFont: bar.uiFont; title: "Engineering"
                    RibbonAction {
                        theme: bar.theme; uiFont: bar.uiFont; text: "Open file"
                        iconSource: Qt.resolvedUrl("../assets/lucide/folder-open.svg")
                        enabled: bar.session.engineeringEditable
                        toolTipText: "Open IEC 61850 engineering configuration"
                        onClicked: bar.workflow.openEngineeringDialog()
                    }
                    RibbonAction {
                        theme: bar.theme; uiFont: bar.uiFont; text: "4I + 4V"
                        iconSource: Qt.resolvedUrl("../assets/lucide/panels-top-left.svg")
                        enabled: bar.session.engineeringEditable
                        toolTipText: "Use built-in 4I + 4V / 4000 fps profile"
                        onClicked: bar.workflow.useBuiltInProfile()
                    }
                }

                RibbonGroup {
                    visible: bar.session.firmwareUpdateRequired || bar.session.firmwareUpdateAvailable || bar.session.firmwareInstallRequired || bar.session.profileSyncRetryAvailable
                    theme: bar.theme; uiFont: bar.uiFont; title: "Attention"
                    RibbonAction {
                        visible: bar.session.firmwareUpdateRequired || bar.session.firmwareUpdateAvailable
                        theme: bar.theme; uiFont: bar.uiFont
                        text: bar.session.firmwareUpdateRequired ? "Update firmware" : "Update available"
                        tone: bar.session.firmwareUpdateRequired ? "accent" : "neutral"
                        onClicked: bar.workflow.openUpdatePrompt()
                    }
                    RibbonAction {
                        visible: bar.session.firmwareInstallRequired
                        theme: bar.theme; uiFont: bar.uiFont; text: "Install firmware"; tone: "accent"
                        onClicked: bar.workflow.openInstallPrompt()
                    }
                    RibbonAction {
                        visible: bar.session.profileSyncRetryAvailable
                        theme: bar.theme; uiFont: bar.uiFont; text: "Retry profile"; tone: "accent"
                        onClicked: bar.workflow.retryProfileSync()
                    }
                }

                Item { Layout.fillWidth: true }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 0
                visible: bar.currentTab === 1

                RibbonGroup {
                    theme: bar.theme; uiFont: bar.uiFont; title: "Frequency"
                    RibbonAction { theme: bar.theme; uiFont: bar.uiFont; text: "0 Hz · DC"; onClicked: bar.controller.setFrequencyValue(0) }
                    RibbonAction { theme: bar.theme; uiFont: bar.uiFont; text: "50 Hz"; onClicked: bar.controller.setFrequencyValue(50) }
                    RibbonAction { theme: bar.theme; uiFont: bar.uiFont; text: "60 Hz"; onClicked: bar.controller.setFrequencyValue(60) }
                }

                RibbonGroup {
                    theme: bar.theme; uiFont: bar.uiFont; title: "Three phase"
                    RibbonAction {
                        theme: bar.theme; uiFont: bar.uiFont; text: "Balanced"
                        iconSource: Qt.resolvedUrl("../assets/lucide/scale.svg")
                        toolTipText: "Reset three-phase magnitudes and angles to the balanced reference"
                        onClicked: bar.controller.balanced()
                    }
                    RibbonAction {
                        theme: bar.theme; uiFont: bar.uiFont; text: "Zero all"
                        iconSource: Qt.resolvedUrl("../assets/lucide/circle-off.svg")
                        onClicked: bar.controller.zeroAll()
                    }
                    RibbonAction {
                        theme: bar.theme; uiFont: bar.uiFont; text: "Link phases"
                        checkable: true
                        checked: bar.controller.phaseLink
                        tone: checked ? "accent" : "neutral"
                        onClicked: bar.controller.phaseLink = checked
                    }
                }

                Item { Layout.fillWidth: true }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 0
                visible: bar.currentTab === 2

                RibbonGroup {
                    theme: bar.theme; uiFont: bar.uiFont; title: "Preview"
                    RibbonAction {
                        theme: bar.theme; uiFont: bar.uiFont; text: "Phasor"
                        checkable: true
                        checked: bar.controller.phasorDockVisible || bar.controller.phasorDetached
                        tone: checked ? "accent" : "neutral"
                        onClicked: {
                            if (bar.controller.phasorDetached) bar.controller.phasorDetached = false
                            bar.controller.phasorDockVisible = checked
                        }
                    }
                    RibbonAction {
                        theme: bar.theme; uiFont: bar.uiFont; text: "Waveform"
                        checkable: true
                        checked: bar.controller.waveformDockVisible || bar.controller.waveformDetached
                        tone: checked ? "accent" : "neutral"
                        onClicked: {
                            if (bar.controller.waveformDetached) bar.controller.waveformDetached = false
                            bar.controller.waveformDockVisible = checked
                        }
                    }
                    RibbonAction {
                        theme: bar.theme; uiFont: bar.uiFont; text: "Monitor"
                        checkable: true
                        checked: bar.controller.telemetryDockVisible
                        tone: checked ? "accent" : "neutral"
                        onClicked: bar.controller.telemetryDockVisible = checked
                    }
                }

                RibbonGroup {
                    theme: bar.theme; uiFont: bar.uiFont; title: "Layout"
                    RibbonAction {
                        theme: bar.theme; uiFont: bar.uiFont; text: "Reset layout"
                        iconSource: Qt.resolvedUrl("../assets/lucide/panels-top-left.svg")
                        onClicked: bar.workflow.resetDockLayout()
                    }
                }

                Item { Layout.fillWidth: true }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 0
                visible: bar.currentTab === 3

                RibbonGroup {
                    theme: bar.theme; uiFont: bar.uiFont; title: "Device"
                    RibbonAction {
                        theme: bar.theme; uiFont: bar.uiFont; text: "Advanced"
                        iconSource: Qt.resolvedUrl("../assets/lucide/settings-2.svg")
                        onClicked: bar.controller.openConfiguration()
                    }
                    RibbonAction {
                        theme: bar.theme; uiFont: bar.uiFont; text: "Diagnostics"
                        iconSource: Qt.resolvedUrl("../assets/lucide/scan-search.svg")
                        onClicked: bar.controller.openDiagnostics()
                    }
                }

                RibbonGroup {
                    theme: bar.theme; uiFont: bar.uiFont; title: "Connection"
                    RibbonAction {
                        theme: bar.theme; uiFont: bar.uiFont
                        text: bar.device.deviceVerified ? bar.device.portName : "Reconnect"
                        tone: bar.device.deviceVerified ? "success" : "accent"
                        toolTipText: bar.device.deviceVerified ? "Verified ARStack ESP32-P4 device" : "Run smart device discovery"
                        onClicked: if (!bar.device.deviceVerified) bar.session.requestConnect()
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }
    }
}
