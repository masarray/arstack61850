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

    color: theme.bg
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

    function groupModel(group) { return group === 0 ? currentModel : voltageModel }
    function groupRepeater(group) { return group === 0 ? currentRep : voltageRep }
    function focusCell(group, row, column) {
        var g = group
        var r = row
        if (r < 0) { g = (g + 1) % 2; r = 3 }
        if (r > 3) { g = (g + 1) % 2; r = 0 }
        var item = groupRepeater(g).itemAt(r)
        if (!item) return
        var field = column === 0 ? item.magEditor : item.phaseEditor
        field.forceActiveFocus()
        field.selectAll()
    }
    function navigate(group, row, column, key) {
        if (key === Qt.Key_Up) focusCell(group, row - 1, column)
        else if (key === Qt.Key_Down) focusCell(group, row + 1, column)
        else if (key === Qt.Key_Left) column === 1 ? focusCell(group, row, 0) : focusCell(group, row - 1, 1)
        else if (key === Qt.Key_Right) column === 0 ? focusCell(group, row, 1) : focusCell(group, row + 1, 0)
    }
    function normalizedAngle(value) {
        var v = value % 360
        if (v > 180) v -= 360
        if (v <= -180) v += 360
        return v
    }
    function editSignal(group, row, field, value) {
        if (isNaN(value) || (field === "magnitude" && value < 0)) return
        var m = groupModel(group)
        m.setProperty(row, field, value)
        if (phaseLink && row < 3) {
            if (field === "magnitude") {
                for (var i = 0; i < 3; ++i) m.setProperty(i, "magnitude", value)
            } else {
                var base = value
                if (row === 1) base += 120
                if (row === 2) base -= 120
                m.setProperty(0, "phase", normalizedAngle(base))
                m.setProperty(1, "phase", normalizedAngle(base - 120))
                m.setProperty(2, "phase", normalizedAngle(base + 120))
            }
        }
        var s = m.get(row)
        activeSignal = s.signalId
        activeUnit = group === 0 ? "A" : "V"
        activeMagnitude = s.magnitude
        activePhase = s.phase
        phasor.requestPaint()
        waveform.requestPaint()
    }
    function balanced() {
        var angles = [0, -120, 120, 0]
        for (var i = 0; i < 4; ++i) {
            currentModel.setProperty(i, "magnitude", i < 3 ? 1.0 : 0.0)
            currentModel.setProperty(i, "phase", angles[i])
            voltageModel.setProperty(i, "magnitude", i < 3 ? 57.74 : 0.0)
            voltageModel.setProperty(i, "phase", angles[i])
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

    component CalmButton: Button {
        id: control
        implicitHeight: 36
        font.family: root.uiFont
        font.pixelSize: 12
        font.weight: Font.DemiBold
        contentItem: Text {
            text: control.text
            color: control.enabled ? theme.textSoft : theme.muted2
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 7
            color: control.down ? "#1b2530" : control.hovered ? "#17202a" : theme.raised
            border.width: 1
            border.color: control.hovered ? "#3a4a5b" : theme.line
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
        nameFilters: ["IEC 61850 SCL (*.scd *.cid *.icd *.iid *.ssd *.xml)", "All files (*)"]
        onAccepted: sclProfiles.loadFile(selectedFile)
    }

    header: Rectangle {
        height: 58
        color: theme.chrome
        border.width: 0
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            spacing: 16
            Rectangle {
                width: 30; height: 30; radius: 7; color: "#101b27"; border.color: "#315071"
                Label { anchors.centerIn: parent; text: "≋"; color: theme.accent; font.pixelSize: 18 }
            }
            ColumnLayout {
                spacing: 0
                Label { text: "ARSTACK61850"; color: theme.muted; font.family: root.uiFont; font.pixelSize: 8; font.weight: Font.Bold; font.letterSpacing: 1.3 }
                Label { text: "SMV Injector"; color: theme.text; font.family: root.uiFont; font.pixelSize: 14; font.weight: Font.DemiBold }
            }
            Item { Layout.fillWidth: true }
            Label { text: sclProfiles.hasProfiles ? (sclProfiles.selectedProfile.svId || "Resolved SV") : "No SCL profile"; color: theme.textSoft; font.family: root.monoFont; font.pixelSize: 10 }
            Rectangle { width: 1; height: 20; color: theme.line }
            Rectangle { width: 7; height: 7; radius: 4; color: theme.muted2 }
            Quiet { text: "Device offline" }
            CalmButton { text: "Connect"; enabled: false; ToolTip.visible: hovered; ToolTip.text: "Native serial control follows in the next tranche" }
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
            spacing: 14
            Rectangle { width: 8; height: 8; radius: 4; color: theme.muted2 }
            ColumnLayout {
                spacing: 1
                Label { text: "DEVICE OFFLINE"; color: theme.textSoft; font.family: root.uiFont; font.pixelSize: 9; font.weight: Font.Bold }
                Quiet { text: "Setpoints editable · PROFILE transport intentionally not armed yet" }
            }
            Item { Layout.fillWidth: true }
            Label { text: activeSignal + "  " + activeMagnitude.toFixed(3) + " " + activeUnit + "  ∠" + activePhase.toFixed(2) + "°"; color: theme.muted; font.family: root.monoFont; font.pixelSize: 9 }
            CalmButton { text: "Deploy profile"; enabled: false }
            CalmButton { text: "Stop"; enabled: false }
            CalmButton { text: "▶  Start live"; enabled: false; implicitWidth: 120 }
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
                spacing: 13
                ColumnLayout {
                    spacing: 3
                    Kicker { text: "ENGINEERING SOURCE" }
                    Label { text: sclProfiles.hasProfiles ? (sclProfiles.selectedProfile.svId || "Compiled SCL") : "Development profile"; color: theme.text; font.family: root.uiFont; font.pixelSize: 17; font.weight: Font.DemiBold }
                    Quiet { Layout.fillWidth: true; elide: Text.ElideMiddle; text: sclProfiles.sourceName.length ? sclProfiles.sourceName : "SCL / CID / SCD / IID" }
                }
                CalmButton { Layout.fillWidth: true; text: "Open SCL / CID"; onClicked: fileDialog.open() }

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
                        onActivated: sclProfiles.selectStream(currentIndex)
                        font.family: root.uiFont
                        font.pixelSize: 11
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 8; rowSpacing: 6
                        Quiet { text: "Class" }
                        Label { text: "CLASS " + (sclProfiles.selectedProfile.compatibilityClass || "—"); color: sclProfiles.selectedProfile.compatibilityClass === "A" ? theme.green : sclProfiles.selectedProfile.compatibilityClass === "B" ? theme.amber : theme.red; font.family: root.uiFont; font.pixelSize: 10; font.weight: Font.Bold }
                        Quiet { text: "Support" }
                        Label { text: sclProfiles.selectedProfile.deviceSupport || "—"; color: theme.textSoft; font.family: root.uiFont; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                        Quiet { text: "APPID" }
                        Label { text: sclProfiles.selectedProfile.appIdHex || "—"; color: theme.textSoft; font.family: root.monoFont; font.pixelSize: 9 }
                        Quiet { text: "Rate" }
                        Label { text: sclProfiles.selectedProfile.publisherRate ? sclProfiles.selectedProfile.publisherRate + "/s" : "—"; color: theme.textSoft; font.family: root.monoFont; font.pixelSize: 9 }
                        Quiet { text: "VLAN" }
                        Label { text: sclProfiles.selectedProfile.vlanPresent ? "P" + sclProfiles.selectedProfile.vlanPriority + " · " + sclProfiles.selectedProfile.vlanId : "untagged"; color: theme.textSoft; font.family: root.monoFont; font.pixelSize: 9 }
                    }
                }

                ColumnLayout {
                    visible: sclProfiles.selectedProfile.compatibilityClass === "B"
                    Layout.fillWidth: true
                    spacing: 7
                    Kicker { text: "COUNTER POLICY" }
                    Quiet { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "Candidate smpCnt modulus requires explicit profile/evidence confirmation before deployment." }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField { id: counterField; Layout.fillWidth: true; text: sclProfiles.selectedProfile.counterModulus || ""; placeholderText: "modulus"; font.family: root.monoFont; validator: IntValidator { bottom: 1; top: 65535 } }
                        CalmButton { text: "Confirm"; onClicked: sclProfiles.confirmCounterModulus(parseInt(counterField.text)) }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: theme.lineSoft }
                Kicker { text: "QUICK SETUP" }
                CalmButton { Layout.fillWidth: true; text: "Balanced 3-phase"; onClicked: root.balanced() }
                CalmButton { Layout.fillWidth: true; text: "Zero output"; onClicked: root.zeroAll() }
                Item { Layout.fillHeight: true }
                Kicker { text: "RUNTIME" }
                Repeater {
                    model: ["Rate", "Missed", "TX fail", "Generation"]
                    RowLayout {
                        Layout.fillWidth: true
                        Quiet { text: modelData }
                        Item { Layout.fillWidth: true }
                        Label { text: "—"; color: theme.textSoft; font.family: root.monoFont; font.pixelSize: 9 }
                    }
                }
            }
        }

        Surface {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 570
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 13
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        spacing: 2
                        Kicker { text: "INJECTION WORKSPACE" }
                        Label { text: "Manual Injection"; color: theme.text; font.family: root.uiFont; font.pixelSize: 23; font.weight: Font.DemiBold }
                    }
                    Item { Layout.fillWidth: true }
                    Quiet { text: "STOPPED" }
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 58
                    radius: 9
                    color: theme.surface2
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14; anchors.rightMargin: 14
                        spacing: 10
                        ColumnLayout {
                            spacing: 0
                            Kicker { text: "FREQUENCY" }
                            RowLayout {
                                TextField { id: frequencyField; text: "50.000"; implicitWidth: 82; selectByMouse: true; color: theme.text; font.family: root.monoFont; font.pixelSize: 16; background: Item {}; onTextEdited: waveform.requestPaint() }
                                Quiet { text: "Hz" }
                            }
                        }
                        CalmButton { text: "50"; onClicked: { frequencyField.text = "50.000"; waveform.requestPaint() } }
                        CalmButton { text: "60"; onClicked: { frequencyField.text = "60.000"; waveform.requestPaint() } }
                        Rectangle { width: 1; height: 28; color: theme.lineSoft }
                        CheckBox { checked: root.phaseLink; text: "3-phase link"; onToggled: root.phaseLink = checked; font.family: root.uiFont; font.pixelSize: 11 }
                        Item { Layout.fillWidth: true }
                        CheckBox { checked: true; enabled: false; text: "Live apply"; font.family: root.uiFont; font.pixelSize: 11 }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 12

                    Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        color: theme.surface2; radius: 9; border.color: theme.lineSoft
                        ColumnLayout {
                            anchors.fill: parent; spacing: 0
                            MatrixHeader { titleText: "Current"; symbolText: "I"; unitText: "A RMS" }
                            MatrixColumns {}
                            Repeater {
                                id: currentRep
                                model: currentModel
                                SignalRow { groupIndex: 0; rowIndex: index; sourceModel: currentModel; sid: signalId; mag: magnitude; angle: phase; isEnabled: enabled; phaseColor: traceColor }
                            }
                            Item { Layout.fillHeight: true }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        color: theme.surface2; radius: 9; border.color: theme.lineSoft
                        ColumnLayout {
                            anchors.fill: parent; spacing: 0
                            MatrixHeader { titleText: "Voltage"; symbolText: "U"; unitText: "V RMS" }
                            MatrixColumns {}
                            Repeater {
                                id: voltageRep
                                model: voltageModel
                                SignalRow { groupIndex: 1; rowIndex: index; sourceModel: voltageModel; sid: signalId; mag: magnitude; angle: phase; isEnabled: enabled; phaseColor: traceColor }
                            }
                            Item { Layout.fillHeight: true }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 7
                    Rectangle { width: 7; height: 7; radius: 4; color: theme.accent }
                    Label { text: activeSignal; color: theme.textSoft; font.family: root.monoFont; font.pixelSize: 10; font.weight: Font.Bold }
                    Quiet { text: activeMagnitude.toFixed(3) + " " + activeUnit + " RMS  ·  " + activePhase.toFixed(2) + "°" }
                    Item { Layout.fillWidth: true }
                    Quiet { text: "↑↓ channel   ←→ field   Enter next" }
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
                        Label { text: "Signal Preview"; color: theme.text; font.family: root.uiFont; font.pixelSize: 19; font.weight: Font.DemiBold }
                    }
                    Item { Layout.fillWidth: true }
                    Quiet { text: "LOCAL" }
                }
                PreviewTitle { titleText: "Phasor"; detailText: activeSignal + " · " + activeMagnitude.toFixed(3) + " " + activeUnit + " ∠ " + activePhase.toFixed(2) + "°" }
                Canvas {
                    id: phasor
                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredHeight: 1
                    onWidthChanged: requestPaint(); onHeightChanged: requestPaint()
                    onPaint: {
                        var ctx=getContext("2d");ctx.reset();var w=width,h=height,cx=w/2,cy=h/2,r=Math.min(w,h)*.39
                        ctx.strokeStyle="#283441";ctx.lineWidth=1
                        for(var ring=1;ring<=3;++ring){ctx.beginPath();ctx.arc(cx,cy,r*ring/3,0,Math.PI*2);ctx.stroke()}
                        for(var deg=0;deg<180;deg+=30){var a=deg*Math.PI/180,dx=Math.cos(a)*r,dy=Math.sin(a)*r;ctx.beginPath();ctx.moveTo(cx-dx,cy-dy);ctx.lineTo(cx+dx,cy+dy);ctx.stroke()}
                        function draw(m,scale,dash){var max=.0001;for(var i=0;i<4;++i)max=Math.max(max,m.get(i).magnitude);for(var j=0;j<4;++j){var s=m.get(j);if(!s.enabled||s.magnitude<=0)continue;var a=s.phase*Math.PI/180,len=r*scale*(.3+.7*Math.min(1,s.magnitude/max)),x=cx+Math.cos(a)*len,y=cy-Math.sin(a)*len,active=s.signalId===root.activeSignal;ctx.save();ctx.globalAlpha=active?1:.70;ctx.strokeStyle=s.traceColor;ctx.fillStyle=s.traceColor;ctx.lineWidth=active?3.2:2;ctx.lineCap="round";ctx.setLineDash(dash?[5,4]:[]);ctx.beginPath();ctx.moveTo(cx,cy);ctx.lineTo(x,y);ctx.stroke();ctx.setLineDash([]);var head=active?8:6;ctx.beginPath();ctx.moveTo(x,y);ctx.lineTo(x-Math.cos(a-.45)*head,y+Math.sin(a-.45)*head);ctx.lineTo(x-Math.cos(a+.45)*head,y+Math.sin(a+.45)*head);ctx.closePath();ctx.fill();if(active){ctx.font="600 11px Inter";ctx.fillText(s.signalId,x+8,y-7)}ctx.restore()}}
                        draw(voltageModel,1,false);draw(currentModel,.78,true)
                    }
                }
                PreviewTitle { titleText: "Waveform"; detailText: frequencyField.text + " Hz · 40 ms" }
                Canvas {
                    id: waveform
                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredHeight: .85
                    onWidthChanged: requestPaint(); onHeightChanged: requestPaint()
                    onPaint: {
                        var ctx=getContext("2d");ctx.reset();var w=width,h=height,l=25,r=8,t=12,b=14,pw=w-l-r,ph=h-t-b,g=16,lane=(ph-g)/2,vy=t+lane/2,iy=t+lane+g+lane/2,f=parseFloat(frequencyField.text)||50
                        ctx.strokeStyle="#202b36";ctx.lineWidth=1;for(var gx=0;gx<=8;++gx){var x=l+pw*gx/8;ctx.beginPath();ctx.moveTo(x,t);ctx.lineTo(x,t+ph);ctx.stroke()}ctx.strokeStyle="#33404d";[vy,iy].forEach(function(y){ctx.beginPath();ctx.moveTo(l,y);ctx.lineTo(l+pw,y);ctx.stroke()})
                        function draw(m,center,dash){var max=.0001;for(var i=0;i<4;++i)max=Math.max(max,m.get(i).magnitude);for(var j=0;j<4;++j){var s=m.get(j);if(!s.enabled||s.magnitude<=0)continue;var active=s.signalId===root.activeSignal,amp=lane*.39*Math.min(1,s.magnitude/max),pr=s.phase*Math.PI/180;ctx.save();ctx.strokeStyle=s.traceColor;ctx.globalAlpha=active?1:.64;ctx.lineWidth=active?2.6:1.7;ctx.lineCap="round";ctx.setLineDash(dash?[5,4]:[]);ctx.beginPath();for(var p=0;p<=220;++p){var q=p/220,time=.04*q,val=Math.sin(2*Math.PI*f*time+pr),x=l+pw*q,y=center-val*amp;if(p===0)ctx.moveTo(x,y);else ctx.lineTo(x,y)}ctx.stroke();ctx.restore()}}
                        draw(voltageModel,vy,false);draw(currentModel,iy,true)
                    }
                }
                Quiet { Layout.fillWidth: true; horizontalAlignment: Text.AlignRight; text: "Generated setpoint · not independent on-wire proof" }
            }
        }
    }

    component MatrixHeader: RowLayout {
        required property string titleText
        required property string symbolText
        required property string unitText
        Layout.fillWidth: true; Layout.preferredHeight: 46
        Layout.leftMargin: 12; Layout.rightMargin: 12
        Rectangle { width: 23; height: 23; radius: 5; color: "#152235"; Label { anchors.centerIn: parent; text: symbolText; color: theme.accent; font.family: root.monoFont; font.pixelSize: 10; font.weight: Font.Bold } }
        Label { text: titleText; color: theme.textSoft; font.family: root.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold }
        Item { Layout.fillWidth: true }
        Quiet { text: unitText }
    }
    component MatrixColumns: RowLayout {
        Layout.fillWidth: true; Layout.preferredHeight: 30
        Layout.leftMargin: 10; Layout.rightMargin: 10; spacing: 8
        Quiet { text: "ON"; Layout.preferredWidth: 28; font.pixelSize: 8 }
        Quiet { text: "CH"; Layout.preferredWidth: 34; font.pixelSize: 8 }
        Quiet { text: "MAGNITUDE"; Layout.fillWidth: true; font.pixelSize: 8 }
        Quiet { text: "PHASE"; Layout.preferredWidth: 108; font.pixelSize: 8 }
    }
    component SignalRow: Rectangle {
        id: row
        required property int groupIndex
        required property int rowIndex
        required property var sourceModel
        required property string sid
        required property real mag
        required property real angle
        required property bool isEnabled
        required property color phaseColor
        property alias magEditor: magField
        property alias phaseEditor: phaseField
        Layout.fillWidth: true; Layout.preferredHeight: 58
        color: (magField.activeFocus || phaseField.activeFocus) ? "#141d27" : "transparent"
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 8
            CheckBox { Layout.preferredWidth: 28; checked: row.isEnabled; onToggled: { row.sourceModel.setProperty(row.rowIndex,"enabled",checked); phasor.requestPaint(); waveform.requestPaint() } }
            RowLayout { Layout.preferredWidth: 34; spacing: 5; Rectangle { width: 6; height: 6; radius: 3; color: row.phaseColor } Label { text: row.sid; color: theme.text; font.family: root.monoFont; font.pixelSize: 11; font.weight: Font.Bold } }
            TextField {
                id: magField
                Layout.fillWidth: true; text: row.mag.toFixed(3); selectByMouse: true; horizontalAlignment: Text.AlignRight
                color: theme.text; font.family: root.monoFont; font.pixelSize: 13; font.weight: Font.DemiBold
                background: Rectangle { radius: 6; color: magField.activeFocus ? "#0c141d" : "#111820"; border.width: magField.activeFocus ? 1 : 0; border.color: theme.accent }
                onActiveFocusChanged: if(activeFocus){root.activeSignal=row.sid;root.activeUnit=row.groupIndex===0?"A":"V";root.activeMagnitude=row.mag;root.activePhase=row.angle;selectAll();phasor.requestPaint();waveform.requestPaint()}
                onEditingFinished: root.editSignal(row.groupIndex,row.rowIndex,"magnitude",parseFloat(text))
                Keys.onPressed: function(e){if([Qt.Key_Up,Qt.Key_Down,Qt.Key_Left,Qt.Key_Right].indexOf(e.key)>=0){root.navigate(row.groupIndex,row.rowIndex,0,e.key);e.accepted=true}else if(e.key===Qt.Key_Return||e.key===Qt.Key_Enter){root.focusCell(row.groupIndex,row.rowIndex+1,0);e.accepted=true}}
            }
            TextField {
                id: phaseField
                Layout.preferredWidth: 108; text: row.angle.toFixed(2); selectByMouse: true; horizontalAlignment: Text.AlignRight
                color: theme.text; font.family: root.monoFont; font.pixelSize: 13; font.weight: Font.DemiBold
                background: Rectangle { radius: 6; color: phaseField.activeFocus ? "#0c141d" : "#111820"; border.width: phaseField.activeFocus ? 1 : 0; border.color: theme.accent }
                onActiveFocusChanged: if(activeFocus){root.activeSignal=row.sid;root.activeUnit=row.groupIndex===0?"A":"V";root.activeMagnitude=row.mag;root.activePhase=row.angle;selectAll();phasor.requestPaint();waveform.requestPaint()}
                onEditingFinished: root.editSignal(row.groupIndex,row.rowIndex,"phase",parseFloat(text))
                Keys.onPressed: function(e){if([Qt.Key_Up,Qt.Key_Down,Qt.Key_Left,Qt.Key_Right].indexOf(e.key)>=0){root.navigate(row.groupIndex,row.rowIndex,1,e.key);e.accepted=true}else if(e.key===Qt.Key_Return||e.key===Qt.Key_Enter){root.focusCell(row.groupIndex,row.rowIndex+1,1);e.accepted=true}}
            }
        }
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: theme.lineSoft }
    }
    component PreviewTitle: RowLayout {
        required property string titleText
        required property string detailText
        Layout.fillWidth: true
        Label { text: titleText; color: theme.textSoft; font.family: root.uiFont; font.pixelSize: 11; font.weight: Font.DemiBold }
        Item { Layout.fillWidth: true }
        Label { text: detailText; color: theme.muted; font.family: root.monoFont; font.pixelSize: 8 }
    }
}
