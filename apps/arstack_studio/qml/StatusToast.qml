// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

Rectangle {
    id: toast
    property var theme
    property string uiFont: "Inter"
    property string message: ""
    property bool error: false

    implicitWidth: Math.min(620, messageLabel.implicitWidth + 30)
    implicitHeight: 36
    radius: 6
    color: "#18212b"
    border.width: 1
    border.color: error ? "#70404a" : "#36536f"
    opacity: message.length > 0 ? 1 : 0
    scale: message.length > 0 ? 1 : 0.985
    visible: opacity > 0

    Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
    Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

    Label {
        id: messageLabel
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        text: toast.message
        color: toast.error ? "#ffb3bb" : toast.theme.textSoft
        font.family: toast.uiFont
        font.pixelSize: 9
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}