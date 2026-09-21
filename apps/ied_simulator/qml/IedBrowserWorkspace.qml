// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    required property var theme
    required property var session
    required property var client
    required property var context
    required property var reports
    required property var utilities
    required property var engineering
    required property var productState

    property int activeSection: 0
    property string activeSectionTitle: "Data Model"
    property var selectedModelNode: context.treeModel.selectedNode

    function activeIedLabel() {
        return context.iedName && context.iedName.length ? context.iedName : "IED"
    }

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
            return ref.length ? root.activeIedLabel() + " / Data Model / " + ref : root.activeIedLabel() + " / Data Model"
        }
        if (activeSection === 1) {
            if (reports.selectedDataSetIndex >= 0 && reports.selectedDataSetIndex < reports.dataSets.length)
                return root.activeIedLabel() + " / DataSets / " + (reports.dataSets[reports.selectedDataSetIndex].reference || "")
            return root.activeIedLabel() + " / DataSets"
        }
        if (activeSection === 2)
            return reports.selectedRcb.reference ? root.activeIedLabel() + " / Reports / " + reports.selectedRcb.reference : root.activeIedLabel() + " / Reports"
        if (activeSection === 3)
            return utilities.selectedSettingGroup.reference
                    ? root.activeIedLabel() + " / Setting Groups / " + utilities.selectedSettingGroup.reference
                    : root.activeIedLabel() + " / Setting Groups"
        if (activeSection === 4)
            return root.activeIedLabel() + " / Files" + (utilities.currentDirectory.length ? " / " + utilities.currentDirectory : "")
        if (activeSection === 5) return root.activeIedLabel() + " / GOOSE"
        return root.activeIedLabel() + " / Global Data"
    }

    function selectSection(section, title) {
        root.activeSection = section
        root.activeSectionTitle = title
    }

    onActiveSectionChanged: ensureActiveService()

    Shortcut { sequence: "Alt+1"; enabled: root.visible; onActivated: root.selectSection(0, "Data Model") }
    Shortcut { sequence: "Alt+2"; enabled: root.visible; onActivated: root.selectSection(1, "DataSets") }
    Shortcut { sequence: "Alt+3"; enabled: root.visible; onActivated: root.selectSection(2, "Reports") }
    Shortcut { sequence: "Alt+4"; enabled: root.visible; onActivated: root.selectSection(3, "Setting Groups") }
    Shortcut { sequence: "Alt+5"; enabled: root.visible; onActivated: root.selectSection(4, "Files") }
    Shortcut { sequence: "Alt+6"; enabled: root.visible; onActivated: root.selectSection(5, "GOOSE") }
    Shortcut { sequence: "Alt+7"; enabled: root.visible; onActivated: root.selectSection(6, "Global Data") }

    Connections {
        target: session
        function onStateChanged() {
            if (session.connected)
                root.ensureActiveService()
        }
    }

    FileDialog {
        id: browserSclDialog
        title: "Open IEC 61850 engineering model"
        fileMode: FileDialog.OpenFile
        nameFilters: ["IEC 61850 engineering files (*.scl *.cid *.scd *.iid *.icd)", "All files (*)"]
        onAccepted: engineering.openFile(selectedFile)
    }

    FileDialog {
        id: saveSclDialog
        title: "Save canonical IED model"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "iid"
        nameFilters: [
            "IEC 61850 IID (*.iid)",
            "IEC 61850 ICD (*.icd)",
            "IEC 61850 CID (*.cid)",
            "IEC 61850 SCD (*.scd)"
        ]
        onAccepted: engineering.exportEngineeringContext(selectedFile, "ed2")
    }

    Rectangle { anchors.fill: parent; color: root.theme.background }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 62
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
                        text: "MODEL"
                        color: root.theme.text
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: context.loaded
                              ? root.activeIedLabel() + " · " + context.authority
                              : "Open SCL or discover one IED endpoint"
                        color: root.theme.muted
                        font.pixelSize: 8
                    }
                }

                Button {
                    text: "Open SCL"
                    enabled: !session.configurationLocked && !engineering.busy
                    onClicked: browserSclDialog.open()
                    ToolTip.visible: hovered
                    ToolTip.text: "Open SCL/CID/SCD/IID/ICD into the persistent Browser model."
                }

                Button {
                    text: engineering.busy ? "Saving…" : "Save SCL"
                    visible: context.loaded && context.authorityKey === "live-discovery"
                    enabled: engineering.engineeringContextExportSupported
                             && !engineering.busy
                    onClicked: saveSclDialog.open()
                    ToolTip.visible: hovered
                    ToolTip.text: context.online
                                  ? "Save the cached canonical IED model locally; no rediscovery is performed."
                                  : "Save the persistent canonical IED model while offline."
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
                    text: "Online"
                    visible: context.loaded
                    enabled: !session.connected && !session.busy && !context.selectionRequired
                    onClicked: {
                        session.host = hostField.text
                        session.port = portField.value
                        session.connectUsingEngineeringContext()
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: context.authorityKey === "scl"
                                  ? "Connect using the active trusted SCL model."
                                  : "Reconnect/discover the active IED endpoint."
                }

                Button {
                    text: session.busy && !session.connected ? "Discovering…" : "Discover IED"
                    enabled: !session.connected && !session.busy
                    onClicked: {
                        session.host = hostField.text
                        session.port = portField.value
                        session.discoverAndConnect()
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: "Ignore trusted SCL for this connect, discover the live MMS model, and publish it into this Browser context."
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
                    visible: context.loaded
                    spacing: 0
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: context.iedName.length ? context.iedName
                              : context.selectionRequired ? "Select active IED" : "IED model"
                        color: root.theme.text
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: context.logicalDeviceCount + " LD · " + context.logicalNodeCount + " LN · "
                              + context.dataAttributeCount + " DA · "
                              + (context.online ? "ONLINE" : "OFFLINE")
                        color: root.theme.muted
                        font.pixelSize: 8
                    }
                }
            }
        }

        Rectangle {
            visible: context.selectionRequired
            Layout.fillWidth: true
            Layout.preferredHeight: context.selectionRequired ? 42 : 0
            color: root.theme.amberSoft
            border.width: 1
            border.color: root.theme.amber

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 8
                Label {
                    text: "Multi-IED SCL · select the active IED"
                    color: root.theme.text
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                }
                ComboBox {
                    id: activeIedPicker
                    Layout.preferredWidth: 220
                    model: context.candidateIeds
                    enabled: !session.configurationLocked
                }
                Button {
                    text: "Use IED"
                    enabled: !session.configurationLocked
                             && activeIedPicker.currentIndex >= 0
                    onClicked: context.selectIed(activeIedPicker.currentText)
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: context.lastError
                    color: root.theme.muted
                    font.pixelSize: 8
                    elide: Text.ElideRight
                }
            }
        }

        SplitView {
            id: browserSplit
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            IedBrowserNavigation {
                id: browserNavigation
                SplitView.preferredWidth: root.productState.browserNavigationWidth
                SplitView.minimumWidth: 240
                SplitView.maximumWidth: 520
                SplitView.fillHeight: true
                theme: root.theme
                session: root.session
                client: root.client
                context: root.context
                reports: root.reports
                utilities: root.utilities
                globalDataCount: globalDataPane.watchCount
                section: root.activeSection
                onSectionRequested: function(section, title) {
                    root.selectSection(section, title)
                }
                onWidthChanged: splitterPersist.restart()
            }

            Timer {
                id: splitterPersist
                interval: 250
                repeat: false
                onTriggered: {
                    const candidate = Math.round(browserNavigation.width)
                    if (candidate >= 240 && candidate <= 520
                            && Math.abs(candidate - root.productState.browserNavigationWidth) >= 2)
                        root.productState.browserNavigationWidth = candidate
                }
            }

            ColumnLayout {
                SplitView.fillWidth: true
                SplitView.fillHeight: true
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

                        Label {
                            text: "COMMANDS"
                            color: root.theme.muted
                            font.pixelSize: 7
                            font.weight: Font.DemiBold
                            Layout.rightMargin: 2
                        }

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
                            onClicked: client.readEngineeringSelected()
                        }

                        Button {
                            visible: root.activeSection === 0
                            text: "Add to Global Data"
                            enabled: root.selectedModelNode.kind === "DA"
                                     && root.selectedModelNode.readable === true
                            onClicked: globalDataPane.addSelectedData(root.selectedModelNode)
                        }

                        Button {
                            visible: root.activeSection === 1
                            text: "Add to Global Data"
                            enabled: reports.selectedDataSetIndex >= 0
                                     && reports.selectedDataSetIndex < reports.dataSets.length
                            onClicked: {
                                var selected = reports.dataSets[reports.selectedDataSetIndex]
                                globalDataPane.addDataSet(
                                    selected.reference || "",
                                    reports.selectedDataSetMembers)
                            }
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
                            visible: root.activeSection === 2
                            text: "Add to Global Data"
                            enabled: reports.selectedRcbIndex >= 0
                            onClicked: globalDataPane.addReport(reports.selectedRcb)
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

                        Button {
                            visible: root.activeSection === 5
                            text: "Add to Global Data"
                            enabled: goosePane.selectedIndex >= 0
                            onClicked: globalDataPane.addGoose(goosePane.selectedStream)
                        }

                        Button {
                            visible: root.activeSection === 6
                            text: client.operationBusy ? "Refreshing…" : "Refresh watched"
                            enabled: client.connected && !client.operationBusy
                                     && globalDataPane.watchCount > 0
                            onClicked: globalDataPane.refreshWatched()
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
                        modelProvider: root.context
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
                        showInventoryPanel: false
                    }

                    FilesWorkspace {
                        theme: root.theme
                        files: root.utilities
                        showConnectionHeader: false
                    }

                    BrowserGoosePane {
                        id: goosePane
                        theme: root.theme
                        context: root.context
                    }

                    BrowserGlobalDataPane {
                        id: globalDataPane
                        theme: root.theme
                        context: root.context
                        client: root.client
                        reports: root.reports
                    }
                }

                BrowserStatusConsole {
                    theme: root.theme
                    session: root.session
                    client: root.client
                    context: root.context
                    reports: root.reports
                    utilities: root.utilities
                }
            }
        }
    }
}
