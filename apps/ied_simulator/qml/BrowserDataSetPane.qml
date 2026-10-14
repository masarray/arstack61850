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

    property int expandedMemberIndex: -1
    property var expandedMemberChildren: []
    readonly property int maximumExpandedChildren: 64

    function memberParts(raw) {
        const text = String(raw || "").trim()
        const start = text.lastIndexOf(" [")
        if (start >= 0 && text.endsWith("]")) {
            const fc = text.substring(start + 2, text.length - 1)
            if (/^[A-Za-z0-9]{1,4}$/.test(fc))
                return {reference: text.substring(0, start).trim(), fc: fc.toUpperCase()}
        }
        return {reference: text, fc: ""}
    }

    function displayValue(value) {
        return value === undefined || value === null || String(value).length === 0
               ? "—" : String(value)
    }
    function memberNode(reference) { return modelProvider.treeModel.nodeForReference(reference) }

    function toggleMember(row, raw) {
        if (expandedMemberIndex === row) {
            expandedMemberIndex = -1
            expandedMemberChildren = []
            return
        }
        const member = memberParts(raw)
        // Lazy bounded projection. No MMS traffic or duplicate semantic tree.
        expandedMemberChildren = modelProvider.treeModel.dataSetChildrenForReference(
            member.reference, member.fc, maximumExpandedChildren)
        expandedMemberIndex = row
    }

    Connections {
        target: root.reports
        function onSelectionChanged() {
            root.expandedMemberIndex = -1
            root.expandedMemberChildren = []
        }
    }

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
                    MouseArea { id: dsMouse; anchors.fill: parent; hoverEnabled: true; onClicked: reports.selectDataSet(dataSetRow.index) }
                }
            }
            ListView {
                id: memberList
                objectName: "browserDataSetMembers"
                clip: true
                reuseItems: true
                cacheBuffer: 0
                model: reports.selectedDataSetMembers
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    id: memberRow
                    required property int index
                    required property string modelData
                    readonly property var parts: root.memberParts(modelData)
                    readonly property var resolved: root.memberNode(parts.reference)
                    readonly property bool canExpand: resolved.hasChildren === true
                    readonly property bool expanded: root.expandedMemberIndex === index
                    readonly property int detailCount: expanded ? root.expandedMemberChildren.length : 0
                    width: memberList.width
                    height: 26 + (expanded ? Math.max(1, detailCount) * 24 : 0)
                    color: index % 2 ? theme.surfaceSoft : theme.surface

                    Column {
                        anchors.fill: parent
                        spacing: 0
                        Rectangle {
                            width: memberRow.width
                            height: 26
                            color: memberMouse.containsMouse ? theme.accentSoft : "transparent"
                            border.width: 1
                            border.color: theme.lineSoft
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 7
                                Label {
                                    Layout.preferredWidth: 11
                                    text: memberRow.canExpand ? (memberRow.expanded ? "▾" : "▸") : ""
                                    color: theme.muted
                                    font.pixelSize: 10
                                }
                                Rectangle {
                                    Layout.preferredWidth: 20
                                    Layout.preferredHeight: 17
                                    radius: 2
                                    color: theme.accent
                                    Label {
                                        anchors.centerIn: parent
                                        text: memberRow.resolved.kind || "?"
                                        color: "#ffffff"
                                        font.pixelSize: 8
                                    }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: memberRow.parts.reference
                                    color: theme.text
                                    font.pixelSize: 10
                                    elide: Text.ElideMiddle
                                    ToolTip.visible: memberMouse.containsMouse
                                    ToolTip.text: text
                                }
                                Label {
                                    visible: memberRow.parts.fc.length > 0
                                    text: "[" + memberRow.parts.fc + "]"
                                    color: theme.muted
                                    font.pixelSize: 9
                                }
                                Label {
                                    Layout.preferredWidth: Math.max(180, root.width * 0.42)
                                    text: root.displayValue(memberRow.resolved.value)
                                    color: theme.textSoft
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }
                            MouseArea {
                                id: memberMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    if (memberRow.canExpand)
                                        root.toggleMember(memberRow.index, memberRow.modelData)
                                    else if (memberRow.resolved.readable === true)
                                        root.modelProvider.treeModel.selectMmsItem(
                                            memberRow.resolved.mmsDomain, memberRow.resolved.mmsItem)
                                }
                            }
                        }
                        Repeater {
                            model: memberRow.expanded ? root.expandedMemberChildren : []
                            delegate: Rectangle {
                                id: attributeRow
                                required property int index
                                required property var modelData
                                width: memberRow.width
                                height: 24
                                color: index % 2 ? theme.surfaceSoft : theme.surface
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 30
                                    anchors.rightMargin: 8
                                    spacing: 7
                                    Label { text: "DA"; color: theme.accent; font.pixelSize: 9; Layout.preferredWidth: 18 }
                                    Label {
                                        Layout.fillWidth: true
                                        text: attributeRow.modelData.label || attributeRow.modelData.reference
                                        color: theme.text
                                        font.pixelSize: 10
                                        elide: Text.ElideMiddle
                                    }
                                    Label {
                                        text: attributeRow.modelData.functionalConstraint
                                              ? "[" + attributeRow.modelData.functionalConstraint + "]" : ""
                                        color: theme.muted
                                        font.pixelSize: 9
                                    }
                                    Label {
                                        Layout.preferredWidth: Math.max(180, root.width * 0.42)
                                        text: root.displayValue(attributeRow.modelData.value)
                                        color: theme.textSoft
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                    }
                                }
                                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.lineSoft }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: root.modelProvider.treeModel.selectMmsItem(
                                        attributeRow.modelData.mmsDomain, attributeRow.modelData.mmsItem)
                                }
                            }
                        }
                        Rectangle {
                            visible: memberRow.expanded && memberRow.detailCount === 0
                            width: memberRow.width
                            height: visible ? 24 : 0
                            color: theme.surfaceSoft
                            Label {
                                anchors.fill: parent
                                leftPadding: 31
                                verticalAlignment: Text.AlignVCenter
                                text: "No FC-matching attributes exposed by the canonical model."
                                color: theme.muted
                                font.pixelSize: 9
                            }
                        }
                    }
                }
            }
        }
    }
}
