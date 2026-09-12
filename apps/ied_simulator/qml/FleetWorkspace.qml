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

    function statusColor(status) {
        if (status === "Running") return theme.green
        if (status === "Starting" || status === "Stopping") return theme.amber
        if (status === "Failed") return theme.red
        return theme.muted
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

    Shortcut {
        sequence: "Ctrl+O"
        onActivated: root.openSclRequested()
    }
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

    Rectangle {
        anchors.fill: parent
        color: theme.background
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Original ARStack command bar: compact, fleet-oriented and deliberately
        // different from vendor ribbon/tab metaphors.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: theme.navigation

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 10

                ColumnLayout {
                    Layout.preferredWidth: 240
                    spacing: 1
                    Label {
                        text: "ARStack IED Lab"
                        color: theme.navigationText
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: "Multi-IED IEC 61850 simulation workspace"
                        color: theme.navigationMuted
                        font.pixelSize: 10
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
                             backend.selectedIed.enabled && backend.endpointConflict.length === 0
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
                    text: backend.imported ? backend.modelStatus : "Open a model to create a simulation fleet"
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
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Fleet panel: every IED is a first-class runtime with its own
            // endpoint instead of a single global simulator state.
            Rectangle {
                Layout.preferredWidth: 258
                Layout.fillHeight: true
                color: theme.surface
                border.width: 1
                border.color: theme.lineSoft

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        Layout.leftMargin: 14
                        Layout.rightMargin: 10
                        Label {
                            text: "SIMULATION FLEET"
                            color: theme.text
                            font.pixelSize: 11
                            font.weight: Font.Bold
                            Layout.fillWidth: true
                        }
                        Label {
                            text: backend.runningCount + "/" + backend.ieds.length
                            color: backend.runningCount > 0 ? theme.green : theme.muted
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: theme.lineSoft }

                    ListView {
                        id: iedList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: backend.ieds
                        spacing: 4
                        topMargin: 8
                        bottomMargin: 8
                        leftMargin: 8
                        rightMargin: 8
                        ScrollBar.vertical: ScrollBar { }

                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: iedList.width - iedList.leftMargin - iedList.rightMargin
                            height: 72
                            radius: 9
                            color: backend.selectedIedIndex === index ? theme.accentSoft : theme.surface
                            border.width: 1
                            border.color: backend.selectedIedIndex === index ? theme.accent : theme.lineSoft

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 9

                                Rectangle {
                                    width: 30
                                    height: 30
                                    radius: 8
                                    color: modelData.status === "Running" ? theme.greenSoft : theme.surfaceRaised
                                    Image {
                                        anchors.centerIn: parent
                                        width: 16
                                        height: 16
                                        source: "qrc:/iedsim/assets/radio-tower.svg"
                                        opacity: modelData.enabled ? 0.9 : 0.35
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Label {
                                        text: modelData.name || "Unnamed IED"
                                        color: theme.text
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        text: modelData.endpoint || "Assign IP"
                                        color: modelData.endpoint === "Assign IP" ? theme.amber : theme.muted
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    RowLayout {
                                        spacing: 5
                                        Rectangle {
                                            width: 7
                                            height: 7
                                            radius: 4
                                            color: root.statusColor(modelData.status)
                                        }
                                        Label {
                                            text: modelData.status
                                            color: root.statusColor(modelData.status)
                                            font.pixelSize: 9
                                        }
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: backend.selectIed(index)
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: backend.ieds.length === 0
                            text: "No IEDs loaded"
                            color: theme.muted
                            font.pixelSize: 11
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: theme.lineSoft }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: backend.imported ? 214 : 0
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        Layout.topMargin: 10
                        Layout.bottomMargin: 10
                        spacing: 7
                        visible: backend.imported

                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: "Selected endpoint"
                                color: theme.text
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                Layout.fillWidth: true
                            }
                            Switch {
                                checked: backend.selectedIed.enabled === undefined ? true : backend.selectedIed.enabled
                                enabled: !backend.running && !backend.starting
                                onToggled: backend.setIedEnabled(backend.selectedIedIndex, checked)
                                scale: 0.78
                            }
                        }

                        ComboBox {
                            id: addressBox
                            Layout.fillWidth: true
                            enabled: !backend.running && !backend.starting
                            model: backend.availableAddresses
                            currentIndex: Math.max(0, backend.availableAddresses.indexOf(backend.listenAddress))
                            onActivated: backend.listenAddress = currentText
                            font.pixelSize: 11
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "Port"; color: theme.muted; font.pixelSize: 10 }
                            SpinBox {
                                Layout.fillWidth: true
                                from: 1
                                to: 65535
                                editable: true
                                value: backend.port
                                enabled: !backend.running && !backend.starting
                                onValueModified: backend.port = value
                                font.pixelSize: 11
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: backend.endpointConflict.length > 0
                            text: backend.endpointConflict
                            color: theme.red
                            font.pixelSize: 9
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            IconAction {
                                theme: root.theme
                                text: "Auto assign"
                                iconSource: "qrc:/iedsim/assets/radio-tower.svg"
                                compact: true
                                enabled: !backend.anyRunning
                                Layout.fillWidth: true
                                onClicked: backend.autoAssignIedAddresses()
                            }
                            IconAction {
                                theme: root.theme
                                text: ""
                                iconSource: "qrc:/iedsim/assets/scan-search.svg"
                                compact: true
                                enabled: !backend.anyRunning
                                onClicked: backend.refreshNetworkInterfaces()
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: backend.networkAddresses.length + " IPv4 address entries detected. Secondary Ethernet IPs appear here automatically."
                            color: theme.muted
                            font.pixelSize: 9
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            // Model navigator is intentionally separate from the IED fleet.
            Rectangle {
                Layout.preferredWidth: 204
                Layout.fillHeight: true
                color: theme.chrome
                border.width: 1
                border.color: theme.lineSoft

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Label {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        Layout.leftMargin: 12
                        text: "MODEL NAVIGATOR"
                        verticalAlignment: Text.AlignVCenter
                        color: theme.text
                        font.pixelSize: 10
                        font.weight: Font.Bold
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: theme.lineSoft }

                    Label {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        Layout.leftMargin: 12
                        text: "Logical devices"
                        verticalAlignment: Text.AlignVCenter
                        color: theme.muted
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                    }
                    ListView {
                        id: ldList
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(230, contentHeight + 2)
                        clip: true
                        model: root.logicalDevices
                        ScrollBar.vertical: ScrollBar { }
                        delegate: Rectangle {
                            required property string modelData
                            width: ldList.width
                            height: 31
                            color: root.selectedLd === modelData ? theme.accentSoft : "transparent"
                            Label {
                                anchors.fill: parent
                                anchors.leftMargin: 14
                                anchors.rightMargin: 6
                                text: modelData
                                verticalAlignment: Text.AlignVCenter
                                color: root.selectedLd === modelData ? theme.accent : theme.textSoft
                                font.pixelSize: 10
                                font.weight: root.selectedLd === modelData ? Font.DemiBold : Font.Normal
                                elide: Text.ElideRight
                            }
                            MouseArea { anchors.fill: parent; onClicked: root.chooseLogicalDevice(modelData) }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: theme.lineSoft }
                    Label {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        Layout.leftMargin: 12
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
                            Label {
                                anchors.fill: parent
                                anchors.leftMargin: 14
                                anchors.rightMargin: 6
                                text: modelData
                                verticalAlignment: Text.AlignVCenter
                                color: root.selectedLn === modelData ? theme.accent : theme.textSoft
                                font.pixelSize: 10
                                font.weight: root.selectedLn === modelData ? Font.DemiBold : Font.Normal
                                elide: Text.ElideRight
                            }
                            MouseArea { anchors.fill: parent; onClicked: root.chooseLogicalNode(modelData) }
                        }
                    }
                }
            }

            // Main data canvas.
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
                        border.width: 0

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 10

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Label {
                                    text: (backend.selectedIed.name || "IED") + "  /  " +
                                          (root.selectedLd || "—") + "  /  " + (root.selectedLn || "—")
                                    color: theme.text
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: backend.selectedIed.manufacturer || "IEC 61850 data model"
                                    color: theme.muted
                                    font.pixelSize: 9
                                }
                            }

                            TextField {
                                id: searchField
                                Layout.preferredWidth: 238
                                placeholderText: "Find DO, DA, FC or value"
                                text: root.searchText
                                onTextChanged: {
                                    root.searchText = text
                                    root.rebuildTable()
                                }
                                selectByMouse: true
                                font.pixelSize: 10
                                leftPadding: 10
                                rightPadding: 10
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
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 0
                            Label { Layout.fillWidth: true; text: "Object / attribute"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                            Label { Layout.preferredWidth: 78; text: "FC"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                            Label { Layout.preferredWidth: 120; text: "Type"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                            Label { Layout.preferredWidth: 180; text: "Value"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                        }
                    }

                    ListView {
                        id: dataList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: root.tableRows
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
                            border.width: 0

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 0

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Item { Layout.preferredWidth: modelData.kind === "DO" ? 0 : 17 }
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
                                Label {
                                    Layout.preferredWidth: 78
                                    text: modelData.fc ? "[" + modelData.fc + "]" : ""
                                    color: theme.muted
                                    font.pixelSize: 9
                                }
                                Label {
                                    Layout.preferredWidth: 120
                                    text: modelData.type
                                    color: theme.muted
                                    font.pixelSize: 9
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.preferredWidth: 180
                                    text: modelData.value
                                    color: modelData.kind === "DA" ? theme.text : theme.textSoft
                                    font.pixelSize: 10
                                    font.weight: modelData.kind === "DA" ? Font.DemiBold : Font.Normal
                                    elide: Text.ElideRight
                                }
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                color: theme.lineSoft
                            }

                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton
                                onClicked: {
                                    if (modelData.kind === "DO") root.toggleObject(modelData.name)
                                    else backend.selectValue(modelData.sourceIndex)
                                }
                                onDoubleClicked: {
                                    if (modelData.kind === "DA") backend.selectValue(modelData.sourceIndex)
                                }
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: root.tableRows.length === 0
                            text: backend.imported ? "No matching leaves" : "Open an engineering model"
                            color: theme.muted
                            font.pixelSize: 11
                        }
                    }
                }
            }

            // Context inspector makes the common operation (edit a DA) one click
            // faster than opening a separate modal for every change.
            Rectangle {
                Layout.preferredWidth: root.width >= 1280 ? 298 : 260
                Layout.fillHeight: true
                color: theme.chrome
                border.width: 1
                border.color: theme.lineSoft

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 9

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "LIVE INSPECTOR"
                            color: theme.text
                            font.pixelSize: 10
                            font.weight: Font.Bold
                            Layout.fillWidth: true
                        }
                        Rectangle {
                            width: 8
                            height: 8
                            radius: 4
                            color: backend.running ? theme.green : theme.muted
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: backend.selectedValue.reference || "Select a data attribute"
                        color: backend.selectedValue.reference ? theme.textSoft : theme.muted
                        font.pixelSize: 10
                        wrapMode: Text.WrapAnywhere
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: theme.lineSoft
                    }

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

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: theme.lineSoft
                        Layout.topMargin: 4
                    }

                    Label {
                        text: "SERVICES"
                        color: theme.text
                        font.pixelSize: 10
                        font.weight: Font.Bold
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        StatusPill {
                            theme: root.theme
                            text: backend.reportCount + " Reports"
                            tone: theme.accent
                            fill: theme.accentSoft
                        }
                        StatusPill {
                            theme: root.theme
                            text: backend.dataSetCount + " DataSets"
                            tone: theme.textSoft
                            fill: theme.surfaceRaised
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        StatusPill {
                            theme: root.theme
                            text: backend.gooseCount + " GOOSE"
                            tone: theme.muted
                            fill: theme.surfaceRaised
                        }
                    }

                    Item { Layout.fillHeight: true }

                    Label {
                        Layout.fillWidth: true
                        text: "Tip: add secondary IPv4 addresses in the laptop Ethernet adapter, then Auto assign. Each IED can keep TCP/102 because it binds to a different local IP."
                        color: theme.muted
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.eventStreamVisible ? 156 : 0
            visible: root.eventStreamVisible
            color: theme.navigationDark
            clip: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    Layout.leftMargin: 14
                    Layout.rightMargin: 10
                    Label {
                        text: "EVENT STREAM"
                        color: theme.navigationText
                        font.pixelSize: 10
                        font.weight: Font.Bold
                        Layout.fillWidth: true
                    }
                    Label {
                        text: backend.activity.length + " events"
                        color: theme.navigationMuted
                        font.pixelSize: 9
                    }
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
                    ScrollBar.vertical: ScrollBar { }

                    delegate: RowLayout {
                        required property var modelData
                        width: activityList.width
                        height: 27
                        spacing: 8

                        Rectangle {
                            Layout.leftMargin: 14
                            width: 6
                            height: 6
                            radius: 3
                            color: root.severityColor(modelData.severity || "Info")
                        }
                        Label {
                            Layout.preferredWidth: 76
                            text: modelData.time || ""
                            color: theme.navigationMuted
                            font.pixelSize: 9
                        }
                        Label {
                            Layout.preferredWidth: 112
                            text: modelData.ied || modelData.category || "Workspace"
                            color: theme.navigationText
                            font.pixelSize: 9
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.preferredWidth: 76
                            text: modelData.category || ""
                            color: root.severityColor(modelData.severity || "Info")
                            font.pixelSize: 9
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.rightMargin: 12
                            text: modelData.message || ""
                            color: theme.navigationMuted
                            font.pixelSize: 9
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
}
