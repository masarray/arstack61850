// SPDX-License-Identifier: GPL-3.0-or-later
// One dark, legible checkbox shared across all Studio engineering panels.
import QtQuick
import QtQuick.Controls

CheckBox {
    id: control
    property var theme
    property string uiFont: "Inter"
    implicitHeight: 30
    spacing: 9
    font.family: uiFont
    font.pixelSize: 11
    indicator: Rectangle {
        implicitWidth: 18
        implicitHeight: 18
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: 5
        color: control.checked ? control.theme.accent : control.theme.surface2
        border.width: 1
        border.color: control.checked ? control.theme.accent
                                      : control.activeFocus ? control.theme.textSoft : control.theme.line
        Text {
            anchors.centerIn: parent
            visible: control.checked
            text: "\u2713"
            color: control.theme.bg
            font.pixelSize: 13
            font.weight: Font.Bold
        }
    }
    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        font: control.font
        color: control.enabled ? control.theme.textSoft : control.theme.muted
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
