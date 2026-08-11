// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

Button {
    id: control
    property var theme
    property string uiFont: "Inter"
    property string tone: "neutral"

    implicitHeight: theme ? theme.controlHeight : 36
    implicitWidth: Math.max(70, contentItem.implicitWidth + 24)
    font.family: uiFont
    font.pixelSize: theme ? theme.labelSize : 11
    font.weight: Font.DemiBold
    activeFocusOnTab: true
    scale: down ? 0.985 : 1.0
    Behavior on scale { NumberAnimation { duration: 70 } }

    readonly property color activeFill: !enabled ? "#121820" :
                                        tone === "success" ? "#194d38" :
                                        tone === "danger" ? theme.redSoft :
                                        tone === "accent" ? theme.accentSoft :
                                        (down ? "#1c2733" : hovered ? theme.raisedHover : theme.raised)
    readonly property color activeEdge: !enabled ? theme.lineSoft :
                                        tone === "success" ? "#347a59" :
                                        tone === "danger" ? "#86434b" :
                                        tone === "accent" ? "#315f8d" :
                                        (activeFocus ? theme.accent : hovered ? "#3a4a5b" : theme.line)
    readonly property color activeText: !enabled ? theme.muted2 :
                                        tone === "success" ? "#d6f4e6" :
                                        tone === "danger" ? "#ffdce0" : theme.textSoft

    contentItem: Text {
        text: control.text
        color: control.activeText
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: control.theme ? control.theme.controlRadius : 7
        color: control.activeFill
        border.width: 1
        border.color: control.activeEdge
        Behavior on color { ColorAnimation { duration: 90 } }
        Behavior on border.color { ColorAnimation { duration: 90 } }
    }
}
