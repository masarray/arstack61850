// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    required property var theme
    required property var reports

    color: theme.background

    function text(value) {
        return value === undefined || value === null || String(value).length === 0 ? "—" : String(value)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        Label {
            text: "DataSet"
            color: theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }

        Label {
            Layout.fillWidth: true
            text: reports.selectedDataSetIndex >= 0 && reports.selectedDataSetIndex < reports.dataSets.length
                  ? root.text(reports.dataSets[reports.selectedDataSetIndex].reference)
                  : "Select a DataSet in the navigation tree."
            color: theme.textSoft
            font.pixelSize: 10
            wrapMode: Text.WrapAnywhere
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "Ordered members"
                color: theme.text
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }
            Item { Layout.fillWidth: true }
            Label {
                text: String(reports.selectedDataSetMembers.length)
                color: theme.muted
                font.pixelSize: 9
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            reuseItems: true
            model: reports.selectedDataSetMembers
            spacing: 1
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                required property int index
                required property string modelData
                width: ListView.view.width
                height: 32
                color: index % 2 ? theme.surfaceSoft : theme.surface
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8
                    Label {
                        Layout.preferredWidth: 34
                        text: String(index + 1)
                        color: theme.muted
                        font.pixelSize: 8
                    }
                    Label {
                        Layout.fillWidth: true
                        text: modelData
                        color: theme.textSoft
                        font.pixelSize: 9
                        elide: Text.ElideMiddle
                    }
                }
            }
        }
    }
}
