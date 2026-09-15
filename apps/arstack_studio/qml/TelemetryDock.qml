// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: telemetry

    property var theme
    property var device
    property var currentModel
    property var voltageModel
    property var historyModel
    property string uiFont: "Inter"
    property string monoFont: "Inter"
    property bool expanded: false

    signal closeRequested()

    readonly property bool hasRuntimeTelemetry:
        telemetry.device.fps !== "—" || telemetry.device.missed !== "—" || telemetry.device.txFailures !== "—"

    implicitHeight: 34
    Layout.preferredHeight: expanded ? 132 : 34
    color: theme.surface
    radius: theme.panelRadius
    border.width: 1
    border.color: theme.line
    clip: true
    Behavior on Layout.preferredHeight { NumberAnimation { duration: 180; easing.type: Easing.InOutCubic } }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: headerBar
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: headerMouse.containsMouse ? theme.raisedHover : theme.raised
            Behavior on color { ColorAnimation { duration: 90 } }

            MouseArea {
                id: headerMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: telemetry.expanded = !telemetry.expanded
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 11
                anchors.rightMargin: 7
                spacing: 8

                Rectangle {
                    width: 7
                    height: 7
                    radius: 4
                    color: telemetry.device.running ? theme.green : (telemetry.device.deviceVerified ? theme.accent : theme.muted2)
                }
                Label {
                    text: telemetry.device.running ? "Injection running" : (telemetry.device.deviceVerified ? "Device ready" : "Monitor")
                    color: telemetry.device.running ? theme.green : theme.textSoft
                    font.family: telemetry.uiFont
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                }

                Item { Layout.fillWidth: true }

                Label {
                    visible: telemetry.device.running && telemetry.hasRuntimeTelemetry
                    text: telemetry.device.fps + " fps  ·  missed " + telemetry.device.missed + "  ·  tx fail " + telemetry.device.txFailures
                    color: theme.muted
                    font.family: telemetry.monoFont
                    font.pixelSize: 9
                    verticalAlignment: Text.AlignVCenter
                }

                DarkToolButton {
                    theme: telemetry.theme
                    uiFont: telemetry.uiFont
                    iconSource: telemetry.expanded
                        ? Qt.resolvedUrl("../assets/lucide/chevron-down.svg")
                        : Qt.resolvedUrl("../assets/lucide/chevron-up.svg")
                    onClicked: telemetry.expanded = !telemetry.expanded
                    ToolTip.visible: hovered
                    ToolTip.text: telemetry.expanded ? "Hide monitor details" : "Show monitor details"
                }
            }
        }

        RowLayout {
            visible: telemetry.expanded
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 9
            spacing: 10

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1.35
                radius: theme.controlRadius
                color: theme.surface2
                border.width: 1
                border.color: theme.lineSoft

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 9
                    spacing: 5
                    Label {
                        text: "Recent activity"
                        color: theme.textSoft
                        font.family: telemetry.uiFont
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: telemetry.historyModel
                        clip: true
                        spacing: 3
                        delegate: RowLayout {
                            required property string timeText
                            required property string messageText
                            required property bool isError
                            width: ListView.view.width
                            spacing: 8
                            Label {
                                text: timeText
                                color: theme.muted
                                font.family: telemetry.monoFont
                                font.pixelSize: 9
                            }
                            Label {
                                Layout.fillWidth: true
                                text: messageText
                                color: isError ? theme.red : theme.textSoft
                                font.family: telemetry.uiFont
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 250
                Layout.fillHeight: true
                radius: theme.controlRadius
                color: theme.surface2
                border.width: 1
                border.color: theme.lineSoft

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 7
                    Label {
                        text: "Transmission"
                        color: theme.textSoft
                        font.family: telemetry.uiFont
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 16
                        rowSpacing: 5
                        Label { text: "Rate"; color: theme.muted; font.family: telemetry.uiFont; font.pixelSize: 9 }
                        Label { text: telemetry.device.fps === "—" ? "—" : telemetry.device.fps + " fps"; color: theme.text; font.family: telemetry.monoFont; font.pixelSize: 10 }
                        Label { text: "Missed"; color: theme.muted; font.family: telemetry.uiFont; font.pixelSize: 9 }
                        Label { text: telemetry.device.missed; color: telemetry.device.missed === "0" ? theme.green : theme.text; font.family: telemetry.monoFont; font.pixelSize: 10 }
                        Label { text: "TX failures"; color: theme.muted; font.family: telemetry.uiFont; font.pixelSize: 9 }
                        Label { text: telemetry.device.txFailures; color: telemetry.device.txFailures === "0" ? theme.green : theme.text; font.family: telemetry.monoFont; font.pixelSize: 10 }
                    }
                }
            }
        }
    }
}
