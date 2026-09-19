// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ARStack port of the ARSAS IED Explorer interaction pattern.  This component
// deliberately owns presentation only: the authoritative IED/runtime model
// remains IedFleetController and the SCL parser/profile builder behind it.
Rectangle {
    id: root

    required property var theme
    required property var backend

    objectName: "iedExplorer"
    color: theme.surface
    border.width: 1
    border.color: theme.lineSoft

    function statusColor(status) {
        if (status === "Running") return theme.green
        if (status === "Starting" || status === "Stopping") return theme.amber
        if (status === "Failed") return theme.red
        return theme.muted
    }

    function statusFill(status) {
        if (status === "Running") return theme.greenSoft
        if (status === "Starting" || status === "Stopping") return theme.amberSoft
        if (status === "Failed") return theme.redSoft
        return theme.surfaceRaised
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            Layout.leftMargin: 13
            Layout.rightMargin: 10
            spacing: 8

            Rectangle {
                width: 8
                height: 8
                radius: 4
                color: backend.runningCount > 0 ? theme.green : theme.accent
            }
            Label {
                text: "IED EXPLORER"
                color: theme.text
                font.pixelSize: 11
                font.weight: Font.Bold
                Layout.fillWidth: true
            }
            Label {
                text: backend.runningCount + "/" + backend.ieds.length
                color: backend.runningCount > 0 ? theme.green : theme.muted
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: theme.lineSoft }

        ListView {
            id: iedList
            objectName: "iedExplorerList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: backend.ieds
            spacing: 8
            topMargin: 9
            bottomMargin: 9
            leftMargin: 8
            rightMargin: 8
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                id: card
                required property var modelData
                required property int index

                width: iedList.width - iedList.leftMargin - iedList.rightMargin
                height: 112
                radius: 14
                color: {
                    if (backend.selectedIedIndex === index && modelData.status === "Running") return "#e9f6ef"
                    if (backend.selectedIedIndex === index) return theme.accentSoft
                    if (modelData.status === "Running") return "#f2fbf6"
                    return theme.surface
                }
                border.width: 1
                border.color: {
                    if (backend.selectedIedIndex === index && modelData.status === "Running") return "#71b58e"
                    if (backend.selectedIedIndex === index) return theme.accent
                    if (modelData.status === "Running") return "#a7d5bb"
                    return theme.lineSoft
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 5

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 62
                        spacing: 8

                        Item {
                            Layout.preferredWidth: 58
                            Layout.preferredHeight: 62

                            Image {
                                id: relayIcon
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.top: parent.top
                                width: 49
                                height: 49
                                source: "qrc:/iedsim/assets/black-fascia-ied.svg"
                                sourceSize: Qt.size(98, 98)
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                cache: true
                                opacity: modelData.enabled ? 1.0 : 0.42
                            }

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottom: parent.bottom
                                height: 15
                                width: 42
                                radius: 5
                                color: root.statusFill(modelData.status)
                                border.width: 1
                                border.color: root.statusColor(modelData.status)
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 3
                                    Rectangle {
                                        width: 4
                                        height: 4
                                        radius: 2
                                        color: root.statusColor(modelData.status)
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Label {
                                        text: modelData.status === "Running" ? "LIVE" :
                                              (modelData.status === "Starting" ? "START" :
                                               (modelData.status === "Stopping" ? "STOP…" :
                                                (modelData.status === "Failed" ? "FAIL" : "READY")))
                                        color: root.statusColor(modelData.status)
                                        font.pixelSize: 7
                                        font.weight: Font.Bold
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 2

                            Label {
                                Layout.fillWidth: true
                                text: modelData.name || "Unnamed IED"
                                color: theme.text
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: modelData.endpoint || "Assign IP"
                                color: (modelData.endpoint || "") === "Assign IP" ? theme.amber : theme.muted
                                font.pixelSize: 9
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: [modelData.manufacturer || "", modelData.type || ""].filter(function(v) { return v.length > 0 }).join(" · ")
                                color: theme.textSoft
                                font.pixelSize: 8
                                elide: Text.ElideRight
                            }
                            Item { Layout.fillHeight: true }
                        }

                        Switch {
                            checked: modelData.enabled === undefined ? true : modelData.enabled
                            enabled: !modelData.running && !modelData.starting
                            scale: 0.72
                            Layout.alignment: Qt.AlignTop
                            onToggled: backend.setIedEnabled(index, checked)
                            ToolTip.visible: hovered
                            ToolTip.text: checked ? "IED enabled" : "IED disabled"
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        Layout.leftMargin: 62
                        spacing: 5

                        IconAction {
                            theme: root.theme
                            compact: true
                            text: ""
                            iconSource: "qrc:/iedsim/assets/play.svg"
                            enabled: modelData.enabled && !modelData.running && !modelData.starting &&
                                     (modelData.endpoint || "") !== "Assign IP"
                            onClicked: {
                                backend.selectIed(index)
                                backend.startIed(index)
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: "Start this IED"
                        }
                        IconAction {
                            theme: root.theme
                            compact: true
                            text: ""
                            danger: true
                            iconSource: "qrc:/iedsim/assets/square.svg"
                            enabled: modelData.running || modelData.starting
                            onClicked: backend.stopIed(index)
                            ToolTip.visible: hovered
                            ToolTip.text: "Stop this IED"
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: backend.selectedIedIndex === index ? "SELECTED" : ""
                            color: theme.accent
                            font.pixelSize: 7
                            font.weight: Font.Bold
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: backend.selectIed(card.index)
                }
            }

            Label {
                anchors.centerIn: parent
                visible: backend.ieds.length === 0
                text: "Open an SCL/SCD/CID model\nto build the IED Explorer"
                horizontalAlignment: Text.AlignHCenter
                color: theme.muted
                font.pixelSize: 10
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: theme.lineSoft }

        // ARSAS-inspired fixed quick actions stay visible independently from the
        // scrolling IED list.  They are adapted to simulator semantics.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: backend.imported ? 267 : 0
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: 9
            Layout.bottomMargin: 9
            spacing: 7
            visible: backend.imported

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                IconAction {
                    theme: root.theme
                    text: "Start all"
                    iconSource: "qrc:/iedsim/assets/play.svg"
                    primary: true
                    Layout.fillWidth: true
                    enabled: backend.ieds.length > 0
                    onClicked: backend.startAllSimulations()
                }
                IconAction {
                    theme: root.theme
                    compact: true
                    text: ""
                    iconSource: "qrc:/iedsim/assets/radio-tower.svg"
                    enabled: !backend.anyRunning
                    onClicked: backend.autoAssignIedAddresses()
                    ToolTip.visible: hovered
                    ToolTip.text: "Auto assign local IPv4 addresses"
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "SELECTED ENDPOINT"
                    color: theme.text
                    font.pixelSize: 9
                    font.weight: Font.Bold
                    Layout.fillWidth: true
                }
                Rectangle {
                    width: 7
                    height: 7
                    radius: 4
                    color: root.statusColor(backend.selectedIed.status || "Ready")
                }
            }

            ComboBox {
                id: addressBox
                Layout.fillWidth: true
                enabled: !backend.running && !backend.starting
                model: backend.availableAddresses
                currentIndex: Math.max(0, backend.availableAddresses.indexOf(backend.listenAddress))
                onActivated: backend.listenAddress = currentText
                font.pixelSize: 10
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: "Port"; color: theme.muted; font.pixelSize: 9 }
                SpinBox {
                    Layout.fillWidth: true
                    from: 1
                    to: 65535
                    editable: true
                    value: backend.port
                    enabled: !backend.running && !backend.starting
                    onValueModified: backend.port = value
                    font.pixelSize: 10
                }
                IconAction {
                    theme: root.theme
                    compact: true
                    text: ""
                    iconSource: "qrc:/iedsim/assets/scan-search.svg"
                    enabled: !backend.anyRunning
                    onClicked: backend.refreshNetworkInterfaces()
                    ToolTip.visible: hovered
                    ToolTip.text: "Refresh laptop IPv4 interfaces"
                }
            }

            Label {
                Layout.fillWidth: true
                visible: backend.endpointConflict.length > 0
                text: backend.endpointConflict
                color: theme.red
                font.pixelSize: 8
                wrapMode: Text.WordWrap
            }

            Label {
                Layout.fillWidth: true
                text: backend.networkAddresses.length + " local IPv4 entries · secondary Ethernet IPs are supported"
                color: theme.muted
                font.pixelSize: 8
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                IconAction {
                    theme: root.theme
                    text: backend.running ? "Stop selected" : "Start selected"
                    iconSource: backend.running ? "qrc:/iedsim/assets/square.svg" : "qrc:/iedsim/assets/play.svg"
                    primary: !backend.running
                    danger: backend.running
                    Layout.fillWidth: true
                    enabled: backend.running ||
                             (backend.imported && !backend.starting && backend.selectedIed.enabled &&
                              backend.endpointConflict.length === 0 && backend.listenAddress.length > 0)
                    onClicked: backend.running ? backend.stopSimulation() : backend.startSimulation()
                }
            }
        }
    }
}
