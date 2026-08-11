// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

Switch {
    id: control
    required property var theme
    implicitWidth: 44
    implicitHeight: 24
    padding: 0
    hoverEnabled: true
    contentItem: Item {}
    indicator: Rectangle {
        x: 0
        y: 2
        width: 44
        height: 22
        radius: 11
        color: control.checked ? control.theme.accent : control.theme.surfaceSoft
        border.width: 1
        border.color: control.checked ? control.theme.accentHover : control.theme.line
        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            radius: 8
            color: control.enabled ? control.theme.text : control.theme.muted
            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        }
    }
}
