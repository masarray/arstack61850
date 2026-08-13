// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SurfacePanel {
    id: panel
    property var device
    property string uiFont: "Inter"
    property string monoFont: "Inter"
    property var syncValues: ["AUTO", "0", "1", "2"]
    property var roleValues: ["SOURCE", "RECEIVER", "MONITOR"]

    function latestSync() {
        const lines = String(device.logText).split("\n")
        for (let i = lines.length - 1; i >= 0; --i) {
            const m = lines[i].match(/SMPSYNCH mode=([A-Z0-9_]+) advertised=([0-2]) source=([A-Z_]+) simulated=([01]) measured=([01])/) 
            if (m) return { mode: m[1], value: m[2], source: m[3], simulated: m[4] === "1", measured: m[5] === "1" }
        }
        return { mode: "AUTO", value: "0", source: "SAFE_DEFAULT", simulated: false, measured: false }
    }

    function emptyPtpState(role) {
        return {
            role: role,
            discipline: "UNLOCKED",
            source: "NONE",
            offset: "NA",
            path: "NA",
            jitter: "NA",
            freq: "0",
            global: false,
            measured: "NA",
            rxAnnounce: "0",
            rxSync: "0",
            rxFollowUp: "0",
            rxPdelay: "0",
            pdelayReq: "0",
            accepted: "0",
            rejected: "0"
        }
    }

    function latestPtpState() {
        const lines = String(device.logText).split("\n")
        for (let i = lines.length - 1; i >= 0; --i) {
            // Both PTPROLE and PTP2 are role-bearing events. Whichever appears
            // newest is authoritative. A stopped role change therefore resets
            // stale timing metrics immediately until a fresh PTP2 snapshot for
            // that role arrives.
            const role = lines[i].match(/PTPROLE role=(SOURCE|RECEIVER|MONITOR)/)
            if (role) return emptyPtpState(role[1])

            const m = lines[i].match(/PTP2 role=(SOURCE|RECEIVER|MONITOR) discipline=(UNLOCKED|ACQUIRING|LOCKED|HOLDOVER|FAULT) source=(\S+) offset=(NA|-?\d+) path=(NA|-?\d+) jitter=(NA|-?\d+) freq=(-?\d+) global=([01]) measured=(NA|[012]) rxAnnounce=(\d+) rxSync=(\d+) rxFollowUp=(\d+) rxPdelay=(\d+) pdelayReq=(\d+) accepted=(\d+) rejected=(\d+)/)
            if (m) return {
                role: m[1], discipline: m[2], source: m[3], offset: m[4], path: m[5], jitter: m[6], freq: m[7],
                global: m[8] === "1", measured: m[9], rxAnnounce: m[10], rxSync: m[11], rxFollowUp: m[12],
                rxPdelay: m[13], pdelayReq: m[14], accepted: m[15], rejected: m[16]
            }
        }
        return emptyPtpState("SOURCE")
    }

    function latestTx() {
        const lines = String(device.logText).split("\n")
        for (let i = lines.length - 1; i >= 0; --i) {
            const m = lines[i].match(/PTP status=(RUNNING|STOPPED) Announce=(\d+) Sync=(\d+) FollowUp=(\d+) PdelayFrames=(\d+) TXfail=(\d+)/)
            if (m) return { followUp: m[4], pdelay: m[5] }
        }
        return { followUp: "—", pdelay: "—" }
    }

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
        device.configurePtp({
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

    readonly property var sync: latestSync()
    readonly property var p2: latestPtpState()
    readonly property var tx: latestTx()
    readonly property bool receiverRole: p2.role === "RECEIVER"
    readonly property bool monitorRole: p2.role === "MONITOR"
    readonly property bool sourceRole: p2.role === "SOURCE"

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
                state: panel.device.ptpRunning ? (panel.receiverRole ? panel.p2.discipline : panel.p2.role + " ACTIVE") : panel.p2.role + " STOPPED"
                stateColor: panel.p2.discipline === "LOCKED" ? panel.theme.green : panel.p2.discipline === "FAULT" ? panel.theme.red : panel.device.ptpRunning ? panel.theme.amber : panel.theme.muted
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
                currentIndex: panel.roleIndex(panel.p2.role)
                enabled: panel.device.deviceVerified && panel.device.ptpAvailable && !panel.device.ptpRunning
                font.family: panel.uiFont
                font.pixelSize: 9
                onActivated: panel.device.setPtpRole(panel.roleValues[currentIndex])
            }
            Label {
                Layout.preferredWidth: 230
                text: panel.receiverRole ? "AUTO follows measured lock evidence." : panel.monitorRole ? "No clock discipline or AUTO promotion." : "Conservative lab source; no traceability claim."
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
                Label { text: panel.p2.discipline; color: panel.p2.discipline === "LOCKED" ? panel.theme.green : panel.p2.discipline === "FAULT" ? panel.theme.red : panel.theme.amber; font.family: panel.monoFont; font.pixelSize: 10; font.weight: Font.DemiBold }
                Label { text: "Selected source"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                Label { text: panel.p2.source === "NONE" ? "—" : panel.p2.source; color: panel.theme.text; font.family: panel.monoFont; font.pixelSize: 9; elide: Text.ElideMiddle }
                Label { text: "Offset"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                Label { text: panel.showMetric(panel.p2.offset, " ns"); color: panel.theme.text; font.family: panel.monoFont; font.pixelSize: 10 }
                Label { text: "Mean path delay"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                Label { text: panel.showMetric(panel.p2.path, " ns"); color: panel.theme.text; font.family: panel.monoFont; font.pixelSize: 10 }
                Label { text: "Path jitter"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                Label { text: panel.showMetric(panel.p2.jitter, " ns"); color: panel.theme.text; font.family: panel.monoFont; font.pixelSize: 10 }
                Label { text: "Clock correction"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                Label { text: panel.p2.freq + " ppb"; color: panel.theme.text; font.family: panel.monoFont; font.pixelSize: 10 }
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
                    currentIndex: panel.syncIndex(panel.sync.mode)
                    enabled: panel.device.deviceVerified
                    font.family: panel.uiFont
                    font.pixelSize: 9
                    onActivated: panel.device.setSmpSynchPolicy(panel.syncValues[currentIndex])
                }
            }
            StateBadge {
                theme: panel.theme
                monoFont: panel.uiFont
                state: "smpSynch=" + panel.sync.value + (panel.sync.simulated ? " SIMULATED" : panel.sync.measured ? " MEASURED" : " SAFE")
                stateColor: panel.sync.simulated ? panel.theme.amber : panel.sync.measured ? panel.theme.green : panel.theme.muted
            }
        }

        Label {
            Layout.fillWidth: true
            text: panel.sync.mode === "AUTO" && panel.receiverRole ? "AUTO: LOCKED non-traceable → 1, LOCKED traceable → 2, bounded HOLDOVER → 1, otherwise 0." : panel.sync.mode === "AUTO" ? "AUTO remains 0 in SOURCE/MONITOR because those roles do not prove local clock discipline." : "Forced 0/1/2 is a live receiver-condition stimulus independent of measured clock lock."
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
            Label { text: panel.sourceRole ? panel.device.ptpAnnounceSent : panel.p2.rxAnnounce; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 12; font.weight: Font.DemiBold }
            Label { text: panel.sourceRole ? panel.device.ptpSyncSent : panel.p2.rxSync; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 12; font.weight: Font.DemiBold }
            Label { text: panel.sourceRole ? panel.tx.followUp : panel.p2.rxFollowUp; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 12; font.weight: Font.DemiBold }
            Label { text: panel.sourceRole ? panel.tx.pdelay : panel.p2.rxPdelay + " / " + panel.p2.pdelayReq; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 12; font.weight: Font.DemiBold }
            Label { text: panel.sourceRole ? panel.device.ptpTxFailures : panel.p2.accepted + " / " + panel.p2.rejected; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 12; font.weight: Font.DemiBold }
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            CalmButton { theme: panel.theme; uiFont: panel.uiFont; text: "Refresh"; enabled: panel.device.deviceVerified; onClicked: { panel.device.sendPtpShow(); panel.device.sendSmpSynchShow() } }
            Item { Layout.fillWidth: true }
            CalmButton { theme: panel.theme; uiFont: panel.uiFont; text: "Apply profile"; enabled: panel.device.deviceVerified && panel.device.ptpAvailable && !panel.device.ptpRunning; onClicked: panel.applyProfile() }
            CalmButton { theme: panel.theme; uiFont: panel.uiFont; tone: "danger"; text: "Stop PTP"; enabled: panel.device.ptpRunning; onClicked: panel.device.stopPtp() }
            CalmButton { theme: panel.theme; uiFont: panel.uiFont; tone: "accent"; text: panel.receiverRole ? "Start receiver" : panel.monitorRole ? "Start monitor" : "Start source"; enabled: panel.device.deviceVerified && panel.device.ptpAvailable && !panel.device.ptpRunning; onClicked: panel.device.startPtp() }
        }
    }
}
