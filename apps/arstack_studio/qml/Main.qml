// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import ARStack.Studio 1.0

ApplicationWindow {
    id: root
    width: 1024
    height: 748
    minimumWidth: 940
    minimumHeight: 640
    visible: true
    title: "ARStack Studio · SMV + PTP · v" + Qt.application.version
    color: studioTheme.bg
    // Dark control palette inherited by native Controls (ComboBox/CheckBox).
    // Without this the platform Windows style can render black text on
    // Studio's intentionally dark backgrounds.
    palette.window: studioTheme.bg
    palette.windowText: studioTheme.text
    palette.base: studioTheme.surface2
    palette.text: studioTheme.text
    palette.button: studioTheme.raised
    palette.buttonText: studioTheme.text
    palette.highlight: studioTheme.accentSoft
    palette.highlightedText: studioTheme.text

    StudioTheme { id: studioTheme }
    Settings {
        id: scenarioSettings
        category: "offline-injection-lab"
        property string lastScenario: ""
    }
    Timer {
        id: saveScenarioTimer
        interval: 400
        onTriggered: {
            if (!m4Pages.active && !root.restoringScenario)
                scenarioSettings.lastScenario = JSON.stringify(root.scenarioSnapshot())
        }
    }
    function adoptRunningBoardScenario(snapshot) {
        if (m4Pages.active) return // M4 drafts are never V1 board readback.
        if (!device.deviceVerified || !device.running ||
            device.scenarioApplying || reconnectScenarioRevision < 0 ||
            reconnectScenarioRevision !== scenarioRevision ||
            restoringScenario || !snapshot || snapshot.boot !== device.bootId ||
            !snapshot.current || snapshot.current.length !== 4 ||
            !snapshot.voltage || snapshot.voltage.length !== 4)
            return
        if (typeof snapshot.hz !== "number" || !validFrequency(snapshot.hz))
            return
        var ct = snapshot.ct
        // The complete board scenario includes waveform shaping. Omitting CT
        // would make the next user edit silently overwrite live CT saturation.
        if (!ct || typeof ct.enabled !== "boolean" ||
            typeof ct.dc !== "number" || typeof ct.harmonic !== "number" ||
            typeof ct.order !== "number" || typeof ct.clip !== "number" ||
            !isFinite(ct.dc) || !isFinite(ct.harmonic) || !isFinite(ct.clip) ||
            ct.dc < -300 || ct.dc > 300 ||
            ct.harmonic < 0 || ct.harmonic > 300 ||
            ct.order < 2 || ct.order > 63 || ct.order !== Math.floor(ct.order) ||
            ct.clip < 1 || ct.clip > 1000 ||
            (ct.enabled && snapshot.hz === 0))
            return
        var groups = [snapshot.current, snapshot.voltage]
        var scales = [currentScale, voltageScale]
        for (var group = 0; group < 2; ++group) {
            for (var i = 0; i < 4; ++i) {
                var channel = groups[group][i]
                if (!channel || typeof channel.counts !== "number" ||
                    typeof channel.angleMdeg !== "number" ||
                    typeof channel.quality !== "number" ||
                    typeof channel.enabled !== "boolean" ||
                    !isFinite(channel.counts) || !isFinite(channel.angleMdeg) ||
                    !isFinite(channel.quality))
                    return
            }
        }
        restoringScenario = true
        for (var g = 0; g < 2; ++g) {
            var model = g === 0 ? currentModel : voltageModel
            for (var row = 0; row < 4; ++row) {
                var b = groups[g][row]
                model.setProperty(row, "magnitude", b.counts / scales[g])
                model.setProperty(row, "phase", b.angleMdeg / 1000.0)
                model.setProperty(row, "quality", b.quality)
                model.setProperty(row, "enabled", b.enabled)
            }
        }
        signalFrequency = snapshot.hz
        ctSaturationEnabled = ct.enabled
        ctDcOffsetPercent = ct.dc
        ctHarmonicPercent = ct.harmonic
        ctHarmonicOrder = ct.order
        ctClipPercent = ct.clip
        previousAcFrequency = signalFrequency > 0 ? signalFrequency : 50
        frequencyField.text = signalFrequency.toFixed(3)
        // Board phase angles are authoritative, not the locally persisted
        // three-phase editor coupling preference.
        phaseLink = false
        selectSignal(activeGroup, activeRow)
        refreshPreview()
        restoringScenario = false
        autoLiveQueuedRevision = -1
        autoLiveApplyTimer.stop()
        pendingApply = false
        reconnectScenarioRevision = -1
        // Reconnection is a read-only adoption, NEVER a CFG, START or STOP.
        root.showMessage("4I+4V values synchronized from the running ESP32-P4.", false)
    }

    function scenarioSnapshot() {
        function values(model) {
            var rows = []
            for (var i = 0; i < 4; ++i) {
                const r = model.get(i)
                rows.push([r.magnitude, r.phase, r.enabled, Number(r.quality)])
            }
            return rows
        }
        return { version: 1, hz: signalFrequency, current: values(currentModel),
                 voltage: values(voltageModel), linked: phaseLink }
    }
    // One full 4I+4V firmware transaction per committed edit, coalesced across
    // focus/Enter/context-menu events. Never emit serial commands per keystroke.
    Timer {
        id: autoLiveApplyTimer
        interval: 40
        repeat: false
        onTriggered: root.drainAutoLiveEdits()
    }
    function scheduleAutoLiveEdit() {
        if (!device.running || !device.deviceVerified || !workflowBar.injectionRunning)
            return
        autoLiveQueuedRevision = scenarioRevision
        autoLiveApplyTimer.restart()
    }
    function drainAutoLiveEdits() {
        if (autoLiveQueuedRevision < 0) return
        if (!device.deviceVerified || !device.running || !workflowBar.injectionRunning) {
            autoLiveQueuedRevision = -1
            return
        }
        // No START or STOP: wait for the previous verified CFG COMMIT+readback
        // to complete, then submit the newest snapshot, never stale drafts.
        if (!workflowBar.session || !workflowBar.session.liveControlReady ||
            device.scenarioApplying) return
        autoLiveQueuedRevision = -1
        // On rejection, retain pendingApply and surface the error. Do not
        // silently retry forever or advance the claimed applied revision.
        applyLiveScenario()
    }
    function markScenarioDirty() {
        if (restoringScenario) return
        if (m4Pages.active) {
            m4Pages.saveCurrent(startScenarioSnapshot(false))
            pendingApply = true
            scenarioRevision += 1
            return // No legacy single-SV serial mutation in M4 draft mode.
        }
        saveScenarioTimer.restart()
        pendingApply = true
        scenarioRevision += 1
        scheduleAutoLiveEdit()
    }
    function restoreLocalScenario() {
        const saved = scenarioSettings.lastScenario
        if (!saved || saved.length > 8192) return
        try {
            const data = JSON.parse(saved)
            if (data.version !== 1 || typeof data.hz !== "number"
                    || !Number.isFinite(data.hz) || !validFrequency(data.hz)
                    || !Array.isArray(data.current) || !Array.isArray(data.voltage)
                    || data.current.length !== 4 || data.voltage.length !== 4)
                throw new Error("Invalid scenario format")
            function validate(rows, group) {
                for (var i = 0; i < 4; ++i) {
                    const row = rows[i]
                    if (!Array.isArray(row) || row.length !== 4
                            || typeof row[0] !== "number" || !Number.isFinite(row[0])
                            || (data.hz === 0 ? Math.abs(row[0]) : row[0]) > maximumMagnitude(group)
                            || (data.hz !== 0 && row[0] < 0)
                            || typeof row[1] !== "number" || !Number.isFinite(row[1])
                            || !validPhase(row[1]) || typeof row[2] !== "boolean"
                            || typeof row[3] !== "number" || !Number.isInteger(row[3])
                            || row[3] < 0 || row[3] > 4294967295)
                        throw new Error("Invalid channel value")
                }
            }
            validate(data.current, 0)
            validate(data.voltage, 1)
            restoringScenario = true
            // Legacy presets can contain disabled output rows. This simple
            // matrix has no channel enable controls by design. At application
            // startup ONLY (never during live RUNNING), migrate disabled
            // outputs to enabled channels with zero magnitude, preserving
            // the previous zero-output effect without hidden OFF flags.
            var migratedDisabled = 0
            function assign(model, rows) {
                for (var i = 0; i < 4; ++i) {
                    const wasDisabled = rows[i][2] === false
                    model.setProperty(i, "magnitude", wasDisabled ? 0 : rows[i][0])
                    model.setProperty(i, "phase", rows[i][1])
                    model.setProperty(i, "enabled", true)
                    model.setProperty(i, "quality", rows[i][3])
                    if (wasDisabled) migratedDisabled += 1
                }
            }
            assign(currentModel, data.current)
            assign(voltageModel, data.voltage)
            signalFrequency = data.hz
            previousAcFrequency = data.hz > 0 ? data.hz : 50
            frequencyField.text = data.hz.toFixed(3)
            phaseLink = data.linked === true
            selectSignal(activeGroup, activeRow)
            refreshPreview()
            restoringScenario = false
            pendingApply = true
            if (migratedDisabled > 0) {
                // Persist the migration to avoid redoing it on next launch.
                // Explicitly notify the operator; no serial output is sent.
                scenarioSettings.lastScenario = JSON.stringify(scenarioSnapshot())
                showMessage("Legacy preset: " + migratedDisabled +
                    " disabled channel(s) restored as enabled at 0 magnitude. Review before START.", true)
            }
        } catch (e) {
            restoringScenario = false
            scenarioSettings.lastScenario = ""
            showMessage("Invalid local experiment; safe 4I+4V defaults restored.", true)
        }
    }
    Component.onCompleted: restoreLocalScenario()

    property string uiFont: interFont.status === FontLoader.Ready ? interFont.name : "Inter"
    property string monoFont: uiFont
    property bool phaseLink: false
    property bool profileDirty: false
    property bool pendingApply: true
    property int scenarioRevision: 0
    property int liveSubmissionRevision: -1
    property int autoLiveQueuedRevision: -1
    // Only adopt a complete same-boot bank if no operator edit intervened.
    property int reconnectScenarioRevision: -1
    // Session-only operator intent. Never persist across application launches:
    // an explicit PTP ON can be restored after a firmware reflash/reconnect
    // without introducing an uncontrolled power-on publisher.
    property bool ptpDesiredForSession: false
    property string ptpDesiredRole: "SOURCE"
    property bool ptpRestoreAfterFirmware: false
    property int ptpRestoreChecks: 0
    property bool ptpDefaultStartAttempted: false
    property bool restoringScenario: false
    property bool offlinePreviewRunning: false
    property real currentScale: 1000.0
    property real voltageScale: 100.0
    property real signalFrequency: 50.0
    property real previousAcFrequency: 50.0
    property bool ctSaturationEnabled: false
    property real ctDcOffsetPercent: 30.0
    property real ctHarmonicPercent: 28.0
    property int ctHarmonicOrder: 2
    property real ctClipPercent: 60.0
    property string activeSignal: "Ia"
    property int activeGroup: 0
    property int activeRow: 0
    property string activeUnit: "A"
    property real activeMagnitude: 1.0
    property real activePhase: 0.0
    property real activeQuality: 0
    property string transientMessage: ""
    property string statusMessage: ""
    property bool transientError: false
    property bool phasorDockVisible: true
    property bool phasorDetached: false
    property bool waveformDockVisible: true
    property bool waveformDetached: false
    property bool telemetryDockVisible: true
    property bool telemetryExpanded: false
    property bool applicationShutdownRequested: false

    font.family: root.uiFont

    // Closing Studio retires its serial observer; SV/PTP still belong to
    // the LAN-connected board until explicit STOP or an actual hardware fault.
    onClosing: function(close) {
        close.accepted = true
        if (root.applicationShutdownRequested) return
        root.applicationShutdownRequested = true
        configurationWindow.hide()
        detachedPhasorWindow.hide()
        detachedWaveformWindow.hide()
        // The debounced persistence timer can still be pending when the user
        // closes immediately after editing. Flush the local draft once, while
        // ListModels and Settings remain alive; no device command is sent.
        saveScenarioTimer.stop()
        if (!root.restoringScenario && !m4Pages.active)
            scenarioSettings.lastScenario = JSON.stringify(root.scenarioSnapshot())
        // C++ owns process termination and bounded worker retirement. Do not
        // race it with a second QML quit path.
    }

    FontLoader {
        id: interFont
        source: Qt.resolvedUrl("../assets/InterVariable.ttf")
    }

    readonly property bool compactLayout: width < 1220
    readonly property bool canDeploy: workflowBar.session ? workflowBar.session.canDeployProfile : false
    readonly property bool canStart: workflowBar.session ? workflowBar.session.startReady : false
    readonly property string toastMessage: transientMessage
    readonly property bool toastError: transientError
    // One toast per real unsolicited stop edge, not per historical SVLIFE
    // readback or additional TX-fault detail arriving during SHOW.
    property double lastNotifiedSvStopIncident: 0

    SclProfileModel { id: sclProfiles }
    M4StreamScenarioModel { id: m4Pages }
    DeviceController { id: device }
    property var m4LegacyDraft: null

    // One set of native editors and preview canvases. The page model stores
    // independent per-IED drafts; no parallel QML waveform/phasor engines.
    function restoreM4Draft(draft) {
        if (!draft || !draft.current || !draft.voltage ||
            draft.current.length !== 4 || draft.voltage.length !== 4) return false
        restoringScenario = true
        const groups = [draft.current, draft.voltage]
        const models = [currentModel, voltageModel]
        for (var g = 0; g < 2; ++g) {
            for (var row = 0; row < 4; ++row) {
                const ch = groups[g][row]
                models[g].setProperty(row, "magnitude", ch[0])
                models[g].setProperty(row, "phase", ch[1])
                models[g].setProperty(row, "enabled", ch[2])
                models[g].setProperty(row, "quality", ch[3])
            }
        }
        signalFrequency = draft.hz
        previousAcFrequency = draft.hz > 0 ? draft.hz : 50
        frequencyField.text = signalFrequency.toFixed(3)
        frequencyField.frequencyDirty = false
        frequencyField.invalidInput = false
        phaseLink = draft.linked === true
        if (draft.ct) {
            ctSaturationEnabled = draft.ct.enabled
            ctDcOffsetPercent = draft.ct.dc
            ctHarmonicPercent = draft.ct.harmonic
            ctHarmonicOrder = draft.ct.order
            ctClipPercent = draft.ct.clip
        }
        selectSignal(activeGroup, activeRow)
        refreshPreview()
        restoringScenario = false
        return true
    }
    function m4OpenDraft() {
        if (!sclProfiles.m4DraftSupported || m4Pages.active || device.running ||
            device.scenarioApplying || workflowBar.startQueued) {
            showMessage("M4 draft requires a qualified 3-SV SCD and stopped board.", true)
            return
        }
        if (!root.commitFrequencyEditor() ||
            !currentMatrix.flushPendingEdits() || !voltageMatrix.flushPendingEdits()) return
        const before = startScenarioSnapshot(false)
        if (!before || !m4Pages.openThree(sclProfiles.m4RosterRows(), before)) {
            showMessage("M4 stream identities invalid: cannot open three pages.", true)
            return
        }
        m4LegacyDraft = before
        // Flush the legacy experiment once before the MU editor becomes active.
        saveScenarioTimer.stop()
        scenarioSettings.lastScenario = JSON.stringify(root.scenarioSnapshot())
        autoLiveApplyTimer.stop()
        autoLiveQueuedRevision = -1
        showMessage("Three MU pages ready. Use Deploy 3 MU for verified firmware roster upload; START remains gated.", false)
    }
    function m4SelectPage(slot) {
        if (!m4Pages.active || slot === m4Pages.selectedSlot) return
        // A tab click need not blur the focused NumericField; commit first.
        if (!root.commitFrequencyEditor() ||
            !currentMatrix.flushPendingEdits() || !voltageMatrix.flushPendingEdits()) return
        const next = m4Pages.select(slot, startScenarioSnapshot(false))
        if (!next || !restoreM4Draft(next))
            showMessage("MU switch rejected: incomplete scenario.", true)
    }
    function m4CloseDraft() {
        if (!m4Pages.active) return
        // Never let the pending legacy save timer persist the outgoing MU.
        saveScenarioTimer.stop()
        const previous = m4LegacyDraft
        m4Pages.clear()
        m4LegacyDraft = null
        if (previous) restoreM4Draft(previous)
        showMessage("M4 draft pages closed. Legacy 4I+4V editor restored.", false)
    }
    Connections {
        target: sclProfiles
        function onSourceChanged() {
            if (m4Pages.active) root.m4CloseDraft()
        }
    }

    Connections {
        target: device
        function onProfileStateChanged() {
            if (device.profileArmed && !device.profileDeploying)
                root.profileDirty = false
        }
        function onDeviceMessage(message) {
            root.showMessage(message, false)
        }
        function onLastErrorChanged() {
            if (device.lastError && device.lastError.length > 0)
                root.showMessage(device.lastError, true)
        }
        function onSvStopReasonChanged() {
            // SVLIFE reports the previous stop even when the board is healthy
            // and idle after reconnect. Toast only a new in-session fault.
            if (!device.deviceVerified || device.running ||
                !device.svUnexpectedStopObserved ||
                device.svStopReason === "" ||
                device.svStopReason === "AWAITING_BOARD_CAUSE" ||
                device.svUnexpectedStopIncident <= root.lastNotifiedSvStopIncident)
                return
            root.lastNotifiedSvStopIncident = device.svUnexpectedStopIncident
            root.showMessage(
                "SV stopped unexpectedly (" + device.svStopReason +
                "). Open diagnostics for the current-board TX error.", true)
        }
        function onDeviceVerifiedChanged() {
            root.reconnectScenarioRevision = device.deviceVerified
                ? root.scenarioRevision : -1
            root.pendingApply = true
            root.liveSubmissionRevision = -1
            root.autoLiveQueuedRevision = -1
            autoLiveApplyTimer.stop()
            root.offlinePreviewRunning = false
        }
        function onBoardScenarioChanged() {
            root.adoptRunningBoardScenario(device.boardScenario)
        }
        function onRunningChanged() {
            // An operator STOP or a firmware fault cancels unsent live edits.
            // Reconnect/START must never replay edits from a previous session.
            if (!device.running) {
                root.autoLiveQueuedRevision = -1
                autoLiveApplyTimer.stop()
            }
        }
        function onScenarioApplicationFinished() {
            // The board ACK/readback gate is now idle. If the operator edited
            // another field during the transaction, drain that newest edit.
            if (root.autoLiveQueuedRevision >= 0 && device.running)
                autoLiveApplyTimer.restart()
        }
        function onLiveScenarioApplied() {
            if (root.liveSubmissionRevision >= 0 &&
                root.liveSubmissionRevision === root.scenarioRevision) {
                root.pendingApply = false
                root.showMessage("4I+4V live setpoints verified by firmware readback.", false)
            } else {
                root.pendingApply = true
                root.showMessage("Live setpoints verified; newer edits remain pending.", false)
            }
            root.liveSubmissionRevision = -1
        }
    }

    Connections {
        target: workflowBar.session
        function onStateChanged() {
            // If a committed edit was queued while the serial/session
            // transport was briefly not ready, deliver it once the
            // verified RUNNING session becomes writable again.
            if (root.autoLiveQueuedRevision >= 0 && device.running &&
                !device.scenarioApplying)
                autoLiveApplyTimer.restart()
        }
        function onFirmwareUpdateFinished(success) {
            // Reset during firmware install stops the board's PTP SOURCE.
            // If the operator explicitly enabled it in this live Studio
            // session, restore once after the verified board is ready.
            // Do not preserve intent after a fresh Studio launch.
            if (success && root.ptpDesiredForSession) {
                root.ptpRestoreChecks = 0
                root.ptpRestoreAfterFirmware = true
            }
        }
    }

    // Operator-friendly default: start SOURCE with the verified profile
    // once the connected board is READY, never in a blind reconnect loop.
    // A deliberate OFF cancels this default for the rest of the session.
    Timer {
        interval: 500
        repeat: true
        running: !root.ptpDefaultStartAttempted
        onTriggered: {
            const session = workflowBar.session
            if (!session || !device.deviceVerified || !session.startReady ||
                    session.updatingFirmware || device.ptpStarting) return
            root.ptpDefaultStartAttempted = true
            if (!device.ptpRunning && !root.startPtpRole("SOURCE"))
                root.showMessage("PTP could not start automatically; use PTP On to retry.", true)
        }
    }

    Timer {
        interval: 300
        repeat: true
        running: root.ptpRestoreAfterFirmware
        onTriggered: {
            // Time-bounded one-shot restoration: never retry indefinitely.
            root.ptpRestoreChecks += 1
            if (root.ptpRestoreChecks > 120) {
                root.ptpRestoreAfterFirmware = false
                root.showMessage("PTP restart was not confirmed after reconnect. Use PTP On to retry.", true)
                return
            }
            const session = workflowBar.session
            if (!session || session.updatingFirmware || !session.startReady ||
                    !device.deviceVerified || !device.ptpAvailable ||
                    device.ptpStarting)
                return
            root.ptpRestoreAfterFirmware = false
            if (device.ptpRunning || !root.ptpDesiredForSession) return
            if (!root.startPtpRole(root.ptpDesiredRole))
                root.showMessage("PTP did not restart after firmware installation. Use PTP On to retry.", true)
        }
    }

    Timer {
        id: messageTimer
        interval: 2600
        onTriggered: {
            root.transientMessage = ""
            root.transientError = false
            root.statusMessage = ""
        }
    }

    ListModel {
        id: currentModel
        ListElement { signalId: "Ia"; magnitude: 1.000; phase: 0.0; enabled: true; quality: 0; traceColor: "#ff6673" }
        ListElement { signalId: "Ib"; magnitude: 1.000; phase: -120.0; enabled: true; quality: 0; traceColor: "#e6b552" }
        ListElement { signalId: "Ic"; magnitude: 1.000; phase: 120.0; enabled: true; quality: 0; traceColor: "#59a7ff" }
        ListElement { signalId: "In"; magnitude: 0.000; phase: 0.0; enabled: true; quality: 0; traceColor: "#9a88df" }
    }

    ListModel {
        id: voltageModel
        ListElement { signalId: "Ua"; magnitude: 57.740; phase: 0.0; enabled: true; quality: 0; traceColor: "#ff6673" }
        ListElement { signalId: "Ub"; magnitude: 57.740; phase: -120.0; enabled: true; quality: 0; traceColor: "#e6b552" }
        ListElement { signalId: "Uc"; magnitude: 57.740; phase: 120.0; enabled: true; quality: 0; traceColor: "#59a7ff" }
        ListElement { signalId: "Un"; magnitude: 0.000; phase: 0.0; enabled: true; quality: 0; traceColor: "#9a88df" }
    }

    ListModel {
        id: statusHistoryModel
        ListElement { timeText: "--:--:--"; messageText: "ARStack Studio ready."; isError: false }
    }

    property string lastToastKey: ""
    property double lastToastAt: 0
    function explainFailure(message) {
        const m = String(message || "")
        if (m.indexOf("ETHERNET_LINK_DOWN") >= 0 ||
            m.indexOf("Ethernet PHY link is DOWN") >= 0 ||
            m.indexOf("source did not emit") >= 0 ||
            m.indexOf("no verified Announce") >= 0)
            return "PTP belum bisa aktif. Sambungkan kabel LAN ESP32-P4 ke switch/port Ethernet aktif, tunggu PHY UP, lalu START PTP."
        if (m.indexOf("ETHERNET_DRIVER_NOT_STARTED") >= 0 ||
            m.indexOf("ETHERNET_DRIVER_UNAVAILABLE") >= 0)
            return "Driver Ethernet ESP32-P4 belum siap. Periksa koneksi board, lalu restart perangkat bila driver tetap STOPPED."
        if (m.indexOf("ETHERNET_TX_INVALID_STATE") >= 0 ||
            m.indexOf("ESP_ERR_INVALID_STATE") >= 0)
            return "PTP TX ditolak EMAC. Periksa Driver/PHY di Advanced → Time Sync; salin diagnostics jika PHY sudah UP."
        if (m.indexOf("LEASE_EXPIRED") >= 0)
            return "Injeksi SV berhenti: heartbeat kontrol USB terputus. Periksa kabel USB/COM lalu START kembali."
        if (m.indexOf("RUNTIME_FAULT") >= 0)
            return "Board menghentikan injeksi SV karena gangguan runtime. Buka Copy diagnostics sebelum START ulang."
        return m
    }
    function showMessage(message, error) {
        if (!message || String(message).trim().length === 0) return
        statusMessage = message
        const critical = error === true
        const userMessage = critical ? explainFailure(message) : String(message)
        const key = (critical ? "E:" : "I:") + userMessage
        const now = Date.now()
        const duplicate = key === lastToastKey && now - lastToastAt < 4000
        const meaningful = critical || /accepted|ready|connected|installed|updated|stopped|started|saved|completed|success/i.test(userMessage)
        if (meaningful && !duplicate) {
            lastToastKey = key
            lastToastAt = now
            transientMessage = userMessage
            transientError = critical
            messageTimer.interval = critical ? 4200 : 2300
            messageTimer.restart()
        }
        if (critical && !duplicate && device)
            device.recordUiDiagnostic(String(message) + " | displayed=" + userMessage)
        statusHistoryModel.append({
            timeText: new Date().toLocaleTimeString(Qt.locale(), "HH:mm:ss"),
            messageText: message,
            isError: error === true
        })
        while (statusHistoryModel.count > 100)
            statusHistoryModel.remove(0)
    }

    function detachPhasor() {
        phasorDockVisible = false
        phasorDetached = true
        detachedPhasorWindow.raise()
        detachedPhasorWindow.requestActivate()
    }

    function detachWaveform() {
        waveformDockVisible = false
        waveformDetached = true
        detachedWaveformWindow.raise()
        detachedWaveformWindow.requestActivate()
    }

    function openEngineeringFile() {
        workflowBar.openEngineeringDialog()
    }

    function openCapture() {
        // A single first-class Studio capture workspace. Reuse the same
        // lifetime-owned panel even while changing advanced tabs.
        profilePanel.showCapture()
        openConfiguration()
    }

    function openConfiguration() {
        configurationWindow.show()
        configurationWindow.raise()
        configurationWindow.requestActivate()
    }

    function openTimingSettings() {
        profilePanel.showTiming()
        openConfiguration()
    }

    function startPtpRole(role) {
        const accepted = profilePanel.startPtpRole(role)
        if (accepted) {
            root.ptpDesiredForSession = true
            root.ptpDesiredRole = role
        }
        return accepted
    }

    function requestStopPtp() {
        root.ptpDefaultStartAttempted = true
        root.ptpDesiredForSession = false
        root.ptpRestoreAfterFirmware = false
        return workflowBar.session && workflowBar.session.requestStopPtp()
    }

    function openDiagnostics() {
        diagnosticsDialog.open()
    }

    function deploySelectedProfile() {
        if (canDeploy && workflowBar.session.requestProfileSync())
            profileDirty = true
    }

    function groupModel(group) {
        return group === 0 ? currentModel : voltageModel
    }

    function groupMatrix(group) {
        return group === 0 ? currentMatrix : voltageMatrix
    }

    function parseOperatorNumber(text) {
        var normalized = String(text).trim()
            .replace(/degrees?/ig, "")
            .replace(/deg/ig, "")
            .replace(/°/g, "")
            .replace(/Hz/ig, "")
            .replace(/[AV]$/i, "").trim()
        // Decimal comma and dot are equally valid. Never interpret mixed
        // punctuation or thousands-grouping as a silently altered number.
        if (normalized.indexOf(",") >= 0 && normalized.indexOf(".") < 0)
            normalized = normalized.replace(",", ".")
        if (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)$/.test(normalized))
            return NaN
        return Number(normalized)
    }

    function maximumMagnitude(group) {
        var scale = group === 0 ? currentScale : voltageScale
        if (!isFinite(scale) || scale <= 0)
            return 0
        return Math.min(1000000000.0, 2147483647.0 / scale)
    }

    function validMagnitude(group, value) {
        return isFinite(value) &&
            (signalFrequency === 0 ? Math.abs(value) : value) <= maximumMagnitude(group) &&
            (signalFrequency === 0 || value >= 0)
    }

    function validPhase(value) {
        return isFinite(value) && Math.abs(value) <= 360000.0
    }

    function validFrequency(value) {
        return isFinite(value) && value >= 0 && value <= 1000.0
    }

    function normalizedAngle(value) {
        var result = value % 360
        if (result > 180) result -= 360
        if (result <= -180) result += 360
        return result
    }

    function refreshPreview() {
        if (previewPanel) previewPanel.requestPaint()
        if (waveformPanel) waveformPanel.requestPaint()
        if (detachedPhasorPanel) detachedPhasorPanel.requestPaint()
        if (detachedWaveformPanel) detachedWaveformPanel.requestPaint()
    }

    function selectSignal(group, row) {
        var signal = groupModel(group).get(row)
        activeGroup = group
        activeRow = row
        activeSignal = signal.signalId
        activeUnit = group === 0 ? "A" : "V"
        activeMagnitude = signal.magnitude
        activePhase = signal.phase
        activeQuality = Number(signal.quality)
        refreshPreview()
    }

    function focusCell(group, row, column) {
        var nextGroup = group
        var nextRow = row
        if (nextRow < 0) { nextGroup = (nextGroup + 1) % 2; nextRow = 3 }
        if (nextRow > 3) { nextGroup = (nextGroup + 1) % 2; nextRow = 0 }
        groupMatrix(nextGroup).focusCell(nextRow, column)
    }

    function navigate(group, row, column, key) {
        if (key === Qt.Key_Up) focusCell(group, row - 1, column)
        else if (key === Qt.Key_Down) focusCell(group, row + 1, column)
        else if (key === Qt.Key_Left) column === 1 ? focusCell(group, row, 0) : focusCell(group, row - 1, 1)
        else if (key === Qt.Key_Right) column === 0 ? focusCell(group, row, 1) : focusCell(group, row + 1, 0)
    }

    function sendSignal(group, row) {
        if (m4Pages.active) return false
        if (!workflowBar.session || !workflowBar.session.liveControlReady) return true
        var signal = groupModel(group).get(row)
        return workflowBar.session.requestSetSignal(
            signal.signalId,
            signal.magnitude,
            signal.phase,
            Number(signal.quality),
            currentScale,
            voltageScale)
    }

    function sendLinkedGroup(group) {
        var ok = true
        for (var i = 0; i < 3; ++i) ok = sendSignal(group, i) && ok
        return ok
    }

    function applyGroupSignals(group) {
        for (var row = 0; row < 4; ++row) sendSignal(group, row)
    }

    function editSignal(group, row, field, value) {
        if ((field === "magnitude" && !validMagnitude(group, value)) ||
            (field === "phase" && !validPhase(value)))
            return false

        var model = groupModel(group)
        model.setProperty(row, field, value)

        if (phaseLink && row < 3) {
            if (field === "magnitude") {
                for (var i = 0; i < 3; ++i) model.setProperty(i, "magnitude", value)
            } else {
                var base = value
                if (row === 1) base += 120
                if (row === 2) base -= 120
                model.setProperty(0, "phase", normalizedAngle(base))
                model.setProperty(1, "phase", normalizedAngle(base - 120))
                model.setProperty(2, "phase", normalizedAngle(base + 120))
            }
        }

        selectSignal(group, row)
        // Running edits only change the draft. Apply live sends one verified
        // eight-channel transaction instead of partial per-channel SETs.
        markScenarioDirty()
        return true
    }

    function applyAllSignals() {
        if (m4Pages.active) {
            showMessage("M4 pages are offline drafts until three-stream deploy/readback is available.", true)
            return false
        }
        if (workflowBar.injectionRunning) return applyLiveScenario()
        if (!workflowBar.session || !workflowBar.session.liveControlReady) return false
        var ok = workflowBar.session.requestSetFrequency(signalFrequency)
        for (var group = 0; group < 2; ++group)
            for (var row = 0; row < 4; ++row) ok = sendSignal(group, row) && ok
        ok = workflowBar.session.requestSetCtSaturation(ctSaturationEnabled, ctDcOffsetPercent,
                 ctHarmonicPercent, ctHarmonicOrder, ctClipPercent) && ok
        pendingApply = !ok
        showMessage(ok ? "Local draft commands queued. Verify physical SV on wire; SETs are not atomic."
                       : "One or more draft commands could not be queued; retry after checking the board.", !ok)
        return ok
    }
    function startScenarioSnapshot(flushDrafts) {
        // Flush every local draft once before serializing the canonical
        // 4I+4V scenario. An invalid draft cannot be sent to the board.
        if (flushDrafts !== false &&
            (!currentMatrix.flushPendingEdits() || !voltageMatrix.flushPendingEdits())) {
            showMessage("Invalid signal input: correct highlighted field before Start.", true)
            return null
        }
        // START always serializes the same ListModels that draw the table,
        // including all eight current phase angles and enabled flags.
        // Never silently START with an invisible disabled channel. The
        // legacy restore path migrates at boot; an unexpected future state
        // must be corrected explicitly instead of serialized unnoticed.
        for (var group = 0; group < 2; ++group) {
            var rows = groupModel(group)
            for (var row = 0; row < 4; ++row) {
                if (rows.get(row).enabled !== true) {
                    showMessage("Hidden disabled channel detected. Restore the scenario before START.", true)
                    return null
                }
            }
        }
        var draft = scenarioSnapshot()
        draft.currentScale = currentScale
        draft.voltageScale = voltageScale
        draft.ct = { enabled: ctSaturationEnabled, dc: ctDcOffsetPercent,
                     harmonic: ctHarmonicPercent, order: ctHarmonicOrder, clip: ctClipPercent }
        return draft
    }

    function applyLiveScenario() {
        if (m4Pages.active) return false
        if (!workflowBar.injectionRunning || !workflowBar.session ||
            !workflowBar.session.liveControlReady || device.scenarioApplying) return false
        // Live must transmit committed model values only. A second text cell
        // that is still being typed remains a draft until Enter/focus-out.
        const snapshot = startScenarioSnapshot(false)
        if (!snapshot) return false
        const revision = scenarioRevision
        if (!workflowBar.session.requestApplyLiveScenario(snapshot)) {
            pendingApply = true
            showMessage("Live setpoints not queued; check board and retry.", true)
            return false
        }
        liveSubmissionRevision = revision
        showMessage("Applying eight setpoints; awaiting firmware commit/readback.", false)
        return true
    }

    function setFrequencyValue(value) {
        if (!validFrequency(value)) return false
        if (signalFrequency === value) {
            frequencyField.text = value.toFixed(3)
            frequencyField.frequencyDirty = false
            frequencyField.invalidInput = false
            return true
        }
        signalFrequency = value
        if (value > 0) previousAcFrequency = value
        frequencyField.text = value.toFixed(3)
        frequencyField.frequencyDirty = false
        frequencyField.invalidInput = false
        refreshPreview()
        markScenarioDirty()
        return true
    }
    function commitFrequencyEditor() {
        if (!frequencyField.frequencyDirty) return true
        const value = parseOperatorNumber(frequencyField.text)
        if (!validFrequency(value)) {
            frequencyField.text = signalFrequency.toFixed(3)
            frequencyField.invalidInput = false
            frequencyField.frequencyDirty = false
            showMessage("Frequency must be within 0..1000 Hz (0 = DC).", true)
            return false
        }
        frequencyField.frequencyDirty = false
        return setFrequencyValue(value)
    }

    function setWaveformMode(mode) {
        if (mode === "DC") {
            if (ctSaturationEnabled) setCtSaturation(false)
            if (signalFrequency > 0) previousAcFrequency = signalFrequency
            setFrequencyValue(0)
            showMessage("DC mode selected. Magnitude is an instantaneous signed value; phase is not used.", false)
        } else {
            setFrequencyValue(previousAcFrequency > 0 ? previousAcFrequency : 50)
            showMessage("AC mode selected. Frequency and phase controls are active.", false)
        }
    }

    function setCtSaturation(enabled) {
        if (enabled && signalFrequency === 0) {
            showMessage("CT saturation shaping requires AC mode.", true)
            return false
        }
        ctSaturationEnabled = enabled
        refreshPreview()
        markScenarioDirty()
        if (!m4Pages.active && workflowBar.session && workflowBar.session.liveControlReady && !pendingApply)
            workflowBar.session.requestSetCtSaturation(enabled, ctDcOffsetPercent,
                                                       ctHarmonicPercent, ctHarmonicOrder, ctClipPercent)
        showMessage(enabled
            ? "CT saturation stress enabled · DC offset + 2nd harmonic + clipping approximation."
            : "CT saturation stress disabled.", false)
        return true
    }

    // One-click simulation shortcuts affect ABC only, never neutral or quality.
    function equalGroupMagnitudes(group, row) {
        if (row < 0 || row > 2) return
        var model = groupModel(group)
        var value = model.get(row).magnitude
        if (!validMagnitude(group, value)) return
        for (var i = 0; i < 3; ++i) model.setProperty(i, "magnitude", value)
        selectSignal(group, row)
        markScenarioDirty()
        if (workflowBar.session && workflowBar.session.liveControlReady && !pendingApply && !sendLinkedGroup(group)) {
            showMessage("ABC update was not accepted; inspect device diagnostics.", true)
            return
        }
        showMessage((pendingApply ? "Local experiment: " : "Device queue: ") + "ABC magnitude queued from " + model.get(row).signalId, false)
    }

    function equalGroupAngles(group, row) {
        if (row < 0 || row > 2) return
        var model = groupModel(group)
        var value = model.get(row).phase
        if (!validPhase(value)) return
        for (var i = 0; i < 3; ++i) model.setProperty(i, "phase", value)
        selectSignal(group, row)
        markScenarioDirty()
        if (workflowBar.session && workflowBar.session.liveControlReady && !pendingApply && !sendLinkedGroup(group)) {
            showMessage("ABC update was not accepted; inspect device diagnostics.", true)
            return
        }
        showMessage((pendingApply ? "Local experiment: " : "Device queue: ") + "ABC angles queued from " + model.get(row).signalId, false)
    }

    function balanceGroupAngles(group, row) {
        if (row < 0 || row > 2) return
        var model = groupModel(group)
        var base = model.get(row).phase + (row === 1 ? 120 : row === 2 ? -120 : 0)
        if (!validPhase(model.get(row).phase)) return
        base = normalizedAngle(base)
        model.setProperty(0, "phase", base)
        model.setProperty(1, "phase", normalizedAngle(base - 120))
        model.setProperty(2, "phase", normalizedAngle(base + 120))
        selectSignal(group, row)
        markScenarioDirty()
        if (workflowBar.session && workflowBar.session.liveControlReady && !pendingApply && !sendLinkedGroup(group)) {
            showMessage("ABC update was not accepted; inspect device diagnostics.", true)
            return
        }
        showMessage((pendingApply ? "Local experiment: " : "Device queue: ") + "Balanced ABC angles queued from " + model.get(row).signalId, false)
    }

    function balanced() {
        var angles = [0, -120, 120, 0]
        for (var i = 0; i < 4; ++i) {
            currentModel.setProperty(i, "magnitude", i < 3 ? 1.0 : 0.0)
            currentModel.setProperty(i, "phase", angles[i])
            currentModel.setProperty(i, "enabled", true)
            voltageModel.setProperty(i, "magnitude", i < 3 ? 57.74 : 0.0)
            voltageModel.setProperty(i, "phase", angles[i])
            voltageModel.setProperty(i, "enabled", true)
        }
        selectSignal(activeGroup, activeRow)
        refreshPreview()
        markScenarioDirty()
        if (!m4Pages.active && workflowBar.session && workflowBar.session.liveControlReady && !pendingApply) applyAllSignals()
    }

    function zeroAll() {
        for (var i = 0; i < 4; ++i) {
            currentModel.setProperty(i, "magnitude", 0.0)
            voltageModel.setProperty(i, "magnitude", 0.0)
        }
        selectSignal(activeGroup, activeRow)
        refreshPreview()
        markScenarioDirty()
        if (!m4Pages.active && workflowBar.session && workflowBar.session.liveControlReady && !pendingApply)
            workflowBar.session.requestZero()
    }

    function setActiveQuality(value) {
        var unsignedValue = Number(value) >>> 0
        groupModel(activeGroup).setProperty(activeRow, "quality", unsignedValue)
        activeQuality = unsignedValue
        markScenarioDirty()
        if (!m4Pages.active && workflowBar.session && workflowBar.session.liveControlReady && !pendingApply)
            workflowBar.session.requestSetQuality(activeSignal, unsignedValue)
    }

    Shortcut { sequence: "Ctrl+B"; enabled: !workflowBar.startQueued; onActivated: root.balanced() }
    Shortcut { sequence: "Ctrl+0"; enabled: !workflowBar.startQueued; onActivated: root.zeroAll() }
    Shortcut { sequence: "F5"; enabled: workflowBar.session && !workflowBar.injectionRunning && !workflowBar.startQueued; onActivated: workflowBar.requestStart() }
    Shortcut { sequence: "F6"; enabled: workflowBar.session && (workflowBar.injectionRunning || workflowBar.startQueued || device.m4Deploying || device.m4ScenarioApplying); onActivated: workflowBar.requestStop() }

    Dialog {
        id: diagnosticsDialog
        width: Math.min(root.width * 0.72, 920)
        height: Math.min(root.height * 0.72, 590)
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "Device diagnostics"
        standardButtons: Dialog.Close
        background: Rectangle {
            color: studioTheme.surface
            radius: 8
            border.width: 1
            border.color: studioTheme.line
        }
        contentItem: ColumnLayout {
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: device.connected ? device.portName + " · 115200 8N1" : "Device offline"
                    color: studioTheme.muted
                    font.family: root.uiFont
                    font.pixelSize: 9
                }
                Item { Layout.fillWidth: true }
                CalmButton {
                    theme: studioTheme
                    uiFont: root.uiFont
                    text: "Copy diagnostics"
                    onClicked: {
                        const snapshot = workflowBar.session ? workflowBar.session.diagnosticsText() : ""
                        if (StudioClipboard.copyText(snapshot)) {
                            root.showMessage("Diagnostics copied to clipboard.", false)
                        } else {
                            root.showMessage("Clipboard unavailable. Select text below and press Ctrl+C.", true)
                        }
                    }
                }
                CalmButton {
                    theme: studioTheme
                    uiFont: root.uiFont
                    text: "Open log folder"
                    onClicked: {
                        if (!device.openDiagnosticLogFolder())
                            root.showMessage("Diagnostic log folder is not accessible.", true)
                    }
                }
                CalmButton { theme: studioTheme; uiFont: root.uiFont; text: "Clear"; onClicked: device.clearLog() }
            }
            Label {
                Layout.fillWidth: true
                text: "Auto-saved system log: " + device.diagnosticLogPath
                color: studioTheme.muted
                font.family: root.monoFont
                font.pixelSize: 9
                wrapMode: Text.WrapAnywhere
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                TextArea {
                    readOnly: true
                    // Include the complete same session/PTP snapshot used by
                    // the Copy button, rather than showing only raw serial lines.
                    // Selectable fall-back remains available if native clipboard
                    // ownership is denied by Windows or another process.
                    text: diagnosticsDialog.visible && workflowBar.session
                        ? workflowBar.session.diagnosticsText() : device.logText
                    selectByMouse: true
                    color: studioTheme.textSoft
                    selectionColor: studioTheme.accent
                    font.family: root.monoFont
                    font.pixelSize: 9
                    wrapMode: TextEdit.WrapAnywhere
                    background: Rectangle { color: "#090e14"; radius: 6; border.width: 1; border.color: studioTheme.lineSoft }
                }
            }
        }
    }



    footer: Rectangle {
        height: 28
        color: studioTheme.chrome
        border.width: 1
        border.color: studioTheme.lineSoft

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 8

            BoardConnectionIndicator {
                id: boardConnection
                device: device
                session: workflowBar.session
                theme: studioTheme
                uiFont: root.uiFont
                compact: root.compactLayout
                Layout.preferredWidth: implicitWidth
                Layout.preferredHeight: implicitHeight
            }
            Rectangle { width: 1; height: 12; color: studioTheme.lineSoft }
            Label {
                text: root.pendingApply
                    ? (workflowBar.injectionRunning
                        ? (device.scenarioApplying ? "4I + 4V · Applying live..." :
                           "4I + 4V · Auto-apply pending")
                        : "4I + 4V · Setpoints pending · Start to apply")
                    : "4I + 4V · 4000 samples/s"
                color: root.pendingApply
                    ? studioTheme.amber : studioTheme.muted
                font.family: root.uiFont
                font.pixelSize: 9
            }
            Item { Layout.fillWidth: true }
            Label {
                visible: root.statusMessage.length > 0
                Layout.maximumWidth: root.compactLayout ? 165 : 300
                text: root.statusMessage
                elide: Text.ElideRight
                color: root.transientError ? studioTheme.amber : studioTheme.textSoft
                font.family: root.uiFont
                font.pixelSize: 9
            }
            Label {
                visible: device.deviceVerified
                text: "PTP " + (device.ptpRunning ? device.ptpRole : "OFF") + " · smpSynch " + device.smpSynchValue
                color: device.ptpRunning ? studioTheme.green : studioTheme.muted
                font.family: root.uiFont
                font.pixelSize: 9
            }
            Label {
                visible: device.deviceVerified
                text: "FPS " + device.fps + "   MISSED " + device.missed + "   TX FAIL " + device.txFailures
                color: (Number(device.missed) > 0 || Number(device.txFailures) > 0) ? studioTheme.amber : studioTheme.textSoft
                font.family: root.monoFont
                font.pixelSize: 9
                font.weight: Font.Medium
            }
        }
    }

    Window {
        id: configurationWindow
        width: 910
        height: 740
        minimumWidth: 760
        minimumHeight: 610
        visible: false
        title: "ARStack Studio · Advanced"
        color: studioTheme.bg
        palette.window: studioTheme.bg
        palette.windowText: studioTheme.text
        palette.base: studioTheme.surface2
        palette.text: studioTheme.text
        palette.button: studioTheme.raised
        palette.buttonText: studioTheme.text

        onClosing: function(close) {
            close.accepted = false
            configurationWindow.hide()
        }

        StatusToast {
            anchors.centerIn: parent
            z: 1500
            theme: studioTheme
            uiFont: root.uiFont
            message: root.toastMessage
            error: root.toastError
        }

        DockFrame {
            anchors.fill: parent
            anchors.margins: 10
            theme: studioTheme
            titleText: "Advanced"
            statusText: device.deviceVerified ? "DEVICE CONNECTED" : "OFFLINE"
            uiFont: root.uiFont
            monoFont: root.monoFont
            closable: false

            ConfigurationHub {
                id: profilePanel
                anchors.fill: parent
                theme: studioTheme
                signalFrequency: root.signalFrequency
                controller: root
                device: device
                profiles: sclProfiles
                session: workflowBar.session
                uiFont: root.uiFont
                monoFont: root.monoFont
            }
        }
    }

    Window {
        id: detachedPhasorWindow
        width: 560
        height: 580
        minimumWidth: 400
        minimumHeight: 400
        visible: root.phasorDetached
        title: "ARStack Studio · Phasor"
        color: studioTheme.bg
        onClosing: function(close) {
            close.accepted = false
            root.phasorDetached = false
            root.phasorDockVisible = true
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 10
            color: studioTheme.bg
            SignalPreview {
                id: detachedPhasorPanel
                anchors.fill: parent
                theme: studioTheme
                currentModel: currentModel
                voltageModel: voltageModel
                uiFont: root.uiFont
                monoFont: root.monoFont
                compact: false
                viewMode: "phasor"
                activeSignal: root.activeSignal
                activeUnit: root.activeUnit
                activeMagnitude: root.activeMagnitude
                activePhase: root.activePhase
                signalFrequency: root.signalFrequency
                            previewRunning: root.offlinePreviewRunning && !device.deviceVerified
                ctSaturationEnabled: root.ctSaturationEnabled
                ctDcOffsetPercent: root.ctDcOffsetPercent
                ctHarmonicPercent: root.ctHarmonicPercent
                ctHarmonicOrder: root.ctHarmonicOrder
                ctClipPercent: root.ctClipPercent
            }
        }
    }

    Window {
        id: detachedWaveformWindow
        width: 720
        height: 480
        minimumWidth: 480
        minimumHeight: 340
        visible: root.waveformDetached
        title: "ARStack Studio · Waveform"
        color: studioTheme.bg
        onClosing: function(close) {
            close.accepted = false
            root.waveformDetached = false
            root.waveformDockVisible = true
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 10
            color: studioTheme.bg
            SignalPreview {
                id: detachedWaveformPanel
                anchors.fill: parent
                theme: studioTheme
                currentModel: currentModel
                voltageModel: voltageModel
                uiFont: root.uiFont
                monoFont: root.monoFont
                compact: false
                viewMode: "waveform"
                activeSignal: root.activeSignal
                activeUnit: root.activeUnit
                activeMagnitude: root.activeMagnitude
                activePhase: root.activePhase
                signalFrequency: root.signalFrequency
                            previewRunning: root.offlinePreviewRunning && !device.deviceVerified
                ctSaturationEnabled: root.ctSaturationEnabled
                ctDcOffsetPercent: root.ctDcOffsetPercent
                ctHarmonicPercent: root.ctHarmonicPercent
                ctHarmonicOrder: root.ctHarmonicOrder
                ctClipPercent: root.ctClipPercent
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        WorkflowBar {
            id: workflowBar
            objectName: "studioWorkflowBar"
            Layout.fillWidth: true
            Layout.preferredHeight: 78
            Layout.minimumHeight: 78
            theme: studioTheme
            controller: root
            device: device
            profiles: sclProfiles
            m4DraftOnly: m4Pages.active
            uiFont: root.uiFont
            monoFont: root.monoFont
            compact: root.compactLayout
        }

        RowLayout {
            id: m4PaginationStrip
            visible: sclProfiles.m4DraftSupported || m4Pages.active
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 31 : 0
            spacing: 5
            Label {
                text: "M4 · 3 SV"
                color: studioTheme.muted
                font.family: root.uiFont
                font.pixelSize: 10
            }
            Repeater {
                model: m4Pages.active ? 3 : 0
                delegate: Rectangle {
                    required property int index
                    implicitWidth: 114
                    implicitHeight: 26
                    radius: 5
                    color: m4Pages.selectedSlot === index ? studioTheme.accentSoft : studioTheme.surface2
                    border.width: 1
                    border.color: m4Pages.selectedSlot === index ? studioTheme.accent : studioTheme.lineSoft
                    Label {
                        anchors.centerIn: parent
                        text: "MU " + (index + 1)
                        font.family: root.uiFont
                        font.pixelSize: 10
                        color: studioTheme.text
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.m4SelectPage(index)
                    }
                }
            }
            Item { Layout.fillWidth: true }
            Label {
                visible: m4Pages.active
                text: device.m4ScenarioApplying ? "APPLYING · MU BANK READBACK REQUIRED"
                     : device.m4ScenariosVerified && device.m4DeployedHash === sclProfiles.m4ConfigurationHash ? "3/3 MU BANKS VERIFIED · START GATED"
                     : device.m4Deploying ? "UPLOADING · VERIFIED ACK REQUIRED"
                     : device.m4Ready && device.m4DeployedHash === sclProfiles.m4ConfigurationHash
                     ? "3 MU ROSTER VERIFIED · START GATED" : "DRAFT · NOT DEPLOYED"
                color: device.m4Ready && device.m4DeployedHash === sclProfiles.m4ConfigurationHash
                    ? studioTheme.green : studioTheme.amber
                font.family: root.uiFont
                font.pixelSize: 9
            }
            Button {
                visible: m4Pages.active
                enabled: m4Pages.active && workflowBar.session && workflowBar.session.canDeployMultiSv &&
                         !device.m4Deploying && !device.m4ScenarioApplying && !workflowBar.startQueued
                text: device.m4Deploying ? "Uploading…" : "Deploy 3 MU"
                font.family: root.uiFont
                font.pixelSize: 10
                onClicked: {
                    if (!root.commitFrequencyEditor() ||
                        !currentMatrix.flushPendingEdits() ||
                        !voltageMatrix.flushPendingEdits()) return
                    if (m4Pages.active)
                        m4Pages.saveCurrent(root.startScenarioSnapshot(false))
                    const accepted = workflowBar.session && workflowBar.session.requestDeployMultiSv()
                    root.showMessage(accepted
                        ? "Uploading ARM3. Board hash, all 3 MAC/svID/VLAN identities and SELECT ACK will be checked."
                        : "M4 deploy blocked. Check stopped board, firmware capability, or diagnostics.", !accepted)
                }
            }
            Button {
                visible: m4Pages.active
                enabled: m4Pages.active && workflowBar.session &&
                         workflowBar.session.canApplyMultiSvScenarios &&
                         !workflowBar.startQueued
                text: device.m4ScenarioApplying ? "Applying…" : "Apply 3 MU"
                font.family: root.uiFont
                font.pixelSize: 10
                onClicked: {
                    if (!root.commitFrequencyEditor() ||
                        !currentMatrix.flushPendingEdits() ||
                        !voltageMatrix.flushPendingEdits()) return
                    if (!m4Pages.saveCurrent(root.startScenarioSnapshot(false))) return
                    const drafts = [m4Pages.draftFor(0), m4Pages.draftFor(1), m4Pages.draftFor(2)]
                    const accepted = workflowBar.session.requestApplyMultiSvScenarios(drafts)
                    root.showMessage(accepted
                        ? "Applying 3 isolated MU signal banks with firmware COMMIT and READBACK."
                        : "Apply rejected: verify current SCD roster and all three scenario drafts.", !accepted)
                }
            }
            Button {
                visible: sclProfiles.m4DraftSupported || device.m4BoardSnapshots.length === 3
                enabled: workflowBar.session && workflowBar.session.canInspectMultiSvBoard &&
                         !workflowBar.startQueued
                text: device.m4BoardInspecting ? "Reading…" : "Inspect board"
                font.family: root.uiFont
                font.pixelSize: 10
                onClicked: {
                    const ok = workflowBar.session.requestInspectMultiSvBoard()
                    root.showMessage(ok
                        ? "Reading MU1/2/3 from the actual board; editor drafts remain unchanged."
                        : "Board inspection unavailable; check identity, serial ownership and firmware.", !ok)
                }
            }
            Button {
                enabled: !device.m4Deploying && !device.m4ScenarioApplying
                text: m4Pages.active ? "Close MU pages" : "Open MU pages"
                font.family: root.uiFont
                font.pixelSize: 10
                onClicked: m4Pages.active ? root.m4CloseDraft() : root.m4OpenDraft()
            }
        }

        RowLayout {
            visible: device.m4BoardSnapshots.length === 3
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 24 : 0
            spacing: 8
            Repeater {
                model: device.m4BoardSnapshots
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 23
                    radius: 5
                    color: studioTheme.surface2
                    border.color: studioTheme.lineSoft
                    border.width: 1
                    HoverHandler { id: bankHover }
                    ToolTip.visible: bankHover.hovered
                    ToolTip.delay: 500
                    ToolTip.text: {
                        const shape = modelData.ct
                        const channels = modelData.channels
                        const rows = channels.map(function(ch) {
                            return ch.id + " = " + ch.counts + " counts, phase " +
                                   ch.phaseMdeg + " mdeg, Q=" + ch.quality +
                                   ", " + (ch.enabled ? "enabled" : "disabled")
                        }).join("\n")
                        return "BOARD READBACK · MU" + modelData.slot +
                               "\nGen " + modelData.generation + " · hash " + modelData.hash +
                               "\nCT shaping: " + (shape.enabled ? "ON" : "OFF") +
                               " · DC " + shape.dcPermille + " ‰ · H " +
                               shape.harmonicPermille + " ‰ / order " + shape.order +
                               " · clip " + shape.clipPermille + " ‰\n" + rows
                    }
                    Label {
                        anchors.centerIn: parent
                        text: "BOARD MU" + modelData.slot + " · " +
                              (modelData.frequencyMilliHz / 1000).toFixed(3) +
                              " Hz · G" + modelData.generation +
                              " · TX " + (modelData.txEnabled ? "ON" : "OFF")
                        color: studioTheme.green
                        font.family: root.uiFont
                        font.pixelSize: 9
                    }
                }
            }
        }

        SplitView {
            id: shellSplit
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Vertical
            handle: Rectangle {
                implicitHeight: 9
                color: SplitHandle.pressed ? studioTheme.accentSoft
                     : SplitHandle.hovered ? studioTheme.raisedHover : "transparent"
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width
                    height: 1
                    color: SplitHandle.hovered || SplitHandle.pressed ? studioTheme.accent : studioTheme.line
                }
                HoverHandler { cursorShape: Qt.SplitVCursor }
            }

            SplitView {
                id: workspaceSplit
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.minimumHeight: 390
                orientation: Qt.Horizontal
                handle: Rectangle {
                    implicitWidth: 9
                    color: SplitHandle.pressed ? studioTheme.accentSoft
                         : SplitHandle.hovered ? studioTheme.raisedHover : "transparent"
                    Rectangle {
                        anchors.centerIn: parent
                        width: 1
                        height: parent.height
                        color: SplitHandle.hovered || SplitHandle.pressed ? studioTheme.accent : studioTheme.line
                    }
                    HoverHandler { cursorShape: Qt.SplitHCursor }
                }

                SurfacePanel {
                    objectName: "studioInjectionSurface"
                    SplitView.fillHeight: true
                    SplitView.minimumWidth: 405
                    SplitView.preferredWidth: 425
                    SplitView.maximumWidth: 465
                    SplitView.fillWidth: false
                    theme: studioTheme

                    // Clicking unoccupied workspace commits the focused cell.
                    // Controls above this background retain their own taps.
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.contentItem.forceActiveFocus()
                    }

                    ColumnLayout {
                        z: 1
                        anchors.fill: parent
                        // Injection width stays 405..465px at every desktop size.
                        // Never expand its children just because the whole window
                        // crosses the 1220px toolbar breakpoint.
                        anchors.margins: 10
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                spacing: 1
                                Label { text: "4I + 4V Injection"; color: studioTheme.text; font.family: root.uiFont; font.pixelSize: 17; font.weight: Font.DemiBold }
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                visible: workflowBar.injectionRunning
                                text: device.scenarioApplying ? "LIVE · VERIFYING" : "LIVE · AUTO"
                                color: device.scenarioApplying ? studioTheme.amber : studioTheme.green
                                font.family: root.uiFont
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 48
                            enabled: !workflowBar.startQueued
                            radius: 8
                            color: "#0b1219"
                            border.width: 0

                            StudioActionMenu {
                                id: frequencyMenu
                                objectName: "studioFrequencyMenu"
                                theme: studioTheme
                                uiFont: root.uiFont
                                x: 80
                                y: 45
                                title: "Frequency · shared across all channels"
                                commandRows: 4
                                Action { text: "DC · 0 Hz"; icon.source: Qt.resolvedUrl("../assets/material/block.svg"); onTriggered: root.setWaveformMode("DC") }
                                Action { text: "50 Hz · nominal"; icon.source: Qt.resolvedUrl("../assets/material/schedule.svg"); onTriggered: root.setFrequencyValue(50) }
                                Action { text: "60 Hz · nominal"; icon.source: Qt.resolvedUrl("../assets/material/schedule.svg"); onTriggered: root.setFrequencyValue(60) }
                                StudioMenuDivider { theme: studioTheme }
                                Action {
                                    text: "Restore previous AC"
                                    icon.source: Qt.resolvedUrl("../assets/material/refresh.svg")
                                    enabled: root.previousAcFrequency > 0
                                    onTriggered: root.setWaveformMode("AC")
                                }
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 11
                                anchors.rightMargin: 11
                                spacing: 8

                                Label {
                                    text: "Frequency"
                                    color: studioTheme.textSoft
                                    font.family: root.uiFont
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                    verticalAlignment: Text.AlignVCenter
                                }
                                NumericField {
                                    id: frequencyField
                                    ToolTip.visible: hovered && !activeFocus
                                    ToolTip.delay: 650
                                    ToolTip.text: "Right-click · Shift+F10"
                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.RightButton
                                        onClicked: frequencyMenu.popup()
                                    }
                                    theme: studioTheme
                                    monoFont: root.monoFont
                                    compact: true
                                    implicitWidth: 90
                                    text: "50.000"
                                    suffixText: "Hz"
                                    enabled: true
                                    validator: DoubleValidator { bottom: 0.0; top: 1000.0; decimals: 3 }
                                    property bool frequencyDirty: false
                                    onTextEdited: {
                                        frequencyDirty = true
                                        invalidInput = !root.validFrequency(
                                            root.parseOperatorNumber(text))
                                    }
                                    onEditingFinished: root.commitFrequencyEditor()
                                    onAccepted: root.commitFrequencyEditor()
                                    onActiveFocusChanged: {
                                        if (!activeFocus) root.commitFrequencyEditor()
                                    }
                                }

                                StudioLinkSwitch {
                                    theme: studioTheme
                                    uiFont: root.uiFont
                                    enabled: root.signalFrequency > 0 && !workflowBar.startQueued
                                    checked: root.phaseLink
                                    onToggled: {
                                        // Link is an editing preference, not a wire
                                        // setpoint. No 8-channel live replay or
                                        // pendingApply on a presentation-only switch.
                                        root.phaseLink = checked
                                        root.saveScenarioTimer.restart()
                                    }
                                }
                                Item { Layout.fillWidth: true }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            // Two stacked tables consume their actual content
                            // heights, never stretch four tiny rows into an
                            // oversized empty card at tall window sizes.
                            Layout.fillHeight: false
                            // Reserve vertical room for frequency, timing,
                            // and quality controls even at 960x620. Larger
                            // windows can use full natural matrix height.
                            Layout.preferredHeight: Math.min(410, Math.max(215, root.height - 365))
                            Layout.maximumHeight: 410
                            Layout.minimumHeight: 195
                            spacing: 8
                            SignalMatrix {
                                id: voltageMatrix
                                enabled: !workflowBar.startQueued
                                objectName: "studioVoltageMatrix"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.preferredHeight: 196
                                Layout.minimumHeight: 92
                                theme: studioTheme
                                controller: root
                                device: device
                                session: workflowBar.session
                                sourceModel: voltageModel
                                uiFont: root.uiFont
                                monoFont: root.monoFont
                                groupIndex: 1
                                titleText: "Voltage"
                                symbolText: "U"
                                unitText: root.signalFrequency === 0 ? "V DC" : "V RMS"
                                compact: true
                            }
                            SignalMatrix {
                                id: currentMatrix
                                enabled: !workflowBar.startQueued
                                objectName: "studioCurrentMatrix"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.preferredHeight: 196
                                Layout.minimumHeight: 92
                                theme: studioTheme
                                controller: root
                                device: device
                                session: workflowBar.session
                                sourceModel: currentModel
                                uiFont: root.uiFont
                                monoFont: root.monoFont
                                groupIndex: 0
                                titleText: "Current"
                                symbolText: "I"
                                unitText: root.signalFrequency === 0 ? "A DC" : "A RMS"
                                compact: true
                            }

                        }

                        StudioTimingTray {
                            objectName: "studioTimingTray"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 57
                            Layout.minimumHeight: 57
                            theme: studioTheme
                            controller: root
                            device: device
                            session: workflowBar.session
                            uiFont: root.uiFont
                            monoFont: root.monoFont
                            compact: true
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: studioTheme.lineSoft }
                        RowLayout {
                            objectName: "studioQualityRow"
                            Layout.fillWidth: true
                            spacing: 6
                            Rectangle { width: 6; height: 6; radius: 3; color: studioTheme.accent }
                            Label { text: root.activeSignal; color: studioTheme.textSoft; font.family: root.monoFont; font.pixelSize: studioTheme.labelSize; font.weight: Font.Bold }
                            // The same magnitude/phase is already visible in
                            // the active matrix row and plots. Removing this
                            // duplicate avoids squeezing the quality controls.
                            Rectangle { width: 1; height: 20; color: studioTheme.lineSoft }
                            QualityEditor {
                                enabled: !workflowBar.startQueued
                                Layout.fillWidth: true
                                theme: studioTheme
                                controller: root
                                device: device
                                uiFont: root.uiFont
                                monoFont: root.monoFont
                                qualityValue: root.activeQuality
                                compact: true
                            }
                        }
                        // No trailing filler: vertically stacked matrices
                        // expand to use the otherwise blank injection space.
                    }
                }

                SplitView {
                    visible: (root.phasorDockVisible && !root.phasorDetached) ||
                             (root.waveformDockVisible && !root.waveformDetached)
                    SplitView.preferredWidth: 560
                    SplitView.minimumWidth: 390
                    SplitView.fillWidth: true
                    SplitView.fillHeight: true
                    orientation: Qt.Vertical
                    handle: Rectangle {
                        implicitHeight: 9
                        color: SplitHandle.pressed ? studioTheme.accentSoft
                             : SplitHandle.hovered ? studioTheme.raisedHover : "transparent"
                        Rectangle {
                            anchors.centerIn: parent
                            width: parent.width
                            height: 1
                            color: SplitHandle.hovered || SplitHandle.pressed ? studioTheme.accent : studioTheme.line
                        }
                        HoverHandler { cursorShape: Qt.SplitVCursor }
                    }

                    DockFrame {
                        objectName: "studioPhasorDock"
                        visible: root.phasorDockVisible && !root.phasorDetached
                        SplitView.fillWidth: true
                        SplitView.minimumHeight: 170
                        SplitView.preferredHeight: Math.max(170, (workspaceSplit.height - 9) / 2)
                        SplitView.fillHeight: false
                        theme: studioTheme
                        titleText: "Phasor"
                        statusText: ""
                        uiFont: root.uiFont
                        monoFont: root.monoFont
                        detachable: true
                        onDetachRequested: root.detachPhasor()
                        onCloseRequested: root.phasorDockVisible = false
                        SignalPreview {
                            id: previewPanel
                            anchors.fill: parent
                            theme: studioTheme
                            currentModel: currentModel
                            voltageModel: voltageModel
                            uiFont: root.uiFont
                            monoFont: root.monoFont
                            compact: root.compactLayout
                            showHeader: false
                            viewMode: "phasor"
                            activeSignal: root.activeSignal
                            activeUnit: root.activeUnit
                            activeMagnitude: root.activeMagnitude
                            activePhase: root.activePhase
                            signalFrequency: root.signalFrequency
                            previewRunning: root.offlinePreviewRunning && !device.deviceVerified
                            ctSaturationEnabled: root.ctSaturationEnabled
                            ctDcOffsetPercent: root.ctDcOffsetPercent
                            ctHarmonicPercent: root.ctHarmonicPercent
                            ctHarmonicOrder: root.ctHarmonicOrder
                            ctClipPercent: root.ctClipPercent
                        }
                    }

                    DockFrame {
                        objectName: "studioWaveformDock"
                        visible: root.waveformDockVisible && !root.waveformDetached
                        SplitView.fillWidth: true
                        SplitView.minimumHeight: 170
                        SplitView.preferredHeight: Math.max(170, (workspaceSplit.height - 9) / 2)
                        SplitView.fillHeight: true
                        theme: studioTheme
                        titleText: "Waveform"
                        statusText: root.signalFrequency.toFixed(3) + " Hz"
                        uiFont: root.uiFont
                        monoFont: root.monoFont
                        detachable: true
                        onDetachRequested: root.detachWaveform()
                        onCloseRequested: root.waveformDockVisible = false
                        SignalPreview {
                            id: waveformPanel
                            anchors.fill: parent
                            theme: studioTheme
                            currentModel: currentModel
                            voltageModel: voltageModel
                            uiFont: root.uiFont
                            monoFont: root.monoFont
                            compact: root.compactLayout
                            showHeader: false
                            viewMode: "waveform"
                            activeSignal: root.activeSignal
                            activeUnit: root.activeUnit
                            activeMagnitude: root.activeMagnitude
                            activePhase: root.activePhase
                            signalFrequency: root.signalFrequency
                            previewRunning: root.offlinePreviewRunning && !device.deviceVerified
                            ctSaturationEnabled: root.ctSaturationEnabled
                            ctDcOffsetPercent: root.ctDcOffsetPercent
                            ctHarmonicPercent: root.ctHarmonicPercent
                            ctHarmonicOrder: root.ctHarmonicOrder
                            ctClipPercent: root.ctClipPercent
                        }
                    }
                }
            }

            TelemetryDock {
                visible: root.telemetryDockVisible && root.telemetryExpanded
                SplitView.fillWidth: true
                SplitView.minimumHeight: root.telemetryExpanded ? 88 : 32
                SplitView.maximumHeight: root.telemetryExpanded ? 320 : 32
                SplitView.preferredHeight: root.telemetryExpanded ? 124 : 32
                Behavior on SplitView.preferredHeight { NumberAnimation { duration: 220; easing.type: Easing.InOutCubic } }
                theme: studioTheme
                device: device
                currentModel: currentModel
                voltageModel: voltageModel
                historyModel: statusHistoryModel
                uiFont: root.uiFont
                monoFont: root.monoFont
                expanded: root.telemetryExpanded
                onExpandedChanged: root.telemetryExpanded = expanded
                onCloseRequested: root.telemetryDockVisible = false
            }
        }
    }

    StatusToast {
        anchors.centerIn: parent
        z: 1500
        theme: studioTheme
        uiFont: root.uiFont
        message: root.toastMessage
        error: root.toastError
    }
}
