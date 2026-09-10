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

    FirmwareManager { id: firmwareManager }

    function openEngineeringFile() { profileInspector.openEngineeringFile() }
    function showRecovery() { expertTabs.currentIndex = 1 }

    Component.onCompleted: {
        // A blank/old-firmware board cannot identify as an ARStack injector yet.
        // Make recovery the first screen operators see while offline.
        if (!hub.device.deviceVerified)
            expertTabs.currentIndex = 1
    }

    component ExpertTab: TabButton {
        implicitHeight: 38
        font.family: hub.uiFont
        font.pixelSize: 10
        font.weight: checked ? Font.DemiBold : Font.Medium
        contentItem: Label {
            text: parent.text
            color: parent.checked ? hub.theme.text : hub.theme.muted
            font: parent.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: parent.checked ? hub.theme.raised : hub.theme.surface2
            border.width: 1
            border.color: parent.checked ? hub.theme.line : hub.theme.lineSoft
            Rectangle {
                visible: parent.parent.checked
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                height: 2
                color: hub.theme.accent
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 6
            Layout.rightMargin: 6
            spacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Label {
                    text: "SETUP & RECOVERY"
                    color: hub.theme.muted
                    font.family: hub.uiFont
                    font.pixelSize: 8
                    font.weight: Font.Bold
                    font.letterSpacing: 0.9
                }
                Label {
                    text: "Device, firmware & engineering"
                    color: hub.theme.text
                    font.family: hub.uiFont
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }
            }
            Label {
                text: hub.device.deviceVerified
                    ? "ESP32-P4 · protocol v" + hub.device.protocolVersion + " · ID " + hub.device.deviceId.slice(-6)
                    : "Recovery available"
                color: hub.device.deviceVerified ? hub.theme.green : hub.theme.amber
                font.family: hub.uiFont
                font.pixelSize: 9
                font.weight: Font.DemiBold
            }
        }

        TabBar {
            id: expertTabs
            Layout.fillWidth: true
            implicitHeight: 38
            background: Rectangle { color: hub.theme.surface2; radius: 7 }
            ExpertTab { text: "SV Setup" }
            ExpertTab { text: "Firmware" }
            ExpertTab { text: "Waveform" }
            ExpertTab { text: "PTP Lab" }
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
                firmware: firmwareManager
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
