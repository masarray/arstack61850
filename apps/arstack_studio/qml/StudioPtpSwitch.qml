// SPDX-License-Identifier: GPL-3.0-or-later
// Presentation-only PTP switch; checked is owned by device telemetry at call site.
import QtQuick
import QtQuick.Controls

Switch {
    id: control
    property var theme
    property string uiFont: "Inter"
    text: "PTP"
    hoverEnabled: true
    font.family: uiFont
    font.pixelSize: 11
    spacing: 9
    indicator: Rectangle {
        implicitWidth: 46
        implicitHeight: 24
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: 12
        color: control.checked ? control.theme.greenSoft : control.theme.surface2
        border.width: 1
        border.color: control.checked ? control.theme.green : control.theme.line
        Rectangle {
            width: 16
            height: 16
            radius: 8
            y: 4
            x: control.checked ? parent.width - width - 4 : 4
            color: control.checked ? control.theme.green : control.theme.textSoft
            Behavior on x { NumberAnimation { duration: 120 } }
        }
    }
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? control.theme.text : control.theme.muted
        leftPadding: control.indicator.width + control.spacing
        verticalAlignment: Text.AlignVCenter
    }
}
