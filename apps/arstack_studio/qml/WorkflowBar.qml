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
    property int activeTab: 0

    implicitHeight: 94
    color: theme.raised
    border.color: theme.line

    component RibbonTab: TabButton {
        implicitWidth: 104
        implicitHeight: 32
        font.family: ribbon.uiFont
        font.pixelSize: 10
        font.weight: checked ? Font.DemiBold : Font.Medium
        contentItem: Label {
            text: parent.text
            color: parent.checked ? ribbon.theme.text : ribbon.theme.muted
            font: parent.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: parent.checked ? ribbon.theme.surface2 : "transparent"
            radius: 6
            Rectangle {
                visible: parent.parent.checked
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                height: 2
                radius: 1
                color: ribbon.theme.accent
            }
        }
    }

    component RibbonAction: CalmButton {
        theme: ribbon.theme
        uiFont: ribbon.uiFont
        implicitHeight: 42
        iconSize: 16
    }

    component RibbonDivider: Rectangle {
        width: 1
        height: 32
        color: ribbon.theme.lineSoft
        Layout.alignment: Qt.AlignVCenter
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 33
            Layout.leftMargin: 9
            Layout.rightMargin: 9
            spacing: 2

            RibbonTab { text: "Home"; checked: ribbon.activeTab === 0; onClicked: ribbon.activeTab = 0 }
            RibbonTab { text: "View"; checked: ribbon.activeTab === 1; onClicked: ribbon.activeTab = 1 }
            RibbonTab { text: "Engineering"; checked: ribbon.activeTab === 2; onClicked: ribbon.activeTab = 2 }
            Item { Layout.fillWidth: true }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: ribbon.theme.lineSoft }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: ribbon.activeTab

            RowLayout {
                Layout.leftMargin: 11
                Layout.rightMargin: 11
                spacing: 7

                RibbonAction {
                    text: "Balanced"
                    iconSource: Qt.resolvedUrl("../assets/lucide/scale.svg")
                    toolTipText: "Apply a balanced three-phase setpoint"
                    onClicked: ribbon.controller.balanced()
                }
                RibbonAction {
                    text: "Zero"
                    iconSource: Qt.resolvedUrl("../assets/lucide/circle-off.svg")
                    toolTipText: "Set all magnitudes to zero"
                    onClicked: ribbon.controller.zeroAll()
                }
                RibbonAction {
                    visible: !ribbon.compact
                    text: "CT Saturation"
                    iconSource: Qt.resolvedUrl("../assets/lucide/activity.svg")
                    tone: ribbon.controller.ctSaturationEnabled ? "accent" : "neutral"
                    enabled: ribbon.controller.signalFrequency > 0
                    toolTipText: ribbon.controller.ctSaturationEnabled ? "Disable CT saturation stress" : "Enable CT saturation stress"
                    onClicked: ribbon.controller.setCtSaturation(!ribbon.controller.ctSaturationEnabled)
                }
                RibbonDivider {}
                RibbonAction {
                    visible: !ribbon.compact
                    text: "Check"
                    iconSource: Qt.resolvedUrl("../assets/lucide/circle-check.svg")
                    toolTipText: "Run readiness checks"
                    onClicked: ribbon.controller.runReadinessCheck()
                }
                RibbonAction {
                    text: "Configure"
                    iconSource: Qt.resolvedUrl("../assets/lucide/settings-2.svg")
                    toolTipText: "Open Smart and Expert configuration"
                    onClicked: ribbon.controller.openConfiguration()
                }
                Item { Layout.fillWidth: true }
                RibbonDivider {}
                RibbonAction {
                    visible: !ribbon.compact
                    tone: "accent"
                    text: ribbon.device.profileDeploying ? "Deploying..." : "Deploy"
                    iconSource: Qt.resolvedUrl("../assets/lucide/upload.svg")
                    enabled: ribbon.controller.canDeploy
                    toolTipText: "Deploy the validated SV profile"
                    onClicked: ribbon.controller.deploySelectedProfile()
                }
                RibbonAction {
                    tone: "danger"
                    text: "Stop"
                    iconSource: Qt.resolvedUrl("../assets/lucide/square.svg")
                    implicitWidth: 78
                    enabled: ribbon.device.deviceVerified && ribbon.device.running
                    onClicked: ribbon.device.stop()
                }
                RibbonAction {
                    tone: "success"
                    text: "Start"
                    iconSource: Qt.resolvedUrl("../assets/lucide/play.svg")
                    implicitWidth: 88
                    enabled: ribbon.controller.canStart
                    onClicked: ribbon.device.start()
                }
            }

            RowLayout {
                Layout.leftMargin: 11
                Layout.rightMargin: 11
                spacing: 7

                RibbonAction {
                    text: "Phasor"
                    iconSource: Qt.resolvedUrl("../assets/lucide/panels-top-left.svg")
                    tone: ribbon.controller.phasorDockVisible || ribbon.controller.phasorDetached ? "accent" : "neutral"
                    onClicked: {
                        ribbon.controller.phasorDetached = false
                        ribbon.controller.phasorDockVisible = !ribbon.controller.phasorDockVisible
                    }
                }
                RibbonAction {
                    text: "Waveform"
                    iconSource: Qt.resolvedUrl("../assets/lucide/activity.svg")
                    tone: ribbon.controller.waveformDockVisible || ribbon.controller.waveformDetached ? "accent" : "neutral"
                    onClicked: {
                        ribbon.controller.waveformDetached = false
                        ribbon.controller.waveformDockVisible = !ribbon.controller.waveformDockVisible
                    }
                }
                RibbonAction {
                    text: "Monitor"
                    iconSource: Qt.resolvedUrl("../assets/lucide/panels-top-left.svg")
                    tone: ribbon.controller.telemetryDockVisible ? "accent" : "neutral"
                    onClicked: ribbon.controller.telemetryDockVisible = !ribbon.controller.telemetryDockVisible
                }
                RibbonDivider {}
                RibbonAction {
                    visible: !ribbon.compact
                    text: "Detach phasor"
                    iconSource: Qt.resolvedUrl("../assets/lucide/external-link.svg")
                    enabled: ribbon.controller.phasorDockVisible && !ribbon.controller.phasorDetached
                    onClicked: ribbon.controller.detachPhasor()
                }
                RibbonAction {
                    visible: !ribbon.compact
                    text: "Detach waveform"
                    iconSource: Qt.resolvedUrl("../assets/lucide/external-link.svg")
                    enabled: ribbon.controller.waveformDockVisible && !ribbon.controller.waveformDetached
                    onClicked: ribbon.controller.detachWaveform()
                }
                Item { Layout.fillWidth: true }
                RibbonAction {
                    text: ribbon.controller.telemetryExpanded ? "Collapse" : "Expand"
                    iconSource: ribbon.controller.telemetryExpanded
                        ? Qt.resolvedUrl("../assets/lucide/chevron-down.svg")
                        : Qt.resolvedUrl("../assets/lucide/chevron-up.svg")
                    onClicked: ribbon.controller.telemetryExpanded = !ribbon.controller.telemetryExpanded
                }
            }

            RowLayout {
                Layout.leftMargin: 11
                Layout.rightMargin: 11
                spacing: 7

                RibbonAction {
                    text: "Configuration"
                    iconSource: Qt.resolvedUrl("../assets/lucide/settings-2.svg")
                    onClicked: ribbon.controller.openConfiguration()
                }
                RibbonAction {
                    text: "Open SCL"
                    iconSource: Qt.resolvedUrl("../assets/lucide/folder-open.svg")
                    onClicked: ribbon.controller.openEngineeringFile()
                }
                RibbonAction {
                    text: "Detect device"
                    iconSource: Qt.resolvedUrl("../assets/lucide/scan-search.svg")
                    enabled: !ribbon.device.connected
                    onClicked: ribbon.device.autoDetectAndConnect()
                }
                RibbonAction {
                    text: "Refresh PTP"
                    iconSource: Qt.resolvedUrl("../assets/lucide/clock-3.svg")
                    enabled: ribbon.device.deviceVerified
                    onClicked: ribbon.device.sendPtpShow()
                }
                Item { Layout.fillWidth: true }
                RibbonAction {
                    text: "Diagnostics"
                    iconSource: Qt.resolvedUrl("../assets/lucide/activity.svg")
                    onClicked: ribbon.controller.openDiagnostics()
                }
            }
        }
    }
}
