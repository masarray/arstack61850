// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

Button {
    id: control
    property var theme
    property bool primary: false
    property bool danger: false
    property url iconSource: ""

    implicitHeight: theme.controlHeight
    implicitWidth: Math.max(88, contentRow.implicitWidth + 30)
    leftPadding: 14
    rightPadding: 14
    hoverEnabled: true

    contentItem: Row {
        id: contentRow
        spacing: 8
        anchors.centerIn: parent
        Image {
            visible: control.iconSource.toString().length > 0
            width: 15
            height: 15
            source: control.iconSource
            sourceSize: Qt.size(15, 15)
            opacity: control.enabled ? 0.95 : 0.38
        }
        Label {
            text: control.text
            color: control.enabled ? theme.text : theme.muted
            font.family: control.font.family
            font.pixelSize: theme.labelSize
            font.weight: Font.DemiBold
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    background: Rectangle {
        radius: theme.controlRadius
        border.width: 1
        border.color: {
            if (!control.enabled) return theme.lineSoft
            if (control.primary) return control.hovered ? theme.accentHover : theme.accent
            if (control.danger) return theme.red
            return control.hovered ? theme.muted : theme.line
        }
        color: {
            if (!control.enabled) return theme.chrome
            if (control.primary) return control.pressed ? "#286fca" : (control.hovered ? theme.accentHover : theme.accent)
            if (control.danger) return control.hovered ? theme.redSoft : theme.surfaceRaised
            return control.pressed ? theme.surfaceSoft : (control.hovered ? theme.surfaceRaised : theme.surface)
        }
    }
}
