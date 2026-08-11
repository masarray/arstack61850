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
    minimumWidth: 1180
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
        readonly property color line: "#26313d"
        readonly property color lineSoft: "#1c252f"
        readonly property color text: "#edf2f7"
        readonly property color textSoft: "#c2ccd7"
        readonly property color muted: "#778493"
        readonly property color muted2: "#566271"
        readonly property color accent: "#69a9ff"
        readonly property color green: "#58d49d"
        readonly property color amber: "#e1b25a"
        readonly property color red: "#ff727f"
    }

    property string uiFont: "Inter"
    property string monoFont: "Cascadia Mono"
    property bool phaseLink: false
    property bool liveApply: true
    property bool profileDirty: false
    property real currentScale: 1000.0
    property real voltageScale: 100.0
    property string activeSignal: "Ia"
    property int activeGroup: 0
    property int activeRow: 0
    property string activeUnit: "A"
    property real activeMagnitude: 1.0
    property real activePhase: 0.0
    property real activeQuality: 0
    property string transientMessage: ""

    readonly property bool selectedProfileDeployable:
        sclProfiles.selectedProfile.compatibilityClass === "A" &&
        sclProfiles.selectedProfile.deviceSupport === "ready"
    readonly property bool canDeploy:
        device.connected && !device.running && selectedProfileDeployable && !device.profileDeploying
    readonly property bool canStart:
        device.connected && !device.running &&
        (!sclProfiles.hasProfiles || (device.profileArmed && !profileDirty))

    SclProfileModel { id: sclProfiles }
    DeviceController { id: device }

    Connections {
        target: device
        function onProfileStateChanged() {
            if (device.profileArmed && !device.profileDeploying)
                root.profileDirty = false
        }
        function onDeviceMessage(message) {
            root.transientMessage = message
            messageTimer.restart()
        }
    }

    Timer {
        id: messageTimer
        interval: 2800
        onTriggered: root.transientMessage = ""
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

    function groupModel(group) {
        return group === 0 ? currentModel : voltageModel
    }

    function groupRepeater(group) {
        return group === 0 ? currentRepeater : voltageRepeater
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

    function normalizedAngle(value) {
        var result = value % 360
        if (result > 180)
            result -= 360
        if (result <= -180)
            result += 360
        return result
    }

    function sendSignal(group, row) {
        if (!device.connected)
            return
        var signal = groupModel(group).get(row)
        device.setSignal(
            signal.signalId,
            signal.magnitude,
            signal.phase,
            Number(signal.quality),
            currentScale,
            voltageScale)
    }

    function sendLinkedGroup(group) {
        for (var i = 0; i < 3; ++i)
            sendSignal(group, i)
    }

    function editSignal(group, row, field, value) {
        if (isNaN(value) || (field === "magnitude" && value < 0))
            return

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
        if (liveApply && device.connected) {
            if (phaseLink && row < 3)
                sendLinkedGroup(group)
            else
                sendSignal(group, row)
        }
    }

    function applyAllSignals() {
        for (var group = 0; group < 2; ++group) {
            for (var row = 0; row < 4; ++row)
                sendSignal(group, row)
        }
        device.setFrequency(parseFloat(frequencyField.text))
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
        if (device.connected && liveApply)
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

    component CalmButton: Button {
        id: calmButton
        implicitHeight: 36
        font.family: root.uiFont
        font.pixelSize: 12
        font.weight: Font.DemiBold
        contentItem: Text {
            text: calmButton.text
            color: calmButton.enabled ? theme.textSoft : theme.muted2
            font: calmButton.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 7
            color: calmButton.down ? "#1b2530" : calmButton.hovered ? "#17202a" : theme.raised
            border.width: 1
            border.color: calmButton.hovered ? "#3a4a5b" : theme.line
        }
    }

    component Kicker: Label {
        color: theme.muted
        font.family: root.uiFont
        font.pixelSize: 9
        font.weight: Font.DemiBold
        font.letterSpacing: 1.25
    }

    component Quiet: Label {
        color: theme.muted
        font.family: root.uiFont
        font.pixelSize: 10
    }

    component Surface: Rectangle {
        color: theme.surface
        radius: 10
        border.width: 1
        border.color: theme.lineSoft
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
            radius: 10
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
        height: 58
        color: theme.chrome
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            spacing: 12

            Rectangle {
                width: 30
                height: 30
                radius: 7
                color: "#101b27"
                border.color: "#315071"
                Label {
                    anchors.centerIn: parent
                    text: "≋"
                    color: theme.accent
                    font.pixelSize: 18
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
                    font.letterSpacing: 1.3
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

            Label {
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
                implicitWidth: 116
                model: device.ports
                enabled: !device.connected
                font.family: root.monoFont
                font.pixelSize: 10
                onPressedChanged: {
                    if (pressed)
                        device.refreshPorts()
                }
            }

            Rectangle {
                width: 7
                height: 7
                radius: 4
                color: device.connected ? theme.green : theme.muted2
            }

            Quiet { text: device.connected ? device.portName : "Device offline" }

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
        height: 62
        color: theme.chrome
        border.width: 1
        border.color: theme.lineSoft
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            spacing: 12

            Rectangle {
                width: 8
                height: 8
                radius: 4
                color: device.running ? theme.green : device.connected ? theme.accent : theme.muted2
            }

            ColumnLayout {
                spacing: 1
                Label {
                    text: device.running ? "INJECTING" :
                          device.connected ? (profileDirty ? "PROFILE CHANGED" : "READY") :
                          "DEVICE OFFLINE"
                    color: theme.textSoft
                    font.family: root.uiFont
                    font.pixelSize: 9
                    font.weight: Font.Bold
                }
                Quiet {
                    text: device.running ? "ESP32-P4 realtime publisher active" :
                          device.connected && profileDirty ? "Validate and deploy the selected SCL profile before Start" :
                          device.connected ? "Setpoints editable · live control armed" :
                          "Setpoints remain editable offline"
                }
            }

            Item { Layout.fillWidth: true }

            Label {
                text: activeSignal + "  " + activeMagnitude.toFixed(3) + " " + activeUnit +
                      "  ∠" + activePhase.toFixed(2) + "°"
                color: theme.muted
                font.family: root.monoFont
                font.pixelSize: 9
            }

            CalmButton { text: "Diagnostics"; onClicked: diagnosticsDialog.open() }
            CalmButton {
                text: "Apply all"
                enabled: device.connected
                onClicked: root.applyAllSignals()
            }
            CalmButton {
                text: device.profileDeploying ? "Deploying…" : "Deploy profile"
                enabled: root.canDeploy
                onClicked: {
                    if (device.deployProfile(sclProfiles.selectedProfile))
                        root.profileDirty = true
                }
            }
            CalmButton {
                text: "Stop"
                enabled: device.connected && device.running
                onClicked: device.stop()
            }
            Button {
                id: startButton
                text: "▶  Start live"
                enabled: root.canStart
                implicitWidth: 120
                implicitHeight: 36
                font.family: root.uiFont
                font.pixelSize: 12
                font.weight: Font.DemiBold
                contentItem: Text {
                    text: startButton.text
                    color: startButton.enabled ? "#d6f4e6" : theme.muted2
                    font: startButton.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 7
                    color: startButton.enabled ? "#194d38" : "#121820"
                    border.color: startButton.enabled ? "#347a59" : theme.lineSoft
                }
                onClicked: device.start()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        Surface {
            Layout.preferredWidth: 246
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 13

                ColumnLayout {
                    spacing: 3
                    Kicker { text: "ENGINEERING SOURCE" }
                    Label {
                        text: sclProfiles.hasProfiles ?
                            (sclProfiles.selectedProfile.svId || "Compiled SCL") :
                            "Development profile"
                        color: theme.text
                        font.family: root.uiFont
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
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
                    spacing: 7
                    Kicker { text: "RESOLVED STREAM" }
                    ComboBox {
                        Layout.fillWidth: true
                        model: sclProfiles
                        textRole: "control"
                        currentIndex: sclProfiles.selectedIndex
                        enabled: !device.running
                        font.family: root.uiFont
                        font.pixelSize: 11
                        onActivated: {
                            sclProfiles.selectStream(currentIndex)
                            root.profileDirty = true
                        }
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 6
                        Quiet { text: "Class" }
                        Label {
                            text: "CLASS " + (sclProfiles.selectedProfile.compatibilityClass || "—")
                            color: sclProfiles.selectedProfile.compatibilityClass === "A" ? theme.green :
                                   sclProfiles.selectedProfile.compatibilityClass === "B" ? theme.amber : theme.red
                            font.family: root.uiFont
                            font.pixelSize: 10
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
                        Label {
                            text: sclProfiles.selectedProfile.appIdHex || "—"
                            color: theme.textSoft
                            font.family: root.monoFont
                            font.pixelSize: 9
                        }
                        Quiet { text: "Rate" }
                        Label {
                            text: sclProfiles.selectedProfile.publisherRate ?
                                sclProfiles.selectedProfile.publisherRate + "/s" : "—"
                            color: theme.textSoft
                            font.family: root.monoFont
                            font.pixelSize: 9
                        }
                        Quiet { text: "VLAN" }
                        Label {
                            text: sclProfiles.selectedProfile.vlanPresent ?
                                "P" + sclProfiles.selectedProfile.vlanPriority + " · VID " + sclProfiles.selectedProfile.vlanId :
                                "untagged"
                            color: theme.textSoft
                            font.family: root.monoFont
                            font.pixelSize: 9
                        }
                        Quiet { text: "Payload" }
                        Label {
                            text: sclProfiles.selectedProfile.payloadBytes ?
                                sclProfiles.selectedProfile.payloadBytes + " B" : "—"
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
                        color: sclProfiles.selectedProfile.errors && sclProfiles.selectedProfile.errors.length ?
                            theme.red : theme.amber
                        text: sclProfiles.selectedProfile.errors && sclProfiles.selectedProfile.errors.length ?
                            sclProfiles.selectedProfile.errors[0] : sclProfiles.selectedProfile.warnings[0]
                    }
                }

                ColumnLayout {
                    visible: sclProfiles.selectedProfile.compatibilityClass === "B"
                    Layout.fillWidth: true
                    spacing: 7
                    Kicker { text: "COUNTER POLICY" }
                    Quiet {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "Candidate smpCnt modulus requires explicit profile/evidence confirmation before deployment."
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            id: counterField
                            Layout.fillWidth: true
                            text: sclProfiles.selectedProfile.counterModulus || ""
                            placeholderText: "modulus"
                            font.family: root.monoFont
                            validator: IntValidator { bottom: 1; top: 65535 }
                        }
                        CalmButton {
                            text: "Confirm"
                            onClicked: {
                                if (sclProfiles.confirmCounterModulus(parseInt(counterField.text)))
                                    root.profileDirty = true
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Kicker { text: "ENGINEERING SCALING" }
                    Quiet {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "Explicit test conversion; generic SCL is not assumed to define physical scaling."
                    }
                    RowLayout {
                        Quiet { text: "Current"; Layout.preferredWidth: 48 }
                        TextField {
                            Layout.fillWidth: true
                            text: root.currentScale.toString()
                            font.family: root.monoFont
                            validator: DoubleValidator { bottom: 0.000001 }
                            onEditingFinished: {
                                var value = parseFloat(text)
                                if (value > 0)
                                    root.currentScale = value
                            }
                        }
                        Quiet { text: "ct/A" }
                    }
                    RowLayout {
                        Quiet { text: "Voltage"; Layout.preferredWidth: 48 }
                        TextField {
                            Layout.fillWidth: true
                            text: root.voltageScale.toString()
                            font.family: root.monoFont
                            validator: DoubleValidator { bottom: 0.000001 }
                            onEditingFinished: {
                                var value = parseFloat(text)
                                if (value > 0)
                                    root.voltageScale = value
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
                    rowSpacing: 5
                    Quiet { text: "Rate" }
                    Label { text: device.fps; color: theme.textSoft; font.family: root.monoFont; font.pixelSize: 9; Layout.alignment: Qt.AlignRight }
                    Quiet { text: "Missed" }
                    Label { text: device.missed; color: theme.textSoft; font.family: root.monoFont; font.pixelSize: 9; Layout.alignment: Qt.AlignRight }
                    Quiet { text: "TX fail" }
                    Label { text: device.txFailures; color: theme.textSoft; font.family: root.monoFont; font.pixelSize: 9; Layout.alignment: Qt.AlignRight }
                    Quiet { text: "Generation" }
                    Label { text: device.signalGeneration; color: theme.textSoft; font.family: root.monoFont; font.pixelSize: 9; Layout.alignment: Qt.AlignRight }
                }
            }
        }

        Surface {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 560
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 13

                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        spacing: 2
                        Kicker { text: "INJECTION WORKSPACE" }
                        Label {
                            text: "Manual Injection"
                            color: theme.text
                            font.family: root.uiFont
                            font.pixelSize: 23
                            font.weight: Font.DemiBold
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        text: device.running ? "● INJECTING" : "STOPPED"
                        color: device.running ? theme.green : theme.muted
                        font.family: root.monoFont
                        font.pixelSize: 9
                        font.weight: Font.Bold
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 58
                    radius: 9
                    color: theme.surface2
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 10
                        ColumnLayout {
                            spacing: 0
                            Kicker { text: "FREQUENCY" }
                            RowLayout {
                                TextField {
                                    id: frequencyField
                                    text: "50.000"
                                    implicitWidth: 82
                                    selectByMouse: true
                                    color: theme.text
                                    font.family: root.monoFont
                                    font.pixelSize: 16
                                    background: Item {}
                                    onTextEdited: waveform.requestPaint()
                                    onEditingFinished: {
                                        if (root.liveApply && device.connected)
                                            device.setFrequency(parseFloat(text))
                                    }
                                }
                                Quiet { text: "Hz" }
                            }
                        }
                        CalmButton {
                            text: "50"
                            onClicked: {
                                frequencyField.text = "50.000"
                                waveform.requestPaint()
                                if (device.connected && root.liveApply)
                                    device.setFrequency(50)
                            }
                        }
                        CalmButton {
                            text: "60"
                            onClicked: {
                                frequencyField.text = "60.000"
                                waveform.requestPaint()
                                if (device.connected && root.liveApply)
                                    device.setFrequency(60)
                            }
                        }
                        Rectangle { width: 1; height: 28; color: theme.lineSoft }
                        CheckBox {
                            checked: root.phaseLink
                            text: "3-phase link"
                            onToggled: root.phaseLink = checked
                            font.family: root.uiFont
                            font.pixelSize: 11
                        }
                        Item { Layout.fillWidth: true }
                        CheckBox {
                            checked: root.liveApply
                            text: "Live apply"
                            onToggled: root.liveApply = checked
                            font.family: root.uiFont
                            font.pixelSize: 11
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 12

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: theme.surface2
                        radius: 9
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
                        radius: 9
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
                    Quiet {
                        text: activeMagnitude.toFixed(3) + " " + activeUnit + " RMS · " +
                              activePhase.toFixed(2) + "°"
                    }
                    Item { Layout.fillWidth: true }
                    Quiet { text: "Quality" }
                    TextField {
                        id: qualityField
                        implicitWidth: 98
                        text: "0x" + (Number(root.activeQuality) >>> 0).toString(16).padStart(8, "0").toUpperCase()
                        color: theme.textSoft
                        font.family: root.monoFont
                        font.pixelSize: 9
                        onEditingFinished: {
                            var trimmed = text.trim()
                            var qualityValue = trimmed.toLowerCase().startsWith("0x") ?
                                parseInt(trimmed.substring(2), 16) : parseInt(trimmed, 10)
                            if (!isNaN(qualityValue) && qualityValue >= 0)
                                root.setActiveQuality(qualityValue)
                        }
                    }
                    Quiet { text: "↑↓ channel  ←→ field  Enter next" }
                }
            }
        }

        Surface {
            Layout.preferredWidth: 386
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        spacing: 2
                        Kicker { text: "GENERATED SETPOINT" }
                        Label {
                            text: "Signal Preview"
                            color: theme.text
                            font.family: root.uiFont
                            font.pixelSize: 19
                            font.weight: Font.DemiBold
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Quiet { text: "LOCAL" }
                }

                PreviewTitle {
                    titleText: "Phasor"
                    detailText: activeSignal + " · " + activeMagnitude.toFixed(3) + " " + activeUnit +
                                " ∠ " + activePhase.toFixed(2) + "°"
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
                        var widthValue = width
                        var heightValue = height
                        var centerX = widthValue / 2
                        var centerY = heightValue / 2
                        var radiusValue = Math.min(widthValue, heightValue) * 0.39

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
                                var length = radiusValue * scale *
                                    (0.30 + 0.70 * Math.min(1, signal.magnitude / maxMagnitude))
                                var endX = centerX + Math.cos(angleValue) * length
                                var endY = centerY - Math.sin(angleValue) * length
                                var active = signal.signalId === root.activeSignal

                                ctx.save()
                                ctx.globalAlpha = active ? 1 : 0.70
                                ctx.strokeStyle = signal.traceColor
                                ctx.fillStyle = signal.traceColor
                                ctx.lineWidth = active ? 3.2 : 2.0
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
                                ctx.lineTo(
                                    endX - Math.cos(angleValue - 0.45) * arrowHead,
                                    endY + Math.sin(angleValue - 0.45) * arrowHead)
                                ctx.lineTo(
                                    endX - Math.cos(angleValue + 0.45) * arrowHead,
                                    endY + Math.sin(angleValue + 0.45) * arrowHead)
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

                PreviewTitle {
                    titleText: "Waveform"
                    detailText: frequencyField.text + " Hz · 40 ms"
                }

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
                        var frequency = parseFloat(frequencyField.text) || 50

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
                                var amplitude = laneHeight * 0.39 *
                                    Math.min(1, signal.magnitude / maxMagnitude)
                                var phaseRadians = signal.phase * Math.PI / 180

                                ctx.save()
                                ctx.strokeStyle = signal.traceColor
                                ctx.globalAlpha = active ? 1 : 0.64
                                ctx.lineWidth = active ? 2.6 : 1.7
                                ctx.lineCap = "round"
                                ctx.setLineDash(dashed ? [5, 4] : [])
                                ctx.beginPath()
                                for (var point = 0; point <= 220; ++point) {
                                    var ratio = point / 220
                                    var time = 0.04 * ratio
                                    var sample = Math.sin(2 * Math.PI * frequency * time + phaseRadians)
                                    var x = left + plotWidth * ratio
                                    var y = center - sample * amplitude
                                    if (point === 0)
                                        ctx.moveTo(x, y)
                                    else
                                        ctx.lineTo(x, y)
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
        radius: 8
        color: "#18212b"
        border.color: device.lastError.length > 0 ? "#70404a" : "#36536f"
        implicitWidth: messageLabel.implicitWidth + 28
        implicitHeight: 38
        Label {
            id: messageLabel
            anchors.centerIn: parent
            text: root.transientMessage.length ? root.transientMessage : device.lastError
            color: theme.textSoft
            font.family: root.uiFont
            font.pixelSize: 10
        }
    }

    component MatrixHeader: RowLayout {
        required property string titleText
        required property string symbolText
        required property string unitText
        Layout.fillWidth: true
        Layout.preferredHeight: 46
        Layout.leftMargin: 12
        Layout.rightMargin: 12
        Rectangle {
            width: 23
            height: 23
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
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
        Item { Layout.fillWidth: true }
        Quiet { text: unitText }
    }

    component MatrixColumns: RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: 30
        Layout.leftMargin: 10
        Layout.rightMargin: 10
        spacing: 8
        Quiet { text: "ON"; Layout.preferredWidth: 28; font.pixelSize: 8 }
        Quiet { text: "CH"; Layout.preferredWidth: 34; font.pixelSize: 8 }
        Quiet { text: "MAGNITUDE"; Layout.fillWidth: true; font.pixelSize: 8 }
        Quiet { text: "PHASE"; Layout.preferredWidth: 108; font.pixelSize: 8 }
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
        Layout.preferredHeight: 58
        color: magnitudeField.activeFocus || phaseField.activeFocus ? "#141d27" : "transparent"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 8

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
                Rectangle {
                    width: 6
                    height: 6
                    radius: 3
                    color: signalRow.phaseColor
                }
                Label {
                    text: signalRow.sid
                    color: theme.text
                    font.family: root.monoFont
                    font.pixelSize: 11
                    font.weight: Font.Bold
                }
            }

            TextField {
                id: magnitudeField
                Layout.fillWidth: true
                text: signalRow.mag.toFixed(3)
                selectByMouse: true
                horizontalAlignment: Text.AlignRight
                color: theme.text
                font.family: root.monoFont
                font.pixelSize: 13
                font.weight: Font.DemiBold
                background: Rectangle {
                    radius: 6
                    color: magnitudeField.activeFocus ? "#0c141d" : "#111820"
                    border.width: magnitudeField.activeFocus ? 1 : 0
                    border.color: theme.accent
                }
                onActiveFocusChanged: {
                    if (activeFocus) {
                        root.selectSignal(signalRow.groupIndex, signalRow.rowIndex)
                        selectAll()
                    }
                }
                onEditingFinished:
                    root.editSignal(signalRow.groupIndex, signalRow.rowIndex, "magnitude", parseFloat(text))
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

            TextField {
                id: phaseField
                Layout.preferredWidth: 108
                text: signalRow.angle.toFixed(2)
                selectByMouse: true
                horizontalAlignment: Text.AlignRight
                color: theme.text
                font.family: root.monoFont
                font.pixelSize: 13
                font.weight: Font.DemiBold
                background: Rectangle {
                    radius: 6
                    color: phaseField.activeFocus ? "#0c141d" : "#111820"
                    border.width: phaseField.activeFocus ? 1 : 0
                    border.color: theme.accent
                }
                onActiveFocusChanged: {
                    if (activeFocus) {
                        root.selectSignal(signalRow.groupIndex, signalRow.rowIndex)
                        selectAll()
                    }
                }
                onEditingFinished:
                    root.editSignal(signalRow.groupIndex, signalRow.rowIndex, "phase", parseFloat(text))
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
        }
    }
}
