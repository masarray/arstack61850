// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import ARStack.Studio 1.0

ApplicationWindow {
    id: root
    width: 1240
    height: 760
    minimumWidth: 960
    minimumHeight: 620
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
        onTriggered: scenarioSettings.lastScenario = JSON.stringify(root.scenarioSnapshot())
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
    function markScenarioDirty() {
        if (restoringScenario) return
        saveScenarioTimer.restart()
        // While STOPPED, a new edit belongs to the next supervised Start batch.
        // Only a genuinely RUNNING board receives incremental live SETs.
        if (!workflowBar.injectionRunning ||
                !workflowBar.session || !workflowBar.session.liveControlReady)
            pendingApply = true
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
            function assign(model, rows) {
                for (var i = 0; i < 4; ++i) {
                    model.setProperty(i, "magnitude", rows[i][0])
                    model.setProperty(i, "phase", rows[i][1])
                    model.setProperty(i, "enabled", rows[i][2])
                    model.setProperty(i, "quality", rows[i][3])
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
    // Session-only operator intent. Never persist across application launches:
    // an explicit PTP ON can be restored after a firmware reflash/reconnect
    // without introducing an uncontrolled power-on publisher.
    property bool ptpDesiredForSession: false
    property bool ptpDefaultStartPending: true
    property bool ptpExplicitOff: false
    property string ptpDesiredRole: "SOURCE"
    property bool ptpRestoreAfterFirmware: false
    property int ptpRestoreChecks: 0
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

    // Primary-window close owns application exit. C++ teardown owns STOP, COM
    // close and worker retirement; QML must not race it with another async STOP.
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
        if (!root.restoringScenario)
            scenarioSettings.lastScenario = JSON.stringify(root.scenarioSnapshot())
        // C++ owns process termination and bounded worker retirement. Do not
        // race it with a second QML quit path.
    }

    FontLoader {
        id: interFont
        source: Qt.resolvedUrl("../assets/InterVariable.ttf")
    }

    readonly property bool compactLayout: width < 1120
    readonly property bool canDeploy: workflowBar.session ? workflowBar.session.canDeployProfile : false
    readonly property bool canStart: workflowBar.session ? workflowBar.session.startReady : false
    readonly property string toastMessage: transientMessage
    readonly property bool toastError: transientError

    SclProfileModel { id: sclProfiles }
    DeviceController { id: device }

    Connections {
        target: device
        function onProfileStateChanged() {
            if (device.profileArmed && !device.profileDeploying)
                root.profileDirty = false
        }
        function onDeviceMessage(message) {
            root.showMessage(message, false)
        }
        function onDeviceVerifiedChanged() {
            root.pendingApply = true
            root.offlinePreviewRunning = false
            if (device.deviceVerified && !root.ptpExplicitOff)
                root.ptpDefaultStartPending = true
        }
    }

    Connections {
        target: workflowBar.session
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

    // One automatic SOURCE start on verified connection. An explicit OFF
    // cancels this for the remainder of the Studio session; never send PTP
    // commands without a verified board or while firmware is updating.
    Timer {
        interval: 400
        repeat: true
        running: root.ptpDefaultStartPending && !root.ptpExplicitOff && device.deviceVerified
        onTriggered: {
            if (device.ptpRunning) {
                root.ptpDefaultStartPending = false
                return
            }
            const session = workflowBar.session
            if (!session || !session.startReady || session.updatingFirmware ||
                    device.ptpStarting || !device.ptpAvailable)
                return
            root.ptpDefaultStartPending = false
            if (!root.startPtpRole("SOURCE"))
                root.showMessage("PTP auto-start was not accepted. Use PTP On to retry.", true)
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

    function showMessage(message, error) {
        statusMessage = message
        transientMessage = error === true ? message : ""
        transientError = error === true
        statusHistoryModel.append({
            timeText: new Date().toLocaleTimeString(Qt.locale(), "HH:mm:ss"),
            messageText: message,
            isError: error === true
        })
        while (statusHistoryModel.count > 100)
            statusHistoryModel.remove(0)
        messageTimer.restart()
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
            root.ptpExplicitOff = false
            root.ptpDefaultStartPending = false
            root.ptpDesiredRole = role
        }
        return accepted
    }

    function requestStopPtp() {
        root.ptpDesiredForSession = false
        root.ptpDefaultStartPending = false
        root.ptpExplicitOff = true
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
            .replace(/\s/g, "")
            .replace(/degrees?/ig, "")
            .replace(/deg/ig, "")
            .replace(/°/g, "")
            .replace(/Hz/ig, "")
            .replace(/[AV]$/i, "")
        if (normalized.indexOf(",") >= 0 && normalized.indexOf(".") < 0)
            normalized = normalized.replace(",", ".")
        if (!normalized.length || normalized === "+" || normalized === "-" || normalized === ".")
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
        markScenarioDirty()
        if (workflowBar.session && workflowBar.session.liveControlReady && !pendingApply) {
            const sent = phaseLink && row < 3 ? sendLinkedGroup(group) : sendSignal(group, row)
            if (!sent) {
                pendingApply = true
                showMessage("Setpoint not queued. STOP and START to apply all displayed 4I+4V values.", true)
            }
        }
        return true
    }

    function applyAllSignals() {
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
    function startScenarioSnapshot() {
        var draft = scenarioSnapshot()
        draft.currentScale = currentScale
        draft.voltageScale = voltageScale
        draft.ct = { enabled: ctSaturationEnabled, dc: ctDcOffsetPercent,
                     harmonic: ctHarmonicPercent, order: ctHarmonicOrder, clip: ctClipPercent }
        return draft
    }

    function setFrequencyValue(value) {
        if (!validFrequency(value)) return false
        signalFrequency = value
        if (value > 0) previousAcFrequency = value
        frequencyField.text = value.toFixed(3)
        frequencyField.invalidInput = false
        refreshPreview()
        markScenarioDirty()
        if (workflowBar.session && workflowBar.session.liveControlReady && !pendingApply &&
                !workflowBar.session.requestSetFrequency(value))
            pendingApply = true
        return true
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
        if (workflowBar.session && workflowBar.session.liveControlReady && !pendingApply)
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
        if (workflowBar.session && workflowBar.session.liveControlReady && !pendingApply) applyAllSignals()
    }

    function zeroAll() {
        for (var i = 0; i < 4; ++i) {
            currentModel.setProperty(i, "magnitude", 0.0)
            voltageModel.setProperty(i, "magnitude", 0.0)
        }
        selectSignal(activeGroup, activeRow)
        refreshPreview()
        markScenarioDirty()
        if (workflowBar.session && workflowBar.session.liveControlReady && !pendingApply &&
                !workflowBar.session.requestZero())
            pendingApply = true
    }

    function setActiveQuality(value) {
        var unsignedValue = Number(value) >>> 0
        groupModel(activeGroup).setProperty(activeRow, "quality", unsignedValue)
        activeQuality = unsignedValue
        markScenarioDirty()
        if (workflowBar.session && workflowBar.session.liveControlReady && !pendingApply &&
                !workflowBar.session.requestSetQuality(activeSignal, unsignedValue))
            pendingApply = true
    }

    Shortcut { sequence: "Ctrl+B"; enabled: !workflowBar.startQueued; onActivated: root.balanced() }
    Shortcut { sequence: "Ctrl+0"; enabled: !workflowBar.startQueued; onActivated: root.zeroAll() }
    Shortcut { sequence: "F5"; enabled: workflowBar.session && workflowBar.session.state !== "RUNNING"; onActivated: workflowBar.requestStart() }
    Shortcut { sequence: "F6"; enabled: workflowBar.session && workflowBar.session.state === "RUNNING"; onActivated: workflowBar.requestStop() }

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
                CalmButton { theme: studioTheme; uiFont: root.uiFont; text: "Copy diagnostics"; onClicked: StudioClipboard.copyText(workflowBar.session.diagnosticsText()) }
                CalmButton { theme: studioTheme; uiFont: root.uiFont; text: "Clear"; onClicked: device.clearLog() }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                TextArea {
                    readOnly: true
                    text: device.logText
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

            Label {
                text: !device.deviceVerified
                    ? "No ESP32 board detected · Preview mode only"
                    : device.portName + " · ESP32-P4" + (workflowBar.injectionRunning ? " · Injecting" : " · Ready")
                color: device.deviceVerified ? studioTheme.textSoft : studioTheme.muted
                font.family: root.uiFont
                font.pixelSize: 9
                font.weight: Font.Medium
            }
            Rectangle { width: 1; height: 12; color: studioTheme.lineSoft }
            Label {
                text: root.pendingApply
                    ? (workflowBar.injectionRunning
                        ? "4I + 4V · Changes pending · restart to apply"
                        : "4I + 4V · Changes sent on Start")
                    : "4I + 4V · 4000 samples/s"
                color: root.pendingApply ? studioTheme.amber : studioTheme.muted
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
            uiFont: root.uiFont
            monoFont: root.monoFont
            compact: root.compactLayout
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
                    SplitView.fillWidth: true
                    SplitView.fillHeight: true
                    SplitView.minimumWidth: root.compactLayout ? 500 : 550
                    SplitView.preferredWidth: root.compactLayout ? 700 : 860
                    theme: studioTheme

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: root.compactLayout ? 8 : 9
                        spacing: root.compactLayout ? 4 : 5

                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                spacing: 1
                                Label { text: "4I + 4V Injection"; color: studioTheme.text; font.family: root.uiFont; font.pixelSize: root.compactLayout ? 17 : 18; font.weight: Font.DemiBold }
                            }
                            Item { Layout.fillWidth: true }
                            // START owns the single ordered scenario transaction.
                            // No redundant Apply button in the operator workflow.
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
                                    compact: root.compactLayout
                                    implicitWidth: 90
                                    text: "50.000"
                                    suffixText: "Hz"
                                    enabled: true
                                    validator: DoubleValidator { bottom: 0.0; top: 1000.0; decimals: 3 }
                                    onTextEdited: {
                                        var value = root.parseOperatorNumber(text)
                                        if (root.validFrequency(value)) {
                                            invalidInput = false
                                            root.signalFrequency = value
                                            if (value > 0) root.previousAcFrequency = value
                                            root.refreshPreview()
                                            root.markScenarioDirty()
                                            if (workflowBar.session && workflowBar.session.liveControlReady && !root.pendingApply)
                                                workflowBar.session.requestSetFrequency(value)
                                        } else invalidInput = true
                                    }
                                    onEditingFinished: {
                                        var value = root.parseOperatorNumber(text)
                                        if (!root.validFrequency(value)) {
                                            text = root.signalFrequency.toFixed(3)
                                            invalidInput = false
                                            root.showMessage("Frequency must be within 0..1000 Hz (0 = DC).", true)
                                        } else text = value.toFixed(3)
                                    }
                                }

                                CalmButton { theme: studioTheme; uiFont: root.uiFont; text: "0"; implicitWidth: 44; toolTipText: "DC"; onClicked: root.setFrequencyValue(0) }
                                CalmButton { theme: studioTheme; uiFont: root.uiFont; text: "50"; implicitWidth: 44; onClicked: root.setFrequencyValue(50) }
                                CalmButton { theme: studioTheme; uiFont: root.uiFont; text: "60"; implicitWidth: 44; onClicked: root.setFrequencyValue(60) }
                                Rectangle { width: 1; height: 24; color: studioTheme.lineSoft }
                                StudioDarkCheckBox {
                                    theme: studioTheme
                                    uiFont: root.uiFont
                                    enabled: root.signalFrequency > 0
                                    checked: root.phaseLink
                                    text: "3-phase link"
                                    onToggled: {
                                        root.phaseLink = checked
                                        root.markScenarioDirty()
                                    }
                                    font.family: root.uiFont
                                    font.pixelSize: 9
                                }
                                Item { Layout.fillWidth: true }
                            }
                        }

                        // Stack voltage above current. Both tables share the
                        // available height instead of leaving an empty lower
                        // half; the inner rows stretch with the desktop.
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 408
                            Layout.preferredHeight: 422
                            spacing: 7
                            SignalMatrix {
                                id: voltageMatrix
                                enabled: !workflowBar.startQueued
                                objectName: "studioVoltageMatrix"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
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
                                compact: root.compactLayout
                            }
                            SignalMatrix {
                                id: currentMatrix
                                enabled: !workflowBar.startQueued
                                objectName: "studioCurrentMatrix"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
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
                                compact: root.compactLayout
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
                            compact: root.compactLayout
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: studioTheme.lineSoft }
                        RowLayout {
                            objectName: "studioQualityRow"
                            Layout.fillWidth: true
                            spacing: 8
                            Rectangle { width: 6; height: 6; radius: 3; color: studioTheme.accent }
                            Label { text: root.activeSignal; color: studioTheme.textSoft; font.family: root.monoFont; font.pixelSize: studioTheme.labelSize; font.weight: Font.Bold }
                            Label {
                                text: root.signalFrequency === 0
                                    ? root.activeMagnitude.toFixed(3) + " " + root.activeUnit + " DC"
                                    : root.activeMagnitude.toFixed(3) + " " + root.activeUnit + " RMS · " + root.activePhase.toFixed(2) + "°"
                                color: studioTheme.muted
                                font.family: root.uiFont
                                font.pixelSize: 9
                            }
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
                                compact: root.compactLayout
                            }
                        }
                    }
                }

                SplitView {
                    visible: (root.phasorDockVisible && !root.phasorDetached) ||
                             (root.waveformDockVisible && !root.waveformDetached)
                    SplitView.preferredWidth: root.compactLayout ? 300 : 380
                    SplitView.minimumWidth: root.compactLayout ? 270 : 300
                    SplitView.maximumWidth: 680
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
                        visible: root.phasorDockVisible && !root.phasorDetached
                        SplitView.fillWidth: true
                        SplitView.minimumHeight: 170
                        SplitView.preferredHeight: 225
                        SplitView.maximumHeight: 265
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
                        visible: root.waveformDockVisible && !root.waveformDetached
                        SplitView.fillWidth: true
                        SplitView.minimumHeight: 170
                        SplitView.preferredHeight: 328
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
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 38
        theme: studioTheme
        uiFont: root.uiFont
        message: root.toastMessage
        error: root.toastError
    }
}
