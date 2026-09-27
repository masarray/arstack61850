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
    required property var reports
    required property var utilities
    required property var controls
    required property var engineering
    required property var productState

    property int activeSection: 0
    property string activeSectionTitle: "Data Model"
    property var selectedModelNode: context.treeModel.selectedNode
    property string offeredCatalogKey: ""
    property string datasetRouteStatus: ""


    signal watchedDataChanged()

    function watchedRows() { return globalDataPane.snapshotRows() }
    function refreshWatchedGlobalData() { return globalDataPane.refreshWatched() }

    function activeIedLabel() {
        return context.iedName && context.iedName.length ? context.iedName : "IED"
    }

    // Shared read-only entry after either Open SCL or live MMS discovery.
    function openDatasetSignals() {
        if (!context.loaded || context.selectionRequired) return false
        root.datasetRouteStatus = ""
        root.selectSection(1, "Dataset Signals")
        return true
    }

    function inspectBoundStaticRcb() {
        if (!reports.selectStaticRcbForDataSet(reports.selectedDataSetIndex)) {
            root.datasetRouteStatus = "No verified static RCB is bound to this DataSet. Membership remains read-only."
            return false
        }
        root.datasetRouteStatus = ""
        root.selectSection(2, "Reports")
        return true
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

    Connections {
        target: globalDataPane
        function onWatchSnapshotChanged() { root.watchedDataChanged() }
    }

    onActiveSectionChanged: ensureActiveService()
    Connections {
        target: context
        function onContextChanged() {
            if (!context.loaded || context.selectionRequired ||
                context.authorityKey !== "live-discovery" || context.dataSetCount <= 0)
                return
            const key = context.structuralFingerprint + "|" + context.iedName
            if (!context.structuralFingerprint.length || key === root.offeredCatalogKey)
                return
            root.offeredCatalogKey = key
            signalCatalogDialog.open()
        }
    }

    Connections {
        target: reports
        function onSelectionChanged() { root.datasetRouteStatus = "" }
    }

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

    // A discovery creates a model, not a subscription. This catalog only
    // chooses the next view; no polling, DataSet write, RCB enable or GI.
    Dialog {
        id: signalCatalogDialog
        objectName: "iedBrowserSignalCatalog"
        title: "Signal Catalog"
        modal: true
        width: 430
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.NoButton
        contentItem: ColumnLayout {
            spacing: 10
            Label {
                Layout.fillWidth: true
                text: context.iedName + " · " + context.dataSetCount
                      + " discovered DataSet(s)"
                color: root.theme.text
                font.pixelSize: root.theme.labelSize
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: "Choose ordered static Dataset Signals, or browse the complete IED model. Neither choice enables reporting."
                color: root.theme.textSoft
                font.pixelSize: root.theme.captionSize
                wrapMode: Text.WordWrap
            }
            ActionButton {
                theme: root.theme
                text: "Dataset Signals"
                enabled: context.dataSetCount > 0
                onClicked: {
                    root.openDatasetSignals()
                    signalCatalogDialog.close()
                }
            }
            ActionButton {
                theme: root.theme
                text: "Browse Data Model"
                onClicked: {
                    root.selectSection(0, "Data Model")
                    signalCatalogDialog.close()
                }
            }
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

        // Two distinct, compact tool rows keep every label and command visible
        // at the supported 1024px window width. The old single RowLayout was
        // wider than its container, leaving ghost/blank controls in the GUI.
        Rectangle {
            Layout.fillWidth: true
            id: browserCommandBar
            objectName: "iedBrowserCommandBar"
            Layout.preferredHeight: 96
            color: root.theme.chrome
            border.width: 1
            border.color: root.theme.lineSoft

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                anchors.topMargin: 7
                anchors.bottomMargin: 7
                spacing: 5

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 35
                    spacing: 8

                    ColumnLayout {
                        Layout.preferredWidth: 240
                        spacing: 1
                        Label {
                            text: "MODEL"
                            color: root.theme.text
                            font.pixelSize: root.theme.labelSize
                            font.weight: Font.DemiBold
                        }
                        Label {
                            Layout.fillWidth: true
                            text: context.loaded
                                  ? root.activeIedLabel() + " · " + context.authority
                                  : "Open SCL or discover one IED endpoint"
                            color: root.theme.muted
                            font.pixelSize: root.theme.captionSize
                            elide: Text.ElideRight
                        }
                    }

                    ActionButton {
                        theme: root.theme
                        text: "Open SCL"
                        enabled: !session.configurationLocked && !engineering.busy
                        onClicked: browserSclDialog.open()
                        ToolTip.visible: hovered
                        ToolTip.text: "Open SCL/CID/SCD/IID/ICD into the persistent Browser model."
                    }
                    ActionButton {
                        theme: root.theme
                        text: "Signal Catalog"
                        visible: context.loaded && !context.selectionRequired
                        onClicked: signalCatalogDialog.open()
                        ToolTip.visible: hovered
                        ToolTip.text: "Choose Dataset Signals or the complete Data Model; no automatic RCB enable."
                    }
                    ActionButton {
                        theme: root.theme
                        text: engineering.busy ? "Saving…" : "Save SCL"
                        visible: context.loaded && context.authorityKey === "live-discovery"
                        enabled: engineering.engineeringContextExportSupported && !engineering.busy
                        onClicked: saveSclDialog.open()
                        ToolTip.visible: hovered
                        ToolTip.text: "Save the cached canonical IED model locally; no rediscovery is performed."
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
                            font.pixelSize: root.theme.labelSize
                            font.weight: Font.DemiBold
                        }
                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: context.logicalDeviceCount + " LD · " + context.logicalNodeCount + " LN · "
                                  + context.dataAttributeCount + " DA · "
                                  + (context.online ? "ONLINE" : "OFFLINE")
                            color: root.theme.muted
                            font.pixelSize: root.theme.captionSize
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 37
                    spacing: 8

                    Label {
                        text: "IED endpoint"
                        color: root.theme.textSoft
                        font.pixelSize: root.theme.captionSize
                    }
                    TextField {
                        id: hostField
                        objectName: "iedBrowserHostField"
                        Layout.preferredWidth: 190
                        placeholderText: "IED IP / hostname"
                        text: session.host
                        selectByMouse: true
                        enabled: !session.configurationLocked
                        onEditingFinished: session.host = text
                    }
                    SpinBox {
                        id: portField
                        objectName: "iedBrowserPortField"
                        Layout.preferredWidth: 102
                        from: 1
                        to: 65535
                        value: session.port
                        editable: true
                        enabled: !session.configurationLocked
                        onValueModified: session.port = value
                    }
                    ActionButton {
                        theme: root.theme
                        text: "Online"
                        visible: context.loaded && !session.connected
                        enabled: !session.busy && !context.selectionRequired
                        primary: enabled
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
                    ActionButton {
                        theme: root.theme
                        text: session.busy && !session.connected ? "Discovering…" : "Discover IED"
                        visible: !session.connected
                        enabled: !session.busy
                        onClicked: {
                            session.host = hostField.text
                            session.port = portField.value
                            session.discoverAndConnect()
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: "Discover the live MMS model and publish it into this Browser context."
                    }
                    ActionButton {
                        theme: root.theme
                        text: "Disconnect"
                        visible: session.connected || session.busy
                        enabled: session.connected || session.busy
                        danger: true
                        onClicked: session.disconnectFromIed()
                    }
                    Rectangle {
                        width: 7
                        height: 7
                        radius: 4
                        color: session.lastError.length ? root.theme.red
                                                       : session.connected ? root.theme.green
                                                                           : session.busy ? root.theme.amber
                                                                                          : root.theme.muted
                    }
                    Label {
                        objectName: "iedBrowserSessionStatus"
                        Layout.fillWidth: true
                        text: session.lastError.length ? session.lastError
                              : session.stateText + (session.endpoint.length ? " · " + session.endpoint : "")
                        color: session.lastError.length ? root.theme.red : root.theme.textSoft
                        font.pixelSize: root.theme.captionSize
                        elide: Text.ElideRight
                        ToolTip.visible: sessionStatusMouse.containsMouse
                        ToolTip.text: text
                        MouseArea {
                            id: sessionStatusMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.NoButton
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
                SplitView.minimumWidth: 276
                SplitView.maximumWidth: 560
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
                                font.pixelSize: root.theme.labelSize
                                font.weight: Font.DemiBold
                            }
                            Label {
                                Layout.fillWidth: true
                                text: root.breadcrumb()
                                color: root.theme.muted
                                font.pixelSize: root.theme.captionSize
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
                        context: root.context
                        client: root.client
                        routeMessage: root.datasetRouteStatus
                        onInspectStaticReportRequested: root.inspectBoundStaticRcb()
                        onInspectRequested: function(reference) {
                            if (root.context.treeModel.selectReference(reference))
                                root.selectSection(0, "Data Model")
                        }
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
