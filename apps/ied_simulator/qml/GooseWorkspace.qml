// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ARStack.IedSimulator 1.0

Item {
    id: root
    required property var theme
    required property IedFleetController simulator
    required property GooseMonitorController monitor
    signal openSimulatorRequested()

    IedCommissioningModel {
        id: publisherModel
        backend: root.simulator
        kindFilter: "GOOSE"
    }

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
                    text: "GOOSE Monitor + Publisher"
                    color: root.theme.text
                    font.pixelSize: root.theme.titleSize
                    font.weight: Font.DemiBold
                }
                Label {
                    text: "One explicit NIC · EtherType 0x88B8 · canonical publisher state · bounded live supervision"
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
                    text: root.monitor.capturing ? "MONITOR LIVE" : "MONITOR STOPPED"
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
                    Layout.preferredWidth: 220
                    model: root.monitor.interfaces
                    enabled: !root.monitor.capturing
                    currentIndex: Math.max(0, root.monitor.interfaces.indexOf(root.monitor.interfaceName))
                    onActivated: {
                        const selected = currentText
                        root.monitor.interfaceName = selected
                        publisherModel.gooseInterfaceName = selected
                    }
                    Component.onCompleted: {
                        if (root.monitor.interfaceName.length === 0 && count > 0) {
                            root.monitor.interfaceName = currentText
                            publisherModel.gooseInterfaceName = currentText
                        }
                    }
                }

                ActionButton {
                    theme: root.theme
                    text: root.monitor.capturing ? "Stop Monitor" : "Start Monitor"
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
                SplitView.minimumWidth: 560
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
                            text: "Live Monitor"
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
                            Layout.maximumWidth: 320
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
                                Layout.preferredWidth: 250
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
                SplitView.preferredWidth: 430
                SplitView.minimumWidth: 360
                color: root.theme.surface
                border.width: 1
                border.color: root.theme.lineSoft
                radius: root.theme.panelRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "Publisher"
                            color: root.theme.text
                            font.pixelSize: root.theme.subtitleSize
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: publisherModel.goosePublisherCount + " active"
                            color: publisherModel.goosePublishing ? root.theme.green : root.theme.muted
                            font.pixelSize: root.theme.captionSize
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: !root.simulator.imported
                        text: "Load an SCL/CID/SCD in Simulator to expose configured GSEControl publishers. Monitoring remains independent and can run without a simulator model."
                        wrapMode: Text.WordWrap
                        color: root.theme.muted
                        font.pixelSize: root.theme.captionSize
                    }

                    ListView {
                        id: publisherList
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(190, contentHeight)
                        Layout.minimumHeight: root.simulator.imported ? 72 : 0
                        clip: true
                        reuseItems: true
                        cacheBuffer: 0
                        model: publisherModel.itemCount
                        delegate: Rectangle {
                            required property int index
                            property var itemData: {
                                publisherModel.revision
                                return publisherModel.item(index)
                            }
                            width: publisherList.width
                            height: 54
                            radius: 5
                            color: itemData.selected ? root.theme.accentSoft : root.theme.chrome
                            border.width: itemData.selected ? 1 : 0
                            border.color: root.theme.accent
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 7
                                spacing: 1
                                Label { Layout.fillWidth: true; text: itemData.reference || itemData.name || "GOOSE"; color: root.theme.text; font.pixelSize: 10; font.weight: Font.DemiBold; elide: Text.ElideMiddle }
                                Label { Layout.fillWidth: true; text: (itemData.appId || "APPID —") + " · " + (itemData.mac || "MAC —") + " · " + (itemData.memberCount || 0) + " members"; color: root.theme.muted; font.pixelSize: 9; elide: Text.ElideRight }
                            }
                            MouseArea { anchors.fill: parent; onClicked: publisherModel.select(index) }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 112
                        radius: 6
                        color: root.theme.chrome
                        border.width: 1
                        border.color: root.theme.lineSoft
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 3
                            Label { text: publisherModel.selectedItem.goId || "No GOOSE publisher selected"; color: root.theme.text; font.pixelSize: 11; font.weight: Font.DemiBold; elide: Text.ElideMiddle; Layout.fillWidth: true }
                            Label { text: publisherModel.selectedItem.dataSetReference || "—"; color: root.theme.textSoft; font.pixelSize: 9; elide: Text.ElideMiddle; Layout.fillWidth: true }
                            Label { text: "ConfRev " + (publisherModel.selectedItem.confRev || "—") + " · Min/Max " + (publisherModel.selectedItem.minTimeMs || "—") + "/" + (publisherModel.selectedItem.maxTimeMs || "—") + " ms"; color: root.theme.muted; font.pixelSize: 9 }
                            Label { text: publisherModel.goosePublicationStatus; color: publisherModel.goosePublishing ? root.theme.green : root.theme.muted; font.pixelSize: 9; elide: Text.ElideRight; Layout.fillWidth: true }
                            Label { text: "TX " + publisherModel.gooseTransmitCount + " · state changes " + publisherModel.gooseStateChangeCount + " · stNum " + (publisherModel.selectedGooseRuntime.stNum || "—"); color: root.theme.muted; font.pixelSize: 9 }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        ActionButton {
                            theme: root.theme
                            text: publisherModel.selectedGooseRuntime.active ? "Stop Publisher" : "Start Publisher"
                            primary: !publisherModel.selectedGooseRuntime.active
                            danger: publisherModel.selectedGooseRuntime.active
                            enabled: root.simulator.imported && publisherModel.itemCount > 0
                            onClicked: publisherModel.selectedGooseRuntime.active
                                       ? publisherModel.stopSelectedGoosePublication()
                                       : publisherModel.startSelectedGoosePublication()
                        }
                        ActionButton {
                            theme: root.theme
                            text: "Edit Source Value"
                            enabled: publisherModel.firstDrivableMember() >= 0
                            onClicked: {
                                const row = publisherModel.firstDrivableMember()
                                if (row >= 0 && publisherModel.focusMemberValue(row)) root.openSimulatorRequested()
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Publisher stimulus always edits the canonical Simulator point store. This workspace does not maintain a GOOSE-only value copy."
                        wrapMode: Text.WordWrap
                        color: root.theme.muted
                        font.pixelSize: 9
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: root.theme.lineSoft
                    }

                    Label { text: "Configured DataSet members"; color: root.theme.text; font.pixelSize: 11; font.weight: Font.DemiBold }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        reuseItems: true
                        cacheBuffer: 0
                        model: publisherModel.memberCount
                        delegate: Rectangle {
                            required property int index
                            property var memberData: {
                                publisherModel.revision
                                return publisherModel.member(index)
                            }
                            width: ListView.view.width
                            height: 40
                            color: "transparent"
                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 0
                                Label { Layout.fillWidth: true; text: memberData.reference || memberData.mmsItem || "—"; color: root.theme.textSoft; font.pixelSize: 9; elide: Text.ElideMiddle }
                                Label { Layout.fillWidth: true; text: (memberData.fc || "—") + " · " + (memberData.type || memberData.cdc || "—"); color: root.theme.muted; font.pixelSize: 8; elide: Text.ElideRight }
                            }
                            MouseArea {
                                anchors.fill: parent
                                onDoubleClicked: {
                                    if (publisherModel.focusMemberValue(index)) root.openSimulatorRequested()
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 78
                        radius: 5
                        color: root.theme.navigationDark
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 7
                            spacing: 2
                            Label { text: "Monitor events (bounded " + root.monitor.eventCapacity + ")"; color: root.theme.navigationText; font.pixelSize: 9; font.weight: Font.DemiBold }
                            Label {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                text: root.monitor.recentEvents.length ? root.monitor.recentEvents[root.monitor.recentEvents.length - 1] : "No events"
                                color: root.theme.navigationMuted
                                font.pixelSize: 8
                                wrapMode: Text.Wrap
                                maximumLineCount: 3
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
    }
}
