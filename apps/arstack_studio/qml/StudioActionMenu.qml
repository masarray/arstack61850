// SPDX-License-Identifier: GPL-3.0-or-later
// Shared action-based Qt Quick desktop menu; no duplicated native popup styling.
import QtQuick
import QtQuick.Controls

Menu {
    id: menu
    property var theme
    property string uiFont: "Inter"
    implicitWidth: 250
    topPadding: 7
    bottomPadding: 7
    leftPadding: 6
    rightPadding: 6
    spacing: 2
    font.family: uiFont
    font.pixelSize: 11
    // Use model Actions so menu handles activation, Escape and keyboard focus.
    delegate: MenuItem {
        id: actionItem
        width: menu.width - menu.leftPadding - menu.rightPadding
        implicitHeight: menu.theme.menuItemHeight
        leftPadding: 12
        rightPadding: 12
        font.family: menu.uiFont
        font.pixelSize: 11
        contentItem: Text {
            text: actionItem.text
            font: actionItem.font
            color: actionItem.enabled ? menu.theme.text : menu.theme.muted
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: menu.theme.controlRadius
            color: actionItem.highlighted ? menu.theme.menuHover : "transparent"
            border.width: actionItem.activeFocus ? 1 : 0
            border.color: menu.theme.accent
        }
    }
    // Render the same separators on Windows rather than native light lines.
    // All commands are Qt Actions so disabled/keyboard behavior remains native.
    background: Rectangle {
        color: menu.theme.menuSurface
        radius: menu.theme.menuRadius
        border.width: 1
        border.color: menu.theme.line
    }
}
