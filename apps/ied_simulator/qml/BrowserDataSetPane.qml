// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    required property var theme
    required property var reports
    required property var modelProvider
    readonly property bool overview: reports.selectedDataSetIndex < 0
    readonly property var selectedDataSet: !overview && reports.selectedDataSetIndex < reports.dataSets.length ? reports.dataSets[reports.selectedDataSetIndex] : ({})
    color: theme.background

    function memberNode(reference) { return modelProvider.treeModel.nodeForReference(reference) }

    ColumnLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 30
            color: theme.surface; border.width: 1; border.color: theme.lineSoft
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 7
                Button { visible: !root.overview; implicitWidth: 24; implicitHeight: 22; text: "‹"; onClicked: reports.selectDataSet(-1) }
                Rectangle {
                    implicitWidth: 24; implicitHeight: 18; radius: 2; color: theme.accent
                    Label { anchors.centerIn: parent; text: root.overview ? "LD" : "DS"; color: "#ffffff"; font.pixelSize: 7; font.weight: Font.Bold }
                }
                Label { Layout.fillWidth: true; text: root.overview ? "DataSets" : (root.selectedDataSet.reference || "DataSet"); color: theme.text; font.pixelSize: 10; font.weight: Font.DemiBold; elide: Text.ElideMiddle }
                Label {
                    visible: !root.overview
                    text: root.selectedDataSet.dynamicOwned === true ? "DYNAMIC OWNED" : "STATIC / READ-ONLY"
                    color: root.selectedDataSet.dynamicOwned === true ? theme.green : theme.muted
                    font.pixelSize: 7
                    font.weight: Font.DemiBold
                    ToolTip.visible: badgeMouse.containsMouse
                    ToolTip.text: root.selectedDataSet.dynamicOwned === true
                                  ? "Association-owned DataSet."
                                  : "Membership is immutable for static DataSets."
                    MouseArea { id: badgeMouse; anchors.fill: parent; hoverEnabled: true }
                }
                Label { text: root.overview ? reports.dataSets.length + " DataSet(s)" : reports.selectedDataSetMembers.length + " member(s)"; color: theme.muted; font.pixelSize: 8 }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 25; color: theme.chrome; border.width: 1; border.color: theme.lineSoft
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                Label { Layout.fillWidth: true; text: "Name"; color: theme.textSoft; font.pixelSize: 8; font.weight: Font.DemiBold }
                Label { Layout.preferredWidth: Math.max(180, root.width * 0.42); text: root.overview ? "Scope / members" : "Value"; color: theme.textSoft; font.pixelSize: 8; font.weight: Font.DemiBold }
            }
        }
        StackLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; currentIndex: root.overview ? 0 : 1
            ListView {
                id: dataSetList; objectName: "browserDataSetOverview"; clip: true; reuseItems: true; model: reports.dataSets
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: dataSetRow; required property int index; required property var modelData
                    width: dataSetList.width; height: 26
                    color: dsMouse.containsMouse ? theme.accentSoft : index % 2 ? theme.surfaceSoft : theme.surface
                    border.width: 1; border.color: theme.lineSoft
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 6
                        Label { text: "›"; color: theme.muted; font.pixelSize: 9 }
                        Rectangle { Layout.preferredWidth: 20; Layout.preferredHeight: 17; radius: 2; color: theme.accent; Label { anchors.centerIn: parent; text: "DS"; color: "#ffffff"; font.pixelSize: 7; font.weight: Font.Bold } }
                        Label { Layout.fillWidth: true; text: dataSetRow.modelData.name || dataSetRow.modelData.reference || "DataSet"; color: theme.text; font.pixelSize: 9; elide: Text.ElideMiddle }
                        Label { Layout.preferredWidth: Math.max(180, root.width * 0.42); text: (dataSetRow.modelData.domain || "") + (dataSetRow.modelData.members ? " · " + dataSetRow.modelData.members.length + " member(s)" : ""); color: theme.textSoft; font.pixelSize: 9; elide: Text.ElideRight }
                    }
                    MouseArea { id: dsMouse; anchors.fill: parent; hoverEnabled: true; onDoubleClicked: reports.selectDataSet(dataSetRow.index) }
                }
            }
            ListView {
                id: memberList; objectName: "browserDataSetMembers"; clip: true; reuseItems: true; model: reports.selectedDataSetMembers
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: memberRow; required property int index; required property string modelData
                    readonly property var resolved: root.memberNode(modelData)
                    width: memberList.width; height: 26
                    color: memberMouse.containsMouse ? theme.accentSoft : index % 2 ? theme.surfaceSoft : theme.surface
                    border.width: 1; border.color: theme.lineSoft
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 6
                        Rectangle { Layout.preferredWidth: 20; Layout.preferredHeight: 17; radius: 2; color: theme.accent; Label { anchors.centerIn: parent; text: memberRow.resolved.kind || "DA"; color: "#ffffff"; font.pixelSize: 7; font.weight: Font.Bold } }
                        Label { Layout.fillWidth: true; text: memberRow.modelData; color: theme.text; font.pixelSize: 9; elide: Text.ElideMiddle }
                        Label { visible: memberRow.resolved.functionalConstraint && String(memberRow.resolved.functionalConstraint).length > 0; text: "[" + memberRow.resolved.functionalConstraint + "]"; color: theme.muted; font.pixelSize: 8 }
                        Label { Layout.preferredWidth: Math.max(180, root.width * 0.42); text: memberRow.resolved.value || "—"; color: theme.textSoft; font.pixelSize: 9; elide: Text.ElideRight }
                    }
                    MouseArea { id: memberMouse; anchors.fill: parent; hoverEnabled: true }
                }
            }
        }
    }
}
