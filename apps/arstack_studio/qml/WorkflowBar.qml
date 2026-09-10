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
    readonly property bool p0FirmwareCompatible: device.deviceVerified && device.protocolVersion === "1"
    readonly property bool deployReady: controller.canDeploy && p0FirmwareCompatible
    readonly property bool startReady:
        p0FirmwareCompatible && profiles.hasProfiles && controller.selectedProfileDeployable &&
        device.profileArmed && !controller.profileDirty && !device.running && !device.profileDeploying

    function deployReason() {
        if (device.running) return "Stop SMV output before deploying a profile."
        if (device.profileDeploying) return "Profile deployment is already in progress."
        if (!device.deviceVerified) return "Install or recover ARStack firmware, then verify the ESP32-P4."
        if (!p0FirmwareCompatible) return "Firmware protocol mismatch. Open Firmware setup to install the P0 firmware."
        if (!profiles.hasProfiles) return "Load 4I+4V Quick Start or a compatible SCL first."
        if (!controller.selectedProfileDeployable) return "The selected SCL stream is outside the P0 4I+4V device boundary."
        return "Deploy the validated 4I+4V SV profile to the injector."
    }

    function startReason() {
        if (device.running) return "SMV output is already running."
        if (!device.deviceVerified) return "Install or recover ARStack firmware first."
        if (!p0FirmwareCompatible) return "Firmware protocol mismatch. Install the P0 firmware first."
        if (!profiles.hasProfiles) return "Load 4I+4V Quick Start or a compatible SCL first."
        if (!controller.selectedProfileDeployable) return "Selected profile is not deployable on the P0 4I+4V runtime."
        if (controller.profileDirty) return "Profile changed. Deploy it again before Start."
        if (!device.profileArmed) return "Deploy the validated profile before Start."
        if (device.profileDeploying) return "Wait for profile deployment to finish."
        return "Start 4I+4V Sampled Values output."
    }

    implicitHeight: 94
    color: theme.surface2
    border.color: theme.line

    component RibbonTab: TabButton {
        implicitWidth: text === "Engineering" ? 112 : 88
        implicitHeight: 31
        hoverEnabled: true
        font.family: ribbon.uiFont
        font.pixelSize: 10
        font.weight: checked ? Font.DemiBold : Font.Medium
        contentItem: Label {
            text: parent.text
            color: parent.checked ? ribbon.theme.text : (parent.hovered ? ribbon.theme.textSoft : ribbon.theme.muted)
            font: parent.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            Behavior on color { ColorAnimation { duration: 90 } }
        }
        background: Rectangle {
            color: parent.hovered || parent.checked ? "#121b25" : "transparent"
            radius: 6
            Behavior on color { ColorAnimation { duration: 90 } }
            Rectangle {
                visible: parent.parent.checked
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                height: 2
                radius: 1
                color: ribbon.theme.accent
            }
        }
    }

    component RibbonAction: CalmButton {
        theme: ribbon.theme
        uiFont: ribbon.uiFont
        implicitHeight: 40
        iconSize: 15
        font.pixelSize: 10
    }

    component RibbonDivider: Rectangle {
        width: 1
        height: 28
        color: ribbon.theme.lineSoft
        Layout.alignment: Qt.AlignVCenter
    }

    component RailLabel: Label {
        color: ribbon.theme.muted
        font.family: ribbon.uiFont
        font.pixelSize: 7
        font.weight: Font.DemiBold
        font.letterSpacing: 0.35
        verticalAlignment: Text.AlignVCenter
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            Layout.leftMargin: 9
            Layout.rightMargin: 9
            spacing: 2

            RibbonTab { text: "Home"; checked: ribbon.activeTab === 0; onClicked: ribbon.activeTab = 0 }
            RibbonTab { text: "View"; checked: ribbon.activeTab === 1; onClicked: ribbon.activeTab = 1 }
            RibbonTab { text: "Engineering"; checked: ribbon.activeTab === 2; onClicked: ribbon.activeTab = 2 }
            Item { Layout.fillWidth: true }

            RowLayout {
                visible: !ribbon.compact
                spacing: 9
                RailLabel {
                    text: ribbon.device.deviceVerified ? "DEVICE · VERIFIED" : "DEVICE · SETUP"
                    color: ribbon.device.deviceVerified ? ribbon.theme.green : ribbon.theme.amber
                }
                Rectangle { width: 1; height: 11; color: ribbon.theme.lineSoft }
                RailLabel {
                    text: ribbon.device.deviceVerified ? "FW · P" + ribbon.device.protocolVersion : "FW · INSTALL"
                    color: ribbon.p0FirmwareCompatible ? ribbon.theme.green : ribbon.theme.amber
                }
                Rectangle { width: 1; height: 11; color: ribbon.theme.lineSoft }
                RailLabel {
                    text: ribbon.profiles.referenceTemplateActive ? "PROFILE · 4I+4V" : (ribbon.profiles.hasProfiles ? "PROFILE · SCL" : "PROFILE · —")
                    color: ribbon.profiles.hasProfiles ? ribbon.theme.textSoft : ribbon.theme.muted
                }
                Rectangle { width: 1; height: 11; color: ribbon.theme.lineSoft }
                RailLabel {
                    text: ribbon.device.running ? "OUTPUT · RUNNING" : (ribbon.device.profileArmed && !ribbon.controller.profileDirty ? "OUTPUT · ARMED" : "OUTPUT · SAFE")
                    color: ribbon.device.running ? ribbon.theme.green : (ribbon.device.profileArmed ? ribbon.theme.accent : ribbon.theme.muted)
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: ribbon.theme.lineSoft }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: ribbon.activeTab

            RowLayout {
                Layout.leftMargin: 10
                Layout.rightMargin: 10
                spacing: 6

                RibbonAction {
                    text: ribbon.profiles.referenceTemplateActive ? "4I+4V Ready" : "4I+4V Quick Start"
                    iconSource: Qt.resolvedUrl("../assets/lucide/radio-tower.svg")
                    tone: ribbon.profiles.referenceTemplateActive ? "accent" : "neutral"
                    enabled: !ribbon.device.running && !ribbon.device.profileDeploying
                    toolTipText: "Load the bundled ARStack 4I+4V 9-2LE reference profile at 4000 fps"
                    onClicked: {
                        if (ribbon.profiles.loadReferenceTemplate()) {
                            ribbon.controller.profileDirty = true
                            ribbon.controller.showMessage(
                                "4I+4V 9-2LE reference loaded · Class A · 4000 fps · smpCnt 0..3999.",
                                false)
                        } else {
                            ribbon.controller.showMessage(
                                ribbon.profiles.fatalError || "Unable to load the bundled reference profile.",
                                true)
                        }
                    }
                }

                RibbonDivider {}

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
                    text: ribbon.device.deviceVerified ? "Configuration" : "Install Firmware"
                    iconSource: Qt.resolvedUrl("../assets/lucide/upload.svg")
                    tone: ribbon.device.deviceVerified ? "neutral" : "accent"
                    toolTipText: ribbon.device.deviceVerified
                        ? "Open profile, firmware and expert configuration"
                        : "Open guided firmware setup for a blank or old-firmware ESP32-P4"
                    onClicked: ribbon.controller.openConfiguration()
                }

                Item { Layout.fillWidth: true }

                RibbonDivider {}

                RibbonAction {
                    visible: !ribbon.compact
                    text: ribbon.device.profileDeploying ? "Deploying…" : "Deploy"
                    iconSource: Qt.resolvedUrl("../assets/lucide/upload.svg")
                    implicitWidth: 86
                    enabled: ribbon.deployReady
                    toolTipText: ribbon.deployReason()
                    onClicked: ribbon.controller.deploySelectedProfile()
                }
                RibbonAction {
                    tone: "danger"
                    text: "Stop"
                    iconSource: Qt.resolvedUrl("../assets/lucide/square.svg")
                    implicitWidth: 82
                    enabled: ribbon.device.deviceVerified && ribbon.device.running
                    toolTipText: ribbon.device.running ? "Stop SMV output" : "Output is already stopped"
                    onClicked: ribbon.device.stop()
                }
                RibbonAction {
                    tone: "success"
                    text: "Start"
                    iconSource: Qt.resolvedUrl("../assets/lucide/play.svg")
                    implicitWidth: 92
                    enabled: ribbon.startReady
                    toolTipText: ribbon.startReason()
                    onClicked: ribbon.device.start()
                }
            }

            RowLayout {
                Layout.leftMargin: 10
                Layout.rightMargin: 10
                spacing: 6

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
                    text: ribbon.controller.telemetryExpanded ? "Collapse monitor" : "Expand monitor"
                    iconSource: ribbon.controller.telemetryExpanded
                        ? Qt.resolvedUrl("../assets/lucide/chevron-down.svg")
                        : Qt.resolvedUrl("../assets/lucide/chevron-up.svg")
                    onClicked: ribbon.controller.telemetryExpanded = !ribbon.controller.telemetryExpanded
                }
            }

            RowLayout {
                Layout.leftMargin: 10
                Layout.rightMargin: 10
                spacing: 6

                RibbonAction {
                    text: ribbon.device.deviceVerified ? "Configuration" : "Firmware Setup"
                    iconSource: Qt.resolvedUrl("../assets/lucide/upload.svg")
                    tone: ribbon.device.deviceVerified ? "neutral" : "accent"
                    onClicked: ribbon.controller.openConfiguration()
                }
                RibbonAction {
                    text: "Open SCL"
                    iconSource: Qt.resolvedUrl("../assets/lucide/folder-open.svg")
                    onClicked: ribbon.controller.openEngineeringFile()
                }
                RibbonAction {
                    text: "4I+4V Reference"
                    iconSource: Qt.resolvedUrl("../assets/lucide/radio-tower.svg")
                    tone: ribbon.profiles.referenceTemplateActive ? "accent" : "neutral"
                    enabled: !ribbon.device.running && !ribbon.device.profileDeploying
                    onClicked: {
                        if (ribbon.profiles.loadReferenceTemplate()) {
                            ribbon.controller.profileDirty = true
                            ribbon.controller.showMessage("ARStack 4I+4V reference engineering profile loaded.", false)
                        } else {
                            ribbon.controller.showMessage(ribbon.profiles.fatalError, true)
                        }
                    }
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
