// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property var theme
    required property var backend

    signal openSclRequested()
    signal addIedRequested()
    signal folderRequested()

    property string selectedLd: ""
    property string selectedLn: ""
    property var logicalDevices: []
    property var logicalNodes: []
    property var tableRows: []
    property var collapsedObjects: ({})
    property string searchText: ""
    property bool eventStreamVisible: true
    property string pendingValue: backend.selectedValue.value || ""
    property string pendingQuality: backend.selectedValue.quality || "Good"
    property string pendingOrigin: backend.selectedValue.origin || "Simulator"

    function hasString(values, needle) {
        for (var i = 0; i < values.length; ++i) {
            if (values[i] === needle) return true
        }
        return false
    }

    function refreshEditor() {
        pendingValue = backend.selectedValue.value || ""
        pendingQuality = backend.selectedValue.quality || "Good"
        pendingOrigin = backend.selectedValue.origin || "Simulator"
    }

    function refreshNavigation() {
        var values = backend.values || []
        var devices = []
        for (var i = 0; i < values.length; ++i) {
            var ld = values[i].logicalDevice || ""
            if (ld.length && !hasString(devices, ld)) devices.push(ld)
        }
        logicalDevices = devices
        if (!hasString(devices, selectedLd)) selectedLd = devices.length ? devices[0] : ""
        rebuildLogicalNodes()
    }

    function rebuildLogicalNodes() {
        var values = backend.values || []
        var nodes = []
        for (var i = 0; i < values.length; ++i) {
            if ((values[i].logicalDevice || "") !== selectedLd) continue
            var ln = values[i].logicalNode || ""
            if (ln.length && !hasString(nodes, ln)) nodes.push(ln)
        }
        logicalNodes = nodes
        if (!hasString(nodes, selectedLn)) selectedLn = nodes.length ? nodes[0] : ""
        rebuildTable()
    }

    function rowMatches(point, query) {
        if (!query.length) return true
        var haystack = [
            point.dataObject || "",
            point.dataAttribute || "",
            point.value || "",
            point.fc || "",
            point.type || "",
            point.reference || ""
        ].join(" ").toLowerCase()
        return haystack.indexOf(query) >= 0
    }

    function rebuildTable() {
        var values = backend.values || []
        var order = []
        var groups = ({})
        var query = searchText.trim().toLowerCase()

        for (var i = 0; i < values.length; ++i) {
            var point = values[i]
            if ((point.logicalDevice || "") !== selectedLd ||
                    (point.logicalNode || "") !== selectedLn) continue
            var objectName = point.dataObject || "(unnamed)"
            if (groups[objectName] === undefined) {
                groups[objectName] = { rows: [], preferred: -1, matches: false }
                order.push(objectName)
            }
            var attribute = point.dataAttribute || ""
            if (groups[objectName].preferred < 0 || attribute === "stVal" ||
                    attribute.endsWith(".stVal") || attribute === "mag.f") {
                groups[objectName].preferred = i
            }
            if (rowMatches(point, query) || objectName.toLowerCase().indexOf(query) >= 0) {
                groups[objectName].matches = true
            }
            groups[objectName].rows.push({
                kind: "DA",
                name: attribute.length ? attribute : objectName,
                value: point.value || "",
                fc: point.fc || "",
                type: point.type || "",
                quality: point.quality || "",
                sourceIndex: i
            })
        }

        var result = []
        for (var j = 0; j < order.length; ++j) {
            var name = order[j]
            var group = groups[name]
            if (query.length && !group.matches) continue
            var preferredIndex = group.preferred >= 0 ? group.preferred : group.rows[0].sourceIndex
            var preferredPoint = values[preferredIndex] || ({})
            result.push({
                kind: "DO",
                name: name,
                value: preferredPoint.value || "",
                fc: "",
                type: preferredPoint.cdc || "",
                quality: preferredPoint.quality || "",
                sourceIndex: preferredIndex
            })
            if (collapsedObjects[name] === true) continue
            for (var k = 0; k < group.rows.length; ++k) result.push(group.rows[k])
        }
        tableRows = result
    }

    function chooseLogicalDevice(name) {
        if (selectedLd === name) return
        selectedLd = name
        rebuildLogicalNodes()
    }

    function chooseLogicalNode(name) {
        if (selectedLn === name) return
        selectedLn = name
        rebuildTable()
    }

    function toggleObject(name) {
        var next = ({})
        for (var key in collapsedObjects) next[key] = collapsedObjects[key]
        next[name] = !(collapsedObjects[name] === true)
        collapsedObjects = next
        rebuildTable()
    }

    function severityColor(severity) {
        if (severity === "Error") return theme.red
        if (severity === "Warning") return theme.amber
        if (severity === "Success") return theme.green
        return theme.accent
    }

    function selectedOptions() {
        return backend.selectedValue.options || []
    }

    Connections {
        target: backend
        function onValuesChanged() { root.refreshNavigation() }
        function onSelectionChanged() {
            root.refreshEditor()
            root.refreshNavigation()
        }
        function onModelChanged() { root.refreshNavigation() }
    }

    Component.onCompleted: refreshNavigation()

    Shortcut { sequence: "Ctrl+O"; onActivated: root.openSclRequested() }
    Shortcut {
        sequence: "F5"
        enabled: backend.imported && !backend.running && !backend.starting
        onActivated: backend.startSimulation()
    }
    Shortcut {
        sequence: "Shift+F5"
        enabled: backend.imported
        onActivated: backend.startAllSimulations()
    }
    Shortcut {
        sequence: "Ctrl+Shift+F5"
        enabled: backend.anyRunning
        onActivated: backend.stopAllSimulations()
    }

    Rectangle { anchors.fill: parent; color: theme.background }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Compact original ARStack command bar.  ARSAS contributes the IED
        // Explorer interaction/identity below, while this app keeps its own
        // fleet-oriented workflow and visual hierarchy.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: theme.navigation

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 10

                RowLayout {
                    Layout.preferredWidth: 258
                    spacing: 10
                    Image {
                        width: 34
                        height: 34
                        source: "qrc:/iedsim/assets/black-fascia-ied.svg"
                        sourceSize: Qt.size(68, 68)
                        fillMode: Image.PreserveAspectFit
                    }
                    ColumnLayout {
                        spacing: 0
                        Label {
                            text: "ARStack IED Lab"
                            color: theme.navigationText
                            font.pixelSize: 17
                            font.weight: Font.DemiBold
                        }
                        Label {
                            text: "Multi-IED simulation workspace"
                            color: theme.navigationMuted
                            font.pixelSize: 9
                        }
                    }
                }

                Rectangle {
                    width: 1
                    Layout.fillHeight: true
                    Layout.topMargin: 13
                    Layout.bottomMargin: 13
                    color: "#36504a"
                }

                IconAction {
                    theme: root.theme
                    text: "Open model"
                    iconSource: "qrc:/iedsim/assets/folder-open.svg"
                    onClicked: root.openSclRequested()
                }
                IconAction {
                    theme: root.theme
                    text: "Add model"
                    iconSource: "qrc:/iedsim/assets/upload.svg"
                    enabled: !backend.anyRunning
                    onClicked: root.addIedRequested()
                }

                Item { Layout.fillWidth: true }

                IconAction {
                    theme: root.theme
                    text: "Start selected"
                    iconSource: "qrc:/iedsim/assets/play.svg"
                    primary: true
                    enabled: backend.imported && !backend.running && !backend.starting &&
                             backend.selectedIed.enabled && backend.endpointConflict.length === 0 &&
                             backend.listenAddress.length > 0
                    onClicked: backend.startSimulation()
                }
                IconAction {
                    theme: root.theme
                    text: "Start all"
                    iconSource: "qrc:/iedsim/assets/radio-tower.svg"
                    enabled: backend.imported
                    onClicked: backend.startAllSimulations()
                }
                IconAction {
                    theme: root.theme
                    text: "Stop selected"
                    iconSource: "qrc:/iedsim/assets/square.svg"
                    danger: true
                    enabled: backend.running || backend.starting
                    onClicked: backend.stopSimulation()
                }
                IconAction {
                    theme: root.theme
                    text: "Stop all"
                    iconSource: "qrc:/iedsim/assets/x.svg"
                    danger: true
                    enabled: backend.anyRunning
                    onClicked: backend.stopAllSimulations()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            color: theme.chrome
            border.width: 1
            border.color: theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12
                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: backend.runningCount > 0 ? theme.green : theme.muted
                }
                Label {
                    text: backend.imported ? backend.modelStatus : "Open a model to build the IED Explorer"
                    color: theme.textSoft
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    text: backend.imported
                        ? (backend.logicalDeviceCount + " LD  ·  " + backend.dataObjectCount + " DO  ·  " + backend.dataAttributeCount + " leaves")
                        : ""
                    color: theme.muted
                    font.pixelSize: 10
                }
                IconAction {
                    theme: root.theme
                    compact: true
                    text: ""
                    iconSource: "qrc:/iedsim/assets/activity.svg"
                    onClicked: root.eventStreamVisible = !root.eventStreamVisible
                    ToolTip.visible: hovered
                    ToolTip.text: root.eventStreamVisible ? "Hide event stream" : "Show event stream"
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Ported from ARSAS: relay-fascia IED cards, live/ready badge,
            // per-IED fast actions, and a fixed endpoint workflow below the list.
            IedExplorer {
                Layout.preferredWidth: root.width >= 1360 ? 286 : 262
                Layout.fillHeight: true
                theme: root.theme
                backend: root.backend
            }

            Rectangle {
                Layout.preferredWidth: 194
                Layout.fillHeight: true
                color: theme.chrome
                border.width: 1
                border.color: theme.lineSoft

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        Layout.leftMargin: 10
                        Layout.rightMargin: 8
                        spacing: 7
                        Image {
                            width: 21
                            height: 21
                            source: "qrc:/iedsim/assets/black-fascia-ied.svg"
                            sourceSize: Qt.size(42, 42)
                            fillMode: Image.PreserveAspectFit
                        }
                        Label {
                            Layout.fillWidth: true
                            text: "MODEL TREE"
                            color: theme.text
                            font.pixelSize: 10
                            font.weight: Font.Bold
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: theme.lineSoft }

                    Label {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 29
                        Layout.leftMargin: 11
                        text: "Logical devices"
                        verticalAlignment: Text.AlignVCenter
                        color: theme.muted
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                    }
                    ListView {
                        id: ldList
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(220, contentHeight + 2)
                        clip: true
                        model: root.logicalDevices
                        ScrollBar.vertical: ScrollBar { }
                        delegate: Rectangle {
                            required property string modelData
                            width: ldList.width
                            height: 31
                            color: root.selectedLd === modelData ? theme.accentSoft : "transparent"
                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 11
                                spacing: 6
                                Label {
                                    text: "LD"
                                    color: root.selectedLd === modelData ? theme.accent : theme.muted
                                    font.pixelSize: 8
                                    font.weight: Font.Bold
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Label {
                                    width: parent.width - 34
                                    text: modelData
                                    verticalAlignment: Text.AlignVCenter
                                    color: root.selectedLd === modelData ? theme.accent : theme.textSoft
                                    font.pixelSize: 9
                                    font.weight: root.selectedLd === modelData ? Font.DemiBold : Font.Normal
                                    elide: Text.ElideRight
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            MouseArea { anchors.fill: parent; onClicked: root.chooseLogicalDevice(modelData) }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: theme.lineSoft }
                    Label {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 29
                        Layout.leftMargin: 11
                        text: "Logical nodes"
                        verticalAlignment: Text.AlignVCenter
                        color: theme.muted
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                    }
                    ListView {
                        id: lnList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: root.logicalNodes
                        ScrollBar.vertical: ScrollBar { }
                        delegate: Rectangle {
                            required property string modelData
                            width: lnList.width
                            height: 31
                            color: root.selectedLn === modelData ? theme.accentSoft : "transparent"
                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 11
                                spacing: 6
                                Label {
                                    text: "LN"
                                    color: root.selectedLn === modelData ? theme.accent : theme.muted
                                    font.pixelSize: 8
                                    font.weight: Font.Bold
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Label {
                                    width: parent.width - 34
                                    text: modelData
                                    color: root.selectedLn === modelData ? theme.accent : theme.textSoft
                                    font.pixelSize: 9
                                    font.weight: root.selectedLn === modelData ? Font.DemiBold : Font.Normal
                                    elide: Text.ElideRight
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            MouseArea { anchors.fill: parent; onClicked: root.chooseLogicalNode(modelData) }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: theme.surface
                border.width: 1
                border.color: theme.lineSoft

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 54
                        color: theme.surface

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 13
                            anchors.rightMargin: 13
                            spacing: 10
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Label {
                                    text: (backend.selectedIed.name || "IED") + "  /  " +
                                          (root.selectedLd || "—") + "  /  " + (root.selectedLn || "—")
                                    color: theme.text
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: (backend.selectedIed.manufacturer || "IEC 61850") +
                                          (backend.running ? "  ·  LIVE MMS" : "  ·  model view")
                                    color: backend.running ? theme.green : theme.muted
                                    font.pixelSize: 9
                                }
                            }
                            TextField {
                                id: searchField
                                Layout.preferredWidth: 226
                                placeholderText: "Search signal / value / FC"
                                text: root.searchText
                                onTextChanged: {
                                    root.searchText = text
                                    root.rebuildTable()
                                }
                                selectByMouse: true
                                font.pixelSize: 10
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: theme.lineSoft }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        color: theme.surfaceRaised
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 11
                            anchors.rightMargin: 11
                            spacing: 0
                            Label { Layout.fillWidth: true; text: "Object / attribute"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                            Label { Layout.preferredWidth: 66; text: "FC"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                            Label { Layout.preferredWidth: 105; text: "Type"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                            Label { Layout.preferredWidth: 148; text: "Value"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                        }
                    }

                    ListView {
                        id: dataList
                        objectName: "iedLiveSignalTable"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: root.tableRows
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { }

                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: dataList.width
                            height: modelData.kind === "DO" ? 34 : 31
                            color: {
                                if (modelData.kind === "DA" && backend.selectedValueIndex === modelData.sourceIndex) return theme.accentSoft
                                if (modelData.kind === "DO") return theme.chrome
                                return index % 2 === 0 ? theme.surface : "#fbfcfc"
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 11
                                anchors.rightMargin: 11
                                spacing: 0
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Item { Layout.preferredWidth: modelData.kind === "DO" ? 0 : 16 }
                                    Label {
                                        visible: modelData.kind === "DO"
                                        text: root.collapsedObjects[modelData.name] === true ? "›" : "⌄"
                                        color: theme.muted
                                        font.pixelSize: 14
                                    }
                                    Rectangle {
                                        width: modelData.kind === "DO" ? 24 : 20
                                        height: 18
                                        radius: 5
                                        color: modelData.kind === "DO" ? theme.accentSoft : theme.surfaceRaised
                                        Label {
                                            anchors.centerIn: parent
                                            text: modelData.kind
                                            color: modelData.kind === "DO" ? theme.accent : theme.muted
                                            font.pixelSize: 8
                                            font.weight: Font.Bold
                                        }
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.name
                                        color: modelData.kind === "DO" ? theme.text : theme.textSoft
                                        font.pixelSize: 10
                                        font.weight: modelData.kind === "DO" ? Font.DemiBold : Font.Normal
                                        elide: Text.ElideRight
                                    }
                                }
                                Label { Layout.preferredWidth: 66; text: modelData.fc ? "[" + modelData.fc + "]" : ""; color: theme.muted; font.pixelSize: 9 }
                                Label { Layout.preferredWidth: 105; text: modelData.type; color: theme.muted; font.pixelSize: 9; elide: Text.ElideRight }
                                Label {
                                    Layout.preferredWidth: 148
                                    text: modelData.value
                                    color: modelData.kind === "DA" ? theme.text : theme.textSoft
                                    font.pixelSize: 10
                                    font.weight: modelData.kind === "DA" ? Font.DemiBold : Font.Normal
                                    elide: Text.ElideRight
                                }
                            }

                            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: theme.lineSoft }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (modelData.kind === "DO") root.toggleObject(modelData.name)
                                    else backend.selectValue(modelData.sourceIndex)
                                }
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: root.tableRows.length === 0
                            text: backend.imported ? "No matching IEC 61850 leaves" : "Open an engineering model"
                            color: theme.muted
                            font.pixelSize: 11
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: root.width >= 1360 ? 288 : 256
                Layout.fillHeight: true
                color: theme.chrome
                border.width: 1
                border.color: theme.lineSoft

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 11
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "LIVE INSPECTOR"
                            color: theme.text
                            font.pixelSize: 10
                            font.weight: Font.Bold
                            Layout.fillWidth: true
                        }
                        Rectangle { width: 8; height: 8; radius: 4; color: backend.running ? theme.green : theme.muted }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: backend.selectedValue.reference || "Select a data attribute"
                        color: backend.selectedValue.reference ? theme.textSoft : theme.muted
                        font.pixelSize: 9
                        wrapMode: Text.WrapAnywhere
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: theme.lineSoft }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 5
                        Label { text: "FC"; color: theme.muted; font.pixelSize: 9 }
                        Label { text: backend.selectedValue.fc || "—"; color: theme.textSoft; font.pixelSize: 10 }
                        Label { text: "Type"; color: theme.muted; font.pixelSize: 9 }
                        Label { text: backend.selectedValue.type || "—"; color: theme.textSoft; font.pixelSize: 10 }
                        Label { text: "Updated"; color: theme.muted; font.pixelSize: 9 }
                        Label { text: backend.selectedValue.updated || "—"; color: theme.textSoft; font.pixelSize: 10 }
                    }

                    Label { text: "Value"; color: theme.muted; font.pixelSize: 9 }
                    Loader {
                        Layout.fillWidth: true
                        sourceComponent: root.selectedOptions().length > 0 ? optionEditor : textEditor
                    }
                    Component {
                        id: textEditor
                        TextField {
                            text: root.pendingValue
                            enabled: backend.selectedValue.writable && backend.running
                            selectByMouse: true
                            font.pixelSize: 11
                            onTextEdited: root.pendingValue = text
                        }
                    }
                    Component {
                        id: optionEditor
                        ComboBox {
                            model: root.selectedOptions()
                            enabled: backend.selectedValue.writable && backend.running
                            currentIndex: Math.max(0, root.selectedOptions().indexOf(root.pendingValue))
                            font.pixelSize: 11
                            onActivated: root.pendingValue = currentText
                        }
                    }

                    Label { text: "Quality"; color: theme.muted; font.pixelSize: 9 }
                    ComboBox {
                        Layout.fillWidth: true
                        model: ["Good", "Questionable", "Invalid"]
                        enabled: backend.selectedValue.writable && backend.running
                        currentIndex: Math.max(0, model.indexOf(root.pendingQuality))
                        font.pixelSize: 11
                        onActivated: root.pendingQuality = currentText
                    }

                    Label { text: "Origin"; color: theme.muted; font.pixelSize: 9 }
                    TextField {
                        Layout.fillWidth: true
                        text: root.pendingOrigin
                        enabled: backend.selectedValue.writable && backend.running
                        font.pixelSize: 11
                        onTextEdited: root.pendingOrigin = text
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        IconAction {
                            theme: root.theme
                            text: "Apply"
                            iconSource: "qrc:/iedsim/assets/circle-check.svg"
                            primary: true
                            Layout.fillWidth: true
                            enabled: backend.running && backend.selectedValue.writable
                            onClicked: backend.applySelectedValue(root.pendingValue, root.pendingQuality, root.pendingOrigin)
                        }
                        IconAction {
                            theme: root.theme
                            text: "Undo"
                            iconSource: "qrc:/iedsim/assets/chevron-up.svg"
                            compact: true
                            enabled: backend.running
                            onClicked: backend.undoLastChange()
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: theme.lineSoft; Layout.topMargin: 4 }
                    Label { text: "SERVICES"; color: theme.text; font.pixelSize: 10; font.weight: Font.Bold }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 5
                        StatusPill { theme: root.theme; text: backend.reportCount + " Reports"; tone: theme.accent; fill: theme.accentSoft }
                        StatusPill { theme: root.theme; text: backend.dataSetCount + " DataSets"; tone: theme.textSoft; fill: theme.surfaceRaised }
                    }
                    StatusPill { theme: root.theme; text: backend.gooseCount + " GOOSE"; tone: theme.muted; fill: theme.surfaceRaised }

                    Item { Layout.fillHeight: true }
                    Label {
                        Layout.fillWidth: true
                        text: "Explorer identity is shared with ARSAS; runtime state remains isolated per simulated IED."
                        color: theme.muted
                        font.pixelSize: 8
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.eventStreamVisible ? 150 : 0
            visible: root.eventStreamVisible
            color: theme.navigationDark
            clip: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 33
                    Layout.leftMargin: 14
                    Layout.rightMargin: 10
                    Label { text: "EVENT STREAM"; color: theme.navigationText; font.pixelSize: 10; font.weight: Font.Bold; Layout.fillWidth: true }
                    Label { text: backend.activity.length + " events"; color: theme.navigationMuted; font.pixelSize: 9 }
                    IconAction {
                        theme: root.theme
                        compact: true
                        text: "Clear"
                        iconSource: "qrc:/iedsim/assets/x.svg"
                        enabled: backend.activity.length > 0
                        onClicked: backend.clearActivity()
                    }
                    IconAction {
                        theme: root.theme
                        compact: true
                        text: "Copy"
                        iconSource: "qrc:/iedsim/assets/activity.svg"
                        onClicked: backend.copyDiagnostics()
                    }
                }

                ListView {
                    id: activityList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: backend.activity
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { }
                    delegate: RowLayout {
                        required property var modelData
                        width: activityList.width
                        height: 27
                        spacing: 8
                        Rectangle { Layout.leftMargin: 14; width: 6; height: 6; radius: 3; color: root.severityColor(modelData.severity || "Info") }
                        Label { Layout.preferredWidth: 76; text: modelData.time || ""; color: theme.navigationMuted; font.pixelSize: 9 }
                        Label { Layout.preferredWidth: 112; text: modelData.ied || modelData.category || "Workspace"; color: theme.navigationText; font.pixelSize: 9; font.weight: Font.DemiBold; elide: Text.ElideRight }
                        Label { Layout.preferredWidth: 76; text: modelData.category || ""; color: root.severityColor(modelData.severity || "Info"); font.pixelSize: 9 }
                        Label { Layout.fillWidth: true; Layout.rightMargin: 12; text: modelData.message || ""; color: theme.navigationMuted; font.pixelSize: 9; elide: Text.ElideRight }
                    }
                }
            }
        }
    }
}
