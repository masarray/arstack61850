// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import ARStack.Studio 1.0

ApplicationWindow {
    id: root
    width: 1480
    height: 900
    minimumWidth: 1080
    minimumHeight: 720
    visible: true
    title: "ARStack Studio · SMV Injector"
    color: theme.bg

    QtObject {
        id: theme
        readonly property color bg: "#090d12"
        readonly property color chrome: "#0c1117"
        readonly property color surface: "#11171f"
        readonly property color surface2: "#0e141b"
        readonly property color raised: "#151d27"
        readonly property color raisedHover: "#1a2430"
        readonly property color line: "#26313d"
        readonly property color lineSoft: "#1c252f"
        readonly property color text: "#edf2f7"
        readonly property color textSoft: "#c2ccd7"
        readonly property color muted: "#778493"
        readonly property color muted2: "#566271"
        readonly property color accent: "#69a9ff"
        readonly property color accentSoft: "#18314c"
        readonly property color green: "#58d49d"
        readonly property color greenSoft: "#153a2d"
        readonly property color amber: "#e1b25a"
        readonly property color amberSoft: "#3b301c"
        readonly property color red: "#ff727f"
        readonly property color redSoft: "#46252b"
    }

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
        if (instrumentState === "RUNNING") return theme.green
        if (instrumentState === "ARMED") return theme.accent
        if (instrumentState === "DEPLOYING" || instrumentState === "PROFILE CHANGED") return theme.amber
        if (instrumentState === "PROFILE BLOCKED") return theme.red
        return device.connected ? theme.textSoft : theme.muted
    }
    readonly property string stateReason: {
        if (!device.connected) return "Connect the injector; setpoints remain editable offline."
        if (device.profileDeploying) return "Committing immutable SCL profile to the device."
        if (device.running) return "Live apply active · valid edits update SMV immediately."
        if (profileDirty) return "Selected profile changed · deploy before Start."
        if (sclProfiles.hasProfiles && !selectedProfileDeployable) return "Selected stream is not deployable on the current ESP32-P4 layout."
        if (sclProfiles.hasProfiles && !device.profileArmed) return "Profile validated · deploy to arm the injector."
        if (device.profileArmed) return "Ready · live setpoint editing is armed."
        return "Connected · development setpoints ready."
    }

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
        interval: 2800
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

    function groupRepeater(group) {
        return group === 0 ? currentRepeater : voltageRepeater
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
        if (result > 180)
            result -= 360
        if (result <= -180)
            result += 360
        return result
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
        phasor.requestPaint()
        waveform.requestPaint()
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
        var rowItem = groupRepeater(nextGroup).itemAt(nextRow)
        if (!rowItem)
            return
        var editor = column === 0 ? rowItem.magnitudeEditor : rowItem.phaseEditor
        editor.forceActiveFocus()
        editor.selectAll()
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
                if (row === 1)
                    base += 120
                if (row === 2)
                    base -= 120
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
        phasor.requestPaint()
        waveform.requestPaint()
        if (device.connected)
            applyAllSignals()
    }

    function zeroAll() {
        for (var i = 0; i < 4; ++i) {
            currentModel.setProperty(i, "magnitude", 0.0)
            voltageModel.setProperty(i, "magnitude", 0.0)
        }
        phasor.requestPaint()
        waveform.requestPaint()
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

    component Kicker: Label {
        color: theme.muted
        font.family: root.uiFont
        font.pixelSize: 9
        font.weight: Font.DemiBold
        font.letterSpacing: 1.2
    }

    component Quiet: Label {
        color: theme.muted
        font.family: root.uiFont
        font.pixelSize: 10
    }

    component Surface: Rectangle {
        color: theme.surface
        radius: 9
        border.width: 1
        border.color: theme.lineSoft
    }

    component CalmButton: Button {
        id: calmButton
        implicitHeight: 34
        font.family: root.uiFont
        font.pixelSize: 11
        font.weight: Font.DemiBold
        contentItem: Text {
            text: calmButton.text
            color: calmButton.enabled ? theme.textSoft : theme.muted2
            font: calmButton.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 6
            color: calmButton.down ? "#1c2733" : calmButton.hovered ? theme.raisedHover : theme.raised
            border.width: 1
            border.color: calmButton.hovered ? "#3a4a5b" : theme.line
        }
    }

    component NumericField: TextField {
        id: numericField
        property bool invalidInput: false
        property bool compact: false
        selectByMouse: true
        horizontalAlignment: Text.AlignRight
        color: invalidInput ? theme.red : theme.text
        selectionColor: theme.accent
        selectedTextColor: theme.bg
        font.family: root.monoFont
        font.pixelSize: compact ? 11 : 13
        font.weight: Font.DemiBold
        leftPadding: 8
        rightPadding: 8
        background: Rectangle {
            radius: 6
            color: numericField.activeFocus ? "#0c141d" : "#111820"
            border.width: numericField.activeFocus || numericField.invalidInput ? 1 : 0
            border.color: numericField.invalidInput ? theme.red : theme.accent
        }
    }

    component StateBadge: Rectangle {
        implicitHeight: 24
        implicitWidth: stateText.implicitWidth + 24
        radius: 6
        color: root.instrumentState === "RUNNING" ? theme.greenSoft :
               root.instrumentState === "PROFILE BLOCKED" ? theme.redSoft :
               (root.instrumentState === "PROFILE CHANGED" || root.instrumentState === "DEPLOYING") ? theme.amberSoft :
               theme.accentSoft
        border.width: 1
        border.color: root.instrumentStateColor
        Label {
            id: stateText
            anchors.centerIn: parent
            text: root.instrumentState
            color: root.instrumentStateColor
            font.family: root.monoFont
            font.pixelSize: 9
            font.weight: Font.Bold
            font.letterSpacing: 0.5
        }
    }

    FileDialog {
        id: fileDialog
        title: "Open IEC 61850 engineering file"
        nameFilters: [
            "IEC 61850 SCL (*.scd *.cid *.icd *.iid *.ssd *.xml)",
            "All files (*)"
        ]
        onAccepted: {
            if (sclProfiles.loadFile(selectedFile))
                root.profileDirty = true
        }
    }

    Dialog {
        id: diagnosticsDialog
        width: Math.min(root.width * 0.72, 980)
        height: Math.min(root.height * 0.72, 620)
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "Device diagnostics"
        standardButtons: Dialog.Close
        background: Rectangle {
            color: theme.surface
            radius: 9
            border.color: theme.line
        }
        contentItem: ColumnLayout {
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Quiet { text: device.connected ? device.portName + " · 115200 8N1" : "Device offline" }
                Item { Layout.fillWidth: true }
                CalmButton { text: "Clear"; onClicked: device.clearLog() }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                TextArea {
                    readOnly: true
                    text: device.logText
                    color: theme.textSoft
                    selectionColor: theme.accent
                    font.family: root.monoFont
                    font.pixelSize: 10
                    wrapMode: TextEdit.WrapAnywhere
                    background: Rectangle {
                        color: "#090e14"
                        radius: 7
                        border.color: theme.lineSoft
                    }
                }
            }
        }
    }

    header: Rectangle {
        height: 56
        color: theme.chrome
        border.width: 1
        border.color: theme.lineSoft

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 10

            Rectangle {
                width: 28
                height: 28
                radius: 6
                color: "#101b27"
                border.color: "#315071"
                Label {
                    anchors.centerIn: parent
                    text: "≋"
                    color: theme.accent
                    font.pixelSize: 17
                }
            }

            ColumnLayout {
                spacing: 0
                Label {
                    text: "ARSTACK61850"
                    color: theme.muted
                    font.family: root.uiFont
                    font.pixelSize: 8
                    font.weight: Font.Bold
                    font.letterSpacing: 1.2
                }
                Label {
                    text: "SMV Injector"
                    color: theme.text
                    font.family: root.uiFont
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                visible: device.connected
                implicitWidth: liveText.implicitWidth + 20
                implicitHeight: 23
                radius: 6
                color: theme.greenSoft
                border.width: 1
                border.color: "#2b674d"
                Label {
                    id: liveText
                    anchors.centerIn: parent
                    text: "LIVE APPLY"
                    color: theme.green
                    font.family: root.monoFont
                    font.pixelSize: 8
                    font.weight: Font.Bold
                    font.letterSpacing: 0.7
                }
            }

            Label {
                visible: !root.compactLayout
                text: sclProfiles.hasProfiles ?
                    (sclProfiles.selectedProfile.svId || "Resolved SV") :
                    "Development profile"
                color: theme.textSoft
                font.family: root.monoFont
                font.pixelSize: 10
            }

            Rectangle { width: 1; height: 20; color: theme.line }

            ComboBox {
                id: portCombo
                implicitWidth: root.compactLayout ? 100 : 116
                model: device.ports
                enabled: !device.connected
                font.family: root.monoFont
                font.pixelSize: 10
                onPressedChanged: if (pressed) device.refreshPorts()
            }

            Rectangle {
                width: 7
                height: 7
                radius: 4
                color: device.connected ? theme.green : theme.muted2
            }

            Quiet {
                visible: !root.compactLayout
                text: device.connected ? device.portName : "Device offline"
            }

            CalmButton {
                text: device.connected ? "Disconnect" : "Connect"
                onClicked: {
                    if (device.connected)
                        device.disconnectPort()
                    else
                        device.connectPort(portCombo.currentText)
                }
            }
        }
    }

    footer: Rectangle {
        height: 66
        color: theme.chrome
        border.width: 1
        border.color: theme.lineSoft

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 10

            StateBadge {}

            ColumnLayout {
                Layout.maximumWidth: root.compactLayout ? 300 : 440
                spacing: 1
                Quiet {
                    Layout.fillWidth: true
                    text: root.stateReason
                    elide: Text.ElideRight
                }
                Label {
                    text: "FPS " + device.fps + "   ·   MISSED " + device.missed + "   ·   TX FAIL " + device.txFailures
                    color: theme.muted
                    font.family: root.monoFont
                    font.pixelSize: 8
                }
            }

            Item { Layout.fillWidth: true }

            CalmButton {
                visible: !root.compactLayout
                text: "Diagnostics"
                onClicked: diagnosticsDialog.open()
            }

            CalmButton {
                text: device.profileDeploying ? "Deploying…" : "Deploy"
                enabled: root.canDeploy
                onClicked: {
                    if (device.deployProfile(sclProfiles.selectedProfile))
                        root.profileDirty = true
                }
            }

            Button {
                id: stopButton
                text: "■  Stop"
                enabled: device.connected && device.running
                implicitWidth: 92
                implicitHeight: 36
                font.family: root.uiFont
                font.pixelSize: 11
                font.weight: Font.DemiBold
                contentItem: Text {
                    text: stopButton.text
                    color: stopButton.enabled ? "#ffdce0" : theme.muted2
                    font: stopButton.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 6
                    color: stopButton.enabled ? theme.redSoft : "#121820"
                    border.width: 1
                    border.color: stopButton.enabled ? "#86434b" : theme.lineSoft
                }
                onClicked: device.stop()
            }

            Button {
                id: startButton
                text: "▶  Start"
                enabled: root.canStart
                implicitWidth: 104
                implicitHeight: 36
                font.family: root.uiFont
                font.pixelSize: 11
                font.weight: Font.DemiBold
                contentItem: Text {
                    text: startButton.text
                    color: startButton.enabled ? "#d6f4e6" : theme.muted2
                    font: startButton.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 6
                    color: startButton.enabled ? "#194d38" : "#121820"
                    border.width: 1
                    border.color: startButton.enabled ? "#347a59" : theme.lineSoft
                }
                onClicked: device.start()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: root.compactLayout ? 10 : 12
        spacing: root.compactLayout ? 10 : 12

        Surface {
            Layout.preferredWidth: root.compactLayout ? 206 : 246
            Layout.minimumWidth: root.compactLayout ? 196 : 220
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: root.compactLayout ? 12 : 15
                spacing: root.compactLayout ? 9 : 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Kicker { text: "ENGINEERING SOURCE" }
                    Label {
                        Layout.fillWidth: true
                        text: sclProfiles.hasProfiles ?
                            (sclProfiles.selectedProfile.svId || "Compiled SCL") :
                            "Development profile"
                        color: theme.text
                        font.family: root.uiFont
                        font.pixelSize: root.compactLayout ? 14 : 16
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Quiet {
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        text: sclProfiles.sourceName.length ? sclProfiles.sourceName : "SCL / CID / SCD / IID"
                    }
                }

                CalmButton {
                    Layout.fillWidth: true
                    text: "Open SCL / CID"
                    enabled: !device.running
                    onClicked: fileDialog.open()
                }

                ColumnLayout {
                    visible: sclProfiles.hasProfiles
                    Layout.fillWidth: true
                    spacing: 6
                    Kicker { text: "RESOLVED STREAM" }
                    ComboBox {
                        Layout.fillWidth: true
                        model: sclProfiles
                        textRole: "control"
                        currentIndex: sclProfiles.selectedIndex
                        enabled: !device.running
                        font.family: root.uiFont
                        font.pixelSize: 10
                        onActivated: {
                            sclProfiles.selectStream(currentIndex)
                            root.profileDirty = true
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 5
                        Quiet { text: "Class" }
                        Label {
                            text: "CLASS " + (sclProfiles.selectedProfile.compatibilityClass || "—")
                            color: sclProfiles.selectedProfile.compatibilityClass === "A" ? theme.green :
                                   sclProfiles.selectedProfile.compatibilityClass === "B" ? theme.amber : theme.red
                            font.family: root.uiFont
                            font.pixelSize: 9
                            font.weight: Font.Bold
                        }
                        Quiet { text: "Support" }
                        Label {
                            text: sclProfiles.selectedProfile.deviceSupport || "—"
                            color: theme.textSoft
                            font.family: root.uiFont
                            font.pixelSize: 9
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Quiet { text: "APPID" }
                        Label { text: sclProfiles.selectedProfile.appIdHex || "—"; color: theme.textSoft; font.family: root.monoFont; font.pixelSize: 9 }
                        Quiet { text: "Rate" }
                        Label {
                            text: sclProfiles.selectedProfile.publisherRate ? sclProfiles.selectedProfile.publisherRate + "/s" : "—"
                            color: theme.textSoft
                            font.family: root.monoFont
                            font.pixelSize: 9
                        }
                        Quiet { text: "VLAN" }
                        Label {
                            text: sclProfiles.selectedProfile.vlanPresent ?
                                "P" + sclProfiles.selectedProfile.vlanPriority + " · " + sclProfiles.selectedProfile.vlanId : "untagged"
                            color: theme.textSoft
                            font.family: root.monoFont
                            font.pixelSize: 9
                        }
                        Quiet { text: "Payload" }
                        Label {
                            text: sclProfiles.selectedProfile.payloadBytes ? sclProfiles.selectedProfile.payloadBytes + " B" : "—"
                            color: theme.textSoft
                            font.family: root.monoFont
                            font.pixelSize: 9
                        }
                    }

                    Quiet {
                        Layout.fillWidth: true
                        visible: (sclProfiles.selectedProfile.warnings || []).length > 0 ||
                                 (sclProfiles.selectedProfile.errors || []).length > 0
                        wrapMode: Text.WordWrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        color: sclProfiles.selectedProfile.errors && sclProfiles.selectedProfile.errors.length ? theme.red : theme.amber
                        text: sclProfiles.selectedProfile.errors && sclProfiles.selectedProfile.errors.length ?
                            sclProfiles.selectedProfile.errors[0] : sclProfiles.selectedProfile.warnings[0]
                    }
                }

                ColumnLayout {
                    visible: sclProfiles.selectedProfile.compatibilityClass === "B"
                    Layout.fillWidth: true
                    spacing: 6
                    Kicker { text: "COUNTER POLICY" }
                    Quiet {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "Candidate smpCnt modulus requires explicit evidence confirmation."
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        NumericField {
                            id: counterField
                            Layout.fillWidth: true
                            compact: true
                            text: sclProfiles.selectedProfile.counterModulus || ""
                            validator: IntValidator { bottom: 1; top: 65535 }
                        }
                        CalmButton {
                            text: "Confirm"
                            onClicked: {
                                if (counterField.acceptableInput && sclProfiles.confirmCounterModulus(parseInt(counterField.text)))
                                    root.profileDirty = true
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Kicker { text: "SCALING" }
                    RowLayout {
                        Layout.fillWidth: true
                        Quiet { text: "I"; Layout.preferredWidth: 18 }
                        NumericField {
                            Layout.fillWidth: true
                            compact: true
                            text: root.currentScale.toString()
                            validator: DoubleValidator { bottom: 0.000001; top: 2147483647; decimals: 6 }
                            onEditingFinished: {
                                var value = root.parseOperatorNumber(text)
                                if (acceptableInput && isFinite(value) && value > 0) {
                                    root.currentScale = value
                                    if (device.connected) root.applyGroupSignals(0)
                                } else {
                                    text = root.currentScale.toString()
                                }
                            }
                        }
                        Quiet { text: "ct/A" }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Quiet { text: "U"; Layout.preferredWidth: 18 }
                        NumericField {
                            Layout.fillWidth: true
                            compact: true
                            text: root.voltageScale.toString()
                            validator: DoubleValidator { bottom: 0.000001; top: 2147483647; decimals: 6 }
                            onEditingFinished: {
                                var value = root.parseOperatorNumber(text)
                                if (acceptableInput && isFinite(value) && value > 0) {
                                    root.voltageScale = value
                                    if (device.connected) root.applyGroupSignals(1)
                                } else {
                                    text = root.voltageScale.toString()
                                }
                            }
                        }
                        Quiet { text: "ct/V" }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: theme.lineSoft }
                Kicker { text: "QUICK SETUP" }
                CalmButton { Layout.fillWidth: true; text: "Balanced 3-phase"; onClicked: root.balanced() }
                CalmButton { Layout.fillWidth: true; text: "Zero output"; onClicked: root.zeroAll() }

                Item { Layout.fillHeight: true }

                Kicker { text: "RUNTIME" }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 4
                    Quiet { text: "Rate" }
                    Label { text: device.fps; color: theme.textSoft; font.family: root.monoFont; font.pixelSize: 9; Layout.alignment: Qt.AlignRight }
                    Quiet { text: "Missed" }
                    Label { text: device.missed; color: Number(device.missed) > 0 ? theme.amber : theme.textSoft; font.family: root.monoFont; font.pixelSize: 9; Layout.alignment: Qt.AlignRight }
                    Quiet { text: "TX fail" }
                    Label { text: device.txFailures; color: Number(device.txFailures) > 0 ? theme.red : theme.textSoft; font.family: root.monoFont; font.pixelSize: 9; Layout.alignment: Qt.AlignRight }
                    Quiet { text: "Generation" }
                    Label { text: device.signalGeneration; color: theme.textSoft; font.family: root.monoFont; font.pixelSize: 9; Layout.alignment: Qt.AlignRight }
                }
            }
        }

        Surface {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: root.compactLayout ? 500 : 560

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: root.compactLayout ? 14 : 17
                spacing: 11

                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        spacing: 1
                        Kicker { text: "INJECTION WORKSPACE" }
                        Label {
                            text: "Manual Injection"
                            color: theme.text
                            font.family: root.uiFont
                            font.pixelSize: root.compactLayout ? 19 : 22
                            font.weight: Font.DemiBold
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        implicitWidth: applyText.implicitWidth + 18
                        implicitHeight: 24
                        radius: 6
                        color: device.connected ? theme.greenSoft : "#141b23"
                        border.width: 1
                        border.color: device.connected ? "#2c674e" : theme.lineSoft
                        Label {
                            id: applyText
                            anchors.centerIn: parent
                            text: device.connected ? "LIVE APPLY" : "LOCAL EDIT"
                            color: device.connected ? theme.green : theme.muted
                            font.family: root.monoFont
                            font.pixelSize: 8
                            font.weight: Font.Bold
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 56
                    radius: 8
                    color: theme.surface2
                    border.width: 1
                    border.color: theme.lineSoft

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 13
                        anchors.rightMargin: 13
                        spacing: 9

                        ColumnLayout {
                            spacing: 0
                            Kicker { text: "FREQUENCY" }
                            RowLayout {
                                NumericField {
                                    id: frequencyField
                                    implicitWidth: 88
                                    text: "50.000"
                                    invalidInput: !acceptableInput
                                    validator: DoubleValidator { bottom: 0.001; top: 1000.0; decimals: 3 }
                                    onTextEdited: {
                                        var value = root.parseOperatorNumber(text)
                                        if (root.validFrequency(value)) {
                                            invalidInput = false
                                            root.signalFrequency = value
                                            waveform.requestPaint()
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
                                Quiet { text: "Hz" }
                            }
                        }

                        CalmButton {
                            text: "50"
                            onClicked: {
                                root.signalFrequency = 50
                                frequencyField.text = "50.000"
                                frequencyField.invalidInput = false
                                waveform.requestPaint()
                                if (device.connected) device.setFrequency(50)
                            }
                        }
                        CalmButton {
                            text: "60"
                            onClicked: {
                                root.signalFrequency = 60
                                frequencyField.text = "60.000"
                                frequencyField.invalidInput = false
                                waveform.requestPaint()
                                if (device.connected) device.setFrequency(60)
                            }
                        }

                        Rectangle { width: 1; height: 26; color: theme.lineSoft }

                        CheckBox {
                            checked: root.phaseLink
                            text: "3-phase link"
                            onToggled: root.phaseLink = checked
                            font.family: root.uiFont
                            font.pixelSize: 10
                        }

                        Item { Layout.fillWidth: true }

                        Quiet {
                            visible: !root.compactLayout
                            text: device.connected ? "Valid edits → SMV immediately" : "Connect to stream edits live"
                            color: device.connected ? theme.green : theme.muted
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 10

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: theme.surface2
                        radius: 8
                        border.color: theme.lineSoft
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0
                            MatrixHeader { titleText: "Current"; symbolText: "I"; unitText: "A RMS" }
                            MatrixColumns {}
                            Repeater {
                                id: currentRepeater
                                model: currentModel
                                SignalRow {
                                    groupIndex: 0
                                    rowIndex: index
                                    sourceModel: currentModel
                                    sid: signalId
                                    mag: magnitude
                                    angle: phase
                                    isEnabled: enabled
                                    signalQuality: quality
                                    phaseColor: traceColor
                                }
                            }
                            Item { Layout.fillHeight: true }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: theme.surface2
                        radius: 8
                        border.color: theme.lineSoft
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0
                            MatrixHeader { titleText: "Voltage"; symbolText: "U"; unitText: "V RMS" }
                            MatrixColumns {}
                            Repeater {
                                id: voltageRepeater
                                model: voltageModel
                                SignalRow {
                                    groupIndex: 1
                                    rowIndex: index
                                    sourceModel: voltageModel
                                    sid: signalId
                                    mag: magnitude
                                    angle: phase
                                    isEnabled: enabled
                                    signalQuality: quality
                                    phaseColor: traceColor
                                }
                            }
                            Item { Layout.fillHeight: true }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 7
                    Rectangle { width: 7; height: 7; radius: 4; color: theme.accent }
                    Label {
                        text: activeSignal
                        color: theme.textSoft
                        font.family: root.monoFont
                        font.pixelSize: 10
                        font.weight: Font.Bold
                    }
                    Quiet { text: activeMagnitude.toFixed(3) + " " + activeUnit + " RMS · " + activePhase.toFixed(2) + "°" }
                    Item { Layout.fillWidth: true }
                    Quiet { text: "Quality" }
                    NumericField {
                        id: qualityField
                        implicitWidth: 102
                        compact: true
                        text: "0x" + (Number(root.activeQuality) >>> 0).toString(16).padStart(8, "0").toUpperCase()
                        onEditingFinished: {
                            var trimmed = text.trim()
                            var qualityValue = trimmed.toLowerCase().startsWith("0x") ?
                                parseInt(trimmed.substring(2), 16) : parseInt(trimmed, 10)
                            if (!isNaN(qualityValue) && qualityValue >= 0 && qualityValue <= 0xFFFFFFFF) {
                                root.setActiveQuality(qualityValue)
                                text = "0x" + (qualityValue >>> 0).toString(16).padStart(8, "0").toUpperCase()
                                invalidInput = false
                            } else {
                                invalidInput = true
                                text = "0x" + (Number(root.activeQuality) >>> 0).toString(16).padStart(8, "0").toUpperCase()
                                root.showMessage("Quality must be a valid 32-bit value.", true)
                            }
                        }
                    }
                    Quiet { visible: !root.compactLayout; text: "↑↓ channel  ←→ field  Enter next" }
                }
            }
        }

        Surface {
            Layout.preferredWidth: root.compactLayout ? 304 : 386
            Layout.minimumWidth: root.compactLayout ? 286 : 330
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: root.compactLayout ? 12 : 15
                spacing: 9

                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        spacing: 1
                        Kicker { text: "GENERATED SETPOINT" }
                        Label {
                            text: "Signal Preview"
                            color: theme.text
                            font.family: root.uiFont
                            font.pixelSize: root.compactLayout ? 16 : 18
                            font.weight: Font.DemiBold
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Quiet { text: "LOCAL" }
                }

                PreviewTitle {
                    titleText: "Phasor"
                    detailText: activeSignal + " · " + activeMagnitude.toFixed(3) + " " + activeUnit + " ∠ " + activePhase.toFixed(2) + "°"
                }

                Canvas {
                    id: phasor
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: 1
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()
                        var centerX = width / 2
                        var centerY = height / 2
                        var radiusValue = Math.min(width, height) * 0.39

                        ctx.strokeStyle = "#283441"
                        ctx.lineWidth = 1
                        for (var ring = 1; ring <= 3; ++ring) {
                            ctx.beginPath()
                            ctx.arc(centerX, centerY, radiusValue * ring / 3, 0, Math.PI * 2)
                            ctx.stroke()
                        }
                        for (var degrees = 0; degrees < 180; degrees += 30) {
                            var axis = degrees * Math.PI / 180
                            var axisX = Math.cos(axis) * radiusValue
                            var axisY = Math.sin(axis) * radiusValue
                            ctx.beginPath()
                            ctx.moveTo(centerX - axisX, centerY - axisY)
                            ctx.lineTo(centerX + axisX, centerY + axisY)
                            ctx.stroke()
                        }

                        function drawGroup(model, scale, dashed) {
                            var maxMagnitude = 0.0001
                            for (var i = 0; i < 4; ++i)
                                maxMagnitude = Math.max(maxMagnitude, model.get(i).magnitude)

                            for (var j = 0; j < 4; ++j) {
                                var signal = model.get(j)
                                if (!signal.enabled || signal.magnitude <= 0)
                                    continue
                                var angleValue = signal.phase * Math.PI / 180
                                var length = radiusValue * scale * (0.30 + 0.70 * Math.min(1, signal.magnitude / maxMagnitude))
                                var endX = centerX + Math.cos(angleValue) * length
                                var endY = centerY - Math.sin(angleValue) * length
                                var active = signal.signalId === root.activeSignal

                                ctx.save()
                                ctx.globalAlpha = active ? 1 : 0.70
                                ctx.strokeStyle = signal.traceColor
                                ctx.fillStyle = signal.traceColor
                                ctx.lineWidth = active ? 3.0 : 1.8
                                ctx.lineCap = "round"
                                ctx.setLineDash(dashed ? [5, 4] : [])
                                ctx.beginPath()
                                ctx.moveTo(centerX, centerY)
                                ctx.lineTo(endX, endY)
                                ctx.stroke()
                                ctx.setLineDash([])

                                var arrowHead = active ? 8 : 6
                                ctx.beginPath()
                                ctx.moveTo(endX, endY)
                                ctx.lineTo(endX - Math.cos(angleValue - 0.45) * arrowHead, endY + Math.sin(angleValue - 0.45) * arrowHead)
                                ctx.lineTo(endX - Math.cos(angleValue + 0.45) * arrowHead, endY + Math.sin(angleValue + 0.45) * arrowHead)
                                ctx.closePath()
                                ctx.fill()

                                if (active) {
                                    ctx.font = "600 11px Inter"
                                    ctx.fillText(signal.signalId, endX + 8, endY - 7)
                                }
                                ctx.restore()
                            }
                        }

                        drawGroup(voltageModel, 1.0, false)
                        drawGroup(currentModel, 0.78, true)
                    }
                }

                PreviewTitle { titleText: "Waveform"; detailText: root.signalFrequency.toFixed(3) + " Hz · 40 ms" }

                Canvas {
                    id: waveform
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: 0.85
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()
                        var left = 25
                        var right = 8
                        var top = 12
                        var bottom = 14
                        var plotWidth = width - left - right
                        var plotHeight = height - top - bottom
                        var gap = 16
                        var laneHeight = (plotHeight - gap) / 2
                        var voltageY = top + laneHeight / 2
                        var currentY = top + laneHeight + gap + laneHeight / 2
                        var frequency = root.signalFrequency

                        ctx.strokeStyle = "#202b36"
                        ctx.lineWidth = 1
                        for (var grid = 0; grid <= 8; ++grid) {
                            var gridX = left + plotWidth * grid / 8
                            ctx.beginPath()
                            ctx.moveTo(gridX, top)
                            ctx.lineTo(gridX, top + plotHeight)
                            ctx.stroke()
                        }

                        ctx.strokeStyle = "#33404d"
                        var baselines = [voltageY, currentY]
                        for (var baselineIndex = 0; baselineIndex < baselines.length; ++baselineIndex) {
                            ctx.beginPath()
                            ctx.moveTo(left, baselines[baselineIndex])
                            ctx.lineTo(left + plotWidth, baselines[baselineIndex])
                            ctx.stroke()
                        }

                        function drawWaveGroup(model, center, dashed) {
                            var maxMagnitude = 0.0001
                            for (var i = 0; i < 4; ++i)
                                maxMagnitude = Math.max(maxMagnitude, model.get(i).magnitude)

                            for (var j = 0; j < 4; ++j) {
                                var signal = model.get(j)
                                if (!signal.enabled || signal.magnitude <= 0)
                                    continue
                                var active = signal.signalId === root.activeSignal
                                var amplitude = laneHeight * 0.39 * Math.min(1, signal.magnitude / maxMagnitude)
                                var phaseRadians = signal.phase * Math.PI / 180

                                ctx.save()
                                ctx.strokeStyle = signal.traceColor
                                ctx.globalAlpha = active ? 1 : 0.64
                                ctx.lineWidth = active ? 2.5 : 1.6
                                ctx.lineCap = "round"
                                ctx.setLineDash(dashed ? [5, 4] : [])
                                ctx.beginPath()
                                for (var point = 0; point <= 180; ++point) {
                                    var ratio = point / 180
                                    var time = 0.04 * ratio
                                    var sample = Math.sin(2 * Math.PI * frequency * time + phaseRadians)
                                    var x = left + plotWidth * ratio
                                    var y = center - sample * amplitude
                                    if (point === 0) ctx.moveTo(x, y)
                                    else ctx.lineTo(x, y)
                                }
                                ctx.stroke()
                                ctx.restore()
                            }
                        }

                        drawWaveGroup(voltageModel, voltageY, false)
                        drawWaveGroup(currentModel, currentY, true)
                    }
                }

                Quiet {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    text: "Generated setpoint · not independent on-wire proof"
                }
            }
        }
    }

    Rectangle {
        visible: root.transientMessage.length > 0 || device.lastError.length > 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 78
        radius: 7
        color: "#18212b"
        border.width: 1
        border.color: root.transientError || device.lastError.length > 0 ? "#70404a" : "#36536f"
        implicitWidth: messageLabel.implicitWidth + 28
        implicitHeight: 38
        Label {
            id: messageLabel
            anchors.centerIn: parent
            text: root.transientMessage.length ? root.transientMessage : device.lastError
            color: root.transientError || device.lastError.length > 0 ? "#ffb3bb" : theme.textSoft
            font.family: root.uiFont
            font.pixelSize: 10
        }
    }

    component MatrixHeader: RowLayout {
        required property string titleText
        required property string symbolText
        required property string unitText
        Layout.fillWidth: true
        Layout.preferredHeight: 44
        Layout.leftMargin: 11
        Layout.rightMargin: 11
        Rectangle {
            width: 22
            height: 22
            radius: 5
            color: "#152235"
            Label {
                anchors.centerIn: parent
                text: symbolText
                color: theme.accent
                font.family: root.monoFont
                font.pixelSize: 10
                font.weight: Font.Bold
            }
        }
        Label {
            text: titleText
            color: theme.textSoft
            font.family: root.uiFont
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
        Item { Layout.fillWidth: true }
        Quiet { text: unitText }
    }

    component MatrixColumns: RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: 28
        Layout.leftMargin: 9
        Layout.rightMargin: 9
        spacing: 7
        Quiet { text: "ON"; Layout.preferredWidth: 28; font.pixelSize: 8 }
        Quiet { text: "CH"; Layout.preferredWidth: 34; font.pixelSize: 8 }
        Quiet { text: "MAGNITUDE"; Layout.fillWidth: true; font.pixelSize: 8 }
        Quiet { text: "PHASE"; Layout.preferredWidth: root.compactLayout ? 86 : 104; font.pixelSize: 8 }
    }

    component SignalRow: Rectangle {
        id: signalRow
        required property int groupIndex
        required property int rowIndex
        required property var sourceModel
        required property string sid
        required property real mag
        required property real angle
        required property bool isEnabled
        required property real signalQuality
        required property color phaseColor
        property alias magnitudeEditor: magnitudeField
        property alias phaseEditor: phaseField

        Layout.fillWidth: true
        Layout.preferredHeight: root.compactLayout ? 54 : 57
        color: magnitudeField.activeFocus || phaseField.activeFocus ? "#141d27" : "transparent"

        onMagChanged: {
            if (!magnitudeField.activeFocus)
                magnitudeField.text = mag.toFixed(3)
        }
        onAngleChanged: {
            if (!phaseField.activeFocus)
                phaseField.text = angle.toFixed(2)
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 9
            anchors.rightMargin: 9
            spacing: 7

            CheckBox {
                Layout.preferredWidth: 28
                checked: signalRow.isEnabled
                onToggled: {
                    signalRow.sourceModel.setProperty(signalRow.rowIndex, "enabled", checked)
                    root.selectSignal(signalRow.groupIndex, signalRow.rowIndex)
                    phasor.requestPaint()
                    waveform.requestPaint()
                    if (device.connected)
                        device.setEnabled(signalRow.sid, checked)
                }
            }

            RowLayout {
                Layout.preferredWidth: 34
                spacing: 5
                Rectangle { width: 6; height: 6; radius: 3; color: signalRow.phaseColor }
                Label {
                    text: signalRow.sid
                    color: theme.text
                    font.family: root.monoFont
                    font.pixelSize: 10
                    font.weight: Font.Bold
                }
            }

            NumericField {
                id: magnitudeField
                Layout.fillWidth: true
                text: signalRow.mag.toFixed(3)
                invalidInput: false
                onActiveFocusChanged: {
                    if (activeFocus) {
                        root.selectSignal(signalRow.groupIndex, signalRow.rowIndex)
                        text = signalRow.mag.toFixed(3)
                        selectAll()
                    }
                }
                onTextEdited: {
                    var value = root.parseOperatorNumber(text)
                    if (root.validMagnitude(signalRow.groupIndex, value)) {
                        invalidInput = false
                        root.editSignal(signalRow.groupIndex, signalRow.rowIndex, "magnitude", value)
                    } else {
                        invalidInput = true
                    }
                }
                onEditingFinished: {
                    var value = root.parseOperatorNumber(text)
                    if (!root.validMagnitude(signalRow.groupIndex, value)) {
                        text = signalRow.mag.toFixed(3)
                        invalidInput = false
                        root.showMessage(signalRow.sid + " magnitude is outside the valid wire/scaling range.", true)
                    } else {
                        text = value.toFixed(3)
                    }
                }
                Keys.onPressed: function(event) {
                    if ([Qt.Key_Up, Qt.Key_Down, Qt.Key_Left, Qt.Key_Right].indexOf(event.key) >= 0) {
                        root.navigate(signalRow.groupIndex, signalRow.rowIndex, 0, event.key)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        root.focusCell(signalRow.groupIndex, signalRow.rowIndex + 1, 0)
                        event.accepted = true
                    }
                }
            }

            NumericField {
                id: phaseField
                Layout.preferredWidth: root.compactLayout ? 86 : 104
                text: signalRow.angle.toFixed(2)
                invalidInput: false
                onActiveFocusChanged: {
                    if (activeFocus) {
                        root.selectSignal(signalRow.groupIndex, signalRow.rowIndex)
                        text = signalRow.angle.toFixed(2)
                        selectAll()
                    }
                }
                onTextEdited: {
                    var value = root.parseOperatorNumber(text)
                    if (root.validPhase(value)) {
                        invalidInput = false
                        root.editSignal(signalRow.groupIndex, signalRow.rowIndex, "phase", value)
                    } else {
                        invalidInput = true
                    }
                }
                onEditingFinished: {
                    var value = root.parseOperatorNumber(text)
                    if (!root.validPhase(value)) {
                        text = signalRow.angle.toFixed(2)
                        invalidInput = false
                        root.showMessage(signalRow.sid + " phase must stay within ±360000°.", true)
                    } else {
                        text = value.toFixed(2)
                    }
                }
                Keys.onPressed: function(event) {
                    if ([Qt.Key_Up, Qt.Key_Down, Qt.Key_Left, Qt.Key_Right].indexOf(event.key) >= 0) {
                        root.navigate(signalRow.groupIndex, signalRow.rowIndex, 1, event.key)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        root.focusCell(signalRow.groupIndex, signalRow.rowIndex + 1, 1)
                        event.accepted = true
                    }
                }
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: theme.lineSoft
        }
    }

    component PreviewTitle: RowLayout {
        required property string titleText
        required property string detailText
        Layout.fillWidth: true
        Label {
            text: titleText
            color: theme.textSoft
            font.family: root.uiFont
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }
        Item { Layout.fillWidth: true }
        Label {
            text: detailText
            color: theme.muted
            font.family: root.monoFont
            font.pixelSize: 8
            elide: Text.ElideRight
        }
    }
}
