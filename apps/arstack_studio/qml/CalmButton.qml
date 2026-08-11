// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

Button {
    id: control
    property var theme
    property string uiFont: "Inter"
    property string tone: "neutral"
    property url iconSource
    property real iconSize: 16
    property string toolTipText: ""

    implicitHeight: theme ? theme.controlHeight : 36
    implicitWidth: Math.max(64, contentItem.implicitWidth + 20)
    font.family: uiFont
    font.pixelSize: theme ? theme.labelSize : 11
    font.weight: Font.DemiBold
    activeFocusOnTab: true
    hoverEnabled: true
    scale: down ? 0.988 : 1.0
    Behavior on scale { NumberAnimation { duration: 70; easing.type: Easing.OutCubic } }

    readonly property color activeFill: !enabled ? "#10161d" :
                                        tone === "success" ? (hovered ? "#1e5c43" : "#194d38") :
                                        tone === "danger" ? (hovered ? "#523039" : theme.redSoft) :
                                        tone === "accent" ? (hovered ? "#1d3c5e" : theme.accentSoft) :
                                        (down ? "#1a2530" : hovered ? theme.raisedHover : "#141c25")
    readonly property color activeEdge: !enabled ? theme.lineSoft :
                                        tone === "success" ? "#347a59" :
                                        tone === "danger" ? "#86434b" :
                                        tone === "accent" ? "#315f8d" :
                                        (activeFocus ? theme.accent : hovered ? "#3a4a5b" : theme.line)
    readonly property color activeText: !enabled ? theme.muted2 :
                                        tone === "success" ? "#d6f4e6" :
                                        tone === "danger" ? "#ffdce0" :
                                        tone === "accent" ? "#dcebff" : theme.textSoft

    contentItem: Row {
        spacing: control.iconSource.toString().length > 0 ? 7 : 0
        anchors.centerIn: parent
        Image {
            visible: control.iconSource.toString().length > 0
            width: control.iconSize
            height: control.iconSize
            anchors.verticalCenter: parent.verticalCenter
            source: control.iconSource
            sourceSize.width: control.iconSize * 2
            sourceSize.height: control.iconSize * 2
            opacity: control.enabled ? 0.94 : 0.30
        }
        Text {
            text: control.text
            color: control.activeText
            font: control.font
            anchors.verticalCenter: parent.verticalCenter
            Behavior on color { ColorAnimation { duration: 90 } }
        }
    }

    ToolTip.visible: hovered && toolTipText.length > 0
    ToolTip.text: toolTipText
    ToolTip.delay: 450

    background: Rectangle {
        radius: control.theme ? control.theme.controlRadius : 7
        color: control.activeFill
        border.width: 1
        border.color: control.activeEdge
        Behavior on color { ColorAnimation { duration: 90 } }
        Behavior on border.color { ColorAnimation { duration: 90 } }
    }
}
