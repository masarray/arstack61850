// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

TextField {
    id: field
    property var theme
    property string monoFont: "Inter"
    property bool invalidInput: false
    property bool compact: false
    property string suffixText: ""

    selectByMouse: true
    horizontalAlignment: Text.AlignRight
    verticalAlignment: TextInput.AlignVCenter
    color: invalidInput ? theme.red : theme.text
    selectionColor: theme.accent
    selectedTextColor: theme.bg
    font.family: monoFont
    font.pixelSize: compact ? 10 : 12
    font.weight: Font.DemiBold
    leftPadding: 8
    rightPadding: suffixText.length > 0 ? 28 : 8
    implicitHeight: compact ? 31 : 34

    Label {
        visible: field.suffixText.length > 0
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: field.suffixText
        color: field.theme.muted
        font.family: field.monoFont
        font.pixelSize: field.compact ? 8 : 9
    }

    background: Rectangle {
        radius: 6
        color: field.activeFocus ? "#0c141d" : "#111820"
        border.width: field.activeFocus || field.invalidInput ? 1 : 0
        border.color: field.invalidInput ? theme.red : theme.accent
        Behavior on color { ColorAnimation { duration: 80 } }
    }
}
