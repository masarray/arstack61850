// SPDX-License-Identifier: GPL-3.0-or-later
// Center-screen operator notice. Presentation ONLY: dismissal never changes
// board state, acknowledges a fault, or retries PTP/SV behind the operator.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: toast
    objectName: "centralOperatorToast"
    property var theme
    property string uiFont: "Inter"
    property string message: ""
    property string detail: ""
    property bool error: false

    width: Math.min(490, parent ? Math.max(260, parent.width - 36) : 490)
    implicitHeight: message.length > 0 ? (detail.length > 0 ? 124 : 72) : 0
    radius: 12
    z: 900
    color: "#1b2632"
    border.width: 1
    border.color: error ? "#b65b62" : "#557287"
    opacity: message.length > 0 ? 1 : 0
    scale: message.length > 0 ? 1 : 0.97
    visible: opacity > 0

    Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
    Behavior on scale { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Rectangle {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: 30
            Layout.preferredHeight: 30
            radius: 15
            color: toast.error ? "#503038" : "#274255"
            Label {
                anchors.centerIn: parent
                text: toast.error ? "!" : "i"
                color: toast.error ? "#ffb8bf" : "#b9daec"
                font.family: toast.uiFont
                font.pixelSize: 16
                font.weight: Font.Bold
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 5
            Label {
                Layout.fillWidth: true
                text: toast.message
                color: "#f2f7fb"
                font.family: toast.uiFont
                font.pixelSize: 13
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                visible: toast.detail.length > 0
                text: toast.detail
                color: "#b7c5d4"
                font.family: toast.uiFont
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }
        }
    }
}
