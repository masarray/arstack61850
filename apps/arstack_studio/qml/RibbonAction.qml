// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: control

    property var theme
    property string uiFont: "Inter"
    property string tone: "neutral"
    property url iconSource
    property bool large: false
    property bool iconOnly: false
    property string toolTipText: ""

    implicitWidth: iconOnly ? 36 : large ? Math.max(76, label.implicitWidth + 42) : Math.max(58, label.implicitWidth + (icon.visible ? 36 : 22))
    implicitHeight: large ? 40 : 34
    hoverEnabled: true
    activeFocusOnTab: true
    font.family: uiFont
    font.pixelSize: large ? 11 : 10
    font.weight: Font.DemiBold

    readonly property color fillColor: !enabled ? "transparent"
        : tone === "success" ? (hovered ? "#1b4b39" : "#163c2f")
        : tone === "danger" ? (hovered ? "#4a2830" : "#392127")
        : tone === "accent" ? (hovered ? "#173654" : "#142b42")
        : tone === "warning" ? (hovered ? "#46391f" : "#362d1c")
        : checked ? theme.accentSoft
        : hovered ? theme.raisedHover : "transparent"
    readonly property color edgeColor: !enabled ? "transparent"
        : tone === "success" ? "#2e7254"
        : tone === "danger" ? "#7a3d47"
        : tone === "accent" ? "#315f8d"
        : tone === "warning" ? "#7b6231"
        : checked ? "#315f8d"
        : activeFocus ? theme.accent : "transparent"
    readonly property color textColor: !enabled ? theme.muted2
        : tone === "success" ? "#d9f7e9"
        : tone === "danger" ? "#ffdce1"
        : tone === "accent" ? "#ddebff"
        : tone === "warning" ? "#f2d79c"
        : checked ? theme.text : theme.textSoft

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight

        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: control.iconOnly ? 0 : 7

            Image {
                id: icon
                visible: control.iconSource.toString().length > 0
                source: control.iconSource
                Layout.preferredWidth: control.large ? 17 : 15
                Layout.preferredHeight: Layout.preferredWidth
                sourceSize.width: Layout.preferredWidth * 2
                sourceSize.height: Layout.preferredHeight * 2
                opacity: control.enabled ? 0.96 : 0.30
            }

            Text {
                id: label
                visible: !control.iconOnly
                text: control.text
                color: control.textColor
                font: control.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
    }

    background: Rectangle {
        radius: 7
        color: control.fillColor
        border.width: control.edgeColor === "transparent" ? 0 : 1
        border.color: control.edgeColor
        Behavior on color { ColorAnimation { duration: 90 } }
        Behavior on border.color { ColorAnimation { duration: 90 } }
    }

    ToolTip.visible: hovered && toolTipText.length > 0
    ToolTip.text: toolTipText
    ToolTip.delay: 420
}
