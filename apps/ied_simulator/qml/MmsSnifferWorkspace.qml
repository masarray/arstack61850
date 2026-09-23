// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ARStack.IedSimulator 1.0

Item {
    id: root
    required property var theme
    required property MmsPassiveSnifferController sniffer

    FileDialog {
        id: openCapture
        title: "Open Ethernet MMS capture"
        fileMode: FileDialog.OpenFile
        nameFilters: ["PCAP Ethernet capture (*.pcap *.cap)", "All files (*)"]
        onAccepted: root.sniffer.importPcap(selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    text: "MMS Sniffer"
                    color: theme.text
                    font.pixelSize: theme.titleSize
                    font.weight: Font.DemiBold
                }
                Label {
                    text: "Passive TCP/102 · TPKT / COTP / Session / MMS · PCAP replay or live interface capture"
                    color: theme.muted
                    font.pixelSize: theme.captionSize
                }
            }
            Label {
                text: sniffer.statusText
                color: sniffer.capturing ? theme.green : theme.textSoft
                font.pixelSize: 9
                font.weight: Font.DemiBold
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            radius: theme.controlRadius
            color: theme.surface
            border.width: 1
            border.color: theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 7

                Label { text: "Interface"; color: theme.textSoft; font.pixelSize: 9 }
                ComboBox {
                    id: interfaceSelector
                    Layout.preferredWidth: 210
                    model: sniffer.interfaces
                    enabled: !sniffer.capturing && !sniffer.busy
                    currentIndex: Math.max(0, sniffer.interfaces.indexOf(sniffer.interfaceName))
                    onActivated: sniffer.interfaceName = currentText
                    Component.onCompleted: {
                        if (!sniffer.interfaceName.length && count > 0)
                            sniffer.interfaceName = currentText
                    }
                }
                Button {
                    text: sniffer.capturing ? "Stop capture" : "Capture live"
                    enabled: !sniffer.busy
                    onClicked: sniffer.capturing ? sniffer.stopCapture() : sniffer.startCapture()
                }
                Button {
                    text: "Open PCAP…"
                    enabled: !sniffer.capturing && !sniffer.busy
                    onClicked: openCapture.open()
                }
                Button {
                    text: "Clear"
                    enabled: !sniffer.busy
                    onClicked: sniffer.clear()
                }
                Item { Layout.fillWidth: true }
                ComboBox {
                    id: filterBox
                    Layout.preferredWidth: 140
                    model: ["All", "Reports", "Read", "Write", "Control", "Association", "Errors"]
                    currentIndex: Math.max(0, model.indexOf(sniffer.filter))
                    onActivated: sniffer.filter = currentText
                }
            }
        }

        Rectangle {
            visible: sniffer.lastError.length > 0
            Layout.fillWidth: true
            implicitHeight: errorText.implicitHeight + 16
            radius: 5
            color: theme.redSoft
            border.width: 1
            border.color: theme.red
            Label {
                id: errorText
                anchors.fill: parent
                anchors.margins: 8
                text: sniffer.lastError
                color: theme.red
                font.pixelSize: 9
                wrapMode: Text.WordWrap
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: theme.surface
            border.width: 1
            border.color: theme.lineSoft
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 15
                Label { text: "TCP " + (sniffer.counters.tcp || 0); color: theme.textSoft; font.pixelSize: 9 }
                Label { text: "MMS " + (sniffer.counters.mms || 0); color: theme.text; font.pixelSize: 9; font.weight: Font.DemiBold }
                Label { text: "Requests " + (sniffer.counters.requests || 0); color: theme.textSoft; font.pixelSize: 9 }
                Label { text: "Responses " + (sniffer.counters.responses || 0); color: theme.textSoft; font.pixelSize: 9 }
                Label { text: "Reports " + (sniffer.counters.reports || 0); color: theme.green; font.pixelSize: 9 }
                Item { Layout.fillWidth: true }
                Label { text: "Retrans " + (sniffer.counters.retransmissions || 0); color: theme.muted; font.pixelSize: 8 }
                Label { text: "Gap " + (sniffer.counters.gaps || 0); color: theme.muted; font.pixelSize: 8 }
                Label { text: "Malformed " + (sniffer.counters.malformed || 0); color: theme.red; font.pixelSize: 8 }
                Label { text: "Dropped " + (sniffer.counters.dropped || 0); color: theme.red; font.pixelSize: 8 }
            }
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            Rectangle {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 490
                color: theme.surface
                border.width: 1
                border.color: theme.lineSoft

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        color: theme.chrome
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            spacing: 5
                            Label { Layout.preferredWidth: 106; text: "TIME"; color: theme.muted; font.pixelSize: 8; font.weight: Font.DemiBold }
                            Label { Layout.fillWidth: true; text: "SOURCE → DESTINATION"; color: theme.muted; font.pixelSize: 8; font.weight: Font.DemiBold }
                            Label { Layout.preferredWidth: 114; text: "SERVICE"; color: theme.muted; font.pixelSize: 8; font.weight: Font.DemiBold }
                            Label { Layout.preferredWidth: 90; text: "TYPE"; color: theme.muted; font.pixelSize: 8; font.weight: Font.DemiBold }
                        }
                    }
                    ListView {
                        id: eventList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        reuseItems: true
                        model: sniffer.events
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            required property int index
                            required property var modelData
                            width: eventList.width
                            height: 42
                            color: sniffer.selectedIndex === index ? theme.surfaceSoft
                                  : index % 2 ? theme.surfaceSoft : theme.surface
                            border.width: sniffer.selectedIndex === index ? 1 : 0
                            border.color: theme.line
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 7
                                spacing: 5
                                Label {
                                    Layout.preferredWidth: 106
                                    text: String(modelData.timestamp || "").slice(11,23)
                                    color: theme.muted
                                    font.pixelSize: 8
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: (modelData.source || "—") + " → " + (modelData.destination || "—")
                                    color: theme.textSoft
                                    font.pixelSize: 8
                                    elide: Text.ElideMiddle
                                }
                                Label {
                                    Layout.preferredWidth: 114
                                    text: modelData.service || "—"
                                    color: modelData.service === "InformationReport" ? theme.green : theme.text
                                    font.pixelSize: 8
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.preferredWidth: 90
                                    text: modelData.kind || "—"
                                    color: modelData.kind === "Decode error" ? theme.red : theme.muted
                                    font.pixelSize: 8
                                    elide: Text.ElideRight
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: sniffer.select(index)
                            }
                        }
                        Label {
                            anchors.centerIn: parent
                            visible: eventList.count === 0
                            text: "No MMS events. Start passive capture or open an Ethernet PCAP."
                            color: theme.muted
                            font.pixelSize: 9
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            Rectangle {
                SplitView.preferredWidth: 315
                SplitView.minimumWidth: 250
                color: theme.surface
                border.width: 1
                border.color: theme.lineSoft
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10
                    Label {
                        text: "Packet details"
                        color: theme.text
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Repeater {
                        model: [
                            ["Time", sniffer.selectedEvent.timestamp],
                            ["Source", sniffer.selectedEvent.source],
                            ["Destination", sniffer.selectedEvent.destination],
                            ["PDU", sniffer.selectedEvent.kind],
                            ["Service", sniffer.selectedEvent.service],
                            ["Invoke ID", sniffer.selectedEvent.invokeId],
                            ["MMS bytes", sniffer.selectedEvent.bytes]
                        ]
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 2
                            Label { text: modelData[0]; color: theme.muted; font.pixelSize: 8 }
                            Label {
                                Layout.fillWidth: true
                                text: modelData[1] === undefined ? "—" : String(modelData[1])
                                color: theme.textSoft
                                font.pixelSize: 9
                                wrapMode: Text.WrapAnywhere
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: sniffer.selectedEvent.detail || ""
                        color: theme.textSoft
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                    }
                    Item { Layout.fillHeight: true }
                    Label {
                        Layout.fillWidth: true
                        text: "Passive observation only. No MMS client association is created. IPv4 TCP/102 and classic Ethernet PCAP are supported; unsupported or truncated packets are not guessed."
                        color: theme.muted
                        font.pixelSize: 8
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
