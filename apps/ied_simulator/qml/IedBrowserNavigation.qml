// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var theme
    required property var session
    required property var client
    required property var context
    required property var reports
    required property var utilities

    property int section: 0
    property string sectionTitle: "Data Model"
    signal sectionRequested(int section, string title)

    color: theme.chrome
    border.width: 1
    border.color: theme.lineSoft

    function choose(sectionValue, titleValue) {
        root.sectionTitle = titleValue
        root.sectionRequested(sectionValue, titleValue)
    }

    function ensureSectionService() {
        if (!session.connected)
            return
        if (section === 1 || section === 2)
            session.ensureReportsConnected()
        else if (section === 3 || section === 4)
            session.ensureUtilitiesConnected()
    }

    onSectionChanged: ensureSectionService()

    component NavButton: Rectangle {
        id: nav
        required property int targetSection
        required property string title
        property string countText: ""
        Layout.fillWidth: true
        implicitHeight: 27
        color: root.section === targetSection ? root.theme.accentSoft
                                              : navMouse.containsMouse ? root.theme.surfaceRaised : "transparent"
        border.width: root.section === targetSection ? 1 : 0
        border.color: root.theme.accent

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 24
            anchors.rightMargin: 8
            spacing: 7
            Label {
                text: root.section === nav.targetSection ? "▾" : "›"
                color: root.section === nav.targetSection ? root.theme.accent : root.theme.muted
                font.pixelSize: 11
                Layout.preferredWidth: 12
            }
            Label {
                Layout.fillWidth: true
                text: nav.title
                color: root.section === nav.targetSection ? root.theme.text : root.theme.textSoft
                font.pixelSize: 9
                font.weight: root.section === nav.targetSection ? Font.DemiBold : Font.Normal
            }
            Label {
                visible: nav.countText.length > 0
                text: nav.countText
                color: root.theme.muted
                font.pixelSize: 8
            }
        }

        MouseArea {
            id: navMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.choose(nav.targetSection, nav.title)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            color: root.theme.statusChrome
            border.width: 1
            border.color: root.theme.lineSoft

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                anchors.topMargin: 6
                anchors.bottomMargin: 6
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    text: "ENGINEERING EXPLORER"
                    color: root.theme.navigationText
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.fillWidth: true
                    text: "One active IED context"
                    color: root.theme.navigationMuted
                    font.pixelSize: 7
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth

            ColumnLayout {
                width: Math.max(0, parent.width)
                spacing: 0

                Rectangle {
                    id: iedRoot
                    Layout.fillWidth: true
                    implicitHeight: 39
                    color: root.theme.surfaceSoft
                    border.width: 1
                    border.color: root.theme.lineSoft

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 8
                        spacing: 6

                        Label {
                            text: "▾"
                            color: root.theme.accent
                            font.pixelSize: 10
                            Layout.preferredWidth: 12
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label {
                                Layout.fillWidth: true
                                text: context.iedName.length ? context.iedName : "IED"
                                color: root.theme.text
                                font.pixelSize: 9
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: context.loaded
                                      ? context.authority + " · " + (context.endpoint.length ? context.endpoint : session.endpoint)
                                      : "No engineering model loaded"
                                color: root.theme.muted
                                font.pixelSize: 7
                                elide: Text.ElideMiddle
                            }
                        }
                        Label {
                            text: context.online ? "ONLINE" : "OFFLINE"
                            color: context.online ? root.theme.green : root.theme.muted
                            font.pixelSize: 7
                            font.weight: Font.DemiBold
                        }
                    }
                }

                NavButton {
                    targetSection: 5
                    title: "GOOSE"
                    countText: context.loaded ? String(context.gooseCount) : ""
                }

                NavButton {
                    targetSection: 2
                    title: "Reports"
                    countText: context.loaded ? String(context.reportCount) : ""
                }

                Loader {
                    Layout.fillWidth: true
                    active: root.section === 2 && reports.reportControls.length > 0
                    visible: active
                    sourceComponent: Component {
                        ColumnLayout {
                            spacing: 0
                            Repeater {
                                model: reports.reportControls
                                delegate: Rectangle {
                                    required property int index
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 27
                                    color: reports.selectedRcbIndex === index
                                           ? root.theme.surfaceRaised : reportMouse.containsMouse ? root.theme.surface : "transparent"
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 28
                                        anchors.rightMargin: 8
                                        spacing: 5
                                        Label {
                                            text: modelData.buffered ? "B" : "U"
                                            color: modelData.buffered ? root.theme.amber : root.theme.accent
                                            font.pixelSize: 7
                                            font.weight: Font.Bold
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.reference || "Report"
                                            color: root.theme.textSoft
                                            font.pixelSize: 8
                                            elide: Text.ElideMiddle
                                        }
                                    }
                                    MouseArea {
                                        id: reportMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: {
                                            reports.selectRcb(index)
                                            root.choose(2, "Reports")
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                NavButton {
                    targetSection: 3
                    title: "Setting Groups"
                    countText: context.loaded ? String(context.settingGroupCount) : ""
                }

                Loader {
                    Layout.fillWidth: true
                    active: root.section === 3 && utilities.settingGroupCount > 0
                    visible: active
                    sourceComponent: Component {
                        ColumnLayout {
                            spacing: 0
                            Repeater {
                                model: utilities.settingGroups
                                delegate: Rectangle {
                                    required property int index
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 27
                                    color: utilities.selectedSettingGroupIndex === index
                                           ? root.theme.surfaceRaised : sgMouse.containsMouse ? root.theme.surface : "transparent"
                                    Label {
                                        anchors.fill: parent
                                        anchors.leftMargin: 28
                                        anchors.rightMargin: 8
                                        verticalAlignment: Text.AlignVCenter
                                        text: modelData.reference || "SGCB"
                                        color: root.theme.textSoft
                                        font.pixelSize: 8
                                        elide: Text.ElideMiddle
                                    }
                                    MouseArea {
                                        id: sgMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: {
                                            utilities.selectSettingGroup(index)
                                            root.choose(3, "Setting Groups")
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                NavButton {
                    targetSection: 4
                    title: "Files"
                    countText: utilities.connected && utilities.fileEntryCount > 0
                               ? String(utilities.fileEntryCount) : ""
                }

                NavButton {
                    targetSection: 1
                    title: "DataSets"
                    countText: context.loaded ? String(context.dataSetCount) : ""
                }

                Loader {
                    Layout.fillWidth: true
                    active: root.section === 1 && reports.dataSets.length > 0
                    visible: active
                    sourceComponent: Component {
                        ColumnLayout {
                            spacing: 0
                            Repeater {
                                model: reports.dataSets
                                delegate: Rectangle {
                                    required property int index
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 27
                                    color: reports.selectedDataSetIndex === index
                                           ? root.theme.surfaceRaised : dsMouse.containsMouse ? root.theme.surface : "transparent"
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 28
                                        anchors.rightMargin: 8
                                        spacing: 5
                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.reference || "DataSet"
                                            color: root.theme.textSoft
                                            font.pixelSize: 8
                                            elide: Text.ElideMiddle
                                        }
                                        Label {
                                            text: modelData.members ? String(modelData.members.length) : ""
                                            color: root.theme.muted
                                            font.pixelSize: 7
                                        }
                                    }
                                    MouseArea {
                                        id: dsMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: {
                                            reports.selectDataSet(index)
                                            root.choose(1, "DataSets")
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                NavButton {
                    targetSection: 0
                    title: "Data Model"
                    countText: context.loaded ? String(context.treeModel.totalNodeCount) : ""
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root.section === 0
                    spacing: 0

                    TextField {
                        id: modelSearch
                        Layout.fillWidth: true
                        Layout.leftMargin: 8
                        Layout.rightMargin: 8
                        Layout.topMargin: 5
                        Layout.bottomMargin: 5
                        placeholderText: "Filter model"
                        enabled: context.loaded
                        onTextChanged: searchDebounce.restart()
                    }

                    Timer {
                        id: searchDebounce
                        interval: 100
                        repeat: false
                        onTriggered: context.treeModel.filterText = modelSearch.text
                    }

                    ListView {
                        id: modelTree
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(260, Math.min(contentHeight, 620))
                        clip: true
                        reuseItems: true
                        cacheBuffer: 0
                        model: context.treeModel
                        ScrollBar.vertical: ScrollBar {}

                        delegate: Rectangle {
                            id: modelRow
                            required property int index
                            width: modelTree.width
                            height: 25
                            color: model.selected ? root.theme.accentSoft
                                                  : modelMouse.containsMouse ? root.theme.surfaceRaised : "transparent"
                            border.width: model.selected ? 1 : 0
                            border.color: root.theme.accent

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 30 + model.depth * 12
                                anchors.rightMargin: 7
                                spacing: 4
                                Label {
                                    Layout.preferredWidth: 11
                                    text: model.hasChildren ? (model.expanded ? "▾" : "▸") : ""
                                    color: root.theme.muted
                                    font.pixelSize: 9
                                }
                                Label {
                                    Layout.preferredWidth: 22
                                    text: model.kind
                                    color: model.kind === "DA" ? root.theme.accent : root.theme.muted
                                    font.pixelSize: 7
                                    font.weight: Font.DemiBold
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: model.label
                                    color: root.theme.textSoft
                                    font.pixelSize: 8
                                    elide: Text.ElideMiddle
                                }
                                Label {
                                    Layout.preferredWidth: 34
                                    text: model.functionalConstraint || ""
                                    color: root.theme.muted
                                    font.pixelSize: 7
                                    horizontalAlignment: Text.AlignRight
                                }
                            }

                            MouseArea {
                                id: modelMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: function(event) {
                                    context.treeModel.selectRow(index)
                                    if (model.hasChildren && event.x < 44 + model.depth * 12)
                                        context.treeModel.toggle(index)
                                    root.choose(0, "Data Model")
                                }
                                onDoubleClicked: {
                                    if (model.hasChildren)
                                        context.treeModel.toggle(index)
                                    else if (client.connected)
                                        client.readEngineeringSelected()
                                }
                            }
                        }
                    }
                }

                Item { Layout.preferredHeight: 12 }
            }
        }
    }
}
