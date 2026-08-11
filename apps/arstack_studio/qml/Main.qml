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

    property string uiFont: "Inter"
    property string monoFont: "Cascadia Mono"
    property bool phaseLink: false
    property bool profileDirty: false
    property real currentScale: 1000.0
    property real voltageScale: 100.0
    property real signalFrequency: 50.0
    property string activeSignal: "Ia"
    property int activeGroup: 0
    property int activeRow: 0
    property string activeUnit: "A"
    property real activeMagnitude: 1.0
    property real activePhase: 0.0
    property real activeQuality: 0
    property string transientMessage: ""
    property bool transientError: false

    readonly property bool compactLayout: width < 1300
    readonly property bool selectedProfileDeployable:
        sclProfiles.selectedProfile.compatibilityClass === "A" &&
        sclProfiles.selectedProfile.deviceSupport === "ready"
    readonly property bool canDeploy:
        device.connected && !device.running && selectedProfileDeployable && !device.profileDeploying
    readonly property bool canStart:
        device.connected && !device.running &&
        (!sclProfiles.hasProfiles || (device.profileArmed && !profileDirty))
    readonly property string instrumentState: {
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
        if (instrumentState === "DEPLOYING" || instrumentState === "PROFILE CHANGED") return studioTheme.amber
        if (instrumentState === "PROFILE BLOCKED") return studioTheme.red
        return device.connected ? studioTheme.textSoft : studioTheme.muted
    }
    readonly property string stateReason: {
        if (!device.connected) return "Connect the injector; setpoints remain editable offline."
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
        function onConnectedChanged() {
            if (device.connected)
                Qt.callLater(root.applyAllSignals)
        }
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

    function showMessage(message, error) {
        transientMessage = message
        transientError = error === true
        messageTimer.restart()
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
        return isFinite(value) && value >= 0 && value <= maximumMagnitude(group)
    }

    function validPhase(value) {
        return isFinite(value) && Math.abs(value) <= 360000.0
    }

    function validFrequency(value) {
        return isFinite(value) && value > 0 && value <= 1000.0
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
        if (!device.connected)
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
        if (device.connected) {
            if (phaseLink && row < 3)
                sendLinkedGroup(group)
            else
                sendSignal(group, row)
        }
        return true
    }

    function applyAllSignals() {
        if (!device.connected)
            return
        for (var group = 0; group < 2; ++group)
            applyGroupSignals(group)
        device.setFrequency(signalFrequency)
    }

    function setFrequencyValue(value) {
        if (!validFrequency(value))
            return false
        signalFrequency = value
        frequencyField.text = value.toFixed(3)
        frequencyField.invalidInput = false
        refreshPreview()
        if (device.connected)
            device.setFrequency(value)
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
        if (device.connected)
            applyAllSignals()
    }

    function zeroAll() {
        for (var i = 0; i < 4; ++i) {
            currentModel.setProperty(i, "magnitude", 0.0)
            voltageModel.setProperty(i, "magnitude", 0.0)
        }
        selectSignal(activeGroup, activeRow)
        refreshPreview()
        if (device.connected)
            device.zero()
    }

    function setActiveQuality(value) {
        var unsignedValue = Number(value) >>> 0
        groupModel(activeGroup).setProperty(activeRow, "quality", unsignedValue)
        activeQuality = unsignedValue
        if (device.connected)
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
        height: 54
        color: studioTheme.chrome
        border.width: 1
        border.color: studioTheme.lineSoft

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 15
            anchors.rightMargin: 15
            spacing: 9

            Rectangle {
                width: 27
                height: 27
                radius: 6
                color: "#101b27"
                border.width: 1
                border.color: "#315071"
                Label { anchors.centerIn: parent; text: "≋"; color: studioTheme.accent; font.pixelSize: 16 }
            }

            ColumnLayout {
                spacing: 0
                Label { text: "ARSTACK61850"; color: studioTheme.muted; font.family: root.uiFont; font.pixelSize: 7; font.weight: Font.Bold; font.letterSpacing: 1.15 }
                Label { text: "SMV Injector"; color: studioTheme.text; font.family: root.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold }
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                visible: device.connected
                implicitWidth: liveApplyText.implicitWidth + 18
                implicitHeight: 22
                radius: 5
                color: studioTheme.greenSoft
                border.width: 1
                border.color: "#2b674d"
                Label {
                    id: liveApplyText
                    anchors.centerIn: parent
                    text: "LIVE APPLY"
                    color: studioTheme.green
                    font.family: root.monoFont
                    font.pixelSize: 7
                    font.weight: Font.Bold
                    font.letterSpacing: 0.65
                }
            }

            Label {
                visible: !root.compactLayout
                text: sclProfiles.hasProfiles ? (sclProfiles.selectedProfile.svId || "Resolved SV") : "Development profile"
                color: studioTheme.textSoft
                font.family: root.monoFont
                font.pixelSize: 9
            }

            Rectangle { width: 1; height: 19; color: studioTheme.line }

            ComboBox {
                id: portCombo
                implicitWidth: root.compactLayout ? 96 : 112
                model: device.ports
                enabled: !device.connected
                font.family: root.monoFont
                font.pixelSize: 9
                onPressedChanged: if (pressed) device.refreshPorts()
            }

            Rectangle { width: 7; height: 7; radius: 4; color: device.connected ? studioTheme.green : studioTheme.muted2 }

            Label {
                visible: !root.compactLayout
                text: device.connected ? device.portName : "Device offline"
                color: studioTheme.muted
                font.family: root.uiFont
                font.pixelSize: 9
            }

            CalmButton {
                theme: studioTheme
                uiFont: root.uiFont
                text: device.connected ? "Disconnect" : "Connect"
                onClicked: {
                    if (device.connected) device.disconnectPort()
                    else device.connectPort(portCombo.currentText)
                }
            }
        }
    }

    footer: Rectangle {
        height: 62
        color: studioTheme.chrome
        border.width: 1
        border.color: studioTheme.lineSoft

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 15
            anchors.rightMargin: 15
            spacing: 9

            StateBadge { theme: studioTheme; state: root.instrumentState; stateColor: root.instrumentStateColor; monoFont: root.monoFont }

            ColumnLayout {
                Layout.maximumWidth: root.compactLayout ? 280 : 420
                spacing: 1
                Label {
                    Layout.fillWidth: true
                    text: root.stateReason
                    color: studioTheme.muted
                    font.family: root.uiFont
                    font.pixelSize: 9
                    elide: Text.ElideRight
                }
                Label {
                    text: "FPS " + device.fps + "   ·   MISSED " + device.missed + "   ·   TX FAIL " + device.txFailures
                    color: studioTheme.muted
                    font.family: root.monoFont
                    font.pixelSize: 7
                }
            }

            Item { Layout.fillWidth: true }

            CalmButton {
                visible: !root.compactLayout
                theme: studioTheme
                uiFont: root.uiFont
                text: "Diagnostics"
                onClicked: diagnosticsDialog.open()
            }

            CalmButton {
                theme: studioTheme
                uiFont: root.uiFont
                tone: "accent"
                text: device.profileDeploying ? "Deploying…" : "Deploy"
                enabled: root.canDeploy
                onClicked: if (device.deployProfile(sclProfiles.selectedProfile)) root.profileDirty = true
            }

            CalmButton {
                theme: studioTheme
                uiFont: root.uiFont
                tone: "danger"
                text: "■  Stop"
                implicitWidth: 88
                implicitHeight: 35
                enabled: device.connected && device.running
                onClicked: device.stop()
            }

            CalmButton {
                theme: studioTheme
                uiFont: root.uiFont
                tone: "success"
                text: "▶  Start"
                implicitWidth: 98
                implicitHeight: 35
                enabled: root.canStart
                onClicked: device.start()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: root.compactLayout ? 9 : 11
        spacing: root.compactLayout ? 9 : 11

        ProfileInspector {
            Layout.preferredWidth: root.compactLayout ? 200 : 238
            Layout.minimumWidth: root.compactLayout ? 192 : 214
            Layout.fillHeight: true
            theme: studioTheme
            controller: root
            device: device
            profiles: sclProfiles
            uiFont: root.uiFont
            monoFont: root.monoFont
            compact: root.compactLayout
        }

        SurfacePanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: root.compactLayout ? 500 : 550
            theme: studioTheme

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: root.compactLayout ? 12 : 15
                spacing: 9

                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        spacing: 1
                        Label { text: "INJECTION WORKSPACE"; color: studioTheme.muted; font.family: root.uiFont; font.pixelSize: 8; font.weight: Font.DemiBold; font.letterSpacing: 1.0 }
                        Label { text: "Manual Injection"; color: studioTheme.text; font.family: root.uiFont; font.pixelSize: root.compactLayout ? 18 : 20; font.weight: Font.DemiBold }
                    }
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        implicitWidth: workspaceApplyText.implicitWidth + 17
                        implicitHeight: 22
                        radius: 5
                        color: device.connected ? studioTheme.greenSoft : "#141b23"
                        border.width: 1
                        border.color: device.connected ? "#2c674e" : studioTheme.lineSoft
                        Label {
                            id: workspaceApplyText
                            anchors.centerIn: parent
                            text: device.connected ? "LIVE APPLY" : "LOCAL EDIT"
                            color: device.connected ? studioTheme.green : studioTheme.muted
                            font.family: root.monoFont
                            font.pixelSize: 7
                            font.weight: Font.Bold
                        }
                    }
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

                        ColumnLayout {
                            spacing: 0
                            Label { text: "FREQUENCY"; color: studioTheme.muted; font.family: root.uiFont; font.pixelSize: 7; font.weight: Font.DemiBold; font.letterSpacing: 0.85 }
                            RowLayout {
                                spacing: 5
                                NumericField {
                                    id: frequencyField
                                    theme: studioTheme
                                    monoFont: root.monoFont
                                    compact: root.compactLayout
                                    implicitWidth: 84
                                    text: "50.000"
                                    validator: DoubleValidator { bottom: 0.001; top: 1000.0; decimals: 3 }
                                    onTextEdited: {
                                        var value = root.parseOperatorNumber(text)
                                        if (root.validFrequency(value)) {
                                            invalidInput = false
                                            root.signalFrequency = value
                                            root.refreshPreview()
                                            if (device.connected) device.setFrequency(value)
                                        } else {
                                            invalidInput = true
                                        }
                                    }
                                    onEditingFinished: {
                                        var value = root.parseOperatorNumber(text)
                                        if (!root.validFrequency(value)) {
                                            text = root.signalFrequency.toFixed(3)
                                            invalidInput = false
                                            root.showMessage("Frequency must be greater than 0 and not exceed 1000 Hz.", true)
                                        } else {
                                            text = value.toFixed(3)
                                        }
                                    }
                                }
                                Label { text: "Hz"; color: studioTheme.muted; font.family: root.uiFont; font.pixelSize: 8 }
                            }
                        }

                        CalmButton { theme: studioTheme; uiFont: root.uiFont; text: "50"; implicitWidth: 44; onClicked: root.setFrequencyValue(50) }
                        CalmButton { theme: studioTheme; uiFont: root.uiFont; text: "60"; implicitWidth: 44; onClicked: root.setFrequencyValue(60) }

                        Rectangle { width: 1; height: 24; color: studioTheme.lineSoft }

                        CheckBox {
                            checked: root.phaseLink
                            text: "3-phase link"
                            onToggled: root.phaseLink = checked
                            font.family: root.uiFont
                            font.pixelSize: 9
                        }

                        Item { Layout.fillWidth: true }

                        Label {
                            visible: !root.compactLayout
                            text: device.connected ? "Valid edit → SMV immediately" : "Connect to stream edits live"
                            color: device.connected ? studioTheme.green : studioTheme.muted
                            font.family: root.uiFont
                            font.pixelSize: 8
                        }
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
                        unitText: "A RMS"
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
                        unitText: "V RMS"
                        compact: root.compactLayout
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: studioTheme.lineSoft }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Rectangle { width: 6; height: 6; radius: 3; color: studioTheme.accent }
                    Label { text: root.activeSignal; color: studioTheme.textSoft; font.family: root.monoFont; font.pixelSize: 9; font.weight: Font.Bold }
                    Label {
                        text: root.activeMagnitude.toFixed(3) + " " + root.activeUnit + " RMS · " + root.activePhase.toFixed(2) + "°"
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

        SignalPreview {
            id: previewPanel
            Layout.preferredWidth: root.compactLayout ? 290 : 360
            Layout.minimumWidth: root.compactLayout ? 280 : 320
            Layout.fillHeight: true
            theme: studioTheme
            currentModel: currentModel
            voltageModel: voltageModel
            uiFont: root.uiFont
            monoFont: root.monoFont
            compact: root.compactLayout
            activeSignal: root.activeSignal
            activeUnit: root.activeUnit
            activeMagnitude: root.activeMagnitude
            activePhase: root.activePhase
            signalFrequency: root.signalFrequency
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