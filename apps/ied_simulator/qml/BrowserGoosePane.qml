// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    required property var theme
    required property var context
    required property var engineering

    property var visibleStreams: {
        var all = engineering.gooseStreams
        if (!context.iedName.length)
            return all
        var filtered = []
        for (var index = 0; index < all.length; ++index) {
            if (String(all[index].iedName) === context.iedName)
                filtered.push(all[index])
        }
        return filtered
    }
    property int selectedIndex: -1
    property var selectedStream: selectedIndex >= 0 && selectedIndex < visibleStreams.length
                                 ? visibleStreams[selectedIndex] : ({})

    onVisibleStreamsChanged: {
        if (!visibleStreams.length)
            selectedIndex = -1
        else if (selectedIndex < 0 || selectedIndex >= visibleStreams.length)
            selectedIndex = 0
    }

    color: theme.background

    function text(value) {
        return value === undefined || value === null || String(value).length === 0 ? "—" : String(value)
    }

    Connections {
        target: engineering
        function onWorkspaceChanged() {
            root.selectedIndex = root.visibleStreams.length > 0 ? 0 : -1
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: Math.max(300, root.width * 0.32)
            Layout.fillHeight: true
            color: theme.chrome
            border.width: 1
            border.color: theme.lineSoft

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 12
                    spacing: 2
                    Label {
                        text: "Configured GOOSE"
                        color: theme.text
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.fillWidth: true
                        text: engineering.loaded
                              ? engineering.sourceName + " · " + root.visibleStreams.length + " stream(s)"
                              : "No engineering model loaded"
                        color: theme.muted
                        font.pixelSize: 8
                        elide: Text.ElideMiddle
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    reuseItems: true
                    model: root.visibleStreams
                    ScrollBar.vertical: ScrollBar {}

                    delegate: Rectangle {
                        required property int index
                        required property var modelData
                        width: ListView.view.width
                        height: 56
                        color: root.selectedIndex === index ? theme.accentSoft
                              : streamMouse.containsMouse ? theme.surfaceRaised : "transparent"
                        border.width: root.selectedIndex === index ? 1 : 0
                        border.color: theme.accent

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 8
                            anchors.topMargin: 6
                            anchors.bottomMargin: 6
                            spacing: 1
                            Label {
                                Layout.fillWidth: true
                                text: modelData.reference || modelData.name || "GOOSE"
                                color: theme.text
                                font.pixelSize: 9
                                font.weight: Font.DemiBold
                                elide: Text.ElideMiddle
                            }
                            Label {
                                Layout.fillWidth: true
                                text: (modelData.iedName || "") + (modelData.dataSet ? " · " + modelData.dataSet : "")
                                color: theme.muted
                                font.pixelSize: 8
                                elide: Text.ElideMiddle
                            }
                            Label {
                                Layout.fillWidth: true
                                text: (modelData.appId || "APPID —") + " · " + (modelData.destinationMac || "MAC —")
                                color: theme.muted
                                font.pixelSize: 7
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            id: streamMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.selectedIndex = index
                        }
                    }
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth

            ColumnLayout {
                width: Math.max(0, parent.width - 36)
                x: 18
                spacing: 10

                Label {
                    text: root.text(root.selectedStream.reference)
                    color: theme.text
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.fillWidth: true
                    text: root.selectedIndex >= 0
                          ? root.text(root.selectedStream.goId)
                          : "Select a configured GOOSE stream."
                    color: theme.muted
                    font.pixelSize: 9
                    wrapMode: Text.WrapAnywhere
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 18
                    rowSpacing: 8

                    Label { text: "IED"; color: theme.muted; font.pixelSize: 9 }
                    Label { text: root.text(root.selectedStream.iedName); color: theme.textSoft; font.pixelSize: 9 }
                    Label { text: "DataSet"; color: theme.muted; font.pixelSize: 9 }
                    Label { Layout.fillWidth: true; text: root.text(root.selectedStream.dataSet); color: theme.textSoft; font.pixelSize: 9; elide: Text.ElideMiddle }
                    Label { text: "ConfRev"; color: theme.muted; font.pixelSize: 9 }
                    Label { text: root.text(root.selectedStream.confRev); color: theme.textSoft; font.pixelSize: 9 }
                    Label { text: "APPID"; color: theme.muted; font.pixelSize: 9 }
                    Label { text: root.text(root.selectedStream.appId); color: theme.textSoft; font.pixelSize: 9 }
                    Label { text: "Destination MAC"; color: theme.muted; font.pixelSize: 9 }
                    Label { text: root.text(root.selectedStream.destinationMac); color: theme.textSoft; font.pixelSize: 9 }
                    Label { text: "VLAN"; color: theme.muted; font.pixelSize: 9 }
                    Label {
                        text: root.selectedStream.vlanId === undefined || root.selectedStream.vlanId === null
                              ? "—"
                              : String(root.selectedStream.vlanId) + " · priority " + root.text(root.selectedStream.vlanPriority)
                        color: theme.textSoft
                        font.pixelSize: 9
                    }
                    Label { text: "Retransmission"; color: theme.muted; font.pixelSize: 9 }
                    Label {
                        text: root.selectedIndex >= 0
                              ? root.text(root.selectedStream.minTimeMs) + " / " + root.text(root.selectedStream.maxTimeMs) + " ms"
                              : "—"
                        color: theme.textSoft
                        font.pixelSize: 9
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "Configured DataSet members"
                        color: theme.text
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        text: root.selectedStream.members ? String(root.selectedStream.members.length) : "0"
                        color: theme.muted
                        font.pixelSize: 8
                    }
                }

                Repeater {
                    model: root.selectedStream.members || []
                    delegate: Rectangle {
                        required property int index
                        required property string modelData
                        Layout.fillWidth: true
                        implicitHeight: 30
                        color: index % 2 ? theme.surfaceSoft : theme.surface
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 8
                            Label {
                                Layout.preferredWidth: 30
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

                Label {
                    Layout.fillWidth: true
                    text: "Engineering-model evidence only. Live Ethernet capture remains in Sniffer; publication remains in IED Simulator."
                    color: theme.muted
                    font.pixelSize: 8
                    wrapMode: Text.WordWrap
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
