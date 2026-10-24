// SPDX-License-Identifier: GPL-3.0-or-later
// Consistent Windows dark popup, shared by the PTP and SV simulation selectors.
// Deliberately presentation-only: no additional protocol/session state.
import QtQuick
import QtQuick.Controls

ComboBox {
    id: combo
    property var theme
    // Qt may instantiate this control before its parent assigns theme.
    // Use compact immutable default tokens until the real StudioTheme binds.
    // Avoid QML TypeError / palette flicker during startup, without a worker.
    readonly property var effectiveTheme: theme || ({
        text: "#edf2f7", muted: "#91a1b3", textSoft: "#c2ccd7",
        surface2: "#0e141b", raised: "#151d27",
        raisedHover: "#1a2430", accent: "#69a9ff", line: "#26313d"
    })
    property string uiFont: "Inter"

    implicitHeight: 36
    hoverEnabled: true
    font.family: uiFont
    font.pixelSize: 11
    palette.text: effectiveTheme.text
    palette.buttonText: effectiveTheme.text
    palette.windowText: effectiveTheme.text
    palette.base: effectiveTheme.surface2
    palette.button: effectiveTheme.raised

    background: Rectangle {
        radius: 7
        color: combo.enabled ? (combo.hovered ? combo.effectiveTheme.raisedHover : combo.effectiveTheme.surface2)
                             : combo.effectiveTheme.surface2
        border.width: 1
        border.color: combo.activeFocus ? combo.effectiveTheme.accent : combo.effectiveTheme.line
    }

    contentItem: Text {
        text: combo.displayText
        color: combo.enabled ? combo.effectiveTheme.text : combo.effectiveTheme.muted
        font: combo.font
        verticalAlignment: Text.AlignVCenter
        leftPadding: 12
        rightPadding: 28
        elide: Text.ElideRight
    }

    indicator: Text {
        text: "\u25be"
        color: combo.enabled ? combo.effectiveTheme.textSoft : combo.effectiveTheme.muted
        font.pixelSize: 14
        anchors.right: parent.right
        anchors.rightMargin: 11
        anchors.verticalCenter: parent.verticalCenter
    }

    delegate: ItemDelegate {
        id: choice
        required property int index
        width: combo.width - 8
        implicitHeight: 36
        highlighted: combo.highlightedIndex === index
        hoverEnabled: true
        contentItem: Text {
            text: combo.textAt(choice.index)
            color: choice.highlighted || choice.hovered ? "#142334" : combo.effectiveTheme.text
            font: combo.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            leftPadding: 10
            rightPadding: 8
        }
        background: Rectangle {
            radius: 5
            color: choice.highlighted || choice.hovered ? "#edf5ff" : combo.effectiveTheme.raised
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
            color: combo.effectiveTheme.raised
            radius: 8
            border.width: 1
            border.color: combo.effectiveTheme.line
        }
    }
}
