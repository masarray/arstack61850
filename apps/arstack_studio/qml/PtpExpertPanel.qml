// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SurfacePanel {
    id: panel
    property var device
    property string uiFont: "Inter"
    property string monoFont: "Inter"

    function applyProfile() {
        device.configurePtp({
            "domain": Number(domainField.text),
            "transportSpecific": Number(transportField.text),
            "vlanEnabled": vlanCheck.checked,
            "vlanId": Number(vlanField.text),
            "vlanPriority": Number(pcpField.text),
            "announceIntervalMs": Number(announceField.text),
            "syncIntervalMs": Number(syncField.text),
            "respondToPeerDelay": pdelayCheck.checked
        })
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 1
                Label { text: "PTP LAB TIMING"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: panel.theme.captionSize; font.weight: Font.DemiBold; font.letterSpacing: 0.9 }
                Label { text: "Expert profile"; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 17; font.weight: Font.DemiBold }
            }
            Item { Layout.fillWidth: true }
            StateBadge {
                theme: panel.theme
                monoFont: panel.uiFont
                state: panel.device.ptpStatus.toUpperCase()
                stateColor: panel.device.ptpRunning ? panel.theme.amber : (panel.device.ptpAvailable ? panel.theme.green : panel.theme.muted)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 58
            radius: 7
            color: panel.theme.amberSoft
            border.width: 1
            border.color: "#6a5529"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 2
                Label { text: "Laboratory timing companion"; color: panel.theme.amber; font.family: panel.uiFont; font.pixelSize: 10; font.weight: Font.DemiBold }
                Label { Layout.fillWidth: true; text: "This transmitter is not a GPS-backed grandmaster and does not prove that SV is synchronized."; wrapMode: Text.WordWrap; color: panel.theme.textSoft; font.family: panel.uiFont; font.pixelSize: 9 }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 4
            columnSpacing: 10
            rowSpacing: 9

            Label { text: "Domain"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9 }
            NumericField { id: domainField; theme: panel.theme; monoFont: panel.uiFont; text: panel.device.ptpDomain === "-" ? "0" : panel.device.ptpDomain; validator: IntValidator { bottom: 0; top: 255 } }
            Label { text: "transportSpecific"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9 }
            NumericField { id: transportField; theme: panel.theme; monoFont: panel.uiFont; text: "0"; validator: IntValidator { bottom: 0; top: 15 } }

            Label { text: "Announce"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9 }
            NumericField { id: announceField; theme: panel.theme; monoFont: panel.uiFont; text: "1000"; validator: IntValidator { bottom: 100; top: 10000 } }
            Label { text: "Sync"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9 }
            NumericField { id: syncField; theme: panel.theme; monoFont: panel.uiFont; text: "250"; validator: IntValidator { bottom: 20; top: 5000 } }

            CheckBox { id: vlanCheck; text: "VLAN"; font.family: panel.uiFont; font.pixelSize: 9 }
            NumericField { id: vlanField; enabled: vlanCheck.checked; theme: panel.theme; monoFont: panel.uiFont; text: "100"; validator: IntValidator { bottom: 1; top: 4094 } }
            Label { text: "VLAN PCP"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9 }
            NumericField { id: pcpField; enabled: vlanCheck.checked; theme: panel.theme; monoFont: panel.uiFont; text: "4"; validator: IntValidator { bottom: 0; top: 7 } }
        }

        CheckBox {
            id: pdelayCheck
            text: "Respond to peer-delay requests using ESP32-P4 hardware timestamps"
            checked: true
            font.family: panel.uiFont
            font.pixelSize: 9
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: panel.theme.lineSoft }

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            rowSpacing: 8
            columnSpacing: 14
            Label { text: "Announce TX"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
            Label { text: "Sync TX"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
            Label { text: "TX failures"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
            Label { text: panel.device.ptpAnnounceSent; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold }
            Label { text: panel.device.ptpSyncSent; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold }
            Label { text: panel.device.ptpTxFailures; color: Number(panel.device.ptpTxFailures) > 0 ? panel.theme.red : panel.theme.text; font.family: panel.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold }
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            CalmButton { theme: panel.theme; uiFont: panel.uiFont; text: "Refresh"; enabled: panel.device.deviceVerified; onClicked: panel.device.sendPtpShow() }
            Item { Layout.fillWidth: true }
            CalmButton { theme: panel.theme; uiFont: panel.uiFont; text: "Apply profile"; enabled: panel.device.deviceVerified && panel.device.ptpAvailable && !panel.device.ptpRunning; onClicked: panel.applyProfile() }
            CalmButton { theme: panel.theme; uiFont: panel.uiFont; tone: "danger"; text: "Stop PTP"; enabled: panel.device.ptpRunning; onClicked: panel.device.stopPtp() }
            CalmButton { theme: panel.theme; uiFont: panel.uiFont; tone: "accent"; text: "Start PTP"; enabled: panel.device.deviceVerified && panel.device.ptpAvailable && !panel.device.ptpRunning; onClicked: panel.device.startPtp() }
        }
    }
}
