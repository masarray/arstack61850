// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property var theme
    required property var session
    required property var client
    required property var reports
    required property var utilities

    property int activeSection: 0
    property string activeSectionTitle: "Data Model"
    property var selectedModelNode: client.treeModel.selectedNode

    function ensureActiveService() {
        if (!session.connected)
            return
        if (activeSection === 1 || activeSection === 2)
            session.ensureReportsConnected()
        else if (activeSection === 3 || activeSection === 4)
            session.ensureUtilitiesConnected()
    }

    function breadcrumb() {
        if (activeSection === 0) {
            var ref = selectedModelNode.reference || selectedModelNode.label || ""
            return ref.length ? "IED / Data Model / " + ref : "IED / Data Model"
        }
        if (activeSection === 1) {
            if (reports.selectedDataSetIndex >= 0 && reports.selectedDataSetIndex < reports.dataSets.length)
                return "IED / DataSets / " + (reports.dataSets[reports.selectedDataSetIndex].reference || "")
            return "IED / DataSets"
        }
        if (activeSection === 2)
            return reports.selectedRcb.reference ? "IED / Reports / " + reports.selectedRcb.reference : "IED / Reports"
        if (activeSection === 3)
            return utilities.selectedSettingGroup.reference
                    ? "IED / Setting Groups / " + utilities.selectedSettingGroup.reference
                    : "IED / Setting Groups"
        if (activeSection === 4)
            return "IED / Files" + (utilities.currentDirectory.length ? " / " + utilities.currentDirectory : "")
        return "IED / GOOSE"
    }

    onActiveSectionChanged: ensureActiveService()

    Connections {
        target: session
        function onStateChanged() {
            if (session.connected)
                root.ensureActiveService()
        }
    }

    Rectangle { anchors.fill: parent; color: root.theme.background }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            color: root.theme.chrome
            border.width: 1
            border.color: root.theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 8

                ColumnLayout {
                    Layout.preferredWidth: 155
                    spacing: 1
                    Label {
                        text: "IED BROWSER"
                        color: root.theme.text
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: "One endpoint · persistent engineering navigation"
                        color: root.theme.muted
                        font.pixelSize: 8
                    }
                }

                TextField {
                    id: hostField
                    Layout.preferredWidth: 205
                    text: session.host
                    placeholderText: "IED IP / hostname"
                    selectByMouse: true
                    enabled: !session.configurationLocked
                    onEditingFinished: session.host = text
                }

                SpinBox {
                    id: portField
                    Layout.preferredWidth: 102
                    from: 1
                    to: 65535
                    value: session.port
                    editable: true
                    enabled: !session.configurationLocked
                    onValueModified: session.port = value
                }

                Button {
                    text: session.connected ? "Connected" : session.busy ? "Connecting..." : "Connect"
                    enabled: !session.connected && !session.busy
                    onClicked: {
                        session.host = hostField.text
                        session.port = portField.value
                        session.connectToIed()
                    }
                }

                Button {
                    text: "Disconnect"
                    enabled: session.connected || session.busy
                    onClicked: session.disconnectFromIed()
                }

                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: session.lastError.length ? root.theme.red
                                                   : session.connected ? root.theme.green
                                                                       : session.busy ? root.theme.amber
                                                                                      : root.theme.muted
                }

                ColumnLayout {
                    spacing: 0
                    Label {
                        text: session.stateText
                        color: session.lastError.length ? root.theme.red : root.theme.textSoft
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: session.endpoint
                        color: root.theme.muted
                        font.pixelSize: 8
                    }
                }

                Item { Layout.fillWidth: true }

                ColumnLayout {
                    visible: session.connected
                    spacing: 0
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: client.iedName.length ? client.iedName : "Online IED"
                        color: root.theme.text
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: client.logicalDeviceCount + " LD · " + client.logicalNodeCount + " LN · "
                              + client.dataAttributeCount + " DA"
                        color: root.theme.muted
                        font.pixelSize: 8
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            IedBrowserNavigation {
                Layout.preferredWidth: Math.max(300, Math.min(360, root.width * 0.27))
                Layout.fillHeight: true
                theme: root.theme
                session: root.session
                client: root.client
                reports: root.reports
                utilities: root.utilities
                section: root.activeSection
                onSectionRequested: function(section, title) {
                    root.activeSection = section
                    root.activeSectionTitle = title
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    color: root.theme.surface
                    border.width: 1
                    border.color: root.theme.lineSoft

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 10
                        spacing: 7

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label {
                                Layout.fillWidth: true
                                text: root.activeSectionTitle
                                color: root.theme.text
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                            }
                            Label {
                                Layout.fillWidth: true
                                text: root.breadcrumb()
                                color: root.theme.muted
                                font.pixelSize: 8
                                elide: Text.ElideMiddle
                            }
                        }

                        Button {
                            visible: root.activeSection === 0
                            text: client.operationBusy ? "Reading…" : "Read"
                            enabled: client.connected && !client.operationBusy
                                     && root.selectedModelNode.readable === true
                            onClicked: client.readSelected()
                        }

                        Button {
                            visible: root.activeSection === 2
                            text: "Enable + GI"
                            enabled: reports.connected && !reports.busy && !reports.active
                                     && reports.selectedRcbIndex >= 0
                            onClicked: reports.enableSelected(true)
                        }

                        Button {
                            visible: root.activeSection === 2
                            text: "Disable"
                            enabled: reports.connected && reports.active && !reports.busy
                            onClicked: reports.disableSelected()
                        }

                        Button {
                            visible: root.activeSection === 3
                            text: "Refresh"
                            enabled: utilities.connected && !utilities.operationBusy
                            onClicked: utilities.refreshSettingGroups()
                        }

                        Button {
                            visible: root.activeSection === 4
                            text: "Browse root"
                            enabled: utilities.connected && !utilities.operationBusy
                            onClicked: utilities.browseDirectory("")
                        }

                        Label {
                            visible: root.activeSection === 5
                            text: "Configured model view"
                            color: root.theme.muted
                            font.pixelSize: 8
                        }
                    }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: root.activeSection

                    MmsClientWorkspace {
                        theme: root.theme
                        client: root.client
                        showConnectionHeader: false
                        showNavigationPanel: false
                    }

                    BrowserDataSetPane {
                        theme: root.theme
                        reports: root.reports
                    }

                    ReportsWorkspace {
                        theme: root.theme
                        reports: root.reports
                        showConnectionHeader: false
                        showInventoryPanel: false
                    }

                    SettingsWorkspace {
                        theme: root.theme
                        settings: root.utilities
                        showConnectionHeader: false
                    }

                    FilesWorkspace {
                        theme: root.theme
                        files: root.utilities
                        showConnectionHeader: false
                    }

                    BrowserGoosePane {
                        theme: root.theme
                        client: root.client
                    }
                }
            }
        }
    }
}
