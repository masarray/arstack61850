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
    readonly property var detail: treeModel.detailModel

    color: theme.background

    function text(value) {
        return value === undefined || value === null || String(value).length === 0
                ? "—" : String(value)
    }

    // Inline inspection is local; network reads remain explicit ribbon commands.
    function drillInto(row) { detail.drillInto(row) }

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
                    visible: root.detail.rootKind !== "" && root.detail.rootKind !== "IED"
                    implicitWidth: 24
                    implicitHeight: 22
                    text: "‹"
                    onClicked: root.detail.selectParent()
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
                        text: root.detail.rootKind || "MODEL"
                        color: "#ffffff"
                        font.pixelSize: 7
                        font.weight: Font.Bold
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: root.detail.rootLabel || "Data Model"
                    color: theme.text
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    elide: Text.ElideMiddle
                }

                Label {
                    visible: root.detail.rootChildCount > 0
                    text: root.detail.rootChildCount + " item(s)"
                    color: theme.muted
                    font.pixelSize: 8
                }
            }
        }

        Rectangle {
            visible: root.detail.rootChildCount > 0
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
            visible: root.detail.rootChildCount > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            reuseItems: true
            cacheBuffer: 0
            model: root.detail
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: childRow
                required property int index
                required property var model
                width: childList.width
                height: 25
                color: childRow.model.selected ? theme.accent
                       : childMouse.containsMouse ? theme.accentSoft
                       : index % 2 ? theme.surfaceSoft : theme.surface
                border.width: 1
                border.color: theme.lineSoft

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 7 + Math.min(childRow.model.depth, 6) * 14
                    anchors.rightMargin: 7
                    spacing: 6

                    Label {
                        Layout.preferredWidth: 10
                        text: childRow.model.hasChildren ? (childRow.model.expanded ? "▾" : "▸") : ""
                        color: childRow.model.selected ? "#ffffff" : theme.muted
                        font.pixelSize: 9
                    }
                    Rectangle {
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 17
                        radius: 2
                        color: childRow.model.selected ? "#ffffff" : theme.accent
                        Label {
                            anchors.centerIn: parent
                            text: childRow.model.kind || ""
                            color: childRow.model.selected ? theme.accent : "#ffffff"
                            font.pixelSize: 7
                            font.weight: Font.Bold
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: childRow.model.label || childRow.model.reference || "—"
                        color: childRow.model.selected ? "#ffffff" : theme.text
                        font.pixelSize: 9
                        elide: Text.ElideMiddle
                    }
                    Label {
                        visible: childRow.model.functionalConstraint
                                 && String(childRow.model.functionalConstraint).length > 0
                        text: "[" + childRow.model.functionalConstraint + "]"
                        color: childRow.model.selected ? "#dff7f3" : theme.muted
                        font.pixelSize: 8
                    }
                    Label {
                        Layout.preferredWidth: Math.max(180, root.width * 0.42)
                        text: childRow.model.value
                              || childRow.model.description
                              || (childRow.model.hasChildren
                                  ? "Expand for attributes" : "—")
                        color: childRow.model.selected ? "#ffffff" : theme.textSoft
                        font.pixelSize: 9
                        elide: Text.ElideRight
                    }
                }

                MouseArea {
                    id: childMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: function(event) {
                        if (childRow.model.hasChildren &&
                            event.x < 24 + Math.min(childRow.model.depth, 6) * 14)
                            root.detail.toggle(childRow.index)
                        else
                            root.detail.select(childRow.index)
                    }
                    onDoubleClicked: function(event) {
                        // First arrow click has already changed expansion; a
                        // double-click on that arrow must not undo it.
                        if (childRow.model.hasChildren &&
                            event.x < 24 + Math.min(childRow.model.depth, 6) * 14)
                            root.detail.select(childRow.index)
                        else
                            root.drillInto(childRow.index)
                    }
                }
            }
        }

        ScrollView {
            visible: root.detail.rootChildCount === 0
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

                // Semantic fields are present only for verified typed MMS
                // quality/UTC values. No false flags for unread/offline data.
                Repeater {
                    model: [
                        {heading: "Quality Details", data: root.currentNode.qualityDetails},
                        {heading: "Time Quality", data: root.currentNode.timestampDetails}
                    ]
                    delegate: ColumnLayout {
                        id: semanticSection
                        required property int index
                        required property var modelData
                        visible: modelData.data !== undefined &&
                                 modelData.data.fields !== undefined
                        Layout.fillWidth: true
                        spacing: 0
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: visible ? 24 : 0
                            color: theme.surfaceRaised
                            Label {
                                anchors.fill: parent
                                leftPadding: 8
                                verticalAlignment: Text.AlignVCenter
                                text: modelData.heading + " · MMS Read"
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                                color: theme.text
                            }
                        }
                        Repeater {
                            model: semanticSection.modelData.data &&
                                   semanticSection.modelData.data.fields
                                   ? semanticSection.modelData.data.fields : []
                            delegate: Rectangle {
                                required property int index
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 25
                                color: index % 2 ? theme.surfaceSoft : theme.surface
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 15
                                    anchors.rightMargin: 8
                                    Label {
                                        text: modelData.label
                                        Layout.preferredWidth: Math.max(150, root.width * 0.34)
                                        color: theme.textSoft
                                        font.pixelSize: 10
                                    }
                                    Label {
                                        text: root.text(modelData.value)
                                        Layout.fillWidth: true
                                        color: theme.text
                                        font.pixelSize: 10
                                    }
                                }
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    height: 1
                                    color: theme.lineSoft
                                }
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
            visible: root.detail.truncated
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            text: "Showing the first 4096 visible rows. Collapse branches or narrow the selection to continue."
            color: theme.amber
            font.pixelSize: 8
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
}
