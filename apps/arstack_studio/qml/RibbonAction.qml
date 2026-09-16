// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

Button {
    id: control

    property var theme
    property string uiFont: "Inter"
    property string tone: "neutral"
    property url iconSource
    property bool large: false
    property string toolTipText: ""

    implicitWidth: large ? 72 : Math.max(70, label.implicitWidth + 28)
    implicitHeight: large ? 54 : 28
    hoverEnabled: true
    activeFocusOnTab: true
    font.family: uiFont
    font.pixelSize: large ? 10 : 9
    font.weight: Font.DemiBold

    readonly property color fillColor: !enabled ? theme.surface2
        : tone === "success" ? (hovered ? "#1e5c43" : "#194d38")
        : tone === "danger" ? (hovered ? "#523039" : theme.redSoft)
        : tone === "accent" ? (hovered ? "#1d3c5e" : theme.accentSoft)
        : hovered ? theme.raisedHover : "transparent"
    readonly property color edgeColor: !enabled ? theme.lineSoft
        : tone === "success" ? "#347a59"
        : tone === "danger" ? "#86434b"
        : tone === "accent" ? "#315f8d"
        : activeFocus ? theme.accent : "transparent"
    readonly property color textColor: !enabled ? theme.muted2
        : tone === "success" ? "#d6f4e6"
        : tone === "danger" ? "#ffdce0"
        : tone === "accent" ? "#dcebff" : theme.textSoft

    contentItem: Item {
        implicitWidth: control.large
            ? Math.max(label.implicitWidth, control.iconSource.toString().length ? 22 : 0)
            : label.implicitWidth + (control.iconSource.toString().length ? 22 : 0)
        implicitHeight: control.large ? 46 : Math.max(18, label.implicitHeight)

        Image {
            id: icon
            visible: control.iconSource.toString().length > 0
            source: control.iconSource
            width: control.large ? 20 : 14
            height: width
            sourceSize.width: width * 2
            sourceSize.height: height * 2
            opacity: control.enabled ? 0.94 : 0.30
            anchors.horizontalCenter: control.large ? parent.horizontalCenter : undefined
            anchors.left: control.large ? undefined : parent.left
            anchors.top: control.large ? parent.top : undefined
            anchors.verticalCenter: control.large ? undefined : parent.verticalCenter
        }

        Text {
            id: label
            text: control.text
            color: control.textColor
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: control.large ? parent.bottom : undefined
            anchors.verticalCenter: control.large ? undefined : parent.verticalCenter
        }
    }

    background: Rectangle {
        radius: 6
        color: control.fillColor
        border.width: control.edgeColor === "transparent" ? 0 : 1
        border.color: control.edgeColor
        Behavior on color { ColorAnimation { duration: 90 } }
    }

    ToolTip.visible: hovered && toolTipText.length > 0
    ToolTip.text: toolTipText
    ToolTip.delay: 420
}
