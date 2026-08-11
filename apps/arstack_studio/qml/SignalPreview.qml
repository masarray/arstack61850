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

    function requestPaint() {
        phasor.requestPaint()
        waveform.requestPaint()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: preview.compact ? 11 : 14
        spacing: 8

        RowLayout {
            visible: preview.showHeader
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 1
                Label { text: "GENERATED SETPOINT"; color: preview.theme.muted; font.family: preview.uiFont; font.pixelSize: preview.theme.captionSize; font.weight: Font.DemiBold; font.letterSpacing: 0.9 }
                Label { text: "Signal Preview"; color: preview.theme.text; font.family: preview.uiFont; font.pixelSize: preview.compact ? 15 : 17; font.weight: Font.DemiBold }
            }
            Item { Layout.fillWidth: true }
            Label { text: "LOCAL"; color: preview.theme.muted; font.family: preview.monoFont; font.pixelSize: preview.theme.captionSize; font.weight: Font.Bold }
        }

        RowLayout {
            visible: preview.viewMode !== "waveform"
            Layout.fillWidth: true
            Label { text: "Phasor"; color: preview.theme.textSoft; font.family: preview.uiFont; font.pixelSize: 10; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            Label {
                Layout.maximumWidth: parent.width * 0.72
                text: preview.signalFrequency === 0
                    ? preview.activeSignal + " · " + preview.activeMagnitude.toFixed(3) + " " + preview.activeUnit + " DC"
                    : preview.activeSignal + " · " + preview.activeMagnitude.toFixed(3) + " " + preview.activeUnit + " ∠ " + preview.activePhase.toFixed(2) + "°"
                color: preview.theme.muted
                font.family: preview.monoFont
                font.pixelSize: 8
                elide: Text.ElideRight
            }
        }

        Canvas {
            id: phasor
            visible: preview.viewMode !== "waveform"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 1
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                var cx = width / 2
                var cy = height / 2
                var r = Math.min(width, height) * 0.39

                ctx.strokeStyle = "#283441"
                ctx.lineWidth = 1
                for (var ring = 1; ring <= 3; ++ring) {
                    ctx.beginPath()
                    ctx.arc(cx, cy, r * ring / 3, 0, Math.PI * 2)
                    ctx.stroke()
                }
                for (var degrees = 0; degrees < 180; degrees += 30) {
                    var axis = degrees * Math.PI / 180
                    var dx = Math.cos(axis) * r
                    var dy = Math.sin(axis) * r
                    ctx.beginPath()
                    ctx.moveTo(cx - dx, cy - dy)
                    ctx.lineTo(cx + dx, cy + dy)
                    ctx.stroke()
                }

                ctx.fillStyle = "#8794a3"
                ctx.font = "500 10px '" + preview.uiFont + "'"
                ctx.textBaseline = "middle"
                ctx.textAlign = "left"
                ctx.fillText("0°", cx + r + 7, cy)
                ctx.textAlign = "center"
                ctx.fillText("90°", cx, cy - r - 9)
                ctx.textAlign = "right"
                ctx.fillText("180°", cx - r - 7, cy)
                ctx.textAlign = "center"
                ctx.fillText("270°", cx, cy + r + 10)

                if (preview.signalFrequency === 0) {
                    ctx.fillStyle = "#778493"
                    ctx.font = "500 11px '" + preview.uiFont + "'"
                    ctx.textAlign = "center"
                    ctx.fillText("DC · phasor not applicable", cx, cy + 4)
                    return
                }

                function drawGroup(model, scale, isCurrent) {
                    var maxMagnitude = 0.0001
                    for (var i = 0; i < 4; ++i)
                        maxMagnitude = Math.max(maxMagnitude, Math.abs(model.get(i).magnitude))
                    for (var j = 0; j < 4; ++j) {
                        var signal = model.get(j)
                        if (!signal.enabled || signal.magnitude === 0)
                            continue
                        var radians = signal.phase * Math.PI / 180
                        var length = r * scale * (0.30 + 0.70 * Math.min(1, signal.magnitude / maxMagnitude))
                        var ex = cx + Math.cos(radians) * length
                        var ey = cy - Math.sin(radians) * length
                        var active = signal.signalId === preview.activeSignal
                        ctx.save()
                        ctx.globalAlpha = isCurrent ? (active ? 1 : 0.88) : (active ? 0.82 : 0.52)
                        ctx.strokeStyle = signal.traceColor
                        ctx.fillStyle = signal.traceColor
                        ctx.lineWidth = isCurrent ? (active ? 3.0 : 2.2) : (active ? 2.2 : 1.45)
                        ctx.lineCap = "round"
                        ctx.beginPath()
                        ctx.moveTo(cx, cy)
                        ctx.lineTo(ex, ey)
                        ctx.stroke()
                        var arrow = active ? 8 : 6
                        ctx.beginPath()
                        ctx.moveTo(ex, ey)
                        ctx.lineTo(ex - Math.cos(radians - 0.45) * arrow, ey + Math.sin(radians - 0.45) * arrow)
                        ctx.lineTo(ex - Math.cos(radians + 0.45) * arrow, ey + Math.sin(radians + 0.45) * arrow)
                        ctx.closePath()
                        ctx.fill()
                        if (active) {
                            ctx.font = "600 11px '" + preview.uiFont + "'"
                            ctx.fillText(signal.signalId, ex + 7, ey - 7)
                        }
                        ctx.restore()
                    }
                }

                drawGroup(preview.voltageModel, 1.0, false)
                drawGroup(preview.currentModel, 0.78, true)
            }
        }

        RowLayout {
            visible: preview.viewMode !== "phasor"
            Layout.fillWidth: true
            Label { text: "Waveform"; color: preview.theme.textSoft; font.family: preview.uiFont; font.pixelSize: 10; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            Label { text: preview.signalFrequency === 0 ? "DC · constant" : preview.signalFrequency.toFixed(3) + " Hz · 40 ms" + (preview.ctSaturationEnabled ? " · CT SAT" : ""); color: preview.ctSaturationEnabled ? preview.theme.amber : preview.theme.muted; font.family: preview.monoFont; font.pixelSize: 8 }
        }

        Canvas {
            id: waveform
            visible: preview.viewMode !== "phasor"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 0.84
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                var left = 24
                var right = 8
                var top = 10
                var bottom = 24
                var plotWidth = width - left - right
                var plotHeight = height - top - bottom
                var gap = 14
                var laneHeight = (plotHeight - gap) / 2
                var voltageY = top + laneHeight / 2
                var currentY = top + laneHeight + gap + laneHeight / 2

                ctx.strokeStyle = "#202b36"
                ctx.lineWidth = 1
                for (var grid = 0; grid <= 8; ++grid) {
                    var xg = left + plotWidth * grid / 8
                    ctx.beginPath()
                    ctx.moveTo(xg, top)
                    ctx.lineTo(xg, top + plotHeight)
                    ctx.stroke()
                }
                ctx.fillStyle = "#687585"
                ctx.font = "500 8px '" + preview.uiFont + "'"
                ctx.textAlign = "center"
                ctx.textBaseline = "top"
                for (var tick = 0; tick <= 4; ++tick) {
                    var tickX = left + plotWidth * tick / 4
                    ctx.fillText((tick * 10) + " ms", tickX, top + plotHeight + 6)
                }
                ctx.strokeStyle = "#33404d"
                for (var b = 0; b < 2; ++b) {
                    var baseline = b === 0 ? voltageY : currentY
                    ctx.beginPath()
                    ctx.moveTo(left, baseline)
                    ctx.lineTo(left + plotWidth, baseline)
                    ctx.stroke()
                }

                function drawWaveGroup(model, center, isCurrent) {
                    var maxMagnitude = 0.0001
                    for (var i = 0; i < 4; ++i)
                        maxMagnitude = Math.max(maxMagnitude, Math.abs(model.get(i).magnitude))
                    for (var j = 0; j < 4; ++j) {
                        var signal = model.get(j)
                        if (!signal.enabled || signal.magnitude === 0)
                            continue
                        var active = signal.signalId === preview.activeSignal
                        var amplitude = laneHeight * 0.39 * Math.min(1, Math.abs(signal.magnitude) / maxMagnitude)
                        var phaseRadians = signal.phase * Math.PI / 180
                        ctx.save()
                        ctx.strokeStyle = signal.traceColor
                        ctx.globalAlpha = isCurrent ? (active ? 1 : 0.88) : (active ? 0.82 : 0.52)
                        ctx.lineWidth = active ? 2.6 : 2.0
                        ctx.lineCap = "round"
                        ctx.beginPath()
                        for (var point = 0; point <= 180; ++point) {
                            var ratio = point / 180
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
                            if (point === 0) ctx.moveTo(x, y)
                            else ctx.lineTo(x, y)
                        }
                        ctx.stroke()
                        ctx.restore()
                    }
                }

                drawWaveGroup(preview.voltageModel, voltageY, false)
                drawWaveGroup(preview.currentModel, currentY, true)
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
