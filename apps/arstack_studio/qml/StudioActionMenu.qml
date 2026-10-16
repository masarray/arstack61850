// SPDX-License-Identifier: GPL-3.0-or-later
// Shared action-based Qt Quick desktop menu; no duplicated native popup styling.
import QtQuick
import QtQuick.Controls

Menu {
    id: actionMenu
    property var theme
    property string uiFont: "Inter"
    // Qt 6.8+ can prefer native Windows menus, which ignore custom QML
    // delegates and render in OS colors. Force our portable styled overlay.
    popupType: Popup.Item
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
        width: actionMenu.width - actionMenu.leftPadding - actionMenu.rightPadding
        implicitHeight: actionMenu.theme.menuItemHeight
        leftPadding: 12
        rightPadding: 12
        font.family: actionMenu.uiFont
        font.pixelSize: 11
        contentItem: Text {
            text: actionItem.text
            font: actionItem.font
            color: actionItem.enabled ? actionMenu.theme.text : actionMenu.theme.muted
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: actionMenu.theme.controlRadius
            color: actionItem.highlighted ? actionMenu.theme.menuHover : "transparent"
            border.width: actionItem.activeFocus ? 1 : 0
            border.color: actionMenu.theme.accent
        }
    }
    // Render the same separators on Windows rather than native light lines.
    // All commands are Qt Actions so disabled/keyboard behavior remains native.
    background: Rectangle {
        color: actionMenu.theme.menuSurface
        radius: actionMenu.theme.menuRadius
        border.width: 1
        border.color: actionMenu.theme.line
    }
}
