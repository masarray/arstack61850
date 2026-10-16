// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ARStack.IedSimulator 1.0

Rectangle {
    id: root
    required property var theme
    required property var context
    required property GooseMonitorController monitor

    // One shared monitor is owned by Main.qml/Sniffer. This pane never
    // starts an independent raw socket, MMS Read, or background poller.
    property int evidenceGeneration: 0
    Connections {
        target: root.monitor
        function onMonitorChanged() { root.evidenceGeneration++ }
    }
    property var observation: {
        const generation = root.evidenceGeneration
        if (root.selectedIndex < 0 || !root.selectedStream.reference)
            return ({matched: false, reason: "Select a configured GOOSE stream"})
        return root.monitor.observedForStream(
            root.selectedStream.reference || "",
            root.selectedStream.dataSet || "",
            root.selectedStream.appId || "",
            root.selectedStream.destinationMac || "")
    }

    property var visibleStreams: context.gooseStreams
    property int selectedIndex: -1
    property var selectedStream: selectedIndex >= 0 && selectedIndex < visibleStreams.length
                                 ? visibleStreams[selectedIndex] : ({})

    color: theme.background

    function text(value) {
        return value === undefined || value === null || String(value).length === 0 ? "—" : String(value)
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            visible: root.selectedIndex < 0
            Layout.preferredWidth: visible ? root.width : 0
            Layout.fillWidth: visible
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
                        text: context.loaded
                              ? context.sourceName + " · " + root.visibleStreams.length + " stream(s)"
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
            visible: root.selectedIndex >= 0
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

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: subscriptionPanel.implicitHeight + 16
                    color: theme.surface
                    border.width: 1
                    border.color: root.observation.matched
                                  ? (root.observation.timedOut ? theme.amber : theme.green)
                                  : theme.line

                    ColumnLayout {
                        id: subscriptionPanel
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 8
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: "GOOSE Subscribe · read-only observation"
                                color: theme.text
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: root.monitor.capturing ? "CAPTURING" : "STOPPED"
                                color: root.monitor.capturing ? theme.green : theme.muted
                                font.pixelSize: 8
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: "NIC"
                                color: theme.muted
                                font.pixelSize: 9
                            }
                            ComboBox {
                                id: gooseNicSelector
                                Layout.fillWidth: true
                                model: root.monitor.interfaces
                                enabled: !root.monitor.capturing
                                currentIndex: root.monitor.interfaces.indexOf(root.monitor.interfaceName)
                                displayText: currentIndex < 0 ? "Select capture interface" : currentText
                                onActivated: root.monitor.interfaceName = currentText
                            }
                            Button {
                                text: root.monitor.capturing ? "Stop" : "GOOSE Subscribe"
                                enabled: root.monitor.capturing || root.monitor.interfaceName.length > 0
                                onClicked: root.monitor.capturing
                                           ? root.monitor.stopCapture()
                                           : root.monitor.startCapture()
                                ToolTip.visible: hovered
                                ToolTip.text: "Starts/stops the shared passive Ethernet capture; does not send GOOSE or MMS commands."
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: root.observation.matched
                                  ? (root.observation.reason + " · " + root.observation.appIdText
                                     + " · st/sq " + root.observation.stNum + "/" + root.observation.sqNum
                                     + " · frames " + root.observation.packets)
                                  : (root.monitor.capturing
                                      ? "Waiting for matching Ethernet GOOSE · " + root.observation.reason
                                      : "No live capture · select NIC and press GOOSE Subscribe")
                            color: root.observation.matched && !root.observation.timedOut
                                   ? theme.green : theme.textSoft
                            font.pixelSize: 9
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: root.observation.matched &&
                                     root.selectedStream.members &&
                                     root.selectedStream.members.length !== root.observation.valueCount
                            text: "DataSet count mismatch · configured "
                                  + root.selectedStream.members.length
                                  + " · observed " + root.observation.valueCount
                                  + " (do not assume aligned signals)"
                            color: theme.amber
                            font.pixelSize: 9
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            Layout.fillWidth: true
                            text: root.monitor.statusText
                            color: theme.muted
                            font.pixelSize: 8
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: gooseAttributes.implicitHeight
                    color: theme.surface
                    border.width: 1
                    border.color: theme.line

                    ColumnLayout {
                        id: gooseAttributes
                        anchors.left: parent.left
                        anchors.right: parent.right
                        spacing: 0
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 26
                            color: theme.surfaceRaised
                            Label {
                                anchors.fill: parent
                                leftPadding: 8
                                verticalAlignment: Text.AlignVCenter
                                text: "▾  Control block attributes"
                                color: theme.text
                                font.pixelSize: 8
                                font.weight: Font.DemiBold
                            }
                        }
                        Repeater {
                            model: [
                                ["IED", root.selectedStream.iedName],
                                ["Control block reference", root.selectedStream.reference],
                                ["Destination MAC address", root.selectedStream.destinationMac],
                                ["Application ID", root.selectedStream.appId],
                                ["GOOSE ID", root.selectedStream.goId],
                                ["DataSet reference", root.selectedStream.dataSet],
                                ["VLAN ID", root.selectedStream.vlanId],
                                ["VLAN priority", root.selectedStream.vlanPriority],
                                ["Configuration revision", root.selectedStream.confRev],
                                ["Minimum retransmission (ms)", root.selectedStream.minTimeMs],
                                ["Maximum retransmission (ms)", root.selectedStream.maxTimeMs]
                            ]
                            delegate: Rectangle {
                                required property int index
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: 25
                                color: index % 2 ? theme.surfaceSoft : theme.surface
                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 0
                                    Label {
                                        Layout.preferredWidth: 190
                                        Layout.fillHeight: true
                                        leftPadding: 7
                                        verticalAlignment: Text.AlignVCenter
                                        text: modelData[0]
                                        color: theme.textSoft
                                        font.pixelSize: 8
                                    }
                                    Rectangle {
                                        Layout.preferredWidth: 1
                                        Layout.fillHeight: true
                                        color: theme.lineSoft
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        leftPadding: 7
                                        rightPadding: 7
                                        verticalAlignment: Text.AlignVCenter
                                        text: root.text(modelData[1])
                                        color: theme.text
                                        font.pixelSize: 8
                                        elide: Text.ElideMiddle
                                    }
                                }
                            }
                        }
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
                            Label {
                                Layout.preferredWidth: 175
                                visible: root.observation.matched
                                text: root.observation.valueCount === (root.selectedStream.members || []).length
                                      && index < root.observation.values.length
                                      ? root.observation.values[index]
                                      : "Unresolved · count mismatch"
                                color: root.observation.timedOut ? theme.amber : theme.green
                                font.pixelSize: 9
                                elide: Text.ElideRight
                            }
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.observation.matched
                             && (!root.selectedStream.members || root.selectedStream.members.length === 0)
                    text: "Unbound wire allData (ordered, no guessed signal names)"
                    color: theme.textSoft
                    font.pixelSize: 9
                }
                Repeater {
                    model: root.observation.matched
                           && (!root.selectedStream.members || root.selectedStream.members.length === 0)
                           ? root.observation.values : []
                    delegate: Label {
                        required property string modelData
                        Layout.fillWidth: true
                        text: modelData
                        color: theme.text
                        font.pixelSize: 9
                        elide: Text.ElideRight
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: "Observed values come from the shared passive Sniffer, bound only by exact GOOSE identity. No GOOSE publish, MMS writes or implicit reads."
                    color: theme.muted
                    font.pixelSize: 8
                    wrapMode: Text.WordWrap
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
