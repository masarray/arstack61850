// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Menu {
    id: actionMenu
    property var theme
    property string uiFont: "Inter"
    property int commandRows: 4
    property int dividerRows: 1
    popupType: Popup.Item
    width: 212
    height: commandRows * 29 + dividerRows * 7 + 8
    topPadding: 4
    bottomPadding: 4
    leftPadding: 4
    rightPadding: 4
    spacing: 0
    font.family: uiFont
    font.pixelSize: 11
    delegate: MenuItem {
        id: actionItem
        width: actionMenu.width - actionMenu.leftPadding - actionMenu.rightPadding
        implicitHeight: 29
        leftPadding: 8
        rightPadding: 8
        font.family: actionMenu.uiFont
        font.pixelSize: 11
        contentItem: RowLayout {
            spacing: 7
            Image {
                visible: actionItem.icon.source.toString().length > 0
                source: actionItem.icon.source
                sourceSize.width: 24
                sourceSize.height: 24
                Layout.preferredWidth: visible ? 15 : 0
                Layout.preferredHeight: 15
                opacity: actionItem.enabled ? 0.95 : 0.33
            }
            Text {
                text: actionItem.text
                font: actionItem.font
                color: actionItem.enabled ? actionMenu.theme.textSoft : actionMenu.theme.muted2
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
        background: Rectangle {
            radius: 4
            color: actionItem.highlighted ? actionMenu.theme.menuHover : "transparent"
            border.width: actionItem.activeFocus ? 1 : 0
            border.color: actionMenu.theme.accent
        }
    }
    background: Rectangle {
        color: actionMenu.theme.menuSurface
        radius: 7
        border.width: 1
        border.color: actionMenu.theme.line
    }
}
