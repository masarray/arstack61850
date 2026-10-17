// SPDX-License-Identifier: GPL-3.0-or-later
// Compact Material-action popup shared by magnitude, angle and frequency.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Menu {
    id: actionMenu
    property var theme
    property string uiFont: "Inter"
    popupType: Popup.Item
    implicitWidth: 232
    topPadding: 5
    bottomPadding: 5
    leftPadding: 5
    rightPadding: 5
    spacing: 0
    font.family: uiFont
    font.pixelSize: 11
    delegate: MenuItem {
        id: actionItem
        width: actionMenu.width - actionMenu.leftPadding - actionMenu.rightPadding
        implicitHeight: actionMenu.theme.menuItemHeight
        leftPadding: 9
        rightPadding: 10
        font.family: actionMenu.uiFont
        font.pixelSize: 11
        contentItem: RowLayout {
            spacing: 9
            Image {
                visible: actionItem.icon.source.toString().length > 0
                source: actionItem.icon.source
                sourceSize.width: 28
                sourceSize.height: 28
                Layout.preferredWidth: visible ? 17 : 0
                Layout.preferredHeight: 17
                opacity: actionItem.enabled ? 0.95 : 0.35
            }
            Text {
                text: actionItem.text
                font: actionItem.font
                color: !actionItem.enabled ? actionMenu.theme.muted2
                     : actionItem.highlighted ? actionMenu.theme.text : actionMenu.theme.textSoft
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
        background: Rectangle {
            radius: actionMenu.theme.controlRadius
            color: actionItem.highlighted ? actionMenu.theme.menuHover : "transparent"
            border.width: actionItem.activeFocus ? 1 : 0
            border.color: actionMenu.theme.accent
        }
    }
    background: Rectangle {
        color: actionMenu.theme.menuSurface
        radius: actionMenu.theme.menuRadius
        border.width: 1
        border.color: actionMenu.theme.line
    }
}
