// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: hub
    property var theme
    property var controller
    property var device
    property var profiles
    property string uiFont: "Inter"
    property string monoFont: "Inter"

    function openEngineeringFile() { profileInspector.openEngineeringFile() }

    component ExpertTab: TabButton {
        implicitHeight: 36
        font.family: hub.uiFont
        font.pixelSize: 9
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
                anchors.leftMargin: 28
                anchors.rightMargin: 28
                height: 2
                color: hub.theme.accent
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 4
            Layout.rightMargin: 4
            ColumnLayout {
                spacing: 0
                Label { text: "EXPERT CONFIGURATION"; color: hub.theme.muted; font.family: hub.uiFont; font.pixelSize: 7; font.weight: Font.Bold; font.letterSpacing: 0.9 }
                Label { text: "Protocol & timing setup"; color: hub.theme.text; font.family: hub.uiFont; font.pixelSize: 14; font.weight: Font.DemiBold }
            }
            Item { Layout.fillWidth: true }
            Label {
                text: hub.device.deviceVerified
                    ? "ESP32-P4 · ID " + hub.device.deviceId.slice(-6)
                    : "Offline editing"
                color: hub.device.deviceVerified ? hub.theme.green : hub.theme.muted
                font.family: hub.uiFont
                font.pixelSize: 8
                font.weight: Font.DemiBold
            }
        }

        TabBar {
            id: expertTabs
            Layout.fillWidth: true
            implicitHeight: 36
            background: Rectangle { color: hub.theme.surface2; radius: 6 }
            ExpertTab { text: "SV Profile" }
            ExpertTab { text: "Waveform" }
            ExpertTab { text: "PTP Lab" }
            ExpertTab { text: "Device Protocol" }
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
