// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var theme
    required property var client
    required property var modelProvider

    readonly property var treeModel: modelProvider.treeModel
    readonly property var currentNode: treeModel.presentationNode
    property int highlightedRow: -1

    color: theme.background

    function text(value) {
        return value === undefined || value === null || String(value).length === 0
                ? "—" : String(value)
    }

    function drillInto(row) {
        if (!treeModel.selectChild(row))
            return
        highlightedRow = -1
        if (treeModel.presentationNode.hasChildren !== true
                && client.connected && !client.operationBusy
                && treeModel.presentationNode.readable === true)
            client.readEngineeringSelected()
    }

    Connections {
        target: root.treeModel
        function onSelectionChanged() { root.highlightedRow = -1 }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: theme.surface
            border.width: 1
            border.color: theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 7

                Button {
                    objectName: "browserDataModelBack"
                    visible: Boolean(root.currentNode.kind) && root.currentNode.kind !== "IED"
                    implicitWidth: 24
                    implicitHeight: 22
                    text: "‹"
                    onClicked: root.treeModel.selectParent()
                    ToolTip.visible: hovered
                    ToolTip.text: "Up one model level"
                }

                Rectangle {
                    implicitWidth: Math.max(24, nodeKind.implicitWidth + 10)
                    implicitHeight: 18
                    radius: 2
                    color: theme.accent
                    Label {
                        id: nodeKind
                        anchors.centerIn: parent
                        text: root.currentNode.kind || "MODEL"
                        color: "#ffffff"
                        font.pixelSize: 7
                        font.weight: Font.Bold
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: root.currentNode.label || "Data Model"
                    color: theme.text
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    elide: Text.ElideMiddle
                }

                Label {
                    visible: root.treeModel.selectedChildCount > 0
                    text: root.treeModel.selectedChildCount + " item(s)"
                    color: theme.muted
                    font.pixelSize: 8
                }
            }
        }

        Rectangle {
            visible: root.treeModel.selectedChildCount > 0
            Layout.fillWidth: true
            Layout.preferredHeight: 25
            color: theme.chrome
            border.width: 1
            border.color: theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    text: "Name"
                    color: theme.textSoft
                    font.pixelSize: 8
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.preferredWidth: Math.max(180, root.width * 0.42)
                    text: "Value / description"
                    color: theme.textSoft
                    font.pixelSize: 8
                    font.weight: Font.DemiBold
                }
            }
        }

        ListView {
            id: childList
            objectName: "browserDataModelChildren"
            visible: root.treeModel.selectedChildCount > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            reuseItems: true
            cacheBuffer: 0
            model: root.treeModel.selectedChildren
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: childRow
                required property int index
                required property var modelData
                width: childList.width
                height: 25
                color: root.highlightedRow === index ? theme.accent
                       : childMouse.containsMouse ? theme.accentSoft
                       : index % 2 ? theme.surfaceSoft : theme.surface
                border.width: 1
                border.color: theme.lineSoft

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 7
                    anchors.rightMargin: 7
                    spacing: 6

                    Label {
                        Layout.preferredWidth: 10
                        text: childRow.modelData.hasChildren === true ? "›" : ""
                        color: root.highlightedRow === childRow.index ? "#ffffff" : theme.muted
                        font.pixelSize: 9
                    }
                    Rectangle {
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 17
                        radius: 2
                        color: root.highlightedRow === childRow.index ? "#ffffff" : theme.accent
                        Label {
                            anchors.centerIn: parent
                            text: childRow.modelData.kind || ""
                            color: root.highlightedRow === childRow.index ? theme.accent : "#ffffff"
                            font.pixelSize: 7
                            font.weight: Font.Bold
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: childRow.modelData.label || childRow.modelData.reference || "—"
                        color: root.highlightedRow === childRow.index ? "#ffffff" : theme.text
                        font.pixelSize: 9
                        elide: Text.ElideMiddle
                    }
                    Label {
                        visible: childRow.modelData.functionalConstraint
                                 && String(childRow.modelData.functionalConstraint).length > 0
                        text: "[" + childRow.modelData.functionalConstraint + "]"
                        color: root.highlightedRow === childRow.index ? "#dff7f3" : theme.muted
                        font.pixelSize: 8
                    }
                    Label {
                        Layout.preferredWidth: Math.max(180, root.width * 0.42)
                        text: childRow.modelData.value
                              || childRow.modelData.description
                              || (childRow.modelData.childCount > 0
                                  ? childRow.modelData.childCount + " item(s)" : "—")
                        color: root.highlightedRow === childRow.index ? "#ffffff" : theme.textSoft
                        font.pixelSize: 9
                        elide: Text.ElideRight
                    }
                }

                MouseArea {
                    id: childMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.highlightedRow = childRow.index
                    onDoubleClicked: root.drillInto(childRow.index)
                }
            }
        }

        ScrollView {
            visible: root.treeModel.selectedChildCount === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth

            ColumnLayout {
                width: Math.max(0, parent.width - 24)
                x: 12
                spacing: 0

                Repeater {
                    model: [
                        {label: "Reference", value: root.currentNode.reference},
                        {label: "Value", value: root.currentNode.value},
                        {label: "Quality", value: root.currentNode.quality},
                        {label: "Timestamp", value: root.currentNode.timestamp},
                        {label: "Functional constraint", value: root.currentNode.functionalConstraint},
                        {label: "MMS type", value: root.currentNode.mmsType},
                        {label: "SCL type", value: root.currentNode.sclType},
                        {label: "Type evidence", value: root.currentNode.typeStatus}
                    ]
                    delegate: Rectangle {
                        required property int index
                        required property var modelData
                        visible: modelData.value !== undefined && modelData.value !== null
                                 && String(modelData.value).length > 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? 27 : 0
                        color: index % 2 ? theme.surfaceSoft : theme.surface
                        border.width: 1
                        border.color: theme.lineSoft
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            Label {
                                Layout.preferredWidth: Math.max(150, root.width * 0.34)
                                text: modelData.label
                                color: theme.muted
                                font.pixelSize: 8
                            }
                            Label {
                                Layout.fillWidth: true
                                text: root.text(modelData.value)
                                color: theme.text
                                font.pixelSize: 9
                                elide: Text.ElideMiddle
                            }
                        }
                    }
                }

                Label {
                    visible: !root.currentNode.kind
                    Layout.fillWidth: true
                    Layout.topMargin: 32
                    text: "No canonical model is loaded."
                    horizontalAlignment: Text.AlignHCenter
                    color: theme.muted
                    font.pixelSize: 10
                }
                Item { Layout.fillHeight: true }
            }
        }

        Label {
            visible: root.treeModel.selectedChildrenTruncated
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            text: "Showing the first 4096 direct children. Refine the canonical tree selection to continue."
            color: theme.amber
            font.pixelSize: 8
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
}
