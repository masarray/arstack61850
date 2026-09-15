// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SurfacePanel {
    id: panel
    property var controller
    property var device
    property var session
    property string uiFont: "Inter"
    property string monoFont: "Inter"
    property var roleValues: ["SOURCE", "RECEIVER", "MONITOR"]
    property bool advancedVisible: false
    property bool startPending: false

    function roleIndex(role) {
        if (role === "RECEIVER") return 1
        if (role === "MONITOR") return 2
        return 0
    }

    function syncIndex(mode) {
        if (mode === "FORCE_0") return 1
        if (mode === "FORCE_1_LOCAL") return 2
        if (mode === "FORCE_2_GLOBAL") return 3
        return 0
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

    function startSelectedMode() {
        if (!panel.session || !panel.device.ptpAvailable) return
        var role = panel.roleValues[modeCombo.currentIndex]
        if (!panel.session.requestPtpRole(role)) {
            if (panel.controller) panel.controller.showMessage("PTP mode could not be selected.", true)
            return
        }
        if (!panel.applyProfile()) {
            if (panel.controller) panel.controller.showMessage("PTP settings were rejected before start.", true)
            return
        }
        panel.startPending = true
        if (!panel.session.requestStartPtp()) {
            panel.startPending = false
            if (panel.controller) panel.controller.showMessage("PTP start command could not be sent.", true)
        }
    }

    readonly property string activeRole: panel.device.ptpRole
    readonly property bool sourceRole: activeRole === "SOURCE"
    readonly property bool receiverRole: activeRole === "RECEIVER"
    readonly property bool sourceEvidence: Number(panel.device.ptpAnnounceSent) > 0
        && Number(panel.device.ptpSyncSent) > 0
        && Number(panel.device.ptpFollowUpSent) > 0
        && Number(panel.device.ptpTxFailures) === 0
    readonly property string headlineState: panel.startPending ? "STARTING"
        : panel.device.ptpRunning
            ? (panel.receiverRole ? panel.device.ptpDiscipline : panel.activeRole + " ACTIVE")
            : panel.device.ptpStatus === "PTP start failed" ? "START FAILED" : "OFF"

    Timer {
        interval: 700
        repeat: true
        running: panel.startPending || panel.device.ptpRunning
        onTriggered: {
            if (panel.session) panel.session.requestPtpRefresh()
            if (panel.device.ptpRunning) panel.startPending = false
        }
    }

    Timer {
        interval: 3500
        repeat: false
        running: panel.startPending
        onTriggered: {
            panel.startPending = false
            if (panel.session) panel.session.requestPtpRefresh()
            if (!panel.device.ptpRunning && panel.controller)
                panel.controller.showMessage("PTP did not reach verified transmit state. Check Ethernet link and retry.", true)
        }
    }

    Connections {
        target: panel.device
        function onPtpStateChanged() {
            if (panel.device.ptpRunning) {
                panel.startPending = false
                if (panel.controller && panel.sourceRole && panel.sourceEvidence)
                    panel.controller.showMessage("PTP Source active · Announce + Sync + Follow_Up verified.", false)
            }
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    spacing: 2
                    Label {
                        text: "PTP TIMING"
                        color: panel.theme.muted
                        font.family: panel.uiFont
                        font.pixelSize: panel.theme.captionSize
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.9
                    }
                    Label {
                        text: "Network timing control"
                        color: panel.theme.text
                        font.family: panel.uiFont
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                    }
                }
                Item { Layout.fillWidth: true }
                StateBadge {
                    theme: panel.theme
                    monoFont: panel.uiFont
                    state: panel.headlineState
                    stateColor: panel.device.ptpDiscipline === "LOCKED" ? panel.theme.green
                        : panel.headlineState === "START FAILED" ? panel.theme.red
                        : panel.device.ptpRunning ? panel.theme.green
                        : panel.startPending ? panel.theme.amber : panel.theme.muted
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 82
                radius: 8
                color: panel.theme.panelAlt
                border.width: 1
                border.color: panel.theme.lineSoft

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 14

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Label {
                            text: "Timing mode"
                            color: panel.theme.muted
                            font.family: panel.uiFont
                            font.pixelSize: 9
                            font.weight: Font.DemiBold
                        }
                        ComboBox {
                            id: modeCombo
                            Layout.fillWidth: true
                            model: ["Provide PTP timing", "Follow external PTP", "Monitor PTP only"]
                            currentIndex: panel.roleIndex(panel.activeRole)
                            enabled: panel.session && panel.device.ptpAvailable && !panel.device.ptpRunning && !panel.startPending && !panel.session.updatingFirmware
                            font.family: panel.uiFont
                            font.pixelSize: 10
                        }
                    }

                    ColumnLayout {
                        Layout.preferredWidth: 310
                        spacing: 3
                        Label {
                            text: modeCombo.currentIndex === 0
                                ? "Board sends Announce + hardware-timestamped two-step Sync/Follow_Up."
                                : modeCombo.currentIndex === 1
                                    ? "Board follows an external PTP source and reports measured clock lock."
                                    : "Board listens to PTP without transmitting or disciplining its clock."
                            wrapMode: Text.WordWrap
                            color: panel.theme.textSoft
                            font.family: panel.uiFont
                            font.pixelSize: 9
                        }
                        Label {
                            text: modeCombo.currentIndex === 0
                                ? "PTP TX does not by itself change SV smpSynch."
                                : modeCombo.currentIndex === 1
                                    ? "AUTO smpSynch follows measured receiver evidence only."
                                    : "Useful for observing timing traffic without changing the network."
                            wrapMode: Text.WordWrap
                            color: panel.theme.muted
                            font.family: panel.uiFont
                            font.pixelSize: 8
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                CalmButton {
                    theme: panel.theme
                    uiFont: panel.uiFont
                    tone: "accent"
                    text: panel.startPending ? "Starting…"
                        : modeCombo.currentIndex === 0 ? "Start PTP Source"
                        : modeCombo.currentIndex === 1 ? "Start PTP Receiver" : "Start Monitor"
                    enabled: panel.session && panel.device.ptpAvailable && !panel.device.ptpRunning && !panel.startPending && !panel.session.updatingFirmware
                    onClicked: panel.startSelectedMode()
                }
                CalmButton {
                    theme: panel.theme
                    uiFont: panel.uiFont
                    tone: "danger"
                    text: "Stop PTP"
                    enabled: panel.session && (panel.device.ptpRunning || panel.startPending) && !panel.session.updatingFirmware
                    onClicked: {
                        panel.startPending = false
                        panel.session.requestStopPtp()
                    }
                }
                CalmButton {
                    theme: panel.theme
                    uiFont: panel.uiFont
                    text: "Refresh"
                    enabled: panel.session && panel.device.deviceVerified && !panel.session.updatingFirmware
                    onClicked: panel.session.requestPtpRefresh()
                }
                Item { Layout.fillWidth: true }
                CheckBox {
                    text: "Advanced settings"
                    checked: panel.advancedVisible
                    onToggled: panel.advancedVisible = checked
                    font.family: panel.uiFont
                    font.pixelSize: 9
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 102
                radius: 8
                color: panel.sourceRole && panel.device.ptpRunning && panel.sourceEvidence ? "#10251d" : panel.theme.surface2
                border.width: 1
                border.color: panel.sourceRole && panel.device.ptpRunning && panel.sourceEvidence ? "#347a59" : panel.theme.lineSoft

                GridLayout {
                    anchors.fill: parent
                    anchors.margins: 11
                    columns: 5
                    columnSpacing: 14
                    rowSpacing: 6

                    Label { text: panel.sourceRole ? "Announce TX" : "Announce RX"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                    Label { text: panel.sourceRole ? "Sync TX" : "Sync RX"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                    Label { text: panel.sourceRole ? "Follow_Up TX" : "Follow_Up RX"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                    Label { text: panel.sourceRole ? "Pdelay TX" : "Pdelay RX / Req"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                    Label { text: panel.sourceRole ? "TX failures" : "Accepted / rejected"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }

                    Label { text: panel.sourceRole ? panel.device.ptpAnnounceSent : panel.device.ptpRxAnnounce; color: panel.theme.text; font.family: panel.monoFont; font.pixelSize: 15; font.weight: Font.DemiBold }
                    Label { text: panel.sourceRole ? panel.device.ptpSyncSent : panel.device.ptpRxSync; color: panel.theme.text; font.family: panel.monoFont; font.pixelSize: 15; font.weight: Font.DemiBold }
                    Label { text: panel.sourceRole ? panel.device.ptpFollowUpSent : panel.device.ptpRxFollowUp; color: panel.theme.text; font.family: panel.monoFont; font.pixelSize: 15; font.weight: Font.DemiBold }
                    Label { text: panel.sourceRole ? panel.device.ptpPdelayFrames : panel.device.ptpRxPdelay + " / " + panel.device.ptpPdelayRequests; color: panel.theme.text; font.family: panel.monoFont; font.pixelSize: 15; font.weight: Font.DemiBold }
                    Label { text: panel.sourceRole ? panel.device.ptpTxFailures : panel.device.ptpAccepted + " / " + panel.device.ptpRejected; color: panel.sourceRole && Number(panel.device.ptpTxFailures) > 0 ? panel.theme.red : panel.theme.text; font.family: panel.monoFont; font.pixelSize: 15; font.weight: Font.DemiBold }

                    Label {
                        Layout.columnSpan: 5
                        text: panel.sourceRole && panel.device.ptpRunning && panel.sourceEvidence
                            ? "Verified transmit path: Announce + Sync + Follow_Up counters are advancing with zero TX failures."
                            : panel.sourceRole && panel.startPending
                                ? "Waiting for the first verified Announce + Sync + Follow_Up transmission…"
                                : panel.device.ptpStatus
                        color: panel.sourceRole && panel.device.ptpRunning && panel.sourceEvidence ? panel.theme.green : panel.theme.textSoft
                        font.family: panel.uiFont
                        font.pixelSize: 9
                    }
                }
            }

            Rectangle {
                visible: panel.receiverRole
                Layout.fillWidth: true
                implicitHeight: 88
                radius: 8
                color: panel.theme.panelAlt
                border.width: 1
                border.color: panel.theme.lineSoft

                GridLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    columns: 4
                    Label { text: "Discipline"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                    Label { text: panel.device.ptpDiscipline; color: panel.device.ptpDiscipline === "LOCKED" ? panel.theme.green : panel.theme.amber; font.family: panel.monoFont; font.pixelSize: 10; font.weight: Font.DemiBold }
                    Label { text: "Selected source"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                    Label { text: panel.device.ptpSource === "NONE" ? "—" : panel.device.ptpSource; color: panel.theme.text; font.family: panel.monoFont; font.pixelSize: 9 }
                    Label { text: "Offset"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                    Label { text: panel.device.ptpOffsetNs === "NA" ? "—" : panel.device.ptpOffsetNs + " ns"; color: panel.theme.text; font.family: panel.monoFont; font.pixelSize: 10 }
                    Label { text: "Path delay"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                    Label { text: panel.device.ptpPathDelayNs === "NA" ? "—" : panel.device.ptpPathDelayNs + " ns"; color: panel.theme.text; font.family: panel.monoFont; font.pixelSize: 10 }
                }
            }

            ColumnLayout {
                visible: panel.advancedVisible
                Layout.fillWidth: true
                spacing: 10

                Rectangle { Layout.fillWidth: true; height: 1; color: panel.theme.lineSoft }
                Label { text: "Advanced PTP profile"; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 12; font.weight: Font.DemiBold }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 4
                    columnSpacing: 10
                    rowSpacing: 7
                    Label { text: "Domain"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9 }
                    NumericField { id: domainField; theme: panel.theme; monoFont: panel.uiFont; text: panel.device.ptpDomain === "-" ? "0" : panel.device.ptpDomain; validator: IntValidator { bottom: 0; top: 255 } }
                    Label { text: "transportSpecific"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9 }
                    NumericField { id: transportField; theme: panel.theme; monoFont: panel.uiFont; text: panel.device.ptpTransportSpecific === "-" ? "0" : String(Number(panel.device.ptpTransportSpecific)); validator: IntValidator { bottom: 0; top: 15 } }
                    Label { text: "Announce interval (ms)"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9 }
                    NumericField { id: announceField; theme: panel.theme; monoFont: panel.uiFont; text: "1000"; validator: IntValidator { bottom: 100; top: 10000 } }
                    Label { text: "Sync interval (ms)"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9 }
                    NumericField { id: syncField; theme: panel.theme; monoFont: panel.uiFont; text: "250"; validator: IntValidator { bottom: 20; top: 5000 } }
                    CheckBox { id: vlanCheck; text: "VLAN"; font.family: panel.uiFont; font.pixelSize: 9 }
                    NumericField { id: vlanField; enabled: vlanCheck.checked; theme: panel.theme; monoFont: panel.uiFont; text: "100"; validator: IntValidator { bottom: 1; top: 4094 } }
                    Label { text: "VLAN PCP"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9 }
                    NumericField { id: pcpField; enabled: vlanCheck.checked; theme: panel.theme; monoFont: panel.uiFont; text: "4"; validator: IntValidator { bottom: 0; top: 7 } }
                    CheckBox { id: pdelayCheck; text: "Peer-delay response"; checked: true; font.family: panel.uiFont; font.pixelSize: 9 }
                    CalmButton {
                        theme: panel.theme
                        uiFont: panel.uiFont
                        text: "Apply advanced settings"
                        enabled: panel.session && panel.device.ptpAvailable && !panel.device.ptpRunning
                        onClicked: panel.applyProfile()
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: panel.theme.lineSoft }
                Label { text: "SV synchronization flag · lab / interoperability"; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 12; font.weight: Font.DemiBold }
                RowLayout {
                    Layout.fillWidth: true
                    ComboBox {
                        Layout.fillWidth: true
                        model: ["AUTO — measured PTP only", "0 — Not synchronized", "1 — Local synchronized (simulation)", "2 — Global synchronized (simulation)"]
                        currentIndex: panel.syncIndex(panel.device.smpSynchMode)
                        enabled: panel.session && panel.device.deviceVerified && !panel.session.updatingFirmware
                        font.family: panel.uiFont
                        font.pixelSize: 9
                        onActivated: panel.session.requestSmpSynch(["AUTO", "0", "1", "2"][currentIndex])
                    }
                    StateBadge {
                        theme: panel.theme
                        monoFont: panel.uiFont
                        state: "smpSynch=" + panel.device.smpSynchValue
                            + (panel.device.smpSynchSimulated ? " SIMULATED" : panel.device.smpSynchMeasured ? " MEASURED" : " SAFE")
                        stateColor: panel.device.smpSynchSimulated ? panel.theme.amber : panel.device.smpSynchMeasured ? panel.theme.green : panel.theme.muted
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: "PTP Source transmission is not proof that the SV clock is externally synchronized. AUTO stays fail-closed unless receiver discipline is measured."
                    wrapMode: Text.WordWrap
                    color: panel.theme.textSoft
                    font.family: panel.uiFont
                    font.pixelSize: 8
                }
            }

            Item { Layout.minimumHeight: 8 }
        }
    }
}
