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
    property bool expanded: true

    signal closeRequested()

    implicitHeight: expanded ? 108 : 32
    color: theme.surface
    radius: theme.panelRadius
    border.width: 1
    border.color: theme.line
    clip: true
    Behavior on implicitHeight { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }

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

                Rectangle { width: 3; height: 14; radius: 2; color: telemetry.device.running ? theme.green : theme.accent }
                Label {
                    text: "Status & output monitor"
                    color: theme.textSoft
                    font.family: telemetry.uiFont
                    font.pixelSize: theme.labelSize
                    font.weight: Font.DemiBold
                }
                Label {
                    text: telemetry.device.running ? "LIVE" : "STANDBY"
                    color: telemetry.device.running ? theme.green : theme.muted
                    font.family: telemetry.monoFont
                    font.pixelSize: theme.captionSize - 1
                    font.weight: Font.Bold
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: "FPS " + telemetry.device.fps + "   ·   MISSED " + telemetry.device.missed + "   ·   TX FAIL " + telemetry.device.txFailures
                    color: theme.muted
                    font.family: telemetry.monoFont
                    font.pixelSize: theme.captionSize - 1
                }
                ToolButton {
                    text: telemetry.expanded ? "⌄" : "⌃"
                    font.pixelSize: theme.bodySize
                    onClicked: telemetry.expanded = !telemetry.expanded
                    ToolTip.visible: hovered
                    ToolTip.text: telemetry.expanded ? "Collapse monitor" : "Expand monitor"
                }
                ToolButton {
                    text: "×"
                    font.pixelSize: theme.bodySize
                    onClicked: telemetry.closeRequested()
                    ToolTip.visible: hovered
                    ToolTip.text: "Close monitor"
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
                Layout.preferredWidth: 1.25
                radius: theme.controlRadius
                color: theme.surface2
                border.width: 1
                border.color: theme.lineSoft

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 3
                    Label {
                        text: "STATUS HISTORY"
                        color: theme.muted
                        font.family: telemetry.uiFont
                        font.pixelSize: theme.captionSize - 1
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.8
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: telemetry.historyModel
                        clip: true
                        spacing: 2
                        delegate: RowLayout {
                            required property string timeText
                            required property string messageText
                            required property bool isError
                            width: ListView.view.width
                            spacing: 7
                            Label { text: timeText; color: theme.muted; font.family: telemetry.monoFont; font.pixelSize: theme.captionSize - 1 }
                            Label { Layout.fillWidth: true; text: messageText; color: isError ? theme.red : theme.textSoft; font.family: telemetry.uiFont; font.pixelSize: theme.captionSize; elide: Text.ElideRight }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 0.75
                radius: theme.controlRadius
                color: theme.surface2
                border.width: 1
                border.color: theme.lineSoft

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4
                    Label {
                        text: "OUTPUT CHANNELS"
                        color: theme.muted
                        font.family: telemetry.uiFont
                        font.pixelSize: theme.captionSize - 1
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.8
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Repeater {
                            model: telemetry.currentModel
                            delegate: RowLayout {
                                required property string signalId
                                required property bool enabled
                                spacing: 4
                                Rectangle { width: 7; height: 7; radius: 4; color: enabled ? theme.green : theme.muted2 }
                                Label { text: signalId; color: theme.textSoft; font.family: telemetry.monoFont; font.pixelSize: theme.captionSize }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Repeater {
                            model: telemetry.voltageModel
                            delegate: RowLayout {
                                required property string signalId
                                required property bool enabled
                                spacing: 4
                                Rectangle { width: 7; height: 7; radius: 4; color: enabled ? theme.accent : theme.muted2 }
                                Label { text: signalId; color: theme.textSoft; font.family: telemetry.monoFont; font.pixelSize: theme.captionSize }
                            }
                        }
                    }
                }
            }
        }
    }
}
