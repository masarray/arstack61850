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
    readonly property bool startPending: panel.device && panel.device.ptpStarting

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

    function ptpProfile() {
        return {
            domain: Number(domainField.text),
            transportSpecific: Number(transportField.text),
            vlanEnabled: vlanCheck.checked,
            vlanId: Number(vlanField.text),
            vlanPriority: Number(pcpField.text),
            announceIntervalMs: Number(announceField.text),
            syncIntervalMs: Number(syncField.text),
            respondToPeerDelay: pdelayCheck.checked
        }
    }

    function applyProfile() {
        return panel.session && panel.session.requestConfigurePtp(panel.ptpProfile())
    }

    function startRole(role) {
        if (!panel.session || !panel.device.ptpAvailable || !panel.device.deviceVerified
                || panel.device.ptpRunning || panel.startPending || panel.session.updatingFirmware)
            return false
        const accepted = panel.session.requestStartPtpConfigured(role, panel.ptpProfile())
        if (!accepted && panel.controller)
            panel.controller.showMessage("PTP start blocked. Check timing diagnostics.", true)
        return Boolean(accepted)
    }

    function startSelectedMode() {
        return panel.startRole(panel.roleValues[modeCombo.currentIndex])
    }

    readonly property string activeRole: panel.device.ptpRole
    readonly property bool sourceRole: activeRole === "SOURCE"
    readonly property string svTimingEvidence: panel.device.smpSynchSimulated
        ? "SIMULATED FLAG · sample clock not necessarily synchronized"
        : panel.device.smpSynchMeasured
            ? "MEASURED SYNC FLAG · verify sample-phase evidence independently"
            : "FREE-RUN / UNQUALIFIED · PTP packets do not prove SV sample lock"
    readonly property bool receiverRole: activeRole === "RECEIVER"
    readonly property bool sourceEvidence: Number(panel.device.ptpAnnounceSent) > 0
        && Number(panel.device.ptpSyncSent) > 0
        && Number(panel.device.ptpFollowUpSent) > 0
        && Number(panel.device.ptpTxFailures) === 0
    readonly property bool stopPending: panel.startPending &&
        (panel.device.ptpStatus.indexOf("Stopping PTP") === 0 ||
         panel.device.ptpStatus.indexOf("Checking that board") === 0 ||
         panel.device.ptpStatus.indexOf("PTP STOP unverified") === 0)
    readonly property string headlineState: panel.stopPending
        ? (panel.device.ptpStatus.indexOf("unverified") !== -1 ? "STOP UNVERIFIED" : "STOPPING")
        : panel.startPending ? "STARTING"
        : panel.device.ptpRunning
            ? (panel.receiverRole ? (panel.device.ptpDiscipline === "LOCKED" ? "BOARD LOCKED" : panel.device.ptpDiscipline)
                : panel.activeRole + " ACTIVE")
            : panel.device.ptpStatus === "PTP start failed" ? "START FAILED" : "OFF"

    Timer {
        // Status refresh only. Native C++ verifies ACKs and owns timeouts.
        interval: 1000
        repeat: true
        running: panel.startPending || panel.device.ptpRunning
        onTriggered: if (panel.session) panel.session.requestPtpRefresh()
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
                StudioPtpSwitch {
                    id: ptpPower
                    theme: panel.theme
                    uiFont: panel.uiFont
                    text: "PTP"
                    checked: Boolean(panel.device.ptpRunning)
                    enabled: panel.session && panel.device.ptpAvailable
                        && !panel.startPending && !panel.session.updatingFirmware
                    ToolTip.visible: hovered
                    ToolTip.text: "PTP on/off · board verified"
                    onClicked: {
                        const wantsRunning = checked
                        checked = Qt.binding(function() { return Boolean(panel.device.ptpRunning) })
                        if (wantsRunning) panel.startSelectedMode()
                        else if (panel.session && !panel.session.requestStopPtp() && panel.controller)
                            panel.controller.showMessage("PTP stop was not accepted. Copy diagnostics.", true)
                    }
                }
                StateBadge {
                    theme: panel.theme
                    monoFont: panel.uiFont
                    state: panel.headlineState
                    stateColor: panel.headlineState === "START FAILED" ? panel.theme.red
                        : panel.receiverRole && panel.device.ptpRunning ? panel.theme.amber
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
                        StudioDarkComboBox {
                            id: modeCombo
                            Layout.fillWidth: true
                            theme: panel.theme
                            uiFont: panel.uiFont
                            model: ["Source — transmit PTP", "Follower — follow PTP source", "Monitor — receive only"]
                            currentIndex: panel.roleIndex(panel.activeRole)
                            enabled: panel.session && panel.device.ptpAvailable
                                && !panel.device.ptpRunning && !panel.startPending
                                && !panel.session.updatingFirmware
                            ToolTip.visible: hovered
                            ToolTip.text: "Source sends · Follower receives · Monitor observes"
                        }
                    }

                    ColumnLayout {
                        Layout.preferredWidth: 270
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

            // Simulation is intentionally simple: auto or explicit SV flag.
            // The firmware already marks overrides as simulated, independently
            // of PTP clock discipline and without pretending sample phase lock.
                Rectangle { Layout.fillWidth: true; height: 1; color: panel.theme.lineSoft }
                Label { text: "SV sample synchronization"; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 12; font.weight: Font.DemiBold }
                RowLayout {
                    Layout.fillWidth: true
                    StudioDarkComboBox {
                        id: svSyncCombo
                        Layout.fillWidth: true
                        theme: panel.theme
                        uiFont: panel.uiFont
                        model: ["AUTO — follow measured timing", "0 — Not synchronized",
                                "1 — Local flag (manual)", "2 — Global flag (manual)"]
                        currentIndex: panel.syncIndex(panel.device.smpSynchMode)
                        enabled: panel.session && panel.device.deviceVerified
                            && !panel.session.updatingFirmware
                        ToolTip.visible: hovered
                        ToolTip.text: "Manual 0/1/2 changes the transmitted SV flag, not the sample clock"
                        onActivated: {
                            const requested = ["AUTO", "0", "1", "2"][currentIndex]
                            const accepted = panel.session && panel.session.requestSmpSynch(requested)
                            // Show the device-confirmed state until the board actually
                            // accepts/readbacks the new flag, then update reactively.
                            // Qt.binding preserves the declaration over reconnects,
                            // rejected commands and external serial changes.
                            currentIndex = Qt.binding(function() {
                                return panel.syncIndex(panel.device.smpSynchMode)
                            })
                            if (!accepted && panel.controller)
                                panel.controller.showMessage("SV sync choice was not accepted. Copy diagnostics for details.", true)
                        }
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
                    text: panel.svTimingEvidence + "\nManual 0/1/2 sets only the transmitted flag."
                    wrapMode: Text.WordWrap
                    color: panel.theme.textSoft
                    font.family: panel.uiFont
                    font.pixelSize: 11
                }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                CalmButton {
                    theme: panel.theme
                    uiFont: panel.uiFont
                    tone: "accent"
                    text: panel.stopPending ? "Stop pending…"
                        : panel.startPending ? "Starting…"
                        : modeCombo.currentIndex === 0 ? "Start PTP Source"
                        : modeCombo.currentIndex === 1 ? "Start PTP Receiver" : "Start Monitor"
                    enabled: panel.session && panel.device.ptpAvailable && !panel.device.ptpRunning && !panel.startPending && !panel.session.updatingFirmware
                    onClicked: panel.startSelectedMode()
                }
                CalmButton {
                    theme: panel.theme
                    uiFont: panel.uiFont
                    tone: "danger"
                    text: panel.device.ptpStatus.indexOf("PTP STOP unverified") === 0 ? "Retry Stop PTP" : "Stop PTP"
                    enabled: panel.session && (panel.device.ptpRunning || panel.startPending) && !panel.session.updatingFirmware
                    onClicked: {
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
                CalmButton {
                    theme: panel.theme
                    uiFont: panel.uiFont
                    text: "Inspect SV lock"
                    enabled: panel.session && panel.device.deviceVerified &&
                             !panel.session.updatingFirmware
                    onClicked: {
                        if (!panel.session.requestSyncDiagnostics() && panel.controller)
                            panel.controller.showMessage(
                                "SV lock inspection is waiting for an active configuration transaction to finish.", true)
                    }
                }
                CalmButton {
                    theme: panel.theme
                    uiFont: panel.uiFont
                    text: "Copy diagnostics"
                    enabled: panel.session && panel.device.deviceVerified
                    onClicked: {
                        StudioClipboard.copyText(panel.session.diagnosticsText())
                        if (panel.controller) panel.controller.showMessage("PTP and firmware diagnostics copied.", false)
                    }
                }
                Item { Layout.fillWidth: true }
                StudioDarkCheckBox {
                    theme: panel.theme
                    uiFont: panel.uiFont
                    text: "Advanced settings"
                    checked: panel.advancedVisible
                    onToggled: panel.advancedVisible = checked
                    font.family: panel.uiFont
                    font.pixelSize: 10
                    palette.windowText: panel.theme.text
                    palette.text: panel.theme.text
                }
            }

            Rectangle {
                Layout.fillWidth: true
                radius: 8
                color: panel.theme.surface2
                border.width: 1
                border.color: panel.theme.lineSoft
                implicitHeight: flightContent.implicitHeight + 24
                ColumnLayout {
                    id: flightContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 12
                    spacing: 6
                    Label {
                        Layout.fillWidth: true
                        text: "SV LOCK · INTERNAL FLIGHT RECORDER"
                        color: panel.theme.text
                        font.family: panel.uiFont
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.fillWidth: true
                        text: panel.device.syncDiagnostics
                        color: panel.theme.textSoft
                        font.family: panel.monoFont
                        font.pixelSize: 9
                        wrapMode: Text.WrapAnywhere
                    }
                    Label {
                        Layout.fillWidth: true
                        text: "Last 16 board LOCK transitions are retained in RAM; no per-frame logging. Internal evidence only—not an independently verified PTP/IED lock."
                        color: panel.theme.muted
                        font.family: panel.uiFont
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                    }
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
                            ? "Board reports Announce + Sync + Follow_Up TX with zero failures. Independent timing is not qualified."
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
                    Label { text: panel.device.ptpDiscipline === "LOCKED" ? "LOCKED (board)" : panel.device.ptpDiscipline; color: panel.theme.amber; font.family: panel.monoFont; font.pixelSize: 10; font.weight: Font.DemiBold }
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
                    NumericField { id: syncField; theme: panel.theme; monoFont: panel.uiFont; text: "1000"; validator: IntValidator { bottom: 20; top: 5000 } }
                    StudioDarkCheckBox { id: vlanCheck; theme: panel.theme; uiFont: panel.uiFont; text: "VLAN" }
                    NumericField { id: vlanField; enabled: vlanCheck.checked; theme: panel.theme; monoFont: panel.uiFont; text: "100"; validator: IntValidator { bottom: 1; top: 4094 } }
                    Label { text: "VLAN PCP"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 9 }
                    NumericField { id: pcpField; enabled: vlanCheck.checked; theme: panel.theme; monoFont: panel.uiFont; text: "4"; validator: IntValidator { bottom: 0; top: 7 } }
                    StudioDarkCheckBox { id: pdelayCheck; theme: panel.theme; uiFont: panel.uiFont; text: "Peer-delay response"; checked: true }
                    CalmButton {
                        theme: panel.theme
                        uiFont: panel.uiFont
                        text: "Apply advanced settings"
                        enabled: panel.session && panel.device.ptpAvailable && !panel.device.ptpRunning && !panel.startPending
                        onClicked: {
                            if (!panel.applyProfile() && panel.controller)
                                panel.controller.showMessage("PTP settings rejected.", true)
                        }
                    }
                }


            }

            Item { Layout.minimumHeight: 8 }
        }
    }
}
