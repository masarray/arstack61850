// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

Rectangle {
    id: badge
    property var theme
    property string state: "OFFLINE"
    property color stateColor: theme.muted
    property string monoFont: "Cascadia Mono"

    implicitHeight: 24
    implicitWidth: stateText.implicitWidth + 24
    radius: 6
    color: state === "RUNNING" ? theme.greenSoft :
           state === "PROFILE BLOCKED" ? theme.redSoft :
           (state === "PROFILE CHANGED" || state === "DEPLOYING") ? theme.amberSoft :
           theme.accentSoft
    border.width: 1
    border.color: stateColor
    Behavior on color { ColorAnimation { duration: 120 } }

    Label {
        id: stateText
        anchors.centerIn: parent
        text: badge.state
        color: badge.stateColor
        font.family: badge.monoFont
        font.pixelSize: 8
        font.weight: Font.Bold
        font.letterSpacing: 0.45
    }
}