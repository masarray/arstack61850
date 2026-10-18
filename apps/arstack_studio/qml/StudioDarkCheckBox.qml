// SPDX-License-Identifier: GPL-3.0-or-later
// One dark, legible checkbox shared across all Studio engineering panels.
import QtQuick
import QtQuick.Controls

CheckBox {
    id: control
    property var theme
    property string uiFont: "Inter"
    implicitHeight: 32
    // The same single indicator is painted for normal, hover and focus.
    // Do not overlay a native checkbox glyph on hover.
    display: AbstractButton.IconOnly
    // Keep one fixed indicator geometry at rest, during hover and focus.
    // Native style decorations must not replace this explicit indicator.
    background: Item { implicitWidth: 1; implicitHeight: 1 }
    implicitWidth: Math.max(28, contentItem.implicitWidth + 30)
    leftPadding: 1
    rightPadding: 2
    spacing: 9
    hoverEnabled: true
    font.family: uiFont
    font.pixelSize: 11
    indicator: Rectangle {
        implicitWidth: 20
        implicitHeight: 20
        width: 20
        height: 20
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: 5
        color: control.checked ? control.theme.accent : control.theme.surface2
        border.width: 1
        border.color: control.checked || control.hovered || control.activeFocus
            ? control.theme.accent : control.theme.line
        Text {
            anchors.centerIn: parent
            visible: control.checked
            text: "\u2713"
            color: control.theme.bg
            font.pixelSize: 14
            font.weight: Font.Bold
        }
    }
    contentItem: Text {
        horizontalAlignment: Text.AlignLeft
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        font: control.font
        color: control.enabled ? control.theme.textSoft : control.theme.muted
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
