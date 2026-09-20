// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    required property var theme
    required property var workspace
    required property var hardening

    property int viewIndex: 0

    signal browserRequested()
    signal endpointRequested(string host, int port)
    signal simulatorRequested()
    signal snifferRequested()

    function resourceName(path) {
        var normalized = String(path).replace(/\\/g, "/")
        var slash = normalized.lastIndexOf("/")
        return slash >= 0 ? normalized.substring(slash + 1) : normalized
    }

    Component.onCompleted: {
        if (workspace.loaded)
            root.viewIndex = 1
    }

    Connections {
        target: workspace
        function onWorkspaceChanged() {
            if (workspace.loaded)
                root.viewIndex = 1
        }
    }

    FileDialog {
        id: openDialog
        title: "Open IEC 61850 engineering resource"
        fileMode: FileDialog.OpenFile
        nameFilters: ["IEC 61850 engineering files (*.scl *.cid *.scd *.iid *.icd)", "All files (*)"]
        onAccepted: {
            if (workspace.openFile(selectedFile))
                root.viewIndex = 1
        }
    }

    Rectangle { anchors.fill: parent; color: root.theme.background }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 6

                Label {
                    text: "File"
                    color: root.theme.text
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    Layout.rightMargin: 8
                }

                Button {
                    text: "Home"
                    checkable: true
                    checked: root.viewIndex === 0
                    implicitHeight: 28
                    onClicked: root.viewIndex = 0
                }

                Button {
                    text: workspace.loaded ? "Current resource" : "Resource"
                    checkable: true
                    checked: root.viewIndex === 1
                    enabled: workspace.loaded
                    implicitHeight: 28
                    onClicked: root.viewIndex = 1
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 22
                    color: root.theme.lineSoft
                    Layout.leftMargin: 3
                    Layout.rightMargin: 3
                }

                ActionButton {
                    theme: root.theme
                    text: "Open SCL"
                    primary: !workspace.loaded
                    enabled: !workspace.busy
                    onClicked: openDialog.open()
                }

                Item { Layout.fillWidth: true }

                Label {
                    visible: workspace.loaded
                    Layout.maximumWidth: 420
                    text: workspace.sourceName + " · " + workspace.editionText
                    color: root.theme.textSoft
                    font.pixelSize: 9
                    elide: Text.ElideMiddle
                }

                Label {
                    text: workspace.busy ? workspace.stateText : ""
                    color: root.theme.muted
                    font.pixelSize: 8
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.viewIndex

            ScrollView {
                contentWidth: availableWidth

                ColumnLayout {
                    width: Math.max(0, parent.width - 28)
                    x: 14
                    spacing: 10

                    Item { Layout.preferredHeight: 4 }

                    Label {
                        text: "Engineering start"
                        color: root.theme.text
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.fillWidth: true
                        text: "Open an engineering resource, continue with an online IED, start the simulator, or inspect network traffic."
                        color: root.theme.textSoft
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.width >= 1120 ? 4 : 2
                        columnSpacing: 8
                        rowSpacing: 8

                        SurfaceCard {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 104
                            theme: root.theme
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 3
                                Label { text: "Engineering file"; color: root.theme.text; font.pixelSize: 10; font.weight: Font.DemiBold }
                                Label {
                                    Layout.fillWidth: true
                                    text: "Open SCL / CID / SCD / IID / ICD without changing protocol runtime."
                                    color: root.theme.muted
                                    font.pixelSize: 8
                                    wrapMode: Text.WordWrap
                                }
                                Item { Layout.fillHeight: true }
                                ActionButton { theme: root.theme; text: "Open SCL"; primary: true; onClicked: openDialog.open() }
                            }
                        }

                        SurfaceCard {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 104
                            theme: root.theme
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 3
                                Label { text: "Online IED"; color: root.theme.text; font.pixelSize: 10; font.weight: Font.DemiBold }
                                Label {
                                    Layout.fillWidth: true
                                    text: "Continue to the unified IED Browser with one logical endpoint."
                                    color: root.theme.muted
                                    font.pixelSize: 8
                                    wrapMode: Text.WordWrap
                                }
                                Item { Layout.fillHeight: true }
                                ActionButton { theme: root.theme; text: "IED Browser"; onClicked: root.browserRequested() }
                            }
                        }

                        SurfaceCard {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 104
                            theme: root.theme
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 3
                                Label { text: "Simulation"; color: root.theme.text; font.pixelSize: 10; font.weight: Font.DemiBold }
                                Label {
                                    Layout.fillWidth: true
                                    text: "Create or stimulate an IEC 61850 IED in the dedicated simulator workspace."
                                    color: root.theme.muted
                                    font.pixelSize: 8
                                    wrapMode: Text.WordWrap
                                }
                                Item { Layout.fillHeight: true }
                                ActionButton { theme: root.theme; text: "IED Simulator"; onClicked: root.simulatorRequested() }
                            }
                        }

                        SurfaceCard {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 104
                            theme: root.theme
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 3
                                Label { text: "Network observation"; color: root.theme.text; font.pixelSize: 10; font.weight: Font.DemiBold }
                                Label {
                                    Layout.fillWidth: true
                                    text: "Open the independent Sniffer workspace for capture and decode workflows."
                                    color: root.theme.muted
                                    font.pixelSize: 8
                                    wrapMode: Text.WordWrap
                                }
                                Item { Layout.fillHeight: true }
                                ActionButton { theme: root.theme; text: "Sniffer"; onClicked: root.snifferRequested() }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 10

                        SurfaceCard {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 260
                            theme: root.theme

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        text: "Recent engineering resources"
                                        color: root.theme.text
                                        font.pixelSize: 10
                                        font.weight: Font.DemiBold
                                    }
                                    Item { Layout.fillWidth: true }
                                    Button {
                                        visible: hardening.recentResources.length > 0
                                        text: "Clear"
                                        flat: true
                                        onClicked: hardening.clearRecentResources()
                                    }
                                }

                                Label {
                                    visible: hardening.recentResources.length === 0
                                    Layout.fillWidth: true
                                    text: "No recent engineering resources yet."
                                    color: root.theme.muted
                                    font.pixelSize: 9
                                }

                                ListView {
                                    id: resourceList
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    reuseItems: true
                                    spacing: 2
                                    model: hardening.recentResources
                                    ScrollBar.vertical: ScrollBar {}

                                    delegate: Rectangle {
                                        required property int index
                                        required property string modelData
                                        width: ListView.view.width
                                        height: 48
                                        radius: 4
                                        color: resourceMouse.containsMouse ? root.theme.surfaceRaised : root.theme.surfaceSoft
                                        border.width: 1
                                        border.color: root.theme.lineSoft

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 9
                                            anchors.rightMargin: 8
                                            spacing: 8

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 1
                                                Label {
                                                    Layout.fillWidth: true
                                                    text: root.resourceName(modelData)
                                                    color: hardening.recentResourceExists(index) ? root.theme.text : root.theme.muted
                                                    font.pixelSize: 9
                                                    font.weight: Font.DemiBold
                                                    elide: Text.ElideRight
                                                }
                                                Label {
                                                    Layout.fillWidth: true
                                                    text: modelData
                                                    color: root.theme.muted
                                                    font.pixelSize: 7
                                                    elide: Text.ElideMiddle
                                                }
                                            }

                                            Label {
                                                text: hardening.recentResourceExists(index) ? "OPEN" : "MISSING"
                                                color: hardening.recentResourceExists(index) ? root.theme.accent : root.theme.amber
                                                font.pixelSize: 7
                                                font.weight: Font.Bold
                                            }
                                        }

                                        MouseArea {
                                            id: resourceMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            enabled: hardening.recentResourceExists(index) && !workspace.busy
                                            onClicked: {
                                                if (workspace.openFile(hardening.recentResourceUrl(index)))
                                                    root.viewIndex = 1
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        SurfaceCard {
                            Layout.preferredWidth: Math.max(330, root.width * 0.32)
                            Layout.fillHeight: true
                            Layout.minimumHeight: 260
                            theme: root.theme

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        text: "Recent online endpoints"
                                        color: root.theme.text
                                        font.pixelSize: 10
                                        font.weight: Font.DemiBold
                                    }
                                    Item { Layout.fillWidth: true }
                                    Button {
                                        visible: hardening.recentEndpoints.length > 0
                                        text: "Clear"
                                        flat: true
                                        onClicked: hardening.clearRecentEndpoints()
                                    }
                                }

                                Label {
                                    visible: hardening.recentEndpoints.length === 0
                                    Layout.fillWidth: true
                                    text: "No recent MMS endpoints yet. Automatic reconnect remains disabled."
                                    color: root.theme.muted
                                    font.pixelSize: 9
                                    wrapMode: Text.WordWrap
                                }

                                ListView {
                                    id: endpointList
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    reuseItems: true
                                    spacing: 2
                                    model: hardening.recentEndpoints
                                    ScrollBar.vertical: ScrollBar {}

                                    delegate: Rectangle {
                                        required property int index
                                        required property string modelData
                                        width: ListView.view.width
                                        height: 38
                                        radius: 4
                                        color: endpointMouse.containsMouse ? root.theme.surfaceRaised : root.theme.surfaceSoft
                                        border.width: 1
                                        border.color: root.theme.lineSoft

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 9
                                            anchors.rightMargin: 8
                                            spacing: 8
                                            Label {
                                                Layout.fillWidth: true
                                                text: modelData
                                                color: root.theme.textSoft
                                                font.pixelSize: 9
                                                elide: Text.ElideRight
                                            }
                                            Label {
                                                text: "BROWSE"
                                                color: root.theme.accent
                                                font.pixelSize: 7
                                                font.weight: Font.Bold
                                            }
                                        }

                                        MouseArea {
                                            id: endpointMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: root.endpointRequested(hardening.recentHost(index),
                                                                               hardening.recentPort(index))
                                        }
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: "Selecting an endpoint restores address context only; it never auto-connects."
                                    color: root.theme.muted
                                    font.pixelSize: 7
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }

                    Item { Layout.preferredHeight: 8 }
                }
            }

            SclWorkspace {
                theme: root.theme
                workspace: root.workspace
            }
        }
    }
}
