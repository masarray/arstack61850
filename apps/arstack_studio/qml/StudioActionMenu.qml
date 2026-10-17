// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Menu {
    id: actionMenu
    property var theme
    property string uiFont: "Inter"
    // The native Menu.title reserves a heading area even when a dark
    // custom menu delegate does not paint it. That produces the oversized
    // blank top area visible in the previous Windows context popup.
    // Keep context for accessibility/diagnostics without a title header.
    property string contextLabel: ""
    title: ""
    property int commandRows: 4
    property int dividerRows: 1
    popupType: Popup.Item
    width: 204
    height: commandRows * 30 + dividerRows * 7 + 8
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
        implicitHeight: 30
        leftPadding: 8
        rightPadding: 8
        font.family: actionMenu.uiFont
        font.pixelSize: 11
        contentItem: RowLayout {
            spacing: 9
            Image {
                visible: actionItem.icon.source.toString().length > 0
                source: actionItem.icon.source
                sourceSize.width: 18
                sourceSize.height: 18
                Layout.preferredWidth: visible ? 16 : 0
                Layout.preferredHeight: 16
                fillMode: Image.PreserveAspectFit
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
            radius: 5
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
