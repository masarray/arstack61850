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
    color: palette.bg

    property var palette: ({
        bg: "#090d12",
        chrome: "#0c1117",
        surface: "#11171f",
        raised: "#151d27",
        quiet: "#0e141b",
        line: "#26313d",
        lineSoft: "#1c252f",
        text: "#edf2f7",
        textSoft: "#c2ccd7",
        muted: "#778493",
        muted2: "#566271",
        accent: "#69a9ff",
        green: "#58d49d",
        amber: "#e1b25a",
        red: "#ff727f"
    })
    property string uiFont: "Inter"
    property string monoFont: "Cascadia Mono"
    property bool phaseLink: false
    property string activeSignal: "Ia"
    property string activeUnit: "A"
    property real activeMagnitude: 1.0
    property real activePhase: 0.0

    SclProfileModel { id: sclProfiles }

    ListModel {
        id: currentModel
        ListElement { signalId: "Ia"; magnitude: 1.000; phase: 0.0; enabled: true; traceColor: "#ff6673" }
        ListElement { signalId: "Ib"; magnitude: 1.000; phase: -120.0; enabled: true; traceColor: "#e6b552" }
        ListElement { signalId: "Ic"; magnitude: 1.000; phase: 120.0; enabled: true; traceColor: "#59a7ff" }
        ListElement { signalId: "In"; magnitude: 0.000; phase: 0.0; enabled: true; traceColor: "#9a88df" }
    }
    ListModel {
        id: voltageModel
        ListElement { signalId: "Ua"; magnitude: 57.740; phase: 0.0; enabled: true; traceColor: "#ff6673" }
        ListElement { signalId: "Ub"; magnitude: 57.740; phase: -120.0; enabled: true; traceColor: "#e6b552" }
        ListElement { signalId: "Uc"; magnitude: 57.740; phase: 120.0; enabled: true; traceColor: "#59a7ff" }
        ListElement { signalId: "Un"; magnitude: 0.000; phase: 0.0; enabled: true; traceColor: "#9a88df" }
    }

    function modelForGroup(group) { return group === 0 ? currentModel : voltageModel }
    function repeaterForGroup(group) { return group === 0 ? currentRepeater : voltageRepeater }
    function focusMatrix(group, row, column) {
        var g = group
        var r = row
        while (r < 0) { g = (g + 1) % 2; r += 4 }
        while (r > 3) { g = (g + 1) % 2; r -= 4 }
        var item = repeaterForGroup(g).itemAt(r)
        if (!item) return
        var editor = column === 0 ? item.magnitudeEditor : item.phaseEditor
        editor.forceActiveFocus()
        editor.selectAll()
    }
    function moveCell(group, row, column, key) {
        if (key === Qt.Key_Up) focusMatrix(group, row - 1, column)
        else if (key === Qt.Key_Down) focusMatrix(group, row + 1, column)
        else if (key === Qt.Key_Left) {
            if (column === 1) focusMatrix(group, row, 0)
            else focusMatrix(group, row - 1, 1)
        } else if (key === Qt.Key_Right) {
            if (column === 0) focusMatrix(group, row, 1)
            else focusMatrix(group, row + 1, 0)
        }
    }
    function normalizeAngle(value) {
        var v = value % 360
        if (v > 180) v -= 360
        if (v <= -180) v += 360
        return v
    }
    function updateSignal(group, row, field, value) {
        if (isNaN(value)) return
        var m = modelForGroup(group)
        if (field === "magnitude" && value < 0) return
        m.setProperty(row, field, value)
        if (phaseLink && row < 3) {
            if (field === "magnitude") {
                for (var i = 0; i < 3; ++i) m.setProperty(i, "magnitude", value)
            } else {
                var base = value
                if (row === 1) base += 120
                if (row === 2) base -= 120
                m.setProperty(0, "phase", normalizeAngle(base))
                m.setProperty(1, "phase", normalizeAngle(base - 120))
                m.setProperty(2, "phase", normalizeAngle(base + 120))
            }
        }
        activeSignal = m.get(row).signalId
        activeUnit = group === 0 ? "A" : "V"
        activeMagnitude = m.get(row).magnitude
        activePhase = m.get(row).phase
        phasor.requestPaint()
        waveform.requestPaint()
    }
    function loadBalanced() {
        var cm = [0, -120, 120, 0]
        for (var i = 0; i < 4; ++i) {
            currentModel.setProperty(i, "magnitude", i < 3 ? 1.0 : 0.0)
            currentModel.setProperty(i, "phase", cm[i])
            voltageModel.setProperty(i, "magnitude", i < 3 ? 57.74 : 0.0)
            voltageModel.setProperty(i, "phase", cm[i])
        }
        phasor.requestPaint(); waveform.requestPaint()
    }
    function zeroAll() {
        for (var i = 0; i < 4; ++i) {
            currentModel.setProperty(i, "magnitude", 0.0)
            voltageModel.setProperty(i, "magnitude", 0.0)
        }
        phasor.requestPaint(); waveform.requestPaint()
    }

    component QuietLabel: Label {
        color: root.palette.muted
        font.family: root.uiFont
        font.pixelSize: 11
        font.weight: Font.Medium
    }
    component SectionKicker: Label {
        color: root.palette.muted
        font.family: root.uiFont
        font.pixelSize: 10
        font.weight: Font.DemiBold
        font.letterSpacing: 1.4
        textFormat: Text.PlainText
    }
    component Surface: Rectangle {
        color: root.palette.surface
        radius: 10
        border.width: 1
        border.color: root.palette.lineSoft
    }
    component CalmButton: Button {
        id: calmButton
        implicitHeight: 36
        font.family: root.uiFont
        font.pixelSize: 12
        font.weight: Font.DemiBold
        contentItem: Text {
            text: calmButton.text
            color: calmButton.enabled ? root.palette.textSoft : root.palette.muted2
            font: calmButton.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 7
            color: calmButton.down ? "#1b2530" : calmButton.hovered ? "#17202a" : root.palette.raised
            border.width: 1
            border.color: calmButton.hovered ? "#3a4a5b" : root.palette.line
        }
    }

    FileDialog {
        id: sclFileDialog
        title: "Open IEC 61850 engineering file"
        nameFilters: ["IEC 61850 SCL (*.scd *.cid *.icd *.iid *.ssd *.xml)", "All files (*)"]
        onAccepted: sclProfiles.loadFile(selectedFile)
    }

    header: Rectangle {
        height: 58
        color: root.palette.chrome
        border.color: root.palette.lineSoft
        border.width: 0
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            spacing: 18
            RowLayout {
                spacing: 10
                Rectangle {
                    width: 30; height: 30; radius: 7
                    color: "#101b27"; border.color: "#315071"
                    Canvas {
                        anchors.fill: parent
                        onPaint: {
                            var c = getContext("2d"); c.clearRect(0,0,width,height)
                            c.strokeStyle = root.palette.accent; c.lineWidth = 1.4
                            c.beginPath(); c.moveTo(7,8); c.lineTo(23,13); c.lineTo(7,18); c.stroke()
                            c.beginPath(); c.moveTo(7,22); c.lineTo(23,17); c.stroke()
                        }
                    }
                }
                ColumnLayout {
                    spacing: 0
                    Label { text: "ARSTACK61850"; color: root.palette.muted; font.family: root.uiFont; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: 1.2 }
                    Label { text: "SMV Injector"; color: root.palette.text; font.family: root.uiFont; font.pixelSize: 14; font.weight: Font.DemiBold }
                }
            }
            Item { Layout.fillWidth: true }
            QuietLabel { text: sclProfiles.hasProfiles ? (sclProfiles.selectedProfile.svId || "Resolved stream") : "No SCL profile" }
            Rectangle { width: 1; height: 20; color: root.palette.line }
            RowLayout {
                spacing: 8
                Rectangle { width: 7; height: 7; radius: 4; color: "#65717e" }
                QuietLabel { text: "Device offline" }
                CalmButton { text: "Connect"; enabled: false; ToolTip.visible: hovered; ToolTip.text: "Qt serial transport is the next integration step" }
            }
        }
    }

    footer: Rectangle {
        height: 62
        color: root.palette.chrome
        border.color: root.palette.lineSoft
        border.width: 1
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            spacing: 16
            RowLayout {
                spacing: 9
                Rectangle { width: 8; height: 8; radius: 4; color: root.palette.muted2 }
                ColumnLayout {
                    spacing: 1
                    Label { text: "DEVICE OFFLINE"; color: root.palette.textSoft; font.family: root.uiFont; font.pixelSize: 10; font.weight: Font.DemiBold }
                    QuietLabel { text: "Setpoints editable · native PROFILE transport not armed yet" }
                }
            }
            Item { Layout.fillWidth: true }
            Label {
                text: activeSignal + "  " + activeMagnitude.toFixed(3) + " " + activeUnit + "  ∠" + activePhase.toFixed(2) + "°"
                color: root.palette.muted
                font.family: root.monoFont
                font.pixelSize: 10
            }
            Rectangle { width: 1; height: 20; color: root.palette.line }
            CalmButton { text: "Deploy profile"; enabled: false }
            CalmButton { text: "Stop"; enabled: false }
            Button {
                id: startButton
                text: "▶  Start live"
                enabled: false
                implicitWidth: 126; implicitHeight: 38
                font.family: root.uiFont; font.pixelSize: 12; font.weight: Font.DemiBold
                contentItem: Text { text: startButton.text; color: startButton.enabled ? "#d8f5e7" : root.palette.muted2; font: startButton.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: 8; color: startButton.enabled ? "#194d38" : "#121820"; border.color: startButton.enabled ? "#347a59" : root.palette.lineSoft }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        Surface {
            Layout.preferredWidth: 238
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 14
                ColumnLayout {
                    spacing: 3
                    SectionKicker { text: "ENGINEERING SOURCE" }
                    Label { text: sclProfiles.hasProfiles ? (sclProfiles.selectedProfile.svId || "Compiled SCL") : "Development profile"; color: root.palette.text; font.family: root.uiFont; font.pixelSize: 18; font.weight: Font.DemiBold }
                    QuietLabel { text: sclProfiles.sourceName.length ? sclProfiles.sourceName : "Import SCL / CID / SCD / IID"; elide: Text.ElideMiddle; Layout.fillWidth: true }
                }

                CalmButton { Layout.fillWidth: true; text: "Open SCL / CID"; onClicked: sclFileDialog.open() }

                ColumnLayout {
                    visible: sclProfiles.hasProfiles
                    spacing: 7
                    SectionKicker { text: "RESOLVED STREAM" }
                    ComboBox {
                        id: streamCombo
                        Layout.fillWidth: true
                        model: sclProfiles
                        textRole: "control"
                        currentIndex: sclProfiles.selectedIndex
                        onActivated: sclProfiles.selectStream(currentIndex)
                        font.family: root.uiFont
                        font.pixelSize: 11
                    }
                    GridLayout {
                        columns: 2
                        columnSpacing: 8; rowSpacing: 6
                        QuietLabel { text: "Class" }
                        Label { text: sclProfiles.selectedProfile.compatibilityClass ? "CLASS " + sclProfiles.selectedProfile.compatibilityClass : "—"; color: sclProfiles.selectedProfile.compatibilityClass === "A" ? root.palette.green : sclProfiles.selectedProfile.compatibilityClass === "B" ? root.palette.amber : root.palette.red; font.family: root.uiFont; font.pixelSize: 11; font.weight: Font.DemiBold }
                        QuietLabel { text: "Support" }
                        Label { text: sclProfiles.selectedProfile.deviceSupport || "—"; color: root.palette.textSoft; font.family: root.uiFont; font.pixelSize: 10 }
                        QuietLabel { text: "APPID" }
                        Label { text: sclProfiles.selectedProfile.appIdHex || "—"; color: root.palette.textSoft; font.family: root.monoFont; font.pixelSize: 10 }
                        QuietLabel { text: "Rate" }
                        Label { text: sclProfiles.selectedProfile.publisherRate ? sclProfiles.selectedProfile.publisherRate + "/s" : "—"; color: root.palette.textSoft; font.family: root.monoFont; font.pixelSize: 10 }
                        QuietLabel { text: "VLAN" }
                        Label { text: sclProfiles.selectedProfile.vlanPresent ? "P" + sclProfiles.selectedProfile.vlanPriority + " · VID " + sclProfiles.selectedProfile.vlanId : "untagged"; color: root.palette.textSoft; font.family: root.monoFont; font.pixelSize: 10 }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: root.palette.lineSoft }

                ColumnLayout {
                    visible: sclProfiles.selectedProfile.compatibilityClass === "B"
                    spacing: 7
                    SectionKicker { text: "COUNTER POLICY" }
                    QuietLabel { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "Candidate smpCnt cycle needs explicit engineering or observed-wire confirmation." }
                    RowLayout {
                        TextField { id: counterField; Layout.fillWidth: true; placeholderText: "modulus"; text: sclProfiles.selectedProfile.counterModulus || ""; font.family: root.monoFont; validator: IntValidator { bottom: 1; top: 65535 } }
                        CalmButton { text: "Confirm"; onClicked: sclProfiles.confirmCounterModulus(parseInt(counterField.text)) }
                    }
                }

                ColumnLayout {
                    spacing: 7
                    SectionKicker { text: "QUICK SETUP" }
                    CalmButton { Layout.fillWidth: true; text: "Balanced 3-phase"; onClicked: root.loadBalanced() }
                    CalmButton { Layout.fillWidth: true; text: "Zero output"; onClicked: root.zeroAll() }
                }

                Item { Layout.fillHeight: true }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    SectionKicker { text: "RUNTIME" }
                    Repeater {
                        model: ["Rate", "Missed", "TX fail", "Generation"]
                        RowLayout {
                            Layout.fillWidth: true
                            QuietLabel { text: modelData }
                            Item { Layout.fillWidth: true }
                            Label { text: "—"; color: root.palette.textSoft; font.family: root.monoFont; font.pixelSize: 10 }
                        }
                    }
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
                spacing: 14
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        spacing: 2
                        SectionKicker { text: "INJECTION WORKSPACE" }
                        Label { text: "Manual Injection"; color: root.palette.text; font.family: root.uiFont; font.pixelSize: 24; font.weight: Font.DemiBold }
                    }
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        width: 88; height: 28; radius: 7; color: root.palette.quiet; border.color: root.palette.lineSoft
                        Row { anchors.centerIn: parent; spacing: 7; Rectangle { width: 6; height: 6; radius: 3; color: root.palette.muted2 } QuietLabel { text: "STOPPED" } }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 62
                    radius: 9
                    color: root.palette.quiet
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14; anchors.rightMargin: 14
                        spacing: 12
                        ColumnLayout {
                            spacing: 1
                            SectionKicker { text: "FREQUENCY" }
                            RowLayout {
                                spacing: 5
                                TextField { id: frequencyField; text: "50.000"; implicitWidth: 86; color: root.palette.text; font.family: root.monoFont; font.pixelSize: 17; selectByMouse: true; background: Item {} }
                                QuietLabel { text: "Hz" }
                            }
                        }
                        CalmButton { text: "50"; onClicked: frequencyField.text = "50.000" }
                        CalmButton { text: "60"; onClicked: frequencyField.text = "60.000" }
                        Rectangle { width: 1; height: 28; color: root.palette.lineSoft }
                        CheckBox {
                            id: linkCheck
                            checked: root.phaseLink
                            onToggled: root.phaseLink = checked
                            text: "3-phase link"
                            font.family: root.uiFont; font.pixelSize: 11
                        }
                        Item { Layout.fillWidth: true }
                        CheckBox { checked: true; text: "Live apply"; enabled: false; font.family: root.uiFont; font.pixelSize: 11; ToolTip.visible: hovered; ToolTip.text: "Enabled when native serial transport lands" }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 12
                    SignalMatrix {
                        id: currentMatrix
                        Layout.fillWidth: true; Layout.fillHeight: true
                        title: "Current"; symbol: "I"; unit: "A RMS"; groupIndex: 0; signalModel: currentModel; signalRepeater: currentRepeater
                    }
                    SignalMatrix {
                        id: voltageMatrix
                        Layout.fillWidth: true; Layout.fillHeight: true
                        title: "Voltage"; symbol: "U"; unit: "V RMS"; groupIndex: 1; signalModel: voltageModel; signalRepeater: voltageRepeater
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Rectangle { width: 7; height: 7; radius: 4; color: root.palette.accent }
                    Label { text: activeSignal; color: root.palette.textSoft; font.family: root.monoFont; font.pixelSize: 11; font.weight: Font.DemiBold }
                    QuietLabel { text: activeMagnitude.toFixed(3) + " " + activeUnit + " RMS  ·  " + activePhase.toFixed(2) + "°" }
                    Item { Layout.fillWidth: true }
                    QuietLabel { text: "↑↓ channel    ←→ field    Enter next" }
                }
            }
        }

        Surface {
            Layout.preferredWidth: 386
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        spacing: 2
                        SectionKicker { text: "GENERATED SETPOINT" }
                        Label { text: "Signal Preview"; color: root.palette.text; font.family: root.uiFont; font.pixelSize: 20; font.weight: Font.DemiBold }
                    }
                    Item { Layout.fillWidth: true }
                    QuietLabel { text: "LOCAL" }
                }

                ColumnLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredHeight: 1
                    spacing: 6
                    RowLayout { Layout.fillWidth: true; Label { text: "Phasor"; color: root.palette.textSoft; font.family: root.uiFont; font.pixelSize: 12; font.weight: Font.DemiBold } Item { Layout.fillWidth: true } Label { text: activeSignal + "  ·  " + activeMagnitude.toFixed(3) + " " + activeUnit + " ∠ " + activePhase.toFixed(2) + "°"; color: root.palette.muted; font.family: root.monoFont; font.pixelSize: 9 } }
                    Canvas {
                        id: phasor
                        Layout.fillWidth: true; Layout.fillHeight: true
                        onWidthChanged: requestPaint(); onHeightChanged: requestPaint()
                        onPaint: {
                            var ctx = getContext("2d"); ctx.reset();
                            var w = width, h = height, cx = w / 2, cy = h / 2, r = Math.min(w, h) * 0.38
                            ctx.strokeStyle = "#283441"; ctx.lineWidth = 1
                            for (var ring = 1; ring <= 3; ++ring) { ctx.beginPath(); ctx.arc(cx, cy, r * ring / 3, 0, Math.PI * 2); ctx.stroke() }
                            for (var deg = 0; deg < 180; deg += 30) { var a = deg * Math.PI / 180; var dx = Math.cos(a) * r; var dy = Math.sin(a) * r; ctx.beginPath(); ctx.moveTo(cx-dx,cy-dy);ctx.lineTo(cx+dx,cy+dy);ctx.stroke() }
                            function draw(model, scale, dashed) {
                                var max = 0.0001
                                for (var i=0;i<4;++i) max = Math.max(max, model.get(i).magnitude)
                                for (var j=0;j<4;++j) {
                                    var s=model.get(j); if (!s.enabled || s.magnitude <= 0) continue
                                    var angle=s.phase*Math.PI/180; var length=r*scale*(0.28+0.72*Math.min(1,s.magnitude/max)); var x=cx+Math.cos(angle)*length; var y=cy-Math.sin(angle)*length
                                    var active=s.signalId===root.activeSignal
                                    ctx.save(); ctx.globalAlpha=active?1:0.70; ctx.strokeStyle=s.traceColor; ctx.fillStyle=s.traceColor; ctx.lineWidth=active?3.2:2.0; ctx.setLineDash(dashed?[5,4]:[]); ctx.lineCap="round"; ctx.beginPath();ctx.moveTo(cx,cy);ctx.lineTo(x,y);ctx.stroke();ctx.setLineDash([])
                                    var head=active?8:6;ctx.beginPath();ctx.moveTo(x,y);ctx.lineTo(x-Math.cos(angle-.45)*head,y+Math.sin(angle-.45)*head);ctx.lineTo(x-Math.cos(angle+.45)*head,y+Math.sin(angle+.45)*head);ctx.closePath();ctx.fill()
                                    if(active){ctx.font="600 11px Inter";ctx.fillText(s.signalId,x+8,y-7)}ctx.restore()
                                }
                            }
                            draw(voltageModel,1.0,false); draw(currentModel,0.78,true)
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredHeight: 0.85
                    spacing: 6
                    RowLayout { Layout.fillWidth: true; Label { text: "Waveform"; color: root.palette.textSoft; font.family: root.uiFont; font.pixelSize: 12; font.weight: Font.DemiBold } Item { Layout.fillWidth: true } Label { text: frequencyField.text + " Hz  ·  40 ms"; color: root.palette.muted; font.family: root.monoFont; font.pixelSize: 9 } }
                    Canvas {
                        id: waveform
                        Layout.fillWidth: true; Layout.fillHeight: true
                        onWidthChanged: requestPaint(); onHeightChanged: requestPaint()
                        onPaint: {
                            var ctx=getContext("2d");ctx.reset();var w=width,h=height,left=25,right=8,top=12,bottom=14,pw=w-left-right,ph=h-top-bottom,gap=16,lane=(ph-gap)/2,vy=top+lane/2,iy=top+lane+gap+lane/2,f=parseFloat(frequencyField.text)||50
                            ctx.lineWidth=1;ctx.strokeStyle="#202b36";for(var gx=0;gx<=8;++gx){var x=left+pw*gx/8;ctx.beginPath();ctx.moveTo(x,top);ctx.lineTo(x,top+ph);ctx.stroke()}ctx.strokeStyle="#33404d";[vy,iy].forEach(function(y){ctx.beginPath();ctx.moveTo(left,y);ctx.lineTo(left+pw,y);ctx.stroke()})
                            function draw(model,center,dashed){var max=.0001;for(var i=0;i<4;++i)max=Math.max(max,model.get(i).magnitude);for(var j=0;j<4;++j){var s=model.get(j);if(!s.enabled||s.magnitude<=0)continue;var active=s.signalId===root.activeSignal,amp=lane*.39*Math.min(1,s.magnitude/max),pr=s.phase*Math.PI/180;ctx.save();ctx.strokeStyle=s.traceColor;ctx.globalAlpha=active?1:.64;ctx.lineWidth=active?2.6:1.7;ctx.lineCap="round";ctx.setLineDash(dashed?[5,4]:[]);ctx.beginPath();for(var p=0;p<=220;++p){var ratio=p/220,t=.04*ratio,val=Math.sin(2*Math.PI*f*t+pr),x=left+pw*ratio,y=center-val*amp;if(p===0)ctx.moveTo(x,y);else ctx.lineTo(x,y)}ctx.stroke();ctx.restore()}}
                            draw(voltageModel,vy,false);draw(currentModel,iy,true)
                        }
                    }
                }
                QuietLabel { Layout.fillWidth: true; horizontalAlignment: Text.AlignRight; text: "Generated setpoint · not independent on-wire proof" }
            }
        }
    }

    component SignalMatrix: Rectangle {
        id: matrix
        required property string title
        required property string symbol
        required property string unit
        required property int groupIndex
        required property var signalModel
        required property var signalRepeater
        color: root.palette.quiet
        radius: 9
        border.color: root.palette.lineSoft
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            RowLayout {
                Layout.fillWidth: true; Layout.preferredHeight: 44
                Layout.leftMargin: 12; Layout.rightMargin: 12
                Rectangle { width: 23; height: 23; radius: 5; color: "#152235"; Label { anchors.centerIn: parent; text: matrix.symbol; color: root.palette.accent; font.family: root.monoFont; font.pixelSize: 10; font.weight: Font.Bold } }
                Label { text: matrix.title; color: root.palette.textSoft; font.family: root.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold }
                Item { Layout.fillWidth: true }
                QuietLabel { text: matrix.unit }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: root.palette.lineSoft }
            RowLayout {
                Layout.fillWidth: true; Layout.preferredHeight: 30
                Layout.leftMargin: 10; Layout.rightMargin: 10; spacing: 8
                QuietLabel { text: "ON"; Layout.preferredWidth: 28; font.pixelSize: 9 }
                QuietLabel { text: "CH"; Layout.preferredWidth: 32; font.pixelSize: 9 }
                QuietLabel { text: "MAGNITUDE"; Layout.fillWidth: true; font.pixelSize: 9 }
                QuietLabel { text: "PHASE"; Layout.preferredWidth: 112; font.pixelSize: 9 }
            }
            Repeater {
                id: internalRepeater
                model: matrix.signalModel
                delegate: Rectangle {
                    id: signalRow
                    required property int index
                    required property string signalId
                    required property real magnitude
                    required property real phase
                    required property bool enabled
                    required property string traceColor
                    property alias magnitudeEditor: magField
                    property alias phaseEditor: phaseField
                    width: parent ? parent.width : 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: 58
                    color: (magField.activeFocus || phaseField.activeFocus) ? "#141d27" : "transparent"
                    border.width: 0
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10; anchors.rightMargin: 10
                        spacing: 8
                        CheckBox {
                            Layout.preferredWidth: 28
                            checked: signalRow.enabled
                            onToggled: matrix.signalModel.setProperty(signalRow.index, "enabled", checked)
                        }
                        RowLayout {
                            Layout.preferredWidth: 32; spacing: 5
                            Rectangle { width: 6; height: 6; radius: 3; color: signalRow.traceColor }
                            Label { text: signalRow.signalId; color: root.palette.text; font.family: root.monoFont; font.pixelSize: 12; font.weight: Font.DemiBold }
                        }
                        TextField {
                            id: magField
                            Layout.fillWidth: true
                            text: signalRow.magnitude.toFixed(3)
                            selectByMouse: true
                            color: root.palette.text
                            font.family: root.monoFont; font.pixelSize: 14; font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignRight
                            background: Rectangle { radius: 6; color: magField.activeFocus ? "#0c141d" : "#111820"; border.width: magField.activeFocus ? 1 : 0; border.color: root.palette.accent }
                            onActiveFocusChanged: if (activeFocus) { root.activeSignal=signalRow.signalId; root.activeUnit=matrix.groupIndex===0?"A":"V"; root.activeMagnitude=signalRow.magnitude; root.activePhase=signalRow.phase; selectAll(); phasor.requestPaint(); waveform.requestPaint() }
                            onEditingFinished: root.updateSignal(matrix.groupIndex, signalRow.index, "magnitude", parseFloat(text))
                            Keys.onPressed: function(event) { if ([Qt.Key_Up,Qt.Key_Down,Qt.Key_Left,Qt.Key_Right].indexOf(event.key)>=0) { root.moveCell(matrix.groupIndex,signalRow.index,0,event.key); event.accepted=true } else if(event.key===Qt.Key_Return||event.key===Qt.Key_Enter){root.focusMatrix(matrix.groupIndex,signalRow.index+1,0);event.accepted=true} }
                        }
                        TextField {
                            id: phaseField
                            Layout.preferredWidth: 112
                            text: signalRow.phase.toFixed(2)
                            selectByMouse: true
                            color: root.palette.text
                            font.family: root.monoFont; font.pixelSize: 14; font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignRight
                            background: Rectangle { radius: 6; color: phaseField.activeFocus ? "#0c141d" : "#111820"; border.width: phaseField.activeFocus ? 1 : 0; border.color: root.palette.accent }
                            onActiveFocusChanged: if (activeFocus) { root.activeSignal=signalRow.signalId; root.activeUnit=matrix.groupIndex===0?"A":"V"; root.activeMagnitude=signalRow.magnitude; root.activePhase=signalRow.phase; selectAll(); phasor.requestPaint(); waveform.requestPaint() }
                            onEditingFinished: root.updateSignal(matrix.groupIndex, signalRow.index, "phase", parseFloat(text))
                            Keys.onPressed: function(event) { if ([Qt.Key_Up,Qt.Key_Down,Qt.Key_Left,Qt.Key_Right].indexOf(event.key)>=0) { root.moveCell(matrix.groupIndex,signalRow.index,1,event.key); event.accepted=true } else if(event.key===Qt.Key_Return||event.key===Qt.Key_Enter){root.focusMatrix(matrix.groupIndex,signalRow.index+1,1);event.accepted=true} }
                        }
                    }
                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: root.palette.lineSoft; opacity: 0.72 }
                }
            }
            Component.onCompleted: {
                // Expose the internal repeater to the root keyboard navigator.
                if (matrix.groupIndex === 0) currentRepeaterProxy.target = internalRepeater
                else voltageRepeaterProxy.target = internalRepeater
            }
        }
    }

    // Proxy objects give root.focusMatrix() stable IDs while repeaters live inside
    // component instances.
    QtObject { id: currentRepeaterProxy; property var target: null; function itemAt(i) { return target ? target.itemAt(i) : null } }
    QtObject { id: voltageRepeaterProxy; property var target: null; function itemAt(i) { return target ? target.itemAt(i) : null } }
    property var currentRepeater: currentRepeaterProxy
    property var voltageRepeater: voltageRepeaterProxy
}
