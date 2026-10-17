// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    required property var theme
    required property var fleet
    required property var session
    required property var client
    required property var context
    required property var gooseBackend
    required property var reports
    required property var utilities
    required property var controls
    required property var engineering
    required property var productState

    property int activeSection: 0
    property string activeSectionTitle: "Data Model"
    property var selectedModelNode: context.treeModel.selectedNode
    property int selectedGooseIndex: -1

    signal watchedDataChanged()

    component RibbonAction: Button {
        id: action
        required property string badge
        property bool emphasized: false

        implicitWidth: 80
        implicitHeight: 86
        hoverEnabled: true
        leftPadding: 3
        rightPadding: 3

        contentItem: Column {
            spacing: 5
            anchors.centerIn: parent
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 37
                height: 35
                radius: 6
                color: action.enabled
                       ? (action.emphasized ? root.theme.accent : root.theme.surfaceSoft)
                       : root.theme.surfaceRaised
                border.width: 1
                border.color: action.enabled
                              ? (action.emphasized ? root.theme.accent : root.theme.line)
                              : root.theme.lineSoft
                Label {
                    anchors.centerIn: parent
                    text: action.badge
                    color: action.enabled
                           ? (action.emphasized ? "#ffffff" : root.theme.textSoft)
                           : root.theme.muted
                    font.pixelSize: action.badge.length > 2 ? 12 : 16
                    font.weight: Font.DemiBold
                }
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 77
                height: 33
                text: action.text
                color: action.enabled ? root.theme.text : root.theme.muted
                font.pixelSize: 11
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignTop
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }

        background: Rectangle {
            radius: 4
            color: !action.enabled ? "transparent"
                   : action.pressed ? root.theme.surfaceSoft
                   : action.hovered ? root.theme.accentSoft : "transparent"
            border.width: action.hovered && action.enabled ? 1 : 0
            border.color: root.theme.accent
        }
    }

    function watchedRows() { return globalDataPane.snapshotRows() }
    function refreshWatchedGlobalData() { return globalDataPane.refreshWatched() }

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
            return ref.length ? root.activeIedLabel() + " › Data Model › " + ref : root.activeIedLabel() + " › Data Model"
        }
        if (activeSection === 1) {
            if (reports.selectedDataSetIndex >= 0 && reports.selectedDataSetIndex < reports.dataSets.length)
                return root.activeIedLabel() + " › DataSets › " + (reports.dataSets[reports.selectedDataSetIndex].reference || "")
            return root.activeIedLabel() + " › DataSets"
        }
        if (activeSection === 2)
            return reports.selectedRcb.reference ? root.activeIedLabel() + " › Reports › " + reports.selectedRcb.reference : root.activeIedLabel() + " › Reports"
        if (activeSection === 3)
            return utilities.selectedSettingGroup.reference
                    ? root.activeIedLabel() + " › Setting Groups › " + utilities.selectedSettingGroup.reference
                    : root.activeIedLabel() + " › Setting Groups"
        if (activeSection === 4)
            return root.activeIedLabel() + " › Files" + (utilities.currentDirectory.length ? " › " + utilities.currentDirectory : "")
        if (activeSection === 5) return root.activeIedLabel() + " › GOOSE"
        return root.activeIedLabel() + " › Global Data"
    }

    function selectSection(section, title) {
        root.activeSection = section
        root.activeSectionTitle = title
    }

    Connections {
        target: globalDataPane
        function onWatchSnapshotChanged() { root.watchedDataChanged() }
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

    Connections {
        target: context
        function onContextChanged() {
            root.selectedGooseIndex = -1
        }
    }

    FileDialog {
        id: browserSclDialog
        title: "Open IEC 61850 engineering model"
        fileMode: FileDialog.OpenFile
        nameFilters: ["IEC 61850 engineering files (*.scl *.cid *.scd *.iid *.icd)", "All files (*)"]
        onAccepted: {
            if (fleet.prepareForNewSource())
                fleet.activeEngineering.openFile(selectedFile)
        }
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
            Layout.preferredHeight: 112
            color: root.theme.chrome
            border.width: 1
            border.color: root.theme.lineSoft

            Flickable {
                anchors.fill: parent
                contentWidth: ribbonRow.width + 16
                contentHeight: height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.HorizontalFlick

                Row {
                    id: ribbonRow
                    x: 8
                    height: parent.height
                    spacing: 7

                    Column {
                        width: 168
                        height: parent.height
                        Row {
                            RibbonAction {
                                badge: "SCL"
                                text: "Open SCL"
                                enabled: !session.configurationLocked && !engineering.busy
                                onClicked: browserSclDialog.open()
                                ToolTip.visible: hovered
                                ToolTip.text: "Open SCL/CID/SCD/IID/ICD into the canonical Browser model."
                            }
                            RibbonAction {
                                badge: "↓"
                                text: engineering.busy ? "Saving…" : "Save SCL"
                                enabled: engineering.engineeringContextExportSupported && !engineering.busy
                                onClicked: saveSclDialog.open()
                                ToolTip.visible: hovered
                                ToolTip.text: "Export the cached canonical model locally without hidden rediscovery."
                            }
                        }
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "APPLICATION"
                            color: root.theme.muted
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                        }
                    }

                    Rectangle { width: 1; height: 96; y: 8; color: root.theme.lineSoft }

                    Column {
                        width: 282
                        height: parent.height
                        spacing: 4
                        Row {
                            spacing: 5
                            TextField {
                                id: hostField
                                width: 172
                                height: 29
                                text: session.host
                                placeholderText: "IED IP / hostname"
                                selectByMouse: true
                                enabled: !session.configurationLocked
                                font.pixelSize: 12
                                onEditingFinished: session.host = text
                            }
                            SpinBox {
                                id: portField
                                width: 80
                                height: 29
                                from: 1
                                to: 65535
                                value: session.port
                                editable: true
                                enabled: !session.configurationLocked
                                onValueModified: session.port = value
                            }
                        }
                        Row {
                            spacing: 6
                            Rectangle {
                                width: 8; height: 8; radius: 4
                                anchors.verticalCenter: parent.verticalCenter
                                color: session.lastError.length ? root.theme.red
                                                               : session.connected ? root.theme.green
                                                                                   : session.busy ? root.theme.amber
                                                                                                  : root.theme.muted
                            }
                            Label {
                                width: 244
                                text: session.stateText + " · " + session.endpoint
                                color: session.lastError.length ? root.theme.red : root.theme.textSoft
                                font.pixelSize: 11
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                            }
                        }
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "ENDPOINT"
                            color: root.theme.muted
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                        }
                    }

                    Rectangle { width: 1; height: 96; y: 8; color: root.theme.lineSoft }

                    Column {
                        width: 246
                        height: parent.height
                        Row {
                            RibbonAction {
                                badge: "ON"
                                text: "Online"
                                emphasized: session.connected
                                enabled: context.loaded && !session.connected && !session.busy && !context.selectionRequired
                                onClicked: {
                                    session.host = hostField.text
                                    session.port = portField.value
                                    session.connectUsingEngineeringContext()
                                }
                            }
                            RibbonAction {
                                badge: "IED"
                                text: session.busy && !session.connected ? "Working…" : "Discover IED"
                                enabled: !session.connected && !session.busy
                                onClicked: {
                                    session.host = hostField.text
                                    session.port = portField.value
                                    session.discoverAndConnect()
                                }
                            }
                            RibbonAction {
                                badge: "×"
                                text: "Close"
                                enabled: session.connected || session.busy
                                onClicked: session.disconnectFromIed()
                            }
                        }
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "IED SESSION"
                            color: root.theme.muted
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                        }
                    }

                    Rectangle { width: 1; height: 96; y: 8; color: root.theme.lineSoft }

                    Column {
                        width: 246
                        height: parent.height
                        Row {
                            RibbonAction {
                                badge: "R"
                                text: "Read"
                                enabled: client.connected && !client.operationBusy
                                         && root.selectedModelNode.readable === true
                                onClicked: client.readEngineeringSelected()
                            }
                            RibbonAction {
                                badge: "CO"
                                text: "Control"
                                enabled: session.connected
                                         && root.selectedModelNode.controlCandidate === true
                                         && !controls.busy
                                onClicked: controlDialog.openFor(root.selectedModelNode)
                            }
                            RibbonAction {
                                badge: "CLR"
                                text: "Clear"
                                enabled: globalDataPane.watchCount > 0
                                onClicked: root.selectSection(6, "Global Data")
                                ToolTip.visible: hovered
                                ToolTip.text: "Open Global Data to review or clear monitored entries."
                            }
                        }
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "DATA"
                            color: root.theme.muted
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                        }
                    }

                    Rectangle { width: 1; height: 96; y: 8; color: root.theme.lineSoft }

                    Column {
                        width: 332
                        height: parent.height
                        Row {
                            RibbonAction {
                                badge: "GI"
                                text: "Enable + GI"
                                emphasized: reports.active
                                enabled: root.activeSection === 2 && reports.connected
                                         && !reports.busy && !reports.active
                                         && !reports.cleanupRequired && reports.selectedRcbIndex >= 0
                                onClicked: reports.enableSelected(true)
                            }
                            RibbonAction {
                                badge: "OFF"
                                text: "Stop active RCB"
                                enabled: reports.connected && reports.active && !reports.busy
                                onClicked: reports.disableSelected()
                ToolTip.visible: hovered
                ToolTip.text: "Stop the MMS report subscription for "
                                  + (reports.subscriptionRcbReference || "the active RCB")
                                  + ", regardless of inspector selection."
                            }
                            RibbonAction {
                                badge: "DS"
                                text: "Add DataSet"
                                enabled: reports.connected && !reports.busy && !reports.active
                                onClicked: dataSetAuthorDialog.newDraft()
                            }
                            RibbonAction {
                                badge: "G"
                                text: "GOOSE"
                                enabled: context.loaded && context.gooseCount > 0
                                onClicked: {
                                    root.selectedGooseIndex = -1
                                    root.selectSection(5, "GOOSE")
                                }
                            }
                        }
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "SERVICES"
                            color: root.theme.muted
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                        }
                    }


                }
            }
        }

        Rectangle {
            visible: context.authorityKey === "scl" && context.candidateIeds.length > 1
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 42 : 0
            color: root.theme.amberSoft
            border.width: 1
            border.color: root.theme.amber

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 8
                Label {
                    text: context.selectionRequired
                          ? "Multi-IED SCL · select active IED"
                          : "Multi-IED SCL · open another IED in its own workspace"
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
                    visible: context.selectionRequired
                    enabled: !session.configurationLocked
                             && activeIedPicker.currentIndex >= 0
                    onClicked: context.selectIed(activeIedPicker.currentText)
                }
                Button {
                    text: "Open in new workspace"
                    enabled: activeIedPicker.currentIndex >= 0
                             && fleet.workspaceCount < 8
                    onClicked: fleet.openSclIedInNewWorkspace(activeIedPicker.currentText)
                    ToolTip.visible: hovered
                    ToolTip.text: "Keep this IED intact; open the selected IED from the same SCL source in a separate Browser slot."
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
                selectedGooseIndex: root.selectedGooseIndex
                section: root.activeSection
                onSectionRequested: function(section, title) {
                    root.selectSection(section, title)
                }
                onGooseRequested: function(index) {
                    root.selectedGooseIndex = index
                    root.selectSection(5, "GOOSE")
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
                            text: "Control…"
                            enabled: session.connected
                                     && root.selectedModelNode.controlCandidate === true
                                     && !controls.busy
                            onClicked: controlDialog.openFor(root.selectedModelNode)
                            ToolTip.visible: hovered
                            ToolTip.text: root.selectedModelNode.controlCandidate === true
                                          ? "Open guarded IEC 61850 Control for the canonical selected object."
                                          : "Select a command-ready FC=CO DataAttribute."
                        }

                        Button {
                            visible: root.activeSection === 0
                            text: "Add to Global Data"
                            enabled: root.selectedModelNode.kind === "DA"
                                     && root.selectedModelNode.readable === true
                            onClicked: globalDataPane.addSelectedData(root.selectedModelNode)
                        }

                        Button {
                            visible: root.activeSection === 0
                            text: "Add to DataSet draft…"
                            enabled: root.selectedModelNode.kind === "DA"
                                     && root.selectedModelNode.readable === true
                                     && root.selectedModelNode.mmsDomain
                                     && root.selectedModelNode.mmsItem
                            onClicked: dataSetAuthorDialog.addSelected(root.selectedModelNode)
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
                            visible: root.activeSection === 1
                            text: "New dynamic…"
                            enabled: reports.connected && !reports.busy && !reports.active
                            onClicked: dataSetAuthorDialog.newDraft()
                        }

                        Button {
                            visible: root.activeSection === 1
                            text: "Delete owned…"
                            enabled: reports.connected && !reports.busy && !reports.active
                                     && reports.selectedDataSetIndex >= 0
                                     && reports.selectedDataSetIndex < reports.dataSets.length
                                     && reports.dataSets[reports.selectedDataSetIndex].dynamicOwned === true
                            onClicked: dataSetAuthorDialog.requestDelete(
                                reports.dataSets[reports.selectedDataSetIndex])
                        }

                        Button {
                            visible: root.activeSection === 2
                            text: "Enable + GI"
                            enabled: reports.connected && !reports.busy && !reports.active
                                     && reports.selectedRcbIndex >= 0
                                     && reports.selectedRcb.dynamicBinding !== true
                            onClicked: reports.enableSelected(true)
                            ToolTip.visible: hovered
                            ToolTip.text: reports.selectedRcb.dynamicBinding === true
                                          ? "Restore the canonical static binding through Author… before using the legacy static enable path."
                                          : "Enable the selected static RCB and request GI."
                        }

                        Button {
                            visible: root.activeSection === 2
                            text: "Author…"
                            enabled: reports.connected && !reports.busy && !reports.active
                                     && reports.selectedRcbIndex >= 0
                                     && reports.dataSets.length > 0
                            onClicked: reportAuthorDialog.openForSelected()
                        }

                        Button {
                            visible: root.activeSection === 2
                            text: "Stop active RCB"
                            enabled: reports.connected && reports.active && !reports.busy
                            onClicked: reports.disableSelected()
                ToolTip.visible: hovered
                ToolTip.text: "Stop the MMS report subscription for "
                                  + (reports.subscriptionRcbReference || "the active RCB")
                                  + ", regardless of inspector selection."
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
                            text: root.gooseBackend.capturing ? "Read-only GOOSE capture active" : "GOOSE capture idle"
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

                    BrowserDataModelPane {
                        theme: root.theme
                        client: root.client
                        modelProvider: root.context
                    }

                    BrowserDataSetPane {
                        theme: root.theme
                        reports: root.reports
                        modelProvider: root.context
                        onDataModelRequested: root.selectSection(0, "Data Model")
                        onWatchAttributeRequested: function(node) {
                            globalDataPane.addSelectedData(node)
                        }
                    }

                    BrowserReportPane {
                        theme: root.theme
                        reports: root.reports
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
                        monitor: root.gooseBackend
                        selectedIndex: root.selectedGooseIndex
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

                BrowserControlDialog {
                    id: controlDialog
                    theme: root.theme
                    session: root.session
                    controls: root.controls
                }

                BrowserDataSetAuthoringDialog {
                    id: dataSetAuthorDialog
                    theme: root.theme
                    reports: root.reports
                }

                BrowserReportAuthoringDialog {
                    id: reportAuthorDialog
                    theme: root.theme
                    reports: root.reports
                }
            }
        }
    }
}
