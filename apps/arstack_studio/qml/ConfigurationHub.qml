// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ARStack.Studio 1.0

Item {
    id: hub
    property var theme
    property var controller
    property var device
    property var profiles
    property string uiFont: "Inter"
    property string monoFont: "Inter"

    function openEngineeringFile() { profileInspector.openEngineeringFile() }
    function showRecovery() { expertTabs.currentIndex = 1 }

    Component.onCompleted: {
        // Recovery is the only advanced page an unconfigured board should need.
        if (!hub.device.deviceVerified)
            expertTabs.currentIndex = 1
    }

    component ExpertTab: TabButton {
        implicitHeight: 42
        font.family: hub.uiFont
        font.pixelSize: 11
        font.weight: checked ? Font.DemiBold : Font.Medium
        contentItem: Label {
            text: parent.text
            color: parent.checked ? hub.theme.text : hub.theme.muted
            font: parent.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: parent.checked ? hub.theme.raised : "transparent"
            radius: 6
            border.width: parent.checked ? 1 : 0
            border.color: hub.theme.line
            Rectangle {
                visible: parent.parent.checked
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 26
                anchors.rightMargin: 26
                height: 2
                color: hub.theme.accent
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.topMargin: 4
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    text: "Advanced settings"
                    color: hub.theme.text
                    font.family: hub.uiFont
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
                Label {
                    text: "Firmware recovery, engineering profile, waveform and timing tools"
                    color: hub.theme.muted
                    font.family: hub.uiFont
                    font.pixelSize: 11
                }
            }

            Rectangle {
                implicitWidth: 112
                implicitHeight: 30
                radius: 15
                color: hub.device.deviceVerified ? "#10251d" : "#261f13"
                border.width: 1
                border.color: hub.device.deviceVerified ? "#347a59" : "#705827"

                RowLayout {
                    anchors.centerIn: parent
                    spacing: 6
                    Rectangle {
                        width: 7
                        height: 7
                        radius: 4
                        color: hub.device.deviceVerified ? hub.theme.green : hub.theme.amber
                    }
                    Label {
                        text: hub.device.deviceVerified ? "Device ready" : "Setup mode"
                        color: hub.device.deviceVerified ? hub.theme.green : hub.theme.amber
                        font.family: hub.uiFont
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }
                }
            }
        }

        TabBar {
            id: expertTabs
            Layout.fillWidth: true
            implicitHeight: 42
            spacing: 4
            background: Rectangle {
                color: hub.theme.surface2
                radius: 8
                border.width: 1
                border.color: hub.theme.lineSoft
            }
            ExpertTab { text: "Injection" }
            ExpertTab { text: "Firmware" }
            ExpertTab { text: "Waveform" }
            ExpertTab { text: "Timing" }
            ExpertTab { text: "Device" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: expertTabs.currentIndex

            ProfileInspector {
                id: profileInspector
                theme: hub.theme
                controller: hub.controller
                device: hub.device
                profiles: hub.profiles
                uiFont: hub.uiFont
                monoFont: hub.monoFont
                compact: false
            }
            FirmwarePanel {
                theme: hub.theme
                device: hub.device
                firmware: FirmwareService
                uiFont: hub.uiFont
                monoFont: hub.monoFont
            }
            WaveformExpertPanel {
                theme: hub.theme
                controller: hub.controller
                uiFont: hub.uiFont
                monoFont: hub.monoFont
            }
            PtpExpertPanel {
                theme: hub.theme
                device: hub.device
                uiFont: hub.uiFont
                monoFont: hub.monoFont
            }
            DeviceExpertPanel {
                theme: hub.theme
                device: hub.device
                uiFont: hub.uiFont
                monoFont: hub.monoFont
            }
        }
    }
}
