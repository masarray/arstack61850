// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SurfacePanel {
    id: panel
    property var device
    property var session
    property string uiFont: "Inter"
    property string monoFont: "Inter"
    property var syncValues: ["AUTO", "0", "1", "2"]
    property var roleValues: ["SOURCE", "RECEIVER", "MONITOR"]

    function syncIndex(mode) {
        if (mode === "FORCE_0") return 1
        if (mode === "FORCE_1_LOCAL") return 2
        if (mode === "FORCE_2_GLOBAL") return 3
        return 0
    }

    function roleIndex(role) {
        if (role === "RECEIVER") return 1
        if (role === "MONITOR") return 2
        return 0
    }

    function showMetric(value, suffix) {
        return value === "NA" ? "—" : value + suffix
    }

    function applyProfile() {
        if (!panel.session) return false
        return panel.session.requestConfigurePtp({
            domain: Number(domainField.text),
            transportSpecific: Number(transportField.text),
            vlanEnabled: vlanCheck.checked,
            vlanId: Number(vlanField.text),
            vlanPriority: Number(pcpField.text),
            announceIntervalMs: Number(announceField.text),
            syncIntervalMs: Number(syncField.text),
            respondToPeerDelay: pdelayCheck.checked
        })
    }

    readonly property string role: panel.device.ptpRole
    readonly property bool receiverRole: role === "RECEIVER"
    readonly property bool monitorRole: role === "MONITOR"
    readonly property bool sourceRole: role === "SOURCE"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 9

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 1
                Label { text: "PTP LAB TIMING"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: panel.theme.captionSize; font.weight: Font.DemiBold; font.letterSpacing: 0.9 }
                Label { text: "Source, measured receiver & synchronization stimulus"; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 17; font.weight: Font.DemiBold }
            }
            Item { Layout.fillWidth: true }
            StateBadge {
                theme: panel.theme
                monoFont: panel.uiFont
                state: panel.device.ptpRunning
                    ? (panel.receiverRole ? panel.device.ptpDiscipline : panel.role + " ACTIVE")
                    : panel.role + " STOPPED"
                stateColor: panel.device.ptpDiscipline === "LOCKED" ? panel.theme.green
                    : panel.device.ptpDiscipline === "FAULT" ? panel.theme.red
                    : panel.device.ptpRunning ? panel.theme.amber : panel.theme.muted
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 47
            radius: 7
            color: panel.theme.amberSoft
            border.width: 1
            border.color: "#6a5529"
            Label {
                anchors.fill: parent
                anchors.margins: 9
                text: "SOURCE emits lab timing. RECEIVER disciplines the ESP32-P4 IEEE1588 clock from external PTP. MONITOR is passive. Forced smpSynch 0/1/2 remain explicit lab simulation."
                wrapMode: Text.WordWrap
                color: panel.theme.textSoft
                font.family: panel.uiFont
                font.pixelSize: 9
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label { text: "Operating role"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9; font.weight: Font.DemiBold }
            ComboBox {
                Layout.fillWidth: true
                model: ["Lab source — emit timing", "Time receiver — measured lock", "Monitor — passive"]
                currentIndex: panel.roleIndex(panel.role)
                enabled: panel.session && panel.device.ptpAvailable && !panel.device.ptpRunning && !panel.session.updatingFirmware
                font.family: panel.uiFont
                font.pixelSize: 9
                onActivated: panel.session.requestPtpRole(panel.roleValues[currentIndex])
            }
            Label {
                Layout.preferredWidth: 230
                text: panel.receiverRole ? "AUTO follows measured lock evidence."
                    : panel.monitorRole ? "No clock discipline or AUTO promotion."
                    : "Conservative lab source; no traceability claim."
                wrapMode: Text.WordWrap
                color: panel.theme.textSoft
                font.family: panel.uiFont
                font.pixelSize: 8
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 4
            columnSpacing: 10
            rowSpacing: 7
            Label { text: "Domain"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9 }
            NumericField { id: domainField; theme: panel.theme; monoFont: panel.uiFont; text: panel.device.ptpDomain === "-" ? "0" : panel.device.ptpDomain; validator: IntValidator { bottom: 0; top: 255 } }
            Label { text: "transportSpecific"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9 }
            NumericField { id: transportField; theme: panel.theme; monoFont: panel.uiFont; text: panel.device.ptpTransportSpecific === "-" ? "0" : panel.device.ptpTransportSpecific; validator: IntValidator { bottom: 0; top: 15 } }
            Label { text: "Announce interval"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9 }
            NumericField { id: announceField; theme: panel.theme; monoFont: panel.uiFont; text: "1000"; validator: IntValidator { bottom: 100; top: 10000 } }
            Label { text: "Sync interval"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9 }
            NumericField { id: syncField; theme: panel.theme; monoFont: panel.uiFont; text: "250"; validator: IntValidator { bottom: 20; top: 5000 } }
            CheckBox { id: vlanCheck; text: "VLAN"; font.family: panel.uiFont; font.pixelSize: 9 }
            NumericField { id: vlanField; enabled: vlanCheck.checked; theme: panel.theme; monoFont: panel.uiFont; text: "100"; validator: IntValidator { bottom: 1; top: 4094 } }
            Label { text: "VLAN PCP"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9 }
            NumericField { id: pcpField; enabled: vlanCheck.checked; theme: panel.theme; monoFont: panel.uiFont; text: "4"; validator: IntValidator { bottom: 0; top: 7 } }
        }

        CheckBox {
            id: pdelayCheck
            visible: panel.sourceRole
            text: "Respond to peer-delay requests using hardware timestamps"
            checked: true
            font.family: panel.uiFont
            font.pixelSize: 9
        }

        Rectangle {
            visible: !panel.sourceRole
            Layout.fillWidth: true
            implicitHeight: 79
            radius: 7
            color: panel.theme.panelAlt
            border.width: 1
            border.color: panel.theme.lineSoft
            GridLayout {
                anchors.fill: parent
                anchors.margins: 9
                columns: 4
                rowSpacing: 4
                columnSpacing: 12
                Label { text: "Discipline"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                Label { text: panel.device.ptpDiscipline; color: panel.device.ptpDiscipline === "LOCKED" ? panel.theme.green : panel.device.ptpDiscipline === "FAULT" ? panel.theme.red : panel.theme.amber; font.family: panel.monoFont; font.pixelSize: 10; font.weight: Font.DemiBold }
                Label { text: "Selected source"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                Label { text: panel.device.ptpSource === "NONE" ? "—" : panel.device.ptpSource; color: panel.theme.text; font.family: panel.monoFont; font.pixelSize: 9; elide: Text.ElideMiddle }
                Label { text: "Offset"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                Label { text: panel.showMetric(panel.device.ptpOffsetNs, " ns"); color: panel.theme.text; font.family: panel.monoFont; font.pixelSize: 10 }
                Label { text: "Mean path delay"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                Label { text: panel.showMetric(panel.device.ptpPathDelayNs, " ns"); color: panel.theme.text; font.family: panel.monoFont; font.pixelSize: 10 }
                Label { text: "Path jitter"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                Label { text: panel.showMetric(panel.device.ptpJitterNs, " ns"); color: panel.theme.text; font.family: panel.monoFont; font.pixelSize: 10 }
                Label { text: "Clock correction"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                Label { text: panel.device.ptpFrequencyPpb + " ppb"; color: panel.theme.text; font.family: panel.monoFont; font.pixelSize: 10 }
            }
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
                    Layout.fillWidth: true
                    model: ["AUTO — measured PTP only", "0 — Not synchronized", "1 — Local synchronized (lab)", "2 — Global synchronized (lab)"]
                    currentIndex: panel.syncIndex(panel.device.smpSynchMode)
                    enabled: panel.session && panel.device.deviceVerified && !panel.session.updatingFirmware
                    font.family: panel.uiFont
                    font.pixelSize: 9
                    onActivated: panel.session.requestSmpSynch(panel.syncValues[currentIndex])
                }
            }
            StateBadge {
                theme: panel.theme
                monoFont: panel.uiFont
                state: "smpSynch=" + panel.device.smpSynchValue
                    + (panel.device.smpSynchSimulated ? " SIMULATED" : panel.device.smpSynchMeasured ? " MEASURED" : " SAFE")
                stateColor: panel.device.smpSynchSimulated ? panel.theme.amber
                    : panel.device.smpSynchMeasured ? panel.theme.green : panel.theme.muted
            }
        }

        Label {
            Layout.fillWidth: true
            text: panel.device.smpSynchMode === "AUTO" && panel.receiverRole
                ? "AUTO: LOCKED non-traceable → 1, LOCKED traceable → 2, bounded HOLDOVER → 1, otherwise 0."
                : panel.device.smpSynchMode === "AUTO"
                    ? "AUTO remains 0 in SOURCE/MONITOR because those roles do not prove local clock discipline."
                    : "Forced 0/1/2 is a live receiver-condition stimulus independent of measured clock lock."
            wrapMode: Text.WordWrap
            color: panel.theme.textSoft
            font.family: panel.uiFont
            font.pixelSize: 8
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 5
            columnSpacing: 12
            rowSpacing: 4
            Label { text: panel.sourceRole ? "Announce TX" : "Announce RX"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
            Label { text: panel.sourceRole ? "Sync TX" : "Sync RX"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
            Label { text: panel.sourceRole ? "FollowUp TX" : "FollowUp RX"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
            Label { text: panel.sourceRole ? "Pdelay TX" : "Pdelay RX / Req"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
            Label { text: panel.sourceRole ? "TX failures" : "Accepted / rejected"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
            Label { text: panel.sourceRole ? panel.device.ptpAnnounceSent : panel.device.ptpRxAnnounce; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 12; font.weight: Font.DemiBold }
            Label { text: panel.sourceRole ? panel.device.ptpSyncSent : panel.device.ptpRxSync; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 12; font.weight: Font.DemiBold }
            Label { text: panel.sourceRole ? panel.device.ptpFollowUpSent : panel.device.ptpRxFollowUp; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 12; font.weight: Font.DemiBold }
            Label { text: panel.sourceRole ? panel.device.ptpPdelayFrames : panel.device.ptpRxPdelay + " / " + panel.device.ptpPdelayRequests; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 12; font.weight: Font.DemiBold }
            Label { text: panel.sourceRole ? panel.device.ptpTxFailures : panel.device.ptpAccepted + " / " + panel.device.ptpRejected; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 12; font.weight: Font.DemiBold }
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            CalmButton {
                theme: panel.theme
                uiFont: panel.uiFont
                text: "Refresh"
                enabled: panel.session && panel.device.deviceVerified && !panel.session.updatingFirmware
                onClicked: panel.session.requestPtpRefresh()
            }
            Item { Layout.fillWidth: true }
            CalmButton {
                theme: panel.theme
                uiFont: panel.uiFont
                text: "Apply profile"
                enabled: panel.session && panel.device.ptpAvailable && !panel.device.ptpRunning && !panel.session.updatingFirmware
                onClicked: panel.applyProfile()
            }
            CalmButton {
                theme: panel.theme
                uiFont: panel.uiFont
                tone: "danger"
                text: "Stop PTP"
                enabled: panel.session && panel.device.ptpRunning && !panel.session.updatingFirmware
                onClicked: panel.session.requestStopPtp()
            }
            CalmButton {
                theme: panel.theme
                uiFont: panel.uiFont
                tone: "accent"
                text: panel.receiverRole ? "Start receiver" : panel.monitorRole ? "Start monitor" : "Start source"
                enabled: panel.session && panel.device.ptpAvailable && !panel.device.ptpRunning && !panel.session.updatingFirmware
                onClicked: panel.session.requestStartPtp()
            }
        }
    }
}
