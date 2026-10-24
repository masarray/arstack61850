// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

Rectangle {
    id: toast
    property var theme
    property string uiFont: "Inter"
    property string message: ""
    property bool error: false

    implicitWidth: Math.min(520, Math.max(290, messageLabel.implicitWidth + 52))
    implicitHeight: Math.max(60, messageLabel.implicitHeight + 30)
    radius: 12
    color: "#18212b"
    border.width: 1
    border.color: error ? "#98505a" : "#36536f"
    opacity: message.length > 0 ? 1 : 0
    scale: message.length > 0 ? 1 : 0.985
    visible: opacity > 0

    Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
    Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

    Label {
        id: messageLabel
        anchors.fill: parent
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        anchors.topMargin: 12
        anchors.bottomMargin: 12
        text: toast.message
        color: toast.error ? "#ffb3bb" : toast.theme.textSoft
        font.family: toast.uiFont
        font.pixelSize: 13
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
        maximumLineCount: 4
        elide: Text.ElideRight
    }
}
