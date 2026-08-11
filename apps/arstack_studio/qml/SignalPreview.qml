// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SurfacePanel {
    id: preview
    property var currentModel
    property var voltageModel
    property string uiFont: "Inter"
    property string monoFont: "Inter"
    property bool compact: false
    property string activeSignal: "Ia"
    property string activeUnit: "A"
    property real activeMagnitude: 1
    property real activePhase: 0
    property real signalFrequency: 50
    property bool ctSaturationEnabled: false
    property real ctDcOffsetPercent: 30
    property real ctHarmonicPercent: 28
    property int ctHarmonicOrder: 2
    property real ctClipPercent: 60
    property bool showHeader: true
    property string viewMode: "both" // both, phasor, waveform

    color: preview.showHeader ? preview.theme.surface : "transparent"
    border.width: preview.showHeader ? 1 : 0
    radius: preview.showHeader ? preview.theme.panelRadius : 0

    function phaseColor(index) {
        if (!preview.theme)
            return ["#ff5b70", "#ffd24a", "#49a9ff", "#aa8cff"][index]
        return [preview.theme.phaseA, preview.theme.phaseB,
                preview.theme.phaseC, preview.theme.phaseN][index]
    }

    function requestPaint() {
        currentPhasorCanvas.requestPaint()
        voltagePhasorCanvas.requestPaint()
        waveformCanvas.requestPaint()
    }

    function paintPhasor(canvas, model) {
        var ctx = canvas.getContext("2d")
        ctx.reset()

        var cx = canvas.width / 2
        var cy = canvas.height / 2 + 1
        var r = Math.max(28, Math.min(canvas.width * 0.36, canvas.height * 0.35))
        var gridColor = preview.theme ? preview.theme.plotGrid : "#22303d"
        var strongGridColor = preview.theme ? preview.theme.plotGridStrong : "#334353"

        ctx.strokeStyle = gridColor
        ctx.lineWidth = 1
        for (var ring = 1; ring <= 4; ++ring) {
            ctx.beginPath()
            ctx.arc(cx, cy, r * ring / 4, 0, Math.PI * 2)
            ctx.stroke()
        }

        for (var degrees = 0; degrees < 180; degrees += 30) {
            var axis = degrees * Math.PI / 180
            var dx = Math.cos(axis) * r
            var dy = Math.sin(axis) * r
            ctx.strokeStyle = degrees % 90 === 0 ? strongGridColor : gridColor
            ctx.beginPath()
            ctx.moveTo(cx - dx, cy - dy)
            ctx.lineTo(cx + dx, cy + dy)
            ctx.stroke()
        }

        ctx.fillStyle = preview.theme ? preview.theme.muted : "#778493"
        ctx.font = "500 9px '" + preview.uiFont + "'"
        ctx.textBaseline = "middle"
        ctx.textAlign = "left"
        ctx.fillText("0°", cx + r + 6, cy)
        ctx.textAlign = "center"
        ctx.fillText("90°", cx, cy - r - 9)
        ctx.textAlign = "right"
        ctx.fillText("180°", cx - r - 6, cy)
        ctx.textAlign = "center"
        ctx.fillText("270°", cx, cy + r + 10)

        ctx.fillStyle = strongGridColor
        ctx.beginPath()
        ctx.arc(cx, cy, 2.2, 0, Math.PI * 2)
        ctx.fill()

        if (preview.signalFrequency === 0) {
            ctx.fillStyle = preview.theme ? preview.theme.muted : "#778493"
            ctx.font = "500 10px '" + preview.uiFont + "'"
            ctx.textAlign = "center"
            ctx.fillText("DC · phasor not applicable", cx, cy + 4)
            return
        }

        var maxMagnitude = 0.0001
        for (var i = 0; i < 4; ++i)
            maxMagnitude = Math.max(maxMagnitude, Math.abs(model.get(i).magnitude))

        for (var j = 0; j < 4; ++j) {
            var signal = model.get(j)
            if (!signal.enabled || Math.abs(signal.magnitude) < 0.0000001)
                continue

            var radians = signal.phase * Math.PI / 180
            var relativeMagnitude = Math.min(1, Math.abs(signal.magnitude) / maxMagnitude)
            var length = r * (0.22 + 0.78 * relativeMagnitude)
            var ex = cx + Math.cos(radians) * length
            var ey = cy - Math.sin(radians) * length
            var color = preview.phaseColor(j)
            var active = signal.signalId === preview.activeSignal

            // Selection is communicated by a soft halo. The electrical trace
            // itself keeps exactly the same width in current and voltage views.
            if (active) {
                ctx.save()
                ctx.globalAlpha = 0.18
                ctx.strokeStyle = color
                ctx.lineWidth = 6.2
                ctx.lineCap = "round"
                ctx.beginPath()
                ctx.moveTo(cx, cy)
                ctx.lineTo(ex, ey)
                ctx.stroke()
                ctx.restore()
            }

            ctx.save()
            ctx.globalAlpha = signal.enabled ? 0.98 : 0.30
            ctx.strokeStyle = color
            ctx.fillStyle = color
            ctx.lineWidth = 2.45
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            ctx.beginPath()
            ctx.moveTo(cx, cy)
            ctx.lineTo(ex, ey)
            ctx.stroke()

            var arrow = 7
            ctx.beginPath()
            ctx.moveTo(ex, ey)
            ctx.lineTo(ex - Math.cos(radians - 0.45) * arrow,
                       ey + Math.sin(radians - 0.45) * arrow)
            ctx.lineTo(ex - Math.cos(radians + 0.45) * arrow,
                       ey + Math.sin(radians + 0.45) * arrow)
            ctx.closePath()
            ctx.fill()

            ctx.font = (active ? "700 10px '" : "600 9px '") + preview.uiFont + "'"
            ctx.textBaseline = "middle"
            if (Math.cos(radians) > 0.25) {
                ctx.textAlign = "left"
                ctx.fillText(signal.signalId, ex + 8, ey)
            } else if (Math.cos(radians) < -0.25) {
                ctx.textAlign = "right"
                ctx.fillText(signal.signalId, ex - 8, ey)
            } else {
                ctx.textAlign = "center"
                ctx.fillText(signal.signalId, ex, ey + (Math.sin(radians) > 0 ? -10 : 10))
            }
            ctx.restore()
        }
    }

    function paintWaveform(canvas) {
        var ctx = canvas.getContext("2d")
        ctx.reset()

        var left = preview.compact ? 42 : 48
        var right = 10
        var top = 12
        var bottom = 23
        var plotWidth = canvas.width - left - right
        var plotHeight = canvas.height - top - bottom
        var gap = preview.compact ? 15 : 18
        var laneHeight = (plotHeight - gap) / 2
        var currentY = top + laneHeight / 2
        var voltageY = top + laneHeight + gap + laneHeight / 2
        var gridColor = preview.theme ? preview.theme.plotGrid : "#22303d"
        var strongGridColor = preview.theme ? preview.theme.plotGridStrong : "#334353"

        // Very subtle lane separation preserves density without introducing
        // different trace styling for current and voltage.
        ctx.fillStyle = "#0c141c"
        ctx.fillRect(left, top, plotWidth, laneHeight)
        ctx.fillRect(left, top + laneHeight + gap, plotWidth, laneHeight)

        ctx.strokeStyle = gridColor
        ctx.lineWidth = 1
        for (var grid = 0; grid <= 8; ++grid) {
            var xg = left + plotWidth * grid / 8
            ctx.beginPath()
            ctx.moveTo(xg, top)
            ctx.lineTo(xg, top + plotHeight)
            ctx.stroke()
        }

        ctx.strokeStyle = strongGridColor
        for (var lane = 0; lane < 2; ++lane) {
            var baseline = lane === 0 ? currentY : voltageY
            ctx.beginPath()
            ctx.moveTo(left, baseline)
            ctx.lineTo(left + plotWidth, baseline)
            ctx.stroke()
        }

        ctx.fillStyle = preview.theme ? preview.theme.muted : "#778493"
        ctx.font = "600 8px '" + preview.uiFont + "'"
        ctx.textAlign = "right"
        ctx.textBaseline = "middle"
        ctx.fillText("CURRENT", left - 7, top + 11)
        ctx.fillText("A", left - 7, top + 23)
        ctx.fillText("VOLTAGE", left - 7, top + laneHeight + gap + 11)
        ctx.fillText("V", left - 7, top + laneHeight + gap + 23)

        ctx.font = "500 8px '" + preview.uiFont + "'"
        ctx.textAlign = "center"
        ctx.textBaseline = "top"
        for (var tick = 0; tick <= 4; ++tick) {
            var tickX = left + plotWidth * tick / 4
            ctx.fillText((tick * 10) + " ms", tickX, top + plotHeight + 6)
        }

        function drawWaveGroup(model, center, isCurrent) {
            var maxMagnitude = 0.0001
            for (var i = 0; i < 4; ++i)
                maxMagnitude = Math.max(maxMagnitude, Math.abs(model.get(i).magnitude))

            for (var j = 0; j < 4; ++j) {
                var signal = model.get(j)
                if (!signal.enabled || Math.abs(signal.magnitude) < 0.0000001)
                    continue

                var amplitude = laneHeight * 0.38 * Math.min(1, Math.abs(signal.magnitude) / maxMagnitude)
                var phaseRadians = signal.phase * Math.PI / 180
                var color = preview.phaseColor(j)
                var active = signal.signalId === preview.activeSignal

                function tracePath(lineWidth, alpha) {
                    ctx.save()
                    ctx.strokeStyle = color
                    ctx.globalAlpha = alpha
                    ctx.lineWidth = lineWidth
                    ctx.lineCap = "round"
                    ctx.lineJoin = "round"
                    ctx.beginPath()
                    for (var point = 0; point <= 220; ++point) {
                        var ratio = point / 220
                        var time = 0.04 * ratio
                        var sample = preview.signalFrequency === 0
                            ? (signal.magnitude < 0 ? -1.0 : 1.0)
                            : Math.sin(2 * Math.PI * preview.signalFrequency * time + phaseRadians)

                        if (isCurrent && preview.ctSaturationEnabled && preview.signalFrequency > 0) {
                            var electricalAngle = 2 * Math.PI * preview.signalFrequency * time + phaseRadians
                            sample += (preview.ctHarmonicPercent / 100.0) *
                                      Math.sin(electricalAngle * preview.ctHarmonicOrder)
                            sample += preview.ctDcOffsetPercent / 100.0
                            var clip = Math.abs(preview.ctClipPercent / 100.0)
                            sample = Math.max(-clip, Math.min(clip, sample))
                        }

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

                if (active)
                    tracePath(5.5, 0.14)

                // Same core width, opacity and phase color for both lanes.
                tracePath(2.15, 0.98)
            }
        }

        drawWaveGroup(preview.currentModel, currentY, true)
        drawWaveGroup(preview.voltageModel, voltageY, false)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: preview.showHeader ? 12 : 8
        spacing: preview.showHeader ? 8 : 6

        RowLayout {
            visible: preview.showHeader
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 1
                Label {
                    text: "GENERATED SETPOINT"
                    color: preview.theme.muted
                    font.family: preview.uiFont
                    font.pixelSize: preview.theme.captionSize - 1
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.9
                }
                Label {
                    text: preview.viewMode === "phasor" ? "Phasor View"
                        : preview.viewMode === "waveform" ? "Waveform View" : "Signal Preview"
                    color: preview.theme.text
                    font.family: preview.uiFont
                    font.pixelSize: preview.compact ? 15 : 17
                    font.weight: Font.DemiBold
                }
            }
            Item { Layout.fillWidth: true }
            Label {
                text: preview.signalFrequency === 0 ? "DC" : preview.signalFrequency.toFixed(3) + " Hz"
                color: preview.theme.muted
                font.family: preview.monoFont
                font.pixelSize: preview.theme.captionSize - 1
                font.weight: Font.DemiBold
            }
        }

        RowLayout {
            visible: preview.viewMode !== "waveform"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: preview.viewMode === "both" ? 180 : 120
            spacing: preview.compact ? 7 : 9

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 150
                radius: 7
                color: preview.theme.plotSurface
                border.width: 1
                border.color: currentPhasorHover.hovered ? "#35506b" : preview.theme.lineSoft
                Behavior on border.color { ColorAnimation { duration: 100 } }

                HoverHandler { id: currentPhasorHover }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 3

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Rectangle { width: 3; height: 13; radius: 2; color: preview.theme.accent }
                        Label {
                            text: "CURRENT PHASOR"
                            color: preview.theme.textSoft
                            font.family: preview.uiFont
                            font.pixelSize: 9
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0.5
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: preview.signalFrequency === 0 ? "A DC" : "A RMS"
                            color: preview.theme.muted
                            font.family: preview.uiFont
                            font.pixelSize: 8
                        }
                    }

                    Canvas {
                        id: currentPhasorCanvas
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 100
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onPaint: preview.paintPhasor(currentPhasorCanvas, preview.currentModel)
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 150
                radius: 7
                color: preview.theme.plotSurface
                border.width: 1
                border.color: voltagePhasorHover.hovered ? "#35506b" : preview.theme.lineSoft
                Behavior on border.color { ColorAnimation { duration: 100 } }

                HoverHandler { id: voltagePhasorHover }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 3

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Rectangle { width: 3; height: 13; radius: 2; color: preview.theme.accent }
                        Label {
                            text: "VOLTAGE PHASOR"
                            color: preview.theme.textSoft
                            font.family: preview.uiFont
                            font.pixelSize: 9
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0.5
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: preview.signalFrequency === 0 ? "V DC" : "V RMS"
                            color: preview.theme.muted
                            font.family: preview.uiFont
                            font.pixelSize: 8
                        }
                    }

                    Canvas {
                        id: voltagePhasorCanvas
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 100
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onPaint: preview.paintPhasor(voltagePhasorCanvas, preview.voltageModel)
                    }
                }
            }
        }

        Rectangle {
            visible: preview.viewMode !== "phasor"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: preview.viewMode === "both" ? 180 : 130
            radius: 7
            color: preview.theme.plotSurface
            border.width: 1
            border.color: waveformHover.hovered ? "#35506b" : preview.theme.lineSoft
            Behavior on border.color { ColorAnimation { duration: 100 } }

            HoverHandler { id: waveformHover }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 5

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Label {
                        text: "PHASE"
                        color: preview.theme.muted
                        font.family: preview.uiFont
                        font.pixelSize: 8
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.6
                    }

                    RowLayout {
                        spacing: 4
                        Rectangle { width: 15; height: 2; radius: 1; color: preview.theme.phaseA }
                        Label { text: "A"; color: preview.theme.textSoft; font.family: preview.uiFont; font.pixelSize: 8; font.weight: Font.DemiBold }
                    }
                    RowLayout {
                        spacing: 4
                        Rectangle { width: 15; height: 2; radius: 1; color: preview.theme.phaseB }
                        Label { text: "B"; color: preview.theme.textSoft; font.family: preview.uiFont; font.pixelSize: 8; font.weight: Font.DemiBold }
                    }
                    RowLayout {
                        spacing: 4
                        Rectangle { width: 15; height: 2; radius: 1; color: preview.theme.phaseC }
                        Label { text: "C"; color: preview.theme.textSoft; font.family: preview.uiFont; font.pixelSize: 8; font.weight: Font.DemiBold }
                    }
                    RowLayout {
                        spacing: 4
                        Rectangle { width: 15; height: 2; radius: 1; color: preview.theme.phaseN }
                        Label { text: "N"; color: preview.theme.textSoft; font.family: preview.uiFont; font.pixelSize: 8; font.weight: Font.DemiBold }
                    }

                    Item { Layout.fillWidth: true }

                    Label {
                        text: preview.signalFrequency === 0
                            ? "DC · CONSTANT"
                            : "40 ms" + (preview.ctSaturationEnabled ? " · CT SAT" : "")
                        color: preview.ctSaturationEnabled ? preview.theme.amber : preview.theme.muted
                        font.family: preview.uiFont
                        font.pixelSize: 8
                        font.weight: Font.DemiBold
                    }
                }

                Canvas {
                    id: waveformCanvas
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 100
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    onPaint: preview.paintWaveform(waveformCanvas)
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: "Generated setpoint · not independent on-wire proof"
            horizontalAlignment: Text.AlignRight
            color: preview.theme.muted
            font.family: preview.uiFont
            font.pixelSize: 8
        }
    }
}
