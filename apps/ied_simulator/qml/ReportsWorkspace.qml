// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ARStack.IedSimulator 1.0

Item {
    id: root
    required property var theme
    required property var reports

    function text(value) {
        return value === undefined || value === null || String(value).length === 0 ? "—" : String(value)
    }

    Rectangle { anchors.fill: parent; color: theme.background }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            color: theme.chrome
            border.width: 1
            border.color: theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 8

                TextField {
                    id: hostField
                    Layout.preferredWidth: 190
                    text: reports.host
                    placeholderText: "IED IP / hostname"
                    selectByMouse: true
                    enabled: !reports.connected && !reports.busy
                    onEditingFinished: reports.host = text
                }
                SpinBox {
                    id: portField
                    Layout.preferredWidth: 100
                    from: 1
                    to: 65535
                    value: reports.port
                    editable: true
                    enabled: !reports.connected && !reports.busy
                    onValueModified: reports.port = value
                }
                Button {
                    text: reports.connected ? "Reconnect" : "Connect"
                    enabled: !reports.busy
                    onClicked: {
                        reports.host = hostField.text
                        reports.port = portField.value
                        if (reports.connected) reports.reconnect()
                        else reports.connectToIed()
                    }
                }
                Button {
                    text: "Disconnect"
                    enabled: reports.connected || reports.busy
                    onClicked: reports.disconnectFromIed()
                }

                Rectangle {
                    width: 8; height: 8; radius: 4
                    color: reports.active ? theme.green
                                         : reports.cleanupRequired ? theme.red
                                         : reports.busy ? theme.amber
                                         : reports.connected ? theme.accent
                                         : reports.lastError.length ? theme.red : theme.muted
                }
                Label {
                    text: reports.stateText
                    color: reports.cleanupRequired ? theme.red : theme.textSoft
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                }

                Item { Layout.fillWidth: true }

                ColumnLayout {
                    spacing: 0
                    visible: reports.connected
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: reports.reportControls.length + " RCB · " + reports.dataSets.length + " DataSet"
                        color: theme.text
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: reports.associationProfile.length ? reports.associationProfile : reports.host + ":" + reports.port
                        color: theme.muted
                        font.pixelSize: 8
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.preferredWidth: Math.max(310, root.width * 0.26)
                Layout.fillHeight: true
                color: theme.chrome
                border.width: 1
                border.color: theme.lineSoft

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Label {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34
                        Layout.leftMargin: 10
                        verticalAlignment: Text.AlignVCenter
                        text: "URCB / BRCB inventory"
                        color: theme.text
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }
                    ListView {
                        id: rcbList
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(150, root.height * 0.27)
                        clip: true
                        reuseItems: true
                        cacheBuffer: 0
                        model: reports.reportControls
                        ScrollBar.vertical: ScrollBar { }
                        delegate: Rectangle {
                            required property int index
                            required property var modelData
                            width: ListView.view.width
                            height: 48
                            color: reports.selectedRcbIndex === index ? theme.accentSoft
                                  : mouse.containsMouse ? theme.surfaceRaised : "transparent"
                            border.width: reports.selectedRcbIndex === index ? 1 : 0
                            border.color: theme.accent
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 9
                                anchors.rightMargin: 8
                                anchors.topMargin: 5
                                anchors.bottomMargin: 5
                                spacing: 1
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        text: root.text(modelData.mode)
                                        color: modelData.buffered ? theme.amber : theme.accent
                                        font.pixelSize: 8
                                        font.weight: Font.Bold
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: root.text(modelData.reference)
                                        color: theme.text
                                        font.pixelSize: 9
                                        elide: Text.ElideMiddle
                                    }
                                    Label {
                                        text: root.text(modelData.availability)
                                        color: modelData.availability === "available" ? theme.green : theme.muted
                                        font.pixelSize: 7
                                    }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: root.text(modelData.dataSet)
                                    color: theme.muted
                                    font.pixelSize: 8
                                    elide: Text.ElideMiddle
                                }
                            }
                            MouseArea {
                                id: mouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: reports.selectRcb(index)
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

                    Label {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        Layout.leftMargin: 10
                        verticalAlignment: Text.AlignVCenter
                        text: "DataSets"
                        color: theme.text
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(100, root.height * 0.18)
                        clip: true
                        reuseItems: true
                        cacheBuffer: 0
                        model: reports.dataSets
                        ScrollBar.vertical: ScrollBar { }
                        delegate: Rectangle {
                            required property int index
                            required property var modelData
                            width: ListView.view.width
                            height: 38
                            color: reports.selectedDataSetIndex === index ? theme.accentSoft
                                  : dsMouse.containsMouse ? theme.surfaceRaised : "transparent"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 9
                                anchors.rightMargin: 8
                                spacing: 5
                                Label {
                                    Layout.fillWidth: true
                                    text: root.text(modelData.reference)
                                    color: theme.textSoft
                                    font.pixelSize: 8
                                    elide: Text.ElideMiddle
                                }
                                Label {
                                    text: modelData.memberCount + " members"
                                    color: theme.muted
                                    font.pixelSize: 7
                                }
                            }
                            MouseArea {
                                id: dsMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: reports.selectDataSet(index)
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

                    Label {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        Layout.leftMargin: 10
                        verticalAlignment: Text.AlignVCenter
                        text: "Ordered members"
                        color: theme.text
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        reuseItems: true
                        cacheBuffer: 0
                        model: reports.selectedDataSetMembers
                        ScrollBar.vertical: ScrollBar { }
                        delegate: Label {
                            required property int index
                            required property string modelData
                            width: ListView.view.width
                            height: 28
                            leftPadding: 10
                            rightPadding: 8
                            verticalAlignment: Text.AlignVCenter
                            text: (index + 1) + ".  " + modelData
                            color: theme.textSoft
                            font.pixelSize: 8
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: Math.max(390, root.width * 0.34)
                Layout.fillHeight: true
                color: theme.background
                border.width: 1
                border.color: theme.lineSoft

                ScrollView {
                    anchors.fill: parent
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: Math.max(0, parent.width - 30)
                        x: 15
                        spacing: 9

                        Item { Layout.preferredHeight: 4 }
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                Layout.fillWidth: true
                                text: root.text(reports.selectedRcb.mode) + "  " + root.text(reports.selectedRcb.reference)
                                color: theme.text
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                elide: Text.ElideMiddle
                            }
                            Rectangle {
                                width: 8; height: 8; radius: 4
                                color: reports.active ? theme.green : reports.cleanupRequired ? theme.red : theme.muted
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 10
                            rowSpacing: 5
                            Repeater {
                                model: [
                                    ["DataSet", reports.selectedRcb.dataSet],
                                    ["RptID", reports.selectedRcb.reportId],
                                    ["ConfRev", reports.selectedRcb.confRev],
                                    ["BufTm", reports.selectedRcb.bufTm],
                                    ["IntgPd", reports.selectedRcb.intgPd],
                                    ["SqNum", reports.selectedRcb.sqNum],
                                    ["RptEna", reports.selectedRcb.enabled],
                                    ["Reserved", reports.selectedRcb.reserved],
                                    ["ResvTms", reports.selectedRcb.resvTms],
                                    ["Owner", reports.selectedRcb.owner]
                                ]
                                delegate: Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 46
                                    radius: 5
                                    color: theme.surface
                                    border.width: 1
                                    border.color: theme.lineSoft
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 7
                                        spacing: 1
                                        Label { text: modelData[0]; color: theme.muted; font.pixelSize: 7 }
                                        Label {
                                            Layout.fillWidth: true
                                            text: root.text(modelData[1])
                                            color: theme.text
                                            font.pixelSize: 8
                                            elide: Text.ElideMiddle
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: detailsColumn.implicitHeight + 16
                            radius: 6
                            color: theme.surface
                            border.width: 1
                            border.color: theme.lineSoft
                            ColumnLayout {
                                id: detailsColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 8
                                spacing: 4
                                Label { text: "Trigger options"; color: theme.muted; font.pixelSize: 7 }
                                Label { Layout.fillWidth: true; text: root.text(reports.selectedRcb.triggerOptions); color: theme.textSoft; font.pixelSize: 8; wrapMode: Text.Wrap }
                                Label { text: "Optional fields"; color: theme.muted; font.pixelSize: 7 }
                                Label { Layout.fillWidth: true; text: root.text(reports.selectedRcb.optionalFields); color: theme.textSoft; font.pixelSize: 8; wrapMode: Text.Wrap }
                                Label { text: "EntryID"; color: theme.muted; font.pixelSize: 7 }
                                Label { Layout.fillWidth: true; text: root.text(reports.selectedRcb.entryId); color: theme.textSoft; font.pixelSize: 8; elide: Text.ElideMiddle }
                                Label {
                                    Layout.fillWidth: true
                                    text: "BRCB capability: "
                                          + (reports.selectedRcb.supportsEntryId ? "EntryID" : "no EntryID")
                                          + " · " + (reports.selectedRcb.supportsPurgeBuf ? "PurgeBuf" : "no PurgeBuf")
                                    color: theme.muted
                                    font.pixelSize: 7
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Button {
                                text: "Enable + GI"
                                enabled: reports.connected && !reports.busy && !reports.active && !reports.cleanupRequired && reports.selectedRcb.reference !== undefined
                                onClicked: reports.enableSelected(true)
                            }
                            Button {
                                text: "Enable"
                                enabled: reports.connected && !reports.busy && !reports.active && !reports.cleanupRequired && reports.selectedRcb.reference !== undefined
                                onClicked: reports.enableSelected(false)
                            }
                            Button {
                                text: "Disable / Release"
                                enabled: reports.active && !reports.busy
                                onClicked: reports.disableSelected()
                            }
                            Button {
                                text: "Retry cleanup"
                                visible: reports.cleanupRequired
                                enabled: !reports.busy
                                onClicked: reports.retryCleanup()
                            }
                        }

                        Rectangle {
                            visible: reports.cleanupRequired || reports.lastError.length > 0
                            Layout.fillWidth: true
                            implicitHeight: warningText.implicitHeight + 16
                            radius: 5
                            color: theme.redSoft
                            border.width: 1
                            border.color: theme.red
                            Label {
                                id: warningText
                                anchors.fill: parent
                                anchors.margins: 8
                                text: reports.cleanupRequired
                                      ? "Cleanup is not confirmed. Do not assume the RCB is free; retry only on the same live association or verify the IED explicitly."
                                      : reports.lastError
                                color: theme.red
                                font.pixelSize: 8
                                wrapMode: Text.Wrap
                            }
                        }

                        Label {
                            text: "Candidate paths"
                            color: theme.text
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                        }
                        Label {
                            Layout.fillWidth: true
                            text: "Static candidates: " + reports.staticCandidates.length
                                  + " · Dynamic candidates: " + reports.dynamicCandidates.length
                            color: theme.textSoft
                            font.pixelSize: 8
                        }
                        Repeater {
                            model: reports.staticCandidates.slice(0, 6)
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: pathText.implicitHeight + 12
                                radius: 4
                                color: theme.surface
                                border.width: 1
                                border.color: theme.lineSoft
                                Label {
                                    id: pathText
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    text: "STATIC · " + root.text(modelData.reference) + " · " + root.text(modelData.decision)
                                          + "\n" + root.text(modelData.reason)
                                    color: theme.textSoft
                                    font.pixelSize: 7
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                        Repeater {
                            model: reports.dynamicCandidates.slice(0, 4)
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: dynamicPathText.implicitHeight + 12
                                radius: 4
                                color: theme.surface
                                border.width: 1
                                border.color: theme.lineSoft
                                Label {
                                    id: dynamicPathText
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    text: "DYNAMIC · " + root.text(modelData.reference) + " · " + root.text(modelData.decision)
                                          + "\n" + root.text(modelData.recommendedAction)
                                    color: theme.textSoft
                                    font.pixelSize: 7
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                        Item { Layout.preferredHeight: 10 }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: theme.chrome
                border.width: 1
                border.color: theme.lineSoft

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        Label {
                            Layout.fillWidth: true
                            text: "Received reports"
                            color: theme.text
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                        }
                        Label {
                            text: String(reports.receivedReportCount)
                            color: reports.receivedReportCount > 0 ? theme.green : theme.muted
                            font.pixelSize: 9
                            font.weight: Font.DemiBold
                        }
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        reuseItems: true
                        cacheBuffer: 0
                        model: reports.receivedReports
                        ScrollBar.vertical: ScrollBar { }
                        delegate: Rectangle {
                            required property var modelData
                            width: ListView.view.width
                            height: 78
                            color: modelData.overflow ? theme.redSoft : "transparent"
                            border.width: 0
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 9
                                anchors.rightMargin: 9
                                anchors.topMargin: 5
                                anchors.bottomMargin: 5
                                spacing: 2
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        Layout.fillWidth: true
                                        text: root.text(modelData.reportId)
                                        color: theme.text
                                        font.pixelSize: 8
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideMiddle
                                    }
                                    Label { text: "sq " + root.text(modelData.sequence); color: theme.muted; font.pixelSize: 7 }
                                    Label { visible: modelData.overflow; text: "OVERFLOW"; color: theme.red; font.pixelSize: 7; font.weight: Font.Bold }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: root.text(modelData.dataSet) + " · EntryID " + root.text(modelData.entryId)
                                    color: theme.muted
                                    font.pixelSize: 7
                                    elide: Text.ElideMiddle
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: root.text(modelData.valuesSummary)
                                    color: theme.textSoft
                                    font.pixelSize: 7
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: "duplicate " + modelData.duplicateCount + " · gap " + modelData.gapCount
                                          + " · reset " + modelData.resetCount + " · overflow " + modelData.overflowCount
                                    color: theme.muted
                                    font.pixelSize: 7
                                }
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }
                    Label {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        Layout.leftMargin: 10
                        verticalAlignment: Text.AlignVCenter
                        text: "Subscription events"
                        color: theme.text
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(100, root.height * 0.23)
                        clip: true
                        reuseItems: true
                        cacheBuffer: 0
                        model: reports.events
                        ScrollBar.vertical: ScrollBar { }
                        delegate: Label {
                            required property string modelData
                            width: ListView.view.width
                            height: Math.max(26, implicitHeight + 8)
                            leftPadding: 9
                            rightPadding: 9
                            topPadding: 4
                            bottomPadding: 4
                            text: modelData
                            color: theme.textSoft
                            font.pixelSize: 7
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }
}
