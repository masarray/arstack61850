// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SurfacePanel {
    id: preview
    property var currentModel
    property var voltageModel
    property string uiFont: "Inter"
    property string monoFont: "Cascadia Mono"
    property bool compact: false
    property string activeSignal: "Ia"
    property string activeUnit: "A"
    property real activeMagnitude: 1
    property real activePhase: 0
    property real signalFrequency: 50

    function requestPaint() {
        phasor.requestPaint()
        waveform.requestPaint()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: preview.compact ? 11 : 14
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 1
                Label { text: "GENERATED SETPOINT"; color: preview.theme.muted; font.family: preview.uiFont; font.pixelSize: 8; font.weight: Font.DemiBold; font.letterSpacing: 1.0 }
                Label { text: "Signal Preview"; color: preview.theme.text; font.family: preview.uiFont; font.pixelSize: preview.compact ? 15 : 17; font.weight: Font.DemiBold }
            }
            Item { Layout.fillWidth: true }
            Label { text: "LOCAL"; color: preview.theme.muted; font.family: preview.monoFont; font.pixelSize: 8; font.weight: Font.Bold }
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: "Phasor"; color: preview.theme.textSoft; font.family: preview.uiFont; font.pixelSize: 10; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            Label {
                Layout.maximumWidth: parent.width * 0.72
                text: preview.activeSignal + " · " + preview.activeMagnitude.toFixed(3) + " " + preview.activeUnit + " ∠ " + preview.activePhase.toFixed(2) + "°"
                color: preview.theme.muted
                font.family: preview.monoFont
                font.pixelSize: 8
                elide: Text.ElideRight
            }
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

                function drawGroup(model, scale, dashed) {
                    var maxMagnitude = 0.0001
                    for (var i = 0; i < 4; ++i)
                        maxMagnitude = Math.max(maxMagnitude, model.get(i).magnitude)
                    for (var j = 0; j < 4; ++j) {
                        var signal = model.get(j)
                        if (!signal.enabled || signal.magnitude <= 0)
                            continue
                        var radians = signal.phase * Math.PI / 180
                        var length = r * scale * (0.30 + 0.70 * Math.min(1, signal.magnitude / maxMagnitude))
                        var ex = cx + Math.cos(radians) * length
                        var ey = cy - Math.sin(radians) * length
                        var active = signal.signalId === preview.activeSignal
                        ctx.save()
                        ctx.globalAlpha = active ? 1 : 0.68
                        ctx.strokeStyle = signal.traceColor
                        ctx.fillStyle = signal.traceColor
                        ctx.lineWidth = active ? 2.8 : 1.7
                        ctx.lineCap = "round"
                        ctx.setLineDash(dashed ? [5, 4] : [])
                        ctx.beginPath()
                        ctx.moveTo(cx, cy)
                        ctx.lineTo(ex, ey)
                        ctx.stroke()
                        ctx.setLineDash([])
                        var arrow = active ? 8 : 6
                        ctx.beginPath()
                        ctx.moveTo(ex, ey)
                        ctx.lineTo(ex - Math.cos(radians - 0.45) * arrow, ey + Math.sin(radians - 0.45) * arrow)
                        ctx.lineTo(ex - Math.cos(radians + 0.45) * arrow, ey + Math.sin(radians + 0.45) * arrow)
                        ctx.closePath()
                        ctx.fill()
                        if (active) {
                            ctx.font = "600 10px Inter"
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
            Layout.fillWidth: true
            Label { text: "Waveform"; color: preview.theme.textSoft; font.family: preview.uiFont; font.pixelSize: 10; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            Label { text: preview.signalFrequency.toFixed(3) + " Hz · 40 ms"; color: preview.theme.muted; font.family: preview.monoFont; font.pixelSize: 8 }
        }

        Canvas {
            id: waveform
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
                var bottom = 12
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
                ctx.strokeStyle = "#33404d"
                for (var b = 0; b < 2; ++b) {
                    var baseline = b === 0 ? voltageY : currentY
                    ctx.beginPath()
                    ctx.moveTo(left, baseline)
                    ctx.lineTo(left + plotWidth, baseline)
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
                        var active = signal.signalId === preview.activeSignal
                        var amplitude = laneHeight * 0.39 * Math.min(1, signal.magnitude / maxMagnitude)
                        var phaseRadians = signal.phase * Math.PI / 180
                        ctx.save()
                        ctx.strokeStyle = signal.traceColor
                        ctx.globalAlpha = active ? 1 : 0.62
                        ctx.lineWidth = active ? 2.3 : 1.5
                        ctx.lineCap = "round"
                        ctx.setLineDash(dashed ? [5, 4] : [])
                        ctx.beginPath()
                        for (var point = 0; point <= 180; ++point) {
                            var ratio = point / 180
                            var time = 0.04 * ratio
                            var sample = Math.sin(2 * Math.PI * preview.signalFrequency * time + phaseRadians)
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