// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SurfacePanel {
    id: panel
    property var device
    property string uiFont: "Inter"
    property string monoFont: "Inter"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Label { text: "Device protocol"; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 18; font.weight: Font.DemiBold }
        Label {
            Layout.fillWidth: true
            text: "The normal workflow discovers and verifies the injector automatically. Manual serial selection lives here only for commissioning and recovery."
            wrapMode: Text.WordWrap
            color: panel.theme.textSoft
            font.family: panel.uiFont
            font.pixelSize: 10
            lineHeight: 1.35
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 104
            radius: 8
            color: panel.theme.surface2
            border.width: 1
            border.color: panel.device.deviceVerified ? "#2c674e" : panel.theme.lineSoft
            GridLayout {
                anchors.fill: parent
                anchors.margins: 12
                columns: 2
                columnSpacing: 18
                rowSpacing: 7
                Label { text: "Product"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                Label { text: panel.device.deviceVerified ? panel.device.deviceProduct : "Not identified"; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 9; font.weight: Font.DemiBold }
                Label { text: "Hardware / ID"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                Label { text: panel.device.deviceVerified ? "ESP32-P4 / " + panel.device.deviceId : "-"; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 9 }
                Label { text: "GUI protocol"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                Label { text: panel.device.deviceVerified ? "v" + panel.device.protocolVersion : "-"; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 9 }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: panel.theme.lineSoft }
        Label { text: "MANUAL RECOVERY"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 7; font.weight: Font.Bold; font.letterSpacing: 0.9 }

        RowLayout {
            Layout.fillWidth: true
            ComboBox {
                id: recoveryPort
                Layout.preferredWidth: 170
                model: panel.device.ports
                enabled: !panel.device.connected
                font.family: panel.uiFont
                font.pixelSize: 9
                onPressedChanged: if (pressed) panel.device.refreshPorts()
            }
            CalmButton { theme: panel.theme; uiFont: panel.uiFont; text: "Refresh"; onClicked: panel.device.refreshPorts() }
            CalmButton {
                theme: panel.theme
                uiFont: panel.uiFont
                tone: panel.device.connected ? "danger" : "accent"
                text: panel.device.connected ? "Disconnect" : "Verify selected port"
                onClicked: panel.device.connected ? panel.device.disconnectPort() : panel.device.connectPort(recoveryPort.currentText)
            }
            Item { Layout.fillWidth: true }
        }

        Label {
            Layout.fillWidth: true
            text: panel.device.discoveryStatus
            color: panel.theme.muted
            font.family: panel.uiFont
            font.pixelSize: 9
            wrapMode: Text.WordWrap
        }
        Item { Layout.fillHeight: true }
    }
}
