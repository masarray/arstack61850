// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SurfacePanel {
    id: panel
    property var device
    property var firmware
    property string uiFont: "Inter"
    property string monoFont: "Inter"

    Timer {
        id: reconnectTimer
        interval: 1600
        repeat: false
        onTriggered: panel.device.autoDetectAndConnect()
    }

    Connections {
        target: panel.firmware
        function onInstallationFinished(resetSucceeded) {
            if (resetSucceeded)
                reconnectTimer.restart()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 1
                Label { text: "FIRMWARE MANAGER"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 7; font.weight: Font.Bold; font.letterSpacing: 0.9 }
                Label { text: "ESP32-P4-ETH install & recovery"; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 15; font.weight: Font.DemiBold }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                implicitWidth: 108
                implicitHeight: 24
                radius: 5
                color: panel.firmware.bundleReady ? "#112a20" : "#2a2112"
                border.width: 1
                border.color: panel.firmware.bundleReady ? "#2c674e" : "#705827"
                Label {
                    anchors.centerIn: parent
                    text: panel.firmware.bundleReady ? "BUNDLE VERIFIED" : "BUNDLE BLOCKED"
                    color: panel.firmware.bundleReady ? panel.theme.green : panel.theme.amber
                    font.family: panel.uiFont
                    font.pixelSize: 7
                    font.weight: Font.Bold
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: "Safe flow: verify the ROM target first, validate the bundled firmware SHA-256, flash only an ESP32-P4, reset, then let Studio verify ARSTACK IDENTIFY. No ESP-IDF or Python is required on the operator PC."
            wrapMode: Text.WordWrap
            color: panel.theme.textSoft
            font.family: panel.uiFont
            font.pixelSize: 9
            lineHeight: 1.35
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 92
            radius: 7
            color: panel.theme.surface2
            border.width: 1
            border.color: panel.theme.lineSoft
            GridLayout {
                anchors.fill: parent
                anchors.margins: 11
                columns: 4
                columnSpacing: 12
                rowSpacing: 6
                Label { text: "Package"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                Label { text: panel.firmware.firmwareVersion === "-" ? "Not installed" : "v" + panel.firmware.firmwareVersion; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 9; font.weight: Font.DemiBold }
                Label { text: "Protocol"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                Label { text: panel.firmware.expectedProtocol === "-" ? "-" : "v" + panel.firmware.expectedProtocol; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 9 }
                Label { text: "Target"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                Label { text: panel.firmware.targetChip; color: panel.firmware.targetVerified ? panel.theme.green : panel.theme.textSoft; font.family: panel.uiFont; font.pixelSize: 9; font.weight: Font.DemiBold }
                Label { text: "SHA-256"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                Label { text: panel.firmware.firmwareSha256.length ? panel.firmware.firmwareSha256.slice(0, 12) + "…" : "-"; color: panel.theme.textSoft; font.family: panel.monoFont; font.pixelSize: 8 }
            }
        }

        Label {
            Layout.fillWidth: true
            text: panel.firmware.bundleStatus
            color: panel.firmware.bundleReady ? panel.theme.green : panel.theme.amber
            font.family: panel.uiFont
            font.pixelSize: 8
            wrapMode: Text.WordWrap
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: panel.theme.lineSoft }

        RowLayout {
            Layout.fillWidth: true
            spacing: 7
            ComboBox {
                id: firmwarePort
                Layout.preferredWidth: 170
                model: panel.device.ports
                enabled: !panel.firmware.busy
                font.family: panel.uiFont
                font.pixelSize: 9
                onPressedChanged: if (pressed) panel.device.refreshPorts()
            }
            CalmButton {
                theme: panel.theme
                uiFont: panel.uiFont
                text: "Refresh ports"
                enabled: !panel.firmware.busy
                onClicked: panel.device.refreshPorts()
            }
            CalmButton {
                theme: panel.theme
                uiFont: panel.uiFont
                text: panel.firmware.busy ? "Working…" : "1 · Verify ESP32-P4"
                tone: panel.firmware.targetVerified ? "success" : "accent"
                enabled: !panel.firmware.busy && firmwarePort.currentText.length > 0
                onClicked: {
                    if (panel.device.connected)
                        panel.device.disconnectPort()
                    panel.firmware.probeTarget(firmwarePort.currentText)
                }
            }
            CalmButton {
                theme: panel.theme
                uiFont: panel.uiFont
                text: "2 · Install / Recover"
                tone: "success"
                enabled: panel.firmware.bundleReady && panel.firmware.targetVerified && !panel.firmware.busy && firmwarePort.currentText === panel.firmware.selectedPort
                toolTipText: enabled ? "Flash the verified ARStack merged firmware image" : "Verify the ESP32-P4 target and firmware bundle first"
                onClicked: panel.firmware.installFirmware(firmwarePort.currentText)
            }
            CalmButton {
                visible: panel.firmware.busy
                theme: panel.theme
                uiFont: panel.uiFont
                text: "Cancel"
                tone: "danger"
                onClicked: panel.firmware.cancel()
            }
            Item { Layout.fillWidth: true }
        }

        Label {
            Layout.fillWidth: true
            text: panel.firmware.status
            color: panel.firmware.targetVerified ? panel.theme.green : panel.theme.textSoft
            font.family: panel.uiFont
            font.pixelSize: 9
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: "FLASH LOG"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 7; font.weight: Font.Bold; font.letterSpacing: 0.8 }
            Item { Layout.fillWidth: true }
            CalmButton { theme: panel.theme; uiFont: panel.uiFont; text: "Clear"; onClicked: panel.firmware.clearLog() }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            TextArea {
                readOnly: true
                text: panel.firmware.logText
                color: panel.theme.textSoft
                selectionColor: panel.theme.accent
                font.family: panel.monoFont
                font.pixelSize: 8
                wrapMode: TextEdit.WrapAnywhere
                background: Rectangle { color: "#090e14"; radius: 6; border.width: 1; border.color: panel.theme.lineSoft }
            }
        }
    }
}
