// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

ToolButton {
    id: control
    property var theme
    property string uiFont: "Inter"
    property url iconSource
    property real iconSize: 18

    implicitWidth: 32
    implicitHeight: 30
    font.family: uiFont
    font.pixelSize: theme.bodySize

    contentItem: Item {
        Image {
            visible: control.iconSource.toString().length > 0
            anchors.centerIn: parent
            width: control.iconSize
            height: control.iconSize
            source: control.iconSource
            sourceSize.width: control.iconSize * 2
            sourceSize.height: control.iconSize * 2
            opacity: control.enabled ? (control.hovered ? 1 : 0.82) : 0.32
        }
        Label {
            visible: control.iconSource.toString().length === 0
            anchors.fill: parent
            text: control.text
            color: control.enabled ? (control.hovered ? "#ffffff" : theme.textSoft) : theme.muted2
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
    background: Rectangle {
        radius: 6
        color: control.down ? theme.accentSoft : (control.hovered ? theme.raisedHover : "transparent")
        border.width: control.activeFocus ? 1 : 0
        border.color: theme.accent
    }
}
