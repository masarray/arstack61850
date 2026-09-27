// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    required property var theme
    required property var reports
    required property var context
    required property var client

    property int valueRevision: 0
    property string routeMessage: ""

    signal inspectRequested(string reference)
    signal inspectStaticReportRequested()

    color: theme.background

    function text(value) {
        return value === undefined || value === null || String(value).length === 0 ? "—" : String(value)
    }

    function resolvedNode(reference) {
        root.valueRevision
        return context.treeModel.nodeForReference(reference)
    }

    readonly property var selectedDataSet: reports && reports.dataSets
        && reports.selectedDataSetIndex >= 0
        && reports.selectedDataSetIndex < reports.dataSets.length
        ? (reports.dataSets[reports.selectedDataSetIndex] || null) : null

    function hasValue(value) {
        return value !== undefined && value !== null && String(value).length > 0
    }

    Connections {
        target: context.treeModel
        function onDataChanged() { root.valueRevision += 1 }
        function onModelReset() { root.valueRevision += 1 }
        function onSelectionChanged() { root.valueRevision += 1 }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Label {
            text: "DataSet members"
            color: theme.text
            font.pixelSize: theme.subtitleSize
            font.weight: Font.DemiBold
        }

        Label {
            Layout.fillWidth: true
            text: root.selectedDataSet !== null
                  ? root.text(root.selectedDataSet.reference)
                  : "Select a DataSet in the navigation tree."
            color: theme.textSoft
            font.pixelSize: theme.labelSize
            wrapMode: Text.WrapAnywhere
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.selectedDataSet !== null
            Rectangle {
                Layout.preferredWidth: 104
                Layout.preferredHeight: 22
                radius: 11
                color: root.selectedDataSet !== null && root.selectedDataSet.dynamicOwned === true
                       ? theme.greenSoft : theme.surface
                border.width: 1
                border.color: root.selectedDataSet !== null && root.selectedDataSet.dynamicOwned === true
                              ? theme.green : theme.lineSoft
                Label {
                    anchors.centerIn: parent
                    text: root.selectedDataSet !== null && root.selectedDataSet.dynamicOwned === true
                          ? "DYNAMIC OWNED" : "STATIC / READ-ONLY"
                    color: root.selectedDataSet !== null && root.selectedDataSet.dynamicOwned === true
                           ? theme.green : theme.muted
                    font.pixelSize: 7
                    font.weight: Font.Bold
                }
            }
            Label {
                Layout.fillWidth: true
                text: root.selectedDataSet !== null && root.selectedDataSet.dynamicOwned === true
                      ? "Created and verified on this live association; eligible for owned-only delete."
                      : "Membership is immutable in Browser authoring."
                color: theme.muted
                font.pixelSize: 8
                elide: Text.ElideRight
            }
        }

        Label {
            Layout.fillWidth: true
            visible: root.routeMessage.length > 0
            text: root.routeMessage
            color: theme.amber
            font.pixelSize: theme.captionSize
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "Ordered members and observed values"
                color: theme.text
                font.pixelSize: theme.labelSize
                font.weight: Font.DemiBold
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                theme: root.theme
                text: "Static Reporting…"
                enabled: root.selectedDataSet !== null
                         && root.selectedDataSet.directoryAvailable === true
                         && root.selectedDataSet.deletable !== true
                         && root.selectedDataSet.immutable === true
                         && root.selectedDataSet.dynamicOwned !== true
                         && reports.selectedDataSetMembers.length > 0
                onClicked: root.inspectStaticReportRequested()
                ToolTip.visible: hovered
                ToolTip.text: "Prepare this exact static DataSet in the shared Reports path. Selection only: no DatSet write, enable or GI."
            }
            ActionButton {
                theme: root.theme
                text: root.client.operationBusy ? "Reading…" : "Read members"
                enabled: root.client.connected && !root.client.operationBusy
                         && reports.selectedDataSetMembers.length > 0
                onClicked: root.client.refreshEngineeringReferences(reports.selectedDataSetMembers)
                ToolTip.visible: hovered
                ToolTip.text: "Read the ordered DataSet members through the canonical model. Requests remain bounded by the client controller."
            }
            Label {
                text: String(reports.selectedDataSetMembers.length)
                color: theme.muted
                font.pixelSize: theme.captionSize
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: theme.surfaceRaised
            border.width: 1
            border.color: theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8
                Label { Layout.preferredWidth: 36; text: "#"; color: theme.textSoft; font.pixelSize: theme.captionSize; font.weight: Font.DemiBold }
                Label { Layout.fillWidth: true; text: "Member"; color: theme.textSoft; font.pixelSize: theme.captionSize; font.weight: Font.DemiBold }
                Label { Layout.preferredWidth: 48; text: "FC"; color: theme.textSoft; font.pixelSize: theme.captionSize; font.weight: Font.DemiBold }
                Label { Layout.preferredWidth: 160; text: "Value"; color: theme.textSoft; font.pixelSize: theme.captionSize; font.weight: Font.DemiBold }
                Label { Layout.preferredWidth: 108; text: "Type"; color: theme.textSoft; font.pixelSize: theme.captionSize; font.weight: Font.DemiBold }
            }
        }

        ListView {
            id: memberList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            reuseItems: true
            model: reports.selectedDataSetMembers
            spacing: 1
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: memberRow
                required property int index
                required property string modelData
                property var resolved: root.resolvedNode(modelData)
                width: ListView.view.width
                height: 32
                color: memberMouse.containsMouse ? theme.surfaceRaised
                                                  : index % 2 ? theme.chrome : theme.surface
                border.width: 0
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8
                    Label {
                        Layout.preferredWidth: 36
                        text: String(index + 1)
                        color: theme.muted
                        font.pixelSize: theme.captionSize
                    }
                    Label {
                        Layout.fillWidth: true
                        text: modelData
                        color: theme.text
                        font.pixelSize: theme.labelSize
                        elide: Text.ElideMiddle
                    }
                    Label {
                        Layout.preferredWidth: 48
                        text: memberRow.resolved.functionalConstraint || ""
                        color: theme.muted
                        font.pixelSize: theme.captionSize
                    }
                    Label {
                        Layout.preferredWidth: 160
                        text: root.text(memberRow.resolved.value)
                        color: root.hasValue(memberRow.resolved.value) ? theme.text : theme.muted
                        font.pixelSize: theme.labelSize
                        font.weight: root.hasValue(memberRow.resolved.value) ? Font.Medium : Font.Normal
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.preferredWidth: 108
                        text: memberRow.resolved.sclType || memberRow.resolved.mmsType || ""
                        color: theme.textSoft
                        font.pixelSize: theme.captionSize
                        elide: Text.ElideRight
                    }
                }

                MouseArea {
                    id: memberMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: memberRow.resolved.reference
                    onDoubleClicked: root.inspectRequested(memberRow.resolved.reference)
                }

                ToolTip.visible: memberMouse.containsMouse
                ToolTip.text: memberMouse.enabled
                              ? "Double-click to inspect this member in Data Model"
                              : modelData
                ToolTip.delay: 650
            }

            Label {
                anchors.centerIn: parent
                visible: memberList.count === 0
                text: "Select a DataSet to inspect its ordered members."
                color: theme.muted
                font.pixelSize: theme.bodySize
            }
        }
    }
}
