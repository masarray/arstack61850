// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

ComboBox {
    id: control
    property var theme
    property string uiFont: "Inter"
    implicitWidth: 180
    implicitHeight: 35
    font.family: uiFont
    font.pixelSize: 11
    hoverEnabled: true
    contentItem: Text {
        text: control.displayText
        color: control.enabled ? control.theme.text : control.theme.muted
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        leftPadding: 11
        rightPadding: 30
    }
    indicator: Text {
        text: "⌄"
        color: control.theme.textSoft
        font.pixelSize: 18
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: 12
    }
    background: Rectangle {
        radius: 7
        color: control.hovered ? control.theme.raisedHover : control.theme.surface2
        border.width: 1
        border.color: control.activeFocus ? control.theme.accent : control.theme.line
    }
    delegate: ItemDelegate {
        id: item
        required property int index
        required property var modelData
        width: control.width
        height: 35
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: String(item.modelData)
            font: control.font
            color: control.theme.text
            leftPadding: 9
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 5
            color: item.highlighted ? control.theme.accentSoft
                  : item.hovered ? control.theme.raisedHover : "transparent"
        }
    }
    popup: Popup {
        y: control.height + 3
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 10, 220)
        padding: 5
        contentItem: ListView {
            implicitHeight: contentHeight
            clip: true
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
            radius: 8
            color: control.theme.raised
            border.width: 1
            border.color: control.theme.line
        }
    }
}
