// SPDX-License-Identifier: GPL-3.0-or-later
// Only changes how subsequent A/B/C setpoint edits are linked.
// Actual 4I+4V TX remains owned by Main.qml's verified scenario transaction.
import QtQuick
import QtQuick.Controls

Switch {
    id: control
    property var theme
    property string uiFont: "Inter"
    text: "3-phase link"
    font.family: uiFont
    font.pixelSize: 10
    font.weight: Font.Medium
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    spacing: 8
    Accessible.name: "3-phase link"
    Accessible.description: "Link subsequent changes to phases A, B and C"
    ToolTip.visible: hovered
    ToolTip.delay: 900
    ToolTip.text: checked ? "Editing A, B or C also updates the linked phases"
                          : "Edit phases A, B and C independently"
    indicator: Rectangle {
        implicitWidth: 44
        implicitHeight: 24
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: 12
        color: !control.enabled ? control.theme.surface2
             : control.checked ? control.theme.greenSoft : control.theme.surface2
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? control.theme.green
                    : control.checked ? control.theme.green : control.theme.line
        opacity: control.enabled ? 1.0 : 0.48
        Behavior on color { ColorAnimation { duration: 150 } }
        Rectangle {
            width: 16
            height: 16
            radius: 8
            y: 4
            x: control.checked ? parent.width - width - 4 : 4
            color: control.checked ? control.theme.green : control.theme.textSoft
            Behavior on x { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
            Behavior on color { ColorAnimation { duration: 140 } }
        }
    }
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? control.theme.text : control.theme.muted
        opacity: control.enabled ? 1.0 : 0.65
        leftPadding: control.indicator.width + control.spacing
        verticalAlignment: Text.AlignVCenter
    }
}
