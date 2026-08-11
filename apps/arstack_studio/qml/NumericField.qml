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
    hoverEnabled: true
    horizontalAlignment: Text.AlignRight
    verticalAlignment: TextInput.AlignVCenter
    color: invalidInput ? theme.red : theme.text
    selectionColor: theme.accent
    selectedTextColor: theme.bg
    font.family: monoFont
    font.pixelSize: compact ? 10 : 12
    font.weight: Font.DemiBold
    leftPadding: 8
    rightPadding: suffixText.length > 0 ? 29 : 8
    implicitHeight: compact ? 31 : 34

    Label {
        visible: field.suffixText.length > 0
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: field.suffixText
        color: field.activeFocus ? field.theme.textSoft : field.theme.muted
        font.family: field.monoFont
        font.pixelSize: field.compact ? 8 : 9
        font.weight: Font.Medium
        Behavior on color { ColorAnimation { duration: 90 } }
    }

    background: Rectangle {
        radius: 6
        color: field.activeFocus ? "#0b141d" : (field.hovered ? "#121c26" : "#10171f")
        border.width: 1
        border.color: field.invalidInput ? field.theme.red
            : field.activeFocus ? field.theme.accent
            : field.hovered ? "#334252" : field.theme.lineSoft
        Behavior on color { ColorAnimation { duration: 90 } }
        Behavior on border.color { ColorAnimation { duration: 90 } }
    }
}
