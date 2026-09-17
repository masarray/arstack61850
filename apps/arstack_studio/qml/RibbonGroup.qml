// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: group

    property var theme
    property string uiFont: "Inter"
    property string title: ""
    default property alias content: actionRow.data

    implicitHeight: 66
    implicitWidth: Math.max(92, actionRow.implicitWidth + 18)
    color: "transparent"

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: group.theme.lineSoft
        opacity: 0.8
    }

    RowLayout {
        id: actionRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: caption.top
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 4
    }

    Label {
        id: caption
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 15
        text: group.title
        color: group.theme.muted
        font.family: group.uiFont
        font.pixelSize: 8
        font.weight: Font.Medium
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
