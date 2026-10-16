// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

Menu {
    id: quick
    property var theme
    property var controller
    property int groupIndex: 0
    property int rowIndex: 0
    property bool phaseMode: false
    width: 238
    padding: 5
    background: Rectangle { radius: 8; color: quick.theme.raised; border.width: 1; border.color: quick.theme.line }
    delegate: MenuItem {
        id: action
        implicitWidth: quick.width - 10
        implicitHeight: 34
        contentItem: Text {
            text: action.text
            color: quick.theme.text
            font.pixelSize: 11
            verticalAlignment: Text.AlignVCenter
            leftPadding: 10
        }
        background: Rectangle { radius: 6; color: action.highlighted ? quick.theme.accentSoft : "transparent" }
    }
    MenuItem {
        text: quick.phaseMode ? "Equal Angle · A B C" : "Equal Value · A B C"
        onTriggered: quick.controller.quickSetGroup(quick.groupIndex, quick.rowIndex,
                                                     quick.phaseMode ? "equal_angle" : "equal_value")
    }
    MenuItem {
        visible: quick.phaseMode
        height: visible ? implicitHeight : 0
        text: "Balance ABC · 120°"
        onTriggered: quick.controller.quickSetGroup(quick.groupIndex, quick.rowIndex, "balance_angle")
    }
}
