// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SurfacePanel {
    id: panel
    property var device
    property string uiFont: "Inter"
    property string monoFont: "Inter"
    property var syncPolicyValues: ["AUTO", "0", "1", "2"]

    function latestSmpSynch() {
        const lines = String(device.logText).split("\n")
        for (let i = lines.length - 1; i >= 0; --i) {
            const match = lines[i].match(/SMPSYNCH mode=([A-Z0-9_]+) advertised=([0-2]) source=([A-Z_]+) simulated=([01]) measured=([01])/) 
            if (match)
                return { "mode": match[1], "value": match[2], "source": match[3], "simulated": match[4] === "1", "measured": match[5] === "1" }
        }
        return { "mode": "AUTO", "value": "0", "source": "SAFE_DEFAULT", "simulated": false, "measured": false }
    }

    function latestPtpCounters() {
        const lines = String(device.logText).split("\n")
        for (let i = lines.length - 1; i >= 0; --i) {
            const match = lines[i].match(/PTP status=(RUNNING|STOPPED) Announce=(\d+) Sync=(\d+) FollowUp=(\d+) PdelayFrames=(\d+) TXfail=(\d+)/)
            if (match)
                return { "followUp": match[4], "pdelay": match[5] }
        }
        return { "followUp": "—", "pdelay": "—" }
    }

    function policyIndex(mode) {
        if (mode === "FORCE_0") return 1
        if (mode === "FORCE_1_LOCAL") return 2
        if (mode === "FORCE_2_GLOBAL") return 3
        return 0
    }

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

    readonly property var smpSynch: latestSmpSynch()
    readonly property var ptpCounters: latestPtpCounters()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 1
                Label { text: "PTP LAB TIMING"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: panel.theme.captionSize; font.weight: Font.DemiBold; font.letterSpacing: 0.9 }
                Label { text: "Expert profile & SV sync simulation"; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 17; font.weight: Font.DemiBold }
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
            implicitHeight: 56
            radius: 7
            color: panel.theme.amberSoft
            border.width: 1
            border.color: "#6a5529"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 9
                spacing: 2
                Label { text: "Laboratory timing companion"; color: panel.theme.amber; font.family: panel.uiFont; font.pixelSize: 10; font.weight: Font.DemiBold }
                Label { Layout.fillWidth: true; text: "Hardware-timestamped PTP traffic is for interoperability testing. Forced smpSynch values are simulated wire conditions, not proof of clock lock."; wrapMode: Text.WordWrap; color: panel.theme.textSoft; font.family: panel.uiFont; font.pixelSize: 9 }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 4
            columnSpacing: 10
            rowSpacing: 8

            Label { text: "Domain"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9 }
            NumericField { id: domainField; theme: panel.theme; monoFont: panel.uiFont; text: panel.device.ptpDomain === "-" ? "0" : panel.device.ptpDomain; validator: IntValidator { bottom: 0; top: 255 } }
            Label { text: "transportSpecific"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9 }
            NumericField { id: transportField; theme: panel.theme; monoFont: panel.uiFont; text: panel.device.ptpTransportSpecific === "-" ? "0" : panel.device.ptpTransportSpecific; validator: IntValidator { bottom: 0; top: 15 } }

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

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Label { text: "SV smpSynch stimulus"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9; font.weight: Font.DemiBold }
                ComboBox {
                    id: syncPolicyBox
                    Layout.fillWidth: true
                    model: [
                        "AUTO — measured PTP only",
                        "0 — Not synchronized",
                        "1 — Local synchronized (lab)",
                        "2 — Global synchronized (lab)"
                    ]
                    currentIndex: panel.policyIndex(panel.smpSynch.mode)
                    enabled: panel.device.deviceVerified
                    font.family: panel.uiFont
                    font.pixelSize: 9
                    onActivated: panel.device.setSmpSynchPolicy(panel.syncPolicyValues[currentIndex])
                }
            }

            ColumnLayout {
                Layout.preferredWidth: 170
                spacing: 4
                Label { text: "Advertised wire state"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9; font.weight: Font.DemiBold }
                RowLayout {
                    spacing: 8
                    StateBadge {
                        theme: panel.theme
                        monoFont: panel.uiFont
                        state: "smpSynch=" + panel.smpSynch.value
                        stateColor: panel.smpSynch.simulated ? panel.theme.amber : (panel.smpSynch.measured ? panel.theme.green : panel.theme.muted)
                    }
                    Label {
                        text: panel.smpSynch.simulated ? "SIMULATED" : panel.smpSynch.source
                        color: panel.smpSynch.simulated ? panel.theme.amber : panel.theme.textSoft
                        font.family: panel.uiFont
                        font.pixelSize: 8
                        font.weight: Font.DemiBold
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: panel.smpSynch.mode === "AUTO"
                  ? "AUTO stays at smpSynch=0 until PTP-P2 supplies measured lock evidence; packet visibility alone never promotes it."
                  : "Compatibility override is live: use 0/1/2 to test relay behavior without pretending the ESP clock is actually synchronized."
            wrapMode: Text.WordWrap
            color: panel.theme.textSoft
            font.family: panel.uiFont
            font.pixelSize: 8
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: panel.theme.lineSoft }

        GridLayout {
            Layout.fillWidth: true
            columns: 5
            rowSpacing: 6
            columnSpacing: 12
            Label { text: "Announce TX"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
            Label { text: "Sync TX"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
            Label { text: "FollowUp TX"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
            Label { text: "Pdelay TX"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
            Label { text: "TX failures"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
            Label { text: panel.device.ptpAnnounceSent; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold }
            Label { text: panel.device.ptpSyncSent; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold }
            Label { text: panel.ptpCounters.followUp; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold }
            Label { text: panel.ptpCounters.pdelay; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold }
            Label { text: panel.device.ptpTxFailures; color: Number(panel.device.ptpTxFailures) > 0 ? panel.theme.red : panel.theme.text; font.family: panel.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold }
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            CalmButton {
                theme: panel.theme
                uiFont: panel.uiFont
                text: "Refresh"
                enabled: panel.device.deviceVerified
                onClicked: {
                    panel.device.sendPtpShow()
                    panel.device.sendSmpSynchShow()
                }
            }
            Item { Layout.fillWidth: true }
            CalmButton { theme: panel.theme; uiFont: panel.uiFont; text: "Apply profile"; enabled: panel.device.deviceVerified && panel.device.ptpAvailable && !panel.device.ptpRunning; onClicked: panel.applyProfile() }
            CalmButton { theme: panel.theme; uiFont: panel.uiFont; tone: "danger"; text: "Stop PTP"; enabled: panel.device.ptpRunning; onClicked: panel.device.stopPtp() }
            CalmButton { theme: panel.theme; uiFont: panel.uiFont; tone: "accent"; text: "Start PTP"; enabled: panel.device.deviceVerified && panel.device.ptpAvailable && !panel.device.ptpRunning; onClicked: panel.device.startPtp() }
        }
    }
}
