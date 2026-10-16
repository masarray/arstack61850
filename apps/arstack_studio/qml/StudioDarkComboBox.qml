// SPDX-License-Identifier: GPL-3.0-or-later
// Consistent Windows dark popup, shared by the PTP and SV simulation selectors.
// Deliberately presentation-only: no additional protocol/session state.
import QtQuick
import QtQuick.Controls

ComboBox {
    id: combo
    property var theme
    property string uiFont: "Inter"

    implicitHeight: 36
    hoverEnabled: true
    font.family: uiFont
    font.pixelSize: 11
    palette.text: theme.text
    palette.buttonText: theme.text
    palette.windowText: theme.text
    palette.base: theme.surface2
    palette.button: theme.raised

    background: Rectangle {
        radius: 7
        color: combo.enabled ? (combo.hovered ? combo.theme.raisedHover : combo.theme.surface2)
                             : combo.theme.surface2
        border.width: 1
        border.color: combo.activeFocus ? combo.theme.accent : combo.theme.line
    }

    contentItem: Text {
        text: combo.displayText
        color: combo.enabled ? combo.theme.text : combo.theme.muted
        font: combo.font
        verticalAlignment: Text.AlignVCenter
        leftPadding: 12
        rightPadding: 28
        elide: Text.ElideRight
    }

    indicator: Text {
        text: "\u25be"
        color: combo.enabled ? combo.theme.textSoft : combo.theme.muted
        font.pixelSize: 14
        anchors.right: parent.right
        anchors.rightMargin: 11
        anchors.verticalCenter: parent.verticalCenter
    }

    delegate: ItemDelegate {
        width: combo.width - 8
        implicitHeight: 36
        highlighted: combo.highlightedIndex === index
        contentItem: Text {
            text: modelData
            color: combo.theme.text
            font: combo.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            leftPadding: 10
            rightPadding: 8
        }
        background: Rectangle {
            radius: 5
            color: parent.highlighted ? combo.theme.accentSoft : combo.theme.raised
        }
    }

    popup: Popup {
        y: combo.height + 3
        width: combo.width
        padding: 4
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 176)
        contentItem: ListView {
            implicitHeight: contentHeight
            clip: true
            model: combo.popup.visible ? combo.delegateModel : null
            currentIndex: combo.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
            color: combo.theme.raised
            radius: 8
            border.width: 1
            border.color: combo.theme.line
        }
    }
}
