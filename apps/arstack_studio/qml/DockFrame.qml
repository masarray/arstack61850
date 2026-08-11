// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: frame

    default property alias contentData: contentHost.data
    property var theme
    property string titleText: "View"
    property string statusText: ""
    property string uiFont: "Inter"
    property string monoFont: "Inter"
    property bool detachable: false
    property bool closable: true

    signal detachRequested()
    signal closeRequested()

    color: theme.surface
    radius: theme.panelRadius
    border.width: 1
    border.color: theme.line
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: theme.raised

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 6
                spacing: 7

                Rectangle { width: 3; height: 14; radius: 2; color: theme.accent }
                Label {
                    Layout.fillWidth: true
                    text: frame.titleText
                    color: theme.textSoft
                    font.family: frame.uiFont
                    font.pixelSize: theme.labelSize
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
                Label {
                    visible: frame.statusText.length > 0
                    text: frame.statusText
                    color: theme.muted
                    font.family: frame.monoFont
                    font.pixelSize: theme.captionSize - 1
                    verticalAlignment: Text.AlignVCenter
                }
                DarkToolButton {
                    visible: frame.detachable
                    iconSource: Qt.resolvedUrl("../assets/lucide/external-link.svg")
                    theme: frame.theme
                    uiFont: frame.uiFont
                    onClicked: frame.detachRequested()
                    ToolTip.visible: hovered
                    ToolTip.text: "Detach into a separate window"
                }
                DarkToolButton {
                    visible: frame.closable
                    iconSource: Qt.resolvedUrl("../assets/lucide/x.svg")
                    iconSize: 19
                    theme: frame.theme
                    uiFont: frame.uiFont
                    onClicked: frame.closeRequested()
                    ToolTip.visible: hovered
                    ToolTip.text: "Close this view"
                }
            }

            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: theme.lineSoft }
        }

        Item {
            id: contentHost
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
