// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ARStack.Studio 1.0

ApplicationWindow {
    id: root
    width: 1480
    height: 900
    minimumWidth: 1080
    minimumHeight: 720
    visible: true
    title: "ARStack Studio · SMV Injector"
    color: studioTheme.bg

    StudioTheme { id: studioTheme }

    property string uiFont: interFont.status === FontLoader.Ready ? interFont.name : "Inter"
    property string monoFont: uiFont
    property bool phaseLink: false
    property bool profileDirty: false
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
    property bool transientError: false
    property bool phasorDockVisible: true
    property bool phasorDetached: false
    property bool waveformDockVisible: true
    property bool waveformDetached: false
    property bool telemetryDockVisible: true
    property bool telemetryExpanded: false

    font.family: root.uiFont

    FontLoader {
        id: interFont
        source: Qt.resolvedUrl("../assets/InterVariable.ttf")
    }

    readonly property bool compactLayout: width < 1300
    readonly property bool selectedProfileDeployable:
        sclProfiles.selectedProfile.compatibilityClass === "A" &&
        sclProfiles.selectedProfile.deviceSupport === "ready"
    readonly property bool canDeploy:
        device.deviceVerified && !device.running && selectedProfileDeployable && !device.profileDeploying
    readonly property bool canStart:
        device.deviceVerified && !device.running &&
        (!sclProfiles.hasProfiles || (device.profileArmed && !profileDirty))
    readonly property string instrumentState: {
        if (device.discovering || (device.connected && !device.deviceVerified)) return "VERIFYING"
        if (!device.connected) return "OFFLINE"
        if (device.profileDeploying) return "DEPLOYING"
        if (device.running) return "RUNNING"
        if (profileDirty) return "PROFILE CHANGED"
        if (sclProfiles.hasProfiles && !selectedProfileDeployable) return "PROFILE BLOCKED"
        if (sclProfiles.hasProfiles && !device.profileArmed) return "PROFILE VALIDATED"
        if (device.profileArmed) return "ARMED"
        return "CONNECTED"
    }
    readonly property color instrumentStateColor: {
        if (instrumentState === "RUNNING") return studioTheme.green
        if (instrumentState === "ARMED") return studioTheme.accent
        if (instrumentState === "VERIFYING" || instrumentState === "DEPLOYING" || instrumentState === "PROFILE CHANGED") return studioTheme.amber
        if (instrumentState === "PROFILE BLOCKED") return studioTheme.red
        return device.connected ? studioTheme.textSoft : studioTheme.muted
    }
    readonly property string stateReason: {
        if (device.discovering || (device.connected && !device.deviceVerified)) return device.discoveryStatus
        if (!device.connected) return device.discoveryStatus
        if (device.profileDeploying) return "Committing immutable SCL profile to the device."
        if (device.running) return "Live apply active · valid edits update SMV immediately."
        if (profileDirty) return "Selected profile changed · deploy before Start."
        if (sclProfiles.hasProfiles && !selectedProfileDeployable) return "Selected stream is outside the current ESP32-P4 deployment boundary."
        if (sclProfiles.hasProfiles && !device.profileArmed) return "Profile validated · deploy to arm the injector."
        if (device.profileArmed) return "Ready · live setpoint editing is armed."
        return "Connected · development setpoints ready."
    }
    readonly property string toastMessage: transientMessage.length ? transientMessage : device.lastError
    readonly property bool toastError: transientMessage.length ? transientError : device.lastError.length > 0

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
            if (device.deviceVerified)
                Qt.callLater(root.applyAllSignals)
        }
    }

    Timer {
        interval: 650
        running: true
        repeat: false
        onTriggered: device.autoDetectAndConnect()
    }

    Timer {
        id: messageTimer
        interval: 2600
        onTriggered: {
            root.transientMessage = ""
            root.transientError = false
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
        ListElement { timeText: "--:--:--"; messageText: "ARStack Studio ready. Edit locally or connect an injector."; isError: false }
    }

    function showMessage(message, error) {
        transientMessage = message
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
        profilePanel.openEngineeringFile()
    }

    function openConfiguration() {
        configurationWindow.show()
        configurationWindow.raise()
        configurationWindow.requestActivate()
    }

    function openDiagnostics() {
        diagnosticsDialog.open()
    }

    function deploySelectedProfile() {
        if (canDeploy && device.deployProfile(sclProfiles.selectedProfile))
            profileDirty = true
    }

    function runReadinessCheck() {
        if (sclProfiles.fatalError.length) {
            showMessage(sclProfiles.fatalError, true)
            return
        }
        if (sclProfiles.hasProfiles && !selectedProfileDeployable) {
            showMessage("Selected stream is valid SCL but outside the current ESP32-P4 deployment boundary.", true)
            return
        }
        if (!device.deviceVerified) {
            showMessage(sclProfiles.hasProfiles ?
                "Profile is valid. Waiting for injector recognition before deployment." :
                "Development setpoints are ready. Waiting for injector recognition.", false)
            return
        }
        if (sclProfiles.hasProfiles && (profileDirty || !device.profileArmed)) {
            showMessage("Profile is valid and the device is connected. Deploy to arm output.", false)
            return
        }
        showMessage(device.running ?
            "Output is running and live apply is active." :
            "Ready to start. Device and output state are consistent.", false)
    }

    Shortcut { sequence: "Ctrl+O"; onActivated: root.openEngineeringFile() }
    Shortcut { sequence: "Ctrl+B"; onActivated: root.balanced() }
    Shortcut { sequence: "Ctrl+0"; onActivated: root.zeroAll() }
    Shortcut { sequence: "Ctrl+K"; onActivated: root.runReadinessCheck() }
    Shortcut { sequence: "Ctrl+D"; onActivated: root.deploySelectedProfile() }
    Shortcut { sequence: "F5"; enabled: root.canStart; onActivated: device.start() }
    Shortcut { sequence: "F6"; enabled: device.connected && device.running; onActivated: device.stop() }

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
        if (previewPanel)
            previewPanel.requestPaint()
        if (waveformPanel)
            waveformPanel.requestPaint()
        if (detachedPhasorPanel)
            detachedPhasorPanel.requestPaint()
        if (detachedWaveformPanel)
            detachedWaveformPanel.requestPaint()
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
        if (nextRow < 0) {
            nextGroup = (nextGroup + 1) % 2
            nextRow = 3
        }
        if (nextRow > 3) {
            nextGroup = (nextGroup + 1) % 2
            nextRow = 0
        }
        groupMatrix(nextGroup).focusCell(nextRow, column)
    }

    function navigate(group, row, column, key) {
        if (key === Qt.Key_Up)
            focusCell(group, row - 1, column)
        else if (key === Qt.Key_Down)
            focusCell(group, row + 1, column)
        else if (key === Qt.Key_Left)
            column === 1 ? focusCell(group, row, 0) : focusCell(group, row - 1, 1)
        else if (key === Qt.Key_Right)
            column === 0 ? focusCell(group, row, 1) : focusCell(group, row + 1, 0)
    }

    function sendSignal(group, row) {
        if (!device.deviceVerified)
            return true
        var signal = groupModel(group).get(row)
        return device.setSignal(
            signal.signalId,
            signal.magnitude,
            signal.phase,
            Number(signal.quality),
            currentScale,
            voltageScale)
    }

    function sendLinkedGroup(group) {
        var ok = true
        for (var i = 0; i < 3; ++i)
            ok = sendSignal(group, i) && ok
        return ok
    }

    function applyGroupSignals(group) {
        for (var row = 0; row < 4; ++row)
            sendSignal(group, row)
    }

    function editSignal(group, row, field, value) {
        if ((field === "magnitude" && !validMagnitude(group, value)) ||
            (field === "phase" && !validPhase(value)))
            return false

        var model = groupModel(group)
        model.setProperty(row, field, value)

        if (phaseLink && row < 3) {
            if (field === "magnitude") {
                for (var i = 0; i < 3; ++i)
                    model.setProperty(i, "magnitude", value)
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
        if (device.deviceVerified) {
            if (phaseLink && row < 3)
                sendLinkedGroup(group)
            else
                sendSignal(group, row)
        }
        return true
    }

    function applyAllSignals() {
        if (!device.deviceVerified)
            return
        device.setFrequency(signalFrequency)
        for (var group = 0; group < 2; ++group)
            applyGroupSignals(group)
        device.setCtSaturation(ctSaturationEnabled, ctDcOffsetPercent,
                               ctHarmonicPercent, ctHarmonicOrder, ctClipPercent)
    }

    function setFrequencyValue(value) {
        if (!validFrequency(value))
            return false
        signalFrequency = value
        if (value > 0)
            previousAcFrequency = value
        frequencyField.text = value.toFixed(3)
        frequencyField.invalidInput = false
        refreshPreview()
        if (device.deviceVerified)
            device.setFrequency(value)
        return true
    }

    function setWaveformMode(mode) {
        if (mode === "DC") {
            if (ctSaturationEnabled)
                setCtSaturation(false)
            if (signalFrequency > 0)
                previousAcFrequency = signalFrequency
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
        if (device.deviceVerified)
            device.setCtSaturation(enabled, ctDcOffsetPercent,
                                   ctHarmonicPercent, ctHarmonicOrder, ctClipPercent)
        showMessage(enabled
            ? "CT saturation stress enabled · DC offset + 2nd harmonic + clipping approximation."
            : "CT saturation stress disabled.", false)
        return true
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
        if (device.deviceVerified)
            applyAllSignals()
    }

    function zeroAll() {
        for (var i = 0; i < 4; ++i) {
            currentModel.setProperty(i, "magnitude", 0.0)
            voltageModel.setProperty(i, "magnitude", 0.0)
        }
        selectSignal(activeGroup, activeRow)
        refreshPreview()
        if (device.deviceVerified)
            device.zero()
    }

    function setActiveQuality(value) {
        var unsignedValue = Number(value) >>> 0
        groupModel(activeGroup).setProperty(activeRow, "quality", unsignedValue)
        activeQuality = unsignedValue
        if (device.deviceVerified)
            device.setQuality(activeSignal, unsignedValue)
    }

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

    header: Rectangle {
        height: 50
        color: studioTheme.chrome
        border.width: 1
        border.color: studioTheme.lineSoft

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 15
            anchors.rightMargin: 15
            spacing: 9

            Rectangle {
                width: 30
                height: 30
                radius: 6
                color: "#101b27"
                border.width: 1
                border.color: "#315071"
                Image {
                    anchors.centerIn: parent
                    width: 17
                    height: 17
                    source: Qt.resolvedUrl("../assets/lucide/radio-tower.svg")
                    sourceSize.width: 34
                    sourceSize.height: 34
                }
            }

            ColumnLayout {
                spacing: 0
                Layout.alignment: Qt.AlignVCenter
                Label { text: "ARSTACK61850"; color: studioTheme.muted; font.family: root.uiFont; font.pixelSize: studioTheme.captionSize - 1; font.weight: Font.Bold; font.letterSpacing: 1.0; verticalAlignment: Text.AlignVCenter }
                Label { text: "SMV Injector"; color: studioTheme.text; font.family: root.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold; verticalAlignment: Text.AlignVCenter }
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                Layout.preferredWidth: root.compactLayout ? 160 : 190
                implicitHeight: 32
                radius: 7
                color: studioTheme.surface2
                border.width: 1
                border.color: device.deviceVerified ? "#2c674e" : (device.discovering ? "#705827" : studioTheme.lineSoft)

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: device.deviceVerified ? studioTheme.green : (device.discovering ? studioTheme.amber : studioTheme.muted2)
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        text: device.deviceVerified ? "Injector ready" : (device.discovering ? "Recognizing..." : "Injector offline")
                        color: device.deviceVerified ? studioTheme.green : studioTheme.textSoft
                        font.family: root.uiFont
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                    }
                    Label {
                        text: device.deviceVerified ? "•••" : "↻"
                        color: studioTheme.accent
                        font.family: root.uiFont
                        font.pixelSize: 13
                        font.weight: Font.Bold
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                MouseArea {
                    id: identityMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    enabled: !device.discovering
                    onClicked: device.deviceVerified ? root.openConfiguration() : device.autoDetectAndConnect()
                }
                ToolTip.visible: identityMouse.containsMouse
                ToolTip.text: device.deviceVerified
                    ? "ARStack ESP32-P4 · ID " + device.deviceId.slice(-6) + " · Protocol v" + device.protocolVersion
                    : device.discoveryStatus
                ToolTip.delay: 450
            }
        }
    }

    footer: Rectangle {
        height: 48
        color: studioTheme.chrome
        border.width: 1
        border.color: studioTheme.lineSoft

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 15
            anchors.rightMargin: 15
            spacing: 9

            StateBadge { theme: studioTheme; state: root.instrumentState; stateColor: root.instrumentStateColor; monoFont: root.monoFont }

            Label {
                Layout.maximumWidth: root.compactLayout ? 320 : 520
                text: root.stateReason
                color: studioTheme.muted
                font.family: root.uiFont
                font.pixelSize: studioTheme.captionSize
                elide: Text.ElideRight
            }

            Item { Layout.fillWidth: true }

            Label {
                visible: !root.compactLayout
                text: "Ctrl+O source   ·   Ctrl+K check   ·   F5 start   ·   F6 stop"
                color: studioTheme.muted
                font.family: root.uiFont
                font.pixelSize: studioTheme.captionSize
            }

            CalmButton {
                theme: studioTheme
                uiFont: root.uiFont
                text: "Diagnostics"
                onClicked: diagnosticsDialog.open()
            }
        }
    }

    Window {
        id: configurationWindow
        width: 760
        height: 780
        minimumWidth: 640
        minimumHeight: 660
        visible: false
        title: "ARStack Studio · Configuration"
        color: studioTheme.bg

        onClosing: function(close) {
            close.accepted = false
            configurationWindow.hide()
        }

        DockFrame {
            anchors.fill: parent
            anchors.margins: 10
            theme: studioTheme
            titleText: "Smart & Expert Configuration"
            statusText: device.deviceVerified ? "ESP32-P4 VERIFIED" : "OFFLINE EDIT"
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
        title: "ARStack Studio · Phasor View"
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
        title: "ARStack Studio · Waveform View"
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
        anchors.margins: root.compactLayout ? 9 : 11
        spacing: root.compactLayout ? 9 : 11

        WorkflowBar {
            Layout.fillWidth: true
            Layout.preferredHeight: 94
            Layout.minimumHeight: 94
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
            SplitView.fillWidth: true
            SplitView.fillHeight: true
            SplitView.minimumWidth: root.compactLayout ? 500 : 550
            SplitView.preferredWidth: root.compactLayout ? 700 : 860
            theme: studioTheme

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: root.compactLayout ? 12 : 15
                spacing: 9

                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        spacing: 1
                        Label { text: "INJECTION WORKSPACE"; color: studioTheme.muted; font.family: root.uiFont; font.pixelSize: studioTheme.captionSize; font.weight: Font.DemiBold; font.letterSpacing: 0.9 }
                        Label { text: "Manual Injection"; color: studioTheme.text; font.family: root.uiFont; font.pixelSize: root.compactLayout ? 18 : 20; font.weight: Font.DemiBold }
                    }
                    Item { Layout.fillWidth: true }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 53
                    radius: 7
                    color: studioTheme.surface2
                    border.width: 1
                    border.color: studioTheme.lineSoft

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 11
                        anchors.rightMargin: 11
                        spacing: 8

                        CalmButton { theme: studioTheme; uiFont: root.uiFont; text: "AC"; tone: root.signalFrequency > 0 ? "accent" : "normal"; implicitWidth: 46; onClicked: root.setWaveformMode("AC") }
                        CalmButton { theme: studioTheme; uiFont: root.uiFont; text: "DC"; tone: root.signalFrequency === 0 ? "accent" : "normal"; implicitWidth: 46; onClicked: root.setWaveformMode("DC") }

                        Rectangle { width: 1; height: 24; color: studioTheme.lineSoft }

                        ColumnLayout {
                            spacing: 0
                            Label { text: root.signalFrequency === 0 ? "DC MODE" : "EDITABLE FREQUENCY"; color: studioTheme.muted; font.family: root.uiFont; font.pixelSize: studioTheme.captionSize - 1; font.weight: Font.DemiBold; font.letterSpacing: 0.8 }
                            RowLayout {
                                spacing: 5
                                NumericField {
                                    id: frequencyField
                                    theme: studioTheme
                                    monoFont: root.monoFont
                                    compact: root.compactLayout
                                     implicitWidth: 84
                                     text: "50.000"
                                     suffixText: "Hz"
                                    enabled: root.signalFrequency > 0
                                    validator: DoubleValidator { bottom: 0.001; top: 1000.0; decimals: 3 }
                                    onTextEdited: {
                                        var value = root.parseOperatorNumber(text)
                                        if (root.validFrequency(value)) {
                                            invalidInput = false
                                            root.signalFrequency = value
                                            if (value > 0) root.previousAcFrequency = value
                                            root.refreshPreview()
                                            if (device.deviceVerified) device.setFrequency(value)
                                        } else {
                                            invalidInput = true
                                        }
                                    }
                                    onEditingFinished: {
                                        var value = root.parseOperatorNumber(text)
                                        if (!root.validFrequency(value)) {
                                            text = root.signalFrequency.toFixed(3)
                                            invalidInput = false
                                            root.showMessage("AC frequency must be greater than 0 and not exceed 1000 Hz.", true)
                                        } else {
                                            text = value.toFixed(3)
                                        }
                                    }
                                }
                            }
                        }

                        CalmButton { visible: root.signalFrequency > 0; theme: studioTheme; uiFont: root.uiFont; text: "50"; implicitWidth: 44; onClicked: root.setFrequencyValue(50) }
                        CalmButton { visible: root.signalFrequency > 0; theme: studioTheme; uiFont: root.uiFont; text: "60"; implicitWidth: 44; onClicked: root.setFrequencyValue(60) }

                        Rectangle { width: 1; height: 24; color: studioTheme.lineSoft }

                        CheckBox {
                            enabled: root.signalFrequency > 0
                            checked: root.phaseLink
                            text: "3-phase link"
                            onToggled: root.phaseLink = checked
                            font.family: root.uiFont
                            font.pixelSize: 9
                        }

                        Item { Layout.fillWidth: true }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 9

                    SignalMatrix {
                        id: currentMatrix
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        theme: studioTheme
                        controller: root
                        device: device
                        sourceModel: currentModel
                        uiFont: root.uiFont
                        monoFont: root.monoFont
                        groupIndex: 0
                        titleText: "Current"
                        symbolText: "I"
                        unitText: root.signalFrequency === 0 ? "A DC" : "A RMS"
                        compact: root.compactLayout
                    }

                    SignalMatrix {
                        id: voltageMatrix
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        theme: studioTheme
                        controller: root
                        device: device
                        sourceModel: voltageModel
                        uiFont: root.uiFont
                        monoFont: root.monoFont
                        groupIndex: 1
                        titleText: "Voltage"
                        symbolText: "U"
                        unitText: root.signalFrequency === 0 ? "V DC" : "V RMS"
                        compact: root.compactLayout
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: studioTheme.lineSoft }

                RowLayout {
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
                        Layout.fillWidth: true
                        theme: studioTheme
                        controller: root
                        device: device
                        uiFont: root.uiFont
                        monoFont: root.monoFont
                        qualityValue: root.activeQuality
                        compact: root.compactLayout
                    }
                    Label {
                        visible: !root.compactLayout
                        text: "↑↓ channel  ←→ field"
                        color: studioTheme.muted
                        font.family: root.uiFont
                        font.pixelSize: 8
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
                SplitView.fillHeight: true
                SplitView.minimumHeight: 170
                SplitView.preferredHeight: 300
                theme: studioTheme
                titleText: "Phasor View"
                statusText: "GENERATED"
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
                SplitView.fillHeight: true
                SplitView.minimumHeight: 170
                SplitView.preferredHeight: 300
                theme: studioTheme
                titleText: "Waveform View"
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
            visible: root.telemetryDockVisible
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
        anchors.bottomMargin: 72
        theme: studioTheme
        uiFont: root.uiFont
        message: root.toastMessage
        error: root.toastError
    }
}
