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
    required property var browserSession
    required property var context

    property int viewIndex: 0

    signal browserRequested()
    signal openSourceRequested(url fileUrl)
    signal discoverRequested(string host, int port)
    signal endpointRequested(string host, int port)
    signal simulatorRequested()
    signal snifferRequested()

    function resourceName(path) {
        var normalized = String(path).replace(/\\/g, "/")
        var slash = normalized.lastIndexOf("/")
        return slash >= 0 ? normalized.substring(slash + 1) : normalized
    }

    FileDialog {
        id: openDialog
        title: "Open IEC 61850 engineering model"
        fileMode: FileDialog.OpenFile
        nameFilters: ["IEC 61850 engineering files (*.scl *.cid *.scd *.iid *.icd)", "All files (*)"]
        onAccepted: root.openSourceRequested(selectedFile)
    }

    Dialog {
        id: discoverDialog
        modal: true
        title: "Discover IED"
        standardButtons: Dialog.NoButton
        width: 430
        x: Math.max(12, Math.round((root.width - width) / 2))
        y: Math.max(12, Math.round((root.height - height) / 2))
        onOpened: {
            discoverHost.text = browserSession.host
            discoverPort.value = browserSession.port
            discoverHost.forceActiveFocus()
            discoverHost.selectAll()
        }

        contentItem: ColumnLayout {
            spacing: 10

            Label {
                Layout.fillWidth: true
                text: "Build the IED engineering model directly from MMS discovery, then open it in the same Browser used by Open SCL."
                color: root.theme.textSoft
                font.pixelSize: 9
                wrapMode: Text.WordWrap
            }

            Label { text: "IED IP / hostname"; color: root.theme.text; font.pixelSize: 9 }
            TextField {
                id: discoverHost
                Layout.fillWidth: true
                placeholderText: "192.168.1.100"
                selectByMouse: true
            }

            Label { text: "MMS TCP port"; color: root.theme.text; font.pixelSize: 9 }
            SpinBox {
                id: discoverPort
                from: 1
                to: 65535
                value: 102
                editable: true
            }

            Label {
                Layout.fillWidth: true
                text: "Discovery is explicit. Recent IED entries never auto-connect on startup."
                color: root.theme.muted
                font.pixelSize: 8
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button { text: "Cancel"; onClicked: discoverDialog.close() }
                ActionButton {
                    theme: root.theme
                    text: "Discover IED"
                    primary: true
                    enabled: discoverHost.text.trim().length > 0
                    onClicked: {
                        const host = discoverHost.text.trim()
                        const port = discoverPort.value
                        discoverDialog.close()
                        root.discoverRequested(host, port)
                    }
                }
            }
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
                    text: "Configuration"
                    checkable: true
                    checked: root.viewIndex === 1
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
                    primary: true
                    enabled: !workspace.busy
                    onClicked: openDialog.open()
                }

                ActionButton {
                    theme: root.theme
                    text: "Discover IED"
                    enabled: true
                    onClicked: discoverDialog.open()
                }

                Item { Layout.fillWidth: true }

                Label {
                    Layout.maximumWidth: 430
                    text: context.loaded
                          ? context.iedName + " · " + context.authority
                          : "No active IED engineering context"
                    color: context.loaded ? root.theme.textSoft : root.theme.muted
                    font.pixelSize: 9
                    elide: Text.ElideMiddle
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
                        text: "Start from an engineering file or a live IED. Both paths converge into one persistent IED Browser model."
                        color: root.theme.textSoft
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.width >= 1260 ? 5 : root.width >= 760 ? 3 : 2
                        columnSpacing: 8
                        rowSpacing: 8

                        SurfaceCard {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 112
                            theme: root.theme
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 3
                                Label { text: "Open SCL"; color: root.theme.text; font.pixelSize: 10; font.weight: Font.DemiBold }
                                Label {
                                    Layout.fillWidth: true
                                    text: "Open SCL / CID / SCD / IID / ICD, browse offline, then go Online from the same Browser."
                                    color: root.theme.muted
                                    font.pixelSize: 8
                                    wrapMode: Text.WordWrap
                                }
                                Item { Layout.fillHeight: true }
                                ActionButton {
                                    theme: root.theme
                                    text: "Open SCL"
                                    primary: true
                                    enabled: !workspace.busy
                                    onClicked: openDialog.open()
                                }
                            }
                        }

                        SurfaceCard {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 112
                            theme: root.theme
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 3
                                Label { text: "Discover IED"; color: root.theme.text; font.pixelSize: 10; font.weight: Font.DemiBold }
                                Label {
                                    Layout.fillWidth: true
                                    text: "Connect to one MMS endpoint, discover its structure, and build the canonical IED model."
                                    color: root.theme.muted
                                    font.pixelSize: 8
                                    wrapMode: Text.WordWrap
                                }
                                Item { Layout.fillHeight: true }
                                ActionButton {
                                    theme: root.theme
                                    text: "Discover IED"
                                    enabled: true
                                    onClicked: discoverDialog.open()
                                }
                            }
                        }

                        SurfaceCard {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 112
                            theme: root.theme
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 3
                                Label { text: "Simulate IED"; color: root.theme.text; font.pixelSize: 10; font.weight: Font.DemiBold }
                                Label {
                                    Layout.fillWidth: true
                                    text: "Open the independent IEC 61850 simulator and commissioning workspace."
                                    color: root.theme.muted
                                    font.pixelSize: 8
                                    wrapMode: Text.WordWrap
                                }
                                Item { Layout.fillHeight: true }
                                ActionButton { theme: root.theme; text: "Simulate IED"; onClicked: root.simulatorRequested() }
                            }
                        }

                        SurfaceCard {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 112
                            theme: root.theme
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 3
                                Label { text: "Sniffer"; color: root.theme.text; font.pixelSize: 10; font.weight: Font.DemiBold }
                                Label {
                                    Layout.fillWidth: true
                                    text: "Open passive GOOSE capture, decode, supervision, and PCAP workflows."
                                    color: root.theme.muted
                                    font.pixelSize: 8
                                    wrapMode: Text.WordWrap
                                }
                                Item { Layout.fillHeight: true }
                                ActionButton { theme: root.theme; text: "Sniffer"; onClicked: root.snifferRequested() }
                            }
                        }

                        SurfaceCard {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 112
                            theme: root.theme
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 3
                                Label { text: "Configuration"; color: root.theme.text; font.pixelSize: 10; font.weight: Font.DemiBold }
                                Label {
                                    Layout.fillWidth: true
                                    text: "Review application state, Browser layout persistence, and raw-Ethernet runtime readiness."
                                    color: root.theme.muted
                                    font.pixelSize: 8
                                    wrapMode: Text.WordWrap
                                }
                                Item { Layout.fillHeight: true }
                                ActionButton { theme: root.theme; text: "Configuration"; onClicked: root.viewIndex = 1 }
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
                            Layout.minimumHeight: 270
                            theme: root.theme

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        text: "Recently opened SCL files"
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
                                    text: "No recently opened engineering files."
                                    color: root.theme.muted
                                    font.pixelSize: 9
                                }

                                ListView {
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
                                            enabled: hardening.recentResourceExists(index)
                                                     && !workspace.busy
                                            onClicked: {
                                                root.openSourceRequested(hardening.recentResourceUrl(index))
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        SurfaceCard {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 270
                            theme: root.theme

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        text: "Recently discovered IEDs"
                                        color: root.theme.text
                                        font.pixelSize: 10
                                        font.weight: Font.DemiBold
                                    }
                                    Item { Layout.fillWidth: true }
                                    Button {
                                        visible: hardening.recentDiscoveredIeds.length > 0
                                        text: "Clear"
                                        flat: true
                                        onClicked: hardening.clearRecentDiscoveredIeds()
                                    }
                                }

                                Label {
                                    visible: hardening.recentDiscoveredIeds.length === 0
                                    Layout.fillWidth: true
                                    text: "No discovered IED history yet."
                                    color: root.theme.muted
                                    font.pixelSize: 9
                                }

                                ListView {
                                    id: discoveredIedList
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    reuseItems: true
                                    spacing: 2
                                    model: hardening.recentDiscoveredIeds
                                    ScrollBar.vertical: ScrollBar {}

                                    delegate: Rectangle {
                                        required property int index
                                        required property string modelData
                                        width: ListView.view.width
                                        height: 44
                                        radius: 4
                                        color: discoveredMouse.containsMouse ? root.theme.surfaceRaised : root.theme.surfaceSoft
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
                                            id: discoveredMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            enabled: true
                                            onClicked: root.endpointRequested(
                                                hardening.recentDiscoveredIedHost(index),
                                                hardening.recentDiscoveredIedPort(index))
                                        }
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: "Selecting a recent IED restores address context only. Discovery or Online connection remains explicit."
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

            ScrollView {
                contentWidth: availableWidth

                ColumnLayout {
                    width: Math.max(0, parent.width - 28)
                    x: 14
                    spacing: 10

                    Item { Layout.preferredHeight: 4 }
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "Configuration"
                            color: root.theme.text
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillWidth: true }
                        Button { text: "Back to Home"; onClicked: root.viewIndex = 0 }
                    }

                    SurfaceCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 112
                        theme: root.theme
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 6
                            Label { text: "Application state"; color: root.theme.text; font.pixelSize: 10; font.weight: Font.DemiBold }
                            Label {
                                Layout.fillWidth: true
                                text: hardening.settingsStatus
                                color: hardening.settingsHealthy ? root.theme.textSoft : root.theme.amber
                                font.pixelSize: 9
                                wrapMode: Text.WordWrap
                            }
                            Label {
                                text: "Automatic reconnect on startup: disabled"
                                color: root.theme.textSoft
                                font.pixelSize: 9
                            }
                            Label {
                                Layout.fillWidth: true
                                text: "State file: " + hardening.statePath
                                color: root.theme.muted
                                font.pixelSize: 8
                                elide: Text.ElideMiddle
                            }
                        }
                    }

                    SurfaceCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 112
                        theme: root.theme
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 6
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: "Raw Ethernet readiness"; color: root.theme.text; font.pixelSize: 10; font.weight: Font.DemiBold }
                                Item { Layout.fillWidth: true }
                                Button { text: "Refresh"; onClicked: hardening.refreshRuntimeReadiness() }
                            }
                            Label {
                                Layout.fillWidth: true
                                text: hardening.npcapStatus
                                color: hardening.rawEthernetReady ? root.theme.textSoft : root.theme.amber
                                font.pixelSize: 9
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    SurfaceCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 112
                        theme: root.theme
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 6
                            Label { text: "Browser layout"; color: root.theme.text; font.pixelSize: 10; font.weight: Font.DemiBold }
                            Label {
                                text: "Navigation width: " + hardening.browserNavigationWidth + " px"
                                color: root.theme.textSoft
                                font.pixelSize: 9
                            }
                            Label {
                                Layout.fillWidth: true
                                text: "The IED Browser splitter persists safely between sessions. Protocol runtime settings are not duplicated here."
                                color: root.theme.muted
                                font.pixelSize: 8
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    Item { Layout.preferredHeight: 8 }
                }
            }
        }
    }
}
