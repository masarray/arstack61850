// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var theme
    required property var session
    required property var client
    required property var reports
    required property var utilities
    required property var engineering

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

    function configuredGooseCount() {
        var streams = engineering.gooseStreams
        if (!client.iedName.length)
            return streams.length
        var count = 0
        for (var index = 0; index < streams.length; ++index) {
            if (String(streams[index].iedName) === client.iedName)
                ++count
        }
        return count
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
            anchors.leftMargin: 10
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
            Layout.preferredHeight: 56
            color: root.theme.statusChrome
            border.width: 1
            border.color: root.theme.lineSoft

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                anchors.topMargin: 7
                anchors.bottomMargin: 7
                spacing: 1
                Label {
                    Layout.fillWidth: true
                    text: client.iedName.length ? client.iedName : "IED"
                    color: root.theme.text
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: session.endpoint
                    color: root.theme.muted
                    font.pixelSize: 8
                    elide: Text.ElideMiddle
                }
                Label {
                    Layout.fillWidth: true
                    text: session.connected
                          ? client.logicalDeviceCount + " LD · " + client.logicalNodeCount + " LN"
                          : "Offline"
                    color: session.connected ? root.theme.green : root.theme.muted
                    font.pixelSize: 8
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

                NavButton {
                    targetSection: 5
                    title: "GOOSE"
                    countText: engineering.loaded ? String(root.configuredGooseCount()) : ""
                }

                NavButton {
                    targetSection: 2
                    title: "Reports"
                    countText: reports.connected ? String(reports.reportControls.length) : ""
                }

                Loader {
                    Layout.fillWidth: true
                    active: root.section === 2 && reports.connected
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
                    countText: utilities.connected ? String(utilities.settingGroupCount) : ""
                }

                Loader {
                    Layout.fillWidth: true
                    active: root.section === 3 && utilities.connected
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
                    countText: reports.connected ? String(reports.dataSets.length) : ""
                }

                Loader {
                    Layout.fillWidth: true
                    active: root.section === 1 && reports.connected
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
                    countText: client.connected ? String(client.treeModel.totalNodeCount) : ""
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
                        enabled: client.connected
                        onTextChanged: searchDebounce.restart()
                    }

                    Timer {
                        id: searchDebounce
                        interval: 100
                        repeat: false
                        onTriggered: client.treeModel.filterText = modelSearch.text
                    }

                    ListView {
                        id: modelTree
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(260, Math.min(contentHeight, 620))
                        clip: true
                        reuseItems: true
                        cacheBuffer: 0
                        model: client.treeModel
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
                                anchors.leftMargin: 18 + model.depth * 12
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
                                    client.treeModel.selectRow(index)
                                    if (model.hasChildren && event.x < 44 + model.depth * 12)
                                        client.treeModel.toggle(index)
                                    root.choose(0, "Data Model")
                                }
                                onDoubleClicked: {
                                    if (model.hasChildren)
                                        client.treeModel.toggle(index)
                                    else
                                        client.readSelected()
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
