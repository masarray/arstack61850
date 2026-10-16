// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

CheckBox {
    id: control
    property var theme
    property string uiFont: "Inter"
    font.family: uiFont
    font.pixelSize: 11
    implicitHeight: 29
    spacing: 8
    indicator: Rectangle {
        implicitWidth: 17
        implicitHeight: 17
        x: 1
        y: (control.height - height) / 2
        radius: 4
        color: control.checked ? control.theme.accent : control.theme.surface2
        border.width: 1
        border.color: control.checked ? control.theme.accent : control.theme.line
        Text {
            anchors.centerIn: parent
            text: "✓"
            visible: control.checked
            color: control.theme.bg
            font.pixelSize: 12
            font.weight: Font.Bold
        }
    }
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? control.theme.textSoft : control.theme.muted
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
    }
}
