// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    property var theme
    property string text: "Ready"
    property color tone: theme.green
    property color fill: theme.greenSoft
    implicitHeight: 26
    implicitWidth: row.implicitWidth + 18
    radius: 13
    color: fill
    border.width: 1
    border.color: Qt.rgba(tone.r, tone.g, tone.b, 0.45)

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 7
        Rectangle { width: 7; height: 7; radius: 4; color: root.tone }
        Label {
            text: root.text
            color: root.tone
            font.pixelSize: root.theme.captionSize
            font.weight: Font.DemiBold
        }
    }
}
