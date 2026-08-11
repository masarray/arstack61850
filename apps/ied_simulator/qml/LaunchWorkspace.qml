// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property var backend
    signal importRequested()
    signal folderRequested()

    function statusTone(severity) {
        if (severity === "Error") return theme.red
        if (severity === "Warning") return theme.amber
        if (severity === "Success") return theme.green
        return theme.accent
    }

    Rectangle {
        anchors.fill: parent
        color: theme.background
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 66
            color: theme.chrome
            border.width: 1
            border.color: theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                spacing: 12

                Rectangle {
                    width: 30; height: 30; radius: 7
                    color: theme.accentSoft
                    border.width: 1; border.color: theme.accent
                    Image { anchors.centerIn: parent; width: 17; height: 17; source: "qrc:/iedsim/assets/radio-tower.svg" }
                }
                Column {
                    spacing: 1
                    Label { text: "ARSTACK61850"; color: theme.muted; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 1.1 }
                    Label { text: "IED Simulator"; color: theme.text; font.pixelSize: theme.subtitleSize; font.weight: Font.DemiBold }
                }
                Item { Layout.fillWidth: true }
                StatusPill {
                    theme: root.theme
                    text: backend.imported ? "Ready" : "Waiting for model"
                    tone: backend.imported ? theme.green : theme.muted
                    fill: backend.imported ? theme.greenSoft : theme.surfaceRaised
                }
                Rectangle { width: 1; height: 24; color: theme.line }
                Label {
                    text: "Local · " + backend.listenAddress + ":" + backend.port
                    color: theme.textSoft
                    font.pixelSize: theme.captionSize
                    font.features: { "tnum": 1 }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 96
            color: theme.surface
            border.width: 1
            border.color: theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 54
                anchors.rightMargin: 54
                spacing: 14

                Repeater {
                    model: [
                        { number: "1", label: "Source Model", state: backend.imported ? "Completed" : "Current", done: backend.imported, active: !backend.imported },
                        { number: "2", label: "Network & Services", state: backend.imported ? "In progress" : "Pending", done: false, active: backend.imported },
                        { number: "3", label: "Run & Verify", state: "Pending", done: false, active: false }
                    ]
                    delegate: RowLayout {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        spacing: 12
                        Rectangle {
                            width: 46; height: 46; radius: 23
                            color: modelData.active ? theme.accentSoft : (modelData.done ? theme.greenSoft : "transparent")
                            border.width: 2
                            border.color: modelData.active ? theme.accent : (modelData.done ? theme.green : theme.muted)
                            Label {
                                anchors.centerIn: parent
                                text: modelData.done ? "✓" : modelData.number
                                color: modelData.done ? theme.green : (modelData.active ? theme.accent : theme.textSoft)
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                            }
                        }
                        Column {
                            spacing: 3
                            Label { text: modelData.label; color: theme.text; font.pixelSize: theme.bodySize; font.weight: Font.Medium }
                            Label {
                                text: modelData.state
                                color: modelData.done ? theme.green : (modelData.active ? theme.accent : theme.muted)
                                font.pixelSize: theme.labelSize
                            }
                        }
                        Rectangle {
                            visible: index < 2
                            Layout.fillWidth: true
                            Layout.leftMargin: 8
                            height: 1
                            color: modelData.done ? theme.green : theme.line
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 18
            Layout.bottomMargin: 12
            spacing: 16

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 2.05
                spacing: 12

                SurfaceCard {
                    theme: root.theme
                    Layout.fillWidth: true
                    Layout.preferredHeight: 286

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 14
                        Label { text: "Imported Model"; color: theme.text; font.pixelSize: theme.subtitleSize; font.weight: Font.DemiBold }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            Rectangle {
                                width: 42; height: 42; radius: 8
                                color: backend.imported ? theme.accentSoft : theme.surfaceRaised
                                Image {
                                    anchors.centerIn: parent; width: 19; height: 19
                                    source: backend.imported ? "qrc:/iedsim/assets/circle-check.svg" : "qrc:/iedsim/assets/upload.svg"
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Label {
                                    text: backend.imported ? backend.sourceName : "Import an SCL engineering model"
                                    color: theme.text; font.pixelSize: theme.bodySize; font.weight: Font.DemiBold
                                    elide: Text.ElideMiddle; Layout.fillWidth: true
                                }
                                Label {
                                    text: backend.imported ? backend.modelStatus : "CID, SCD, IID, ICD and SCL files are supported."
                                    color: backend.fatalError.length ? theme.red : theme.muted
                                    font.pixelSize: theme.captionSize; Layout.fillWidth: true; elide: Text.ElideRight
                                }
                            }
                            ActionButton {
                                theme: root.theme
                                text: backend.imported ? "Replace" : "Import SCL"
                                iconSource: "qrc:/iedsim/assets/folder-open.svg"
                                onClicked: root.importRequested()
                            }
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: theme.lineSoft }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 6
                            columnSpacing: 0
                            Repeater {
                                model: [
                                    { label: "IEDs", value: backend.ieds.length },
                                    { label: "Logical Devices", value: backend.logicalDeviceCount },
                                    { label: "Data Objects", value: backend.dataObjectCount },
                                    { label: "Data Attributes", value: backend.dataAttributeCount },
                                    { label: "DataSets", value: backend.dataSetCount },
                                    { label: "GOOSE", value: backend.gooseCount }
                                ]
                                delegate: Item {
                                    required property var modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    implicitHeight: 58
                                    Rectangle { visible: index > 0; width: 1; height: 40; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; color: theme.line }
                                    Column {
                                        anchors.centerIn: parent; spacing: 5
                                        Label { anchors.horizontalCenter: parent.horizontalCenter; text: backend.imported ? modelData.value : "—"; color: theme.text; font.pixelSize: 19; font.weight: Font.Medium }
                                        Label { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.label; color: theme.muted; font.pixelSize: 10 }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 56
                            radius: 6
                            color: backend.imported ? theme.greenSoft : theme.surfaceRaised
                            border.width: 1
                            border.color: backend.imported ? Qt.rgba(theme.green.r, theme.green.g, theme.green.b, 0.42) : theme.line
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 12; spacing: 10
                                Image { width: 20; height: 20; source: backend.imported ? "qrc:/iedsim/assets/circle-check.svg" : "qrc:/iedsim/assets/scan-search.svg" }
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 2
                                    Label { text: backend.imported ? "Model validation passed" : "No model loaded"; color: backend.imported ? theme.green : theme.textSoft; font.pixelSize: theme.labelSize; font.weight: Font.DemiBold }
                                    Label { text: backend.imported ? "The engineering file is structurally ready for simulation." : "Import a model to configure the endpoint and services."; color: theme.muted; font.pixelSize: theme.captionSize }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 12

                    SurfaceCard {
                        theme: root.theme
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 16; spacing: 12
                            Label { text: "Model Overview"; color: theme.text; font.pixelSize: theme.subtitleSize; font.weight: Font.DemiBold }
                            ListView {
                                Layout.fillWidth: true; Layout.fillHeight: true
                                clip: true; spacing: 3
                                model: backend.ieds
                                delegate: Rectangle {
                                    required property var modelData
                                    required property int index
                                    width: ListView.view.width; height: 42; radius: 5
                                    color: index === backend.selectedIedIndex ? theme.accentSoft : "transparent"
                                    border.width: index === backend.selectedIedIndex ? 1 : 0
                                    border.color: theme.accent
                                    RowLayout {
                                        anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 9
                                        Rectangle { width: 8; height: 8; radius: 4; color: backend.imported ? theme.green : theme.muted }
                                        ColumnLayout {
                                            Layout.fillWidth: true; spacing: 1
                                            Label { text: modelData.name || "Unnamed IED"; color: theme.text; font.pixelSize: theme.labelSize; font.weight: Font.Medium; elide: Text.ElideRight; Layout.fillWidth: true }
                                            Label { text: (modelData.manufacturer || "Engineering model") + (modelData.type ? " · " + modelData.type : ""); color: theme.muted; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                                        }
                                    }
                                    MouseArea { anchors.fill: parent; onClicked: backend.selectedIedIndex = index }
                                }
                                Label {
                                    anchors.centerIn: parent; visible: backend.ieds.length === 0
                                    text: "IED hierarchy appears here after import."
                                    color: theme.muted; font.pixelSize: theme.captionSize
                                }
                            }
                        }
                    }

                    SurfaceCard {
                        theme: root.theme
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 16; spacing: 10
                            Label { text: "Launch Readiness"; color: theme.text; font.pixelSize: theme.subtitleSize; font.weight: Font.DemiBold }
                            Repeater {
                                model: [
                                    { label: "Import complete", ready: backend.imported },
                                    { label: "Model validated", ready: backend.imported && !backend.fatalError.length },
                                    { label: "Endpoint available", ready: backend.port > 0 },
                                    { label: "Services configured", ready: backend.imported }
                                ]
                                delegate: Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true; height: 36; radius: 4
                                    color: theme.surfaceRaised
                                    RowLayout {
                                        anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 9
                                        Rectangle { width: 16; height: 16; radius: 8; color: modelData.ready ? theme.greenSoft : theme.chrome; border.width: 1; border.color: modelData.ready ? theme.green : theme.line }
                                        Label { text: modelData.label; color: theme.textSoft; font.pixelSize: theme.captionSize; Layout.fillWidth: true }
                                        Label { text: modelData.ready ? "OK" : "WAIT"; color: modelData.ready ? theme.green : theme.muted; font.pixelSize: 10; font.weight: Font.DemiBold }
                                    }
                                }
                            }
                            Item { Layout.fillHeight: true }
                            Label { text: "The runtime workspace opens after the MMS endpoint starts."; color: theme.muted; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                    }
                }
            }

            SurfaceCard {
                theme: root.theme
                Layout.fillHeight: true
                Layout.preferredWidth: 430
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 18; spacing: 12
                    Label { text: "Run Configuration"; color: theme.text; font.pixelSize: theme.subtitleSize; font.weight: Font.DemiBold }
                    Label { text: "LOCAL IP ADDRESS"; color: theme.muted; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 0.8 }
                    ComboBox {
                        id: addressBox
                        Layout.fillWidth: true; implicitHeight: theme.controlHeight
                        model: backend.availableAddresses
                        currentIndex: Math.max(0, backend.availableAddresses.indexOf(backend.listenAddress))
                        onActivated: backend.listenAddress = currentText
                        contentItem: Label { leftPadding: 10; text: addressBox.displayText; color: theme.text; verticalAlignment: Text.AlignVCenter; font.pixelSize: theme.labelSize }
                        background: Rectangle { radius: theme.controlRadius; color: theme.surfaceRaised; border.width: 1; border.color: addressBox.activeFocus ? theme.accent : theme.line }
                    }
                    Label { text: "PORT"; color: theme.muted; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 0.8 }
                    TextField {
                        Layout.fillWidth: true; implicitHeight: theme.controlHeight
                        text: backend.port.toString(); color: theme.text; font.pixelSize: theme.labelSize
                        validator: IntValidator { bottom: 1; top: 65535 }
                        onEditingFinished: backend.port = Number(text)
                        background: Rectangle { radius: theme.controlRadius; color: theme.surfaceRaised; border.width: 1; border.color: parent.activeFocus ? theme.accent : theme.line }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: theme.lineSoft; Layout.topMargin: 4; Layout.bottomMargin: 4 }
                    Label { text: "SERVICES"; color: theme.muted; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 0.8 }
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label { text: "GOOSE Publishing"; color: theme.textSoft; font.pixelSize: theme.labelSize }
                            Label { text: "Publish configured GOOSE control blocks"; color: theme.muted; font.pixelSize: 10 }
                        }
                        ToggleSwitch { theme: root.theme; checked: backend.gooseEnabled; onToggled: backend.gooseEnabled = checked }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label { text: "File Service"; color: theme.textSoft; font.pixelSize: theme.labelSize }
                            Label { text: "Expose a controlled lab folder"; color: theme.muted; font.pixelSize: 10 }
                        }
                        ToggleSwitch { theme: root.theme; checked: backend.fileServiceEnabled; onToggled: backend.fileServiceEnabled = checked }
                    }
                    Label { text: "FILE SERVICE FOLDER"; color: backend.fileServiceEnabled ? theme.muted : theme.line; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 0.8 }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 7
                        TextField {
                            Layout.fillWidth: true; implicitHeight: theme.controlHeight
                            enabled: backend.fileServiceEnabled
                            placeholderText: "Choose a folder"
                            text: backend.fileFolder; onEditingFinished: backend.fileFolder = text
                            color: theme.text; placeholderTextColor: theme.muted; font.pixelSize: theme.captionSize
                            background: Rectangle { radius: theme.controlRadius; color: theme.surfaceRaised; border.width: 1; border.color: parent.activeFocus ? theme.accent : theme.line }
                        }
                        ActionButton { theme: root.theme; text: "Browse"; enabled: backend.fileServiceEnabled; iconSource: "qrc:/iedsim/assets/folder-open.svg"; onClicked: root.folderRequested() }
                    }
                    Label { text: "MODE / BEHAVIOR"; color: theme.muted; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 0.8 }
                    ComboBox {
                        Layout.fillWidth: true; implicitHeight: theme.controlHeight
                        model: ["Simulation Mode (Live)", "Training Mode (Isolated)", "Read-only Demonstration"]
                        contentItem: Label { leftPadding: 10; text: parent.displayText; color: theme.text; verticalAlignment: Text.AlignVCenter; font.pixelSize: theme.labelSize }
                        background: Rectangle { radius: theme.controlRadius; color: theme.surfaceRaised; border.width: 1; border.color: parent.activeFocus ? theme.accent : theme.line }
                    }
                    Item { Layout.fillHeight: true }
                    Rectangle {
                        visible: backend.starting
                        Layout.fillWidth: true; height: 42; radius: 6
                        color: theme.accentSoft; border.width: 1; border.color: theme.accent
                        Label { anchors.centerIn: parent; text: "Starting MMS endpoint…"; color: theme.accent; font.pixelSize: theme.labelSize; font.weight: Font.DemiBold }
                    }
                    ActionButton {
                        theme: root.theme
                        Layout.fillWidth: true
                        implicitHeight: 52
                        text: backend.starting ? "STARTING…" : "RUN SIMULATION"
                        primary: true
                        iconSource: "qrc:/iedsim/assets/play.svg"
                        enabled: backend.imported && !backend.starting && !backend.fatalError.length
                        onClicked: backend.startSimulation()
                    }
                    Label {
                        Layout.fillWidth: true
                        text: backend.imported ? "The model will be discoverable through the configured MMS endpoint." : "Import and validate a model before starting."
                        color: theme.muted; font.pixelSize: 10; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }

        SurfaceCard {
            theme: root.theme
            Layout.fillWidth: true
            Layout.preferredHeight: 124
            Layout.leftMargin: 18; Layout.rightMargin: 18; Layout.bottomMargin: 16
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 7
                RowLayout {
                    Layout.fillWidth: true
                    Image { width: 15; height: 15; source: "qrc:/iedsim/assets/activity.svg" }
                    Label { text: "Activity & Diagnostics"; color: theme.textSoft; font.pixelSize: theme.labelSize; font.weight: Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    StatusPill { theme: root.theme; text: backend.imported ? "Ready for discovery" : "Standby"; tone: backend.imported ? theme.green : theme.muted; fill: backend.imported ? theme.greenSoft : theme.surfaceRaised }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: theme.lineSoft }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    model: backend.activity
                    delegate: RowLayout {
                        required property var modelData
                        width: ListView.view.width; height: 30; spacing: 12
                        Label { text: modelData.time; color: theme.muted; font.pixelSize: 10; Layout.preferredWidth: 86 }
                        Rectangle { width: 7; height: 7; radius: 4; color: root.statusTone(modelData.severity) }
                        Label { text: modelData.category; color: theme.textSoft; font.pixelSize: 10; font.weight: Font.DemiBold; Layout.preferredWidth: 80 }
                        Label { text: modelData.message; color: theme.textSoft; font.pixelSize: theme.captionSize; Layout.fillWidth: true; elide: Text.ElideRight }
                        Label { text: modelData.severity; color: root.statusTone(modelData.severity); font.pixelSize: 10; Layout.preferredWidth: 58 }
                    }
                }
            }
        }
    }
}
