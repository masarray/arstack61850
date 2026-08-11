// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: editor
    property var theme
    property var controller
    property var device
    property string uiFont: "Inter"
    property string monoFont: "Cascadia Mono"
    property real qualityValue: 0
    property bool compact: false

    spacing: 7

    function hexText(value) {
        return "0x" + (Number(value) >>> 0).toString(16).padStart(8, "0").toUpperCase()
    }

    function validityName(value) {
        switch ((Number(value) >>> 0) & 0x03) {
        case 0: return "GOOD"
        case 1: return "INVALID"
        case 2: return "RESERVED"
        default: return "QUESTIONABLE"
        }
    }

    function flagSummary(value) {
        var q = Number(value) >>> 0
        var parts = []
        if (q & (1 << 2)) parts.push("overflow")
        if (q & (1 << 3)) parts.push("out-of-range")
        if (q & (1 << 4)) parts.push("bad-ref")
        if (q & (1 << 5)) parts.push("oscillatory")
        if (q & (1 << 6)) parts.push("failure")
        if (q & (1 << 7)) parts.push("old-data")
        if (q & (1 << 8)) parts.push("inconsistent")
        if (q & (1 << 9)) parts.push("inaccurate")
        if (q & (1 << 11)) parts.push("test")
        if (q & (1 << 12)) parts.push("operator-blocked")
        return parts.length ? parts.join(" · ") : "no detail flags"
    }

    function presetIndex(value) {
        var q = Number(value) >>> 0
        if (q === 0) return 0
        if (q === 1) return 1
        if (q === 3) return 2
        if (q === (1 << 11)) return 3
        return -1
    }

    function presetValue(index) {
        if (index === 0) return 0
        if (index === 1) return 1
        if (index === 2) return 3
        if (index === 3) return (1 << 11)
        return Number(qualityValue) >>> 0
    }

    function applyValue(value) {
        controller.setActiveQuality(Number(value) >>> 0)
    }

    onQualityValueChanged: {
        if (!rawField.activeFocus)
            rawField.text = hexText(qualityValue)
    }

    Label {
        text: "QUALITY"
        color: editor.theme.muted
        font.family: editor.uiFont
        font.pixelSize: 8
        font.weight: Font.DemiBold
        font.letterSpacing: 0.8
    }

    Rectangle {
        implicitHeight: 22
        implicitWidth: validityText.implicitWidth + 16
        radius: 5
        color: editor.validityName(editor.qualityValue) === "GOOD" ? editor.theme.greenSoft :
               editor.validityName(editor.qualityValue) === "QUESTIONABLE" ? editor.theme.amberSoft : editor.theme.redSoft
        border.width: 1
        border.color: editor.validityName(editor.qualityValue) === "GOOD" ? "#2b674d" :
                      editor.validityName(editor.qualityValue) === "QUESTIONABLE" ? "#705a31" : "#78404a"
        Label {
            id: validityText
            anchors.centerIn: parent
            text: editor.validityName(editor.qualityValue)
            color: editor.validityName(editor.qualityValue) === "GOOD" ? editor.theme.green :
                   editor.validityName(editor.qualityValue) === "QUESTIONABLE" ? editor.theme.amber : editor.theme.red
            font.family: editor.monoFont
            font.pixelSize: 8
            font.weight: Font.Bold
        }
    }

    ComboBox {
        id: presetBox
        implicitWidth: editor.compact ? 104 : 118
        model: ["Good", "Invalid", "Questionable", "Test"]
        currentIndex: editor.presetIndex(editor.qualityValue)
        font.family: editor.uiFont
        font.pixelSize: 9
        onActivated: editor.applyValue(editor.presetValue(currentIndex))
    }

    NumericField {
        id: rawField
        theme: editor.theme
        monoFont: editor.monoFont
        compact: true
        implicitWidth: editor.compact ? 106 : 120
        Component.onCompleted: text = editor.hexText(editor.qualityValue)
        onEditingFinished: {
            var trimmed = text.trim()
            var parsed = trimmed.toLowerCase().startsWith("0x") ?
                parseInt(trimmed.substring(2), 16) : parseInt(trimmed, 10)
            if (!isNaN(parsed) && parsed >= 0 && parsed <= 0xFFFFFFFF) {
                invalidInput = false
                editor.applyValue(parsed)
                text = editor.hexText(parsed)
            } else {
                invalidInput = true
                text = editor.hexText(editor.qualityValue)
                controller.showMessage("Quality must be a valid 32-bit value.", true)
            }
        }
    }

    Label {
        Layout.fillWidth: true
        visible: !editor.compact
        text: editor.flagSummary(editor.qualityValue)
        elide: Text.ElideRight
        color: editor.theme.muted
        font.family: editor.uiFont
        font.pixelSize: 9
    }
}