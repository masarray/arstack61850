// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ARStack.IedSimulator 1.0

Item {
    id: root
    required property var theme
    required property GooseMonitorController monitor

    FileDialog {
        id: savePcapDialog
        title: "Save retained GOOSE capture"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pcap"
        nameFilters: ["PCAP capture (*.pcap)", "All files (*)"]
        onAccepted: root.monitor.exportPcap(selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    text: "GOOSE Sniffer"
                    color: root.theme.text
                    font.pixelSize: root.theme.titleSize
                    font.weight: Font.DemiBold
                }
                Label {
                    text: "Passive Layer-2 capture · EtherType 0x88B8 · decode · supervision · bounded PCAP retention"
                    color: root.theme.muted
                    font.pixelSize: root.theme.captionSize
                }
            }

            Rectangle {
                implicitWidth: 126
                implicitHeight: 28
                radius: 5
                color: root.monitor.capturing ? root.theme.greenSoft : root.theme.surfaceSoft
                border.width: 1
                border.color: root.monitor.capturing ? root.theme.green : root.theme.line
                Label {
                    anchors.centerIn: parent
                    text: root.monitor.capturing ? "CAPTURE LIVE" : "STOPPED"
                    color: root.monitor.capturing ? root.theme.green : root.theme.textSoft
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            radius: root.theme.controlRadius
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                Label {
                    text: "Ethernet"
                    color: root.theme.textSoft
                    font.pixelSize: root.theme.captionSize
                    font.weight: Font.DemiBold
                }

                ComboBox {
                    id: interfaceSelector
                    Layout.preferredWidth: 240
                    model: root.monitor.interfaces
                    enabled: !root.monitor.capturing
                    currentIndex: Math.max(0, root.monitor.interfaces.indexOf(root.monitor.interfaceName))
                    onActivated: root.monitor.interfaceName = currentText
                    Component.onCompleted: {
                        if (root.monitor.interfaceName.length === 0 && count > 0)
                            root.monitor.interfaceName = currentText
                    }
                }

                ActionButton {
                    theme: root.theme
                    text: root.monitor.capturing ? "Stop Capture" : "Start Capture"
                    primary: !root.monitor.capturing
                    danger: root.monitor.capturing
                    onClicked: root.monitor.capturing ? root.monitor.stopCapture() : root.monitor.startCapture()
                }
                ActionButton {
                    theme: root.theme
                    text: "Clear"
                    onClicked: root.monitor.clear()
                }
                ActionButton {
                    theme: root.theme
                    text: "Save PCAP"
                    enabled: root.monitor.retainedPacketCount > 0
                    onClicked: savePcapDialog.open()
                }

                Item { Layout.fillWidth: true }

                Label {
                    text: "Streams " + root.monitor.streamCount + "/" + root.monitor.streamCapacity
                          + "   Packets " + root.monitor.packetCount
                          + "   Timeout " + root.monitor.timeoutCount
                          + "   Seq issues " + root.monitor.sequenceIssueCount
                    color: root.theme.textSoft
                    font.pixelSize: root.theme.captionSize
                }
            }
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            Rectangle {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 620
                color: root.theme.surface
                border.width: 1
                border.color: root.theme.lineSoft
                radius: root.theme.panelRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "Captured streams"
                            color: root.theme.text
                            font.pixelSize: root.theme.subtitleSize
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: root.monitor.statusText
                            color: root.monitor.capturing ? root.theme.green : root.theme.muted
                            font.pixelSize: root.theme.captionSize
                            elide: Text.ElideRight
                            Layout.maximumWidth: 360
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 26
                        color: root.theme.chrome
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 6
                            Label { Layout.preferredWidth: 72; text: "APPID"; color: root.theme.muted; font.pixelSize: 9 }
                            Label { Layout.preferredWidth: 120; text: "Source"; color: root.theme.muted; font.pixelSize: 9 }
                            Label { Layout.fillWidth: true; text: "gocbRef / DataSet"; color: root.theme.muted; font.pixelSize: 9 }
                            Label { Layout.preferredWidth: 56; text: "st/sq"; color: root.theme.muted; font.pixelSize: 9 }
                            Label { Layout.preferredWidth: 110; text: "Status"; color: root.theme.muted; font.pixelSize: 9 }
                        }
                    }

                    ListView {
                        id: streamList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        reuseItems: true
                        cacheBuffer: 0
                        spacing: 2
                        model: root.monitor

                        delegate: Rectangle {
                            width: streamList.width
                            height: 46
                            radius: 5
                            color: timedOut ? root.theme.redSoft
                                            : anomaly ? root.theme.amberSoft
                                                      : root.monitor.selectedRow === index
                                                        ? root.theme.accentSoft : root.theme.surface
                            border.width: root.monitor.selectedRow === index ? 1 : 0
                            border.color: root.theme.accent

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 6
                                Label {
                                    Layout.preferredWidth: 72
                                    text: appIdText
                                    color: root.theme.text
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                }
                                Label {
                                    Layout.preferredWidth: 120
                                    text: sourceMac
                                    color: root.theme.textSoft
                                    font.pixelSize: 9
                                    elide: Text.ElideRight
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Label {
                                        Layout.fillWidth: true
                                        text: goCbRef
                                        color: root.theme.text
                                        font.pixelSize: 10
                                        elide: Text.ElideMiddle
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: dataSetReference
                                        color: root.theme.muted
                                        font.pixelSize: 9
                                        elide: Text.ElideMiddle
                                    }
                                }
                                Label {
                                    Layout.preferredWidth: 56
                                    text: stNum + "/" + sqNum
                                    color: root.theme.textSoft
                                    font.pixelSize: 10
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    text: status
                                    color: timedOut ? root.theme.red : anomaly ? root.theme.amber : root.theme.green
                                    font.pixelSize: 9
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: root.monitor.select(index)
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 174
                        radius: 6
                        color: root.theme.chrome
                        border.width: 1
                        border.color: root.theme.lineSoft

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 10

                            ColumnLayout {
                                Layout.preferredWidth: 270
                                Layout.fillHeight: true
                                spacing: 3
                                Label { text: "Selected stream"; color: root.theme.text; font.pixelSize: 11; font.weight: Font.DemiBold }
                                Label { text: root.monitor.selectedStream.goId || "—"; color: root.theme.textSoft; font.pixelSize: 10; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                Label { text: "Dst " + (root.monitor.selectedStream.destinationMac || "—"); color: root.theme.muted; font.pixelSize: 9 }
                                Label { text: "VLAN " + (root.monitor.selectedStream.vlanId === undefined ? "—" : root.monitor.selectedStream.vlanId)
                                              + "  PCP " + (root.monitor.selectedStream.vlanPriority === undefined ? "—" : root.monitor.selectedStream.vlanPriority); color: root.theme.muted; font.pixelSize: 9 }
                                Label { text: "ConfRev " + (root.monitor.selectedStream.confRev || "—")
                                              + "  TTL " + (root.monitor.selectedStream.ttlMs || "—") + " ms"; color: root.theme.muted; font.pixelSize: 9 }
                                Label { text: "Frames " + (root.monitor.selectedStream.packets || 0)
                                              + "  Values " + (root.monitor.selectedStream.valueCount || 0); color: root.theme.muted; font.pixelSize: 9 }
                                Item { Layout.fillHeight: true }
                            }

                            Rectangle { Layout.fillHeight: true; width: 1; color: root.theme.lineSoft }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                spacing: 3
                                Label { text: "Decoded allData"; color: root.theme.text; font.pixelSize: 11; font.weight: Font.DemiBold }
                                ListView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    model: root.monitor.selectedValues
                                    delegate: Label {
                                        width: ListView.view.width
                                        text: modelData
                                        color: root.theme.textSoft
                                        font.pixelSize: 9
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                SplitView.preferredWidth: 360
                SplitView.minimumWidth: 320
                color: root.theme.surface
                border.width: 1
                border.color: root.theme.lineSoft
                radius: root.theme.panelRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    Label {
                        text: "Capture diagnostics"
                        color: root.theme.text
                        font.pixelSize: root.theme.subtitleSize
                        font.weight: Font.DemiBold
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 10
                        rowSpacing: 4
                        Label { text: "Decode errors"; color: root.theme.muted; font.pixelSize: 9 }
                        Label { text: String(root.monitor.decodeErrorCount); color: root.theme.textSoft; font.pixelSize: 9; Layout.alignment: Qt.AlignRight }
                        Label { text: "Rejected frames"; color: root.theme.muted; font.pixelSize: 9 }
                        Label { text: String(root.monitor.rejectedFrameCount); color: root.theme.textSoft; font.pixelSize: 9; Layout.alignment: Qt.AlignRight }
                        Label { text: "Dropped streams"; color: root.theme.muted; font.pixelSize: 9 }
                        Label { text: String(root.monitor.droppedStreamCount); color: root.theme.textSoft; font.pixelSize: 9; Layout.alignment: Qt.AlignRight }
                        Label { text: "Duplicates"; color: root.theme.muted; font.pixelSize: 9 }
                        Label { text: String(root.monitor.duplicateCount); color: root.theme.textSoft; font.pixelSize: 9; Layout.alignment: Qt.AlignRight }
                        Label { text: "Retained PCAP"; color: root.theme.muted; font.pixelSize: 9 }
                        Label { text: root.monitor.retainedPacketCount + "/" + root.monitor.retainedPacketCapacity; color: root.theme.textSoft; font.pixelSize: 9; Layout.alignment: Qt.AlignRight }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: root.theme.lineSoft }

                    Label {
                        text: "Recent events"
                        color: root.theme.text
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }

                    ListView {
                        id: eventList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        reuseItems: true
                        cacheBuffer: 0
                        model: root.monitor.recentEvents

                        delegate: Rectangle {
                            width: eventList.width
                            height: eventText.implicitHeight + 12
                            color: index % 2 ? root.theme.chrome : "transparent"
                            Label {
                                id: eventText
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 6
                                text: modelData
                                color: root.theme.textSoft
                                font.pixelSize: 8
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Sniffer is passive. GOOSE publication and stimulation belong to IED Simulator commissioning."
                        color: root.theme.muted
                        font.pixelSize: 8
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
