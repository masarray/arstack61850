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

    property bool navigationVisible: true
    property bool detailsVisible: true
    property bool statusVisible: true
    property string selectedLd: ""
    property string selectedLn: ""
    property var logicalDevices: []
    property var logicalNodes: []
    property var tableRows: []
    property var collapsedObjects: ({})
    property string pendingValue: backend.selectedValue.value || ""
    property string pendingQuality: backend.selectedValue.quality || "Good"
    property string pendingOrigin: backend.selectedValue.origin || "Simulator"

    function severityColor(severity) {
        if (severity === "Error") return theme.red
        if (severity === "Warning") return theme.amber
        if (severity === "Success") return theme.green
        return theme.accent
    }

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

    function rebuildTable() {
        var values = backend.values || []
        var order = []
        var groups = ({})
        for (var i = 0; i < values.length; ++i) {
            var point = values[i]
            if ((point.logicalDevice || "") !== selectedLd ||
                    (point.logicalNode || "") !== selectedLn) continue
            var objectName = point.dataObject || "(unnamed)"
            if (groups[objectName] === undefined) {
                groups[objectName] = { rows: [], preferred: -1 }
                order.push(objectName)
            }
            var attribute = point.dataAttribute || ""
            if (groups[objectName].preferred < 0 || attribute === "stVal" ||
                    attribute.endsWith(".stVal") || attribute === "mag.f") {
                groups[objectName].preferred = i
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

    function activityCount(severity) {
        var items = backend.activity || []
        var count = 0
        for (var i = 0; i < items.length; ++i) {
            if ((items[i].severity || "Info") === severity) ++count
        }
        return count
    }

    Connections {
        target: backend
        function onValuesChanged() { root.refreshNavigation() }
        function onSelectionChanged() { root.refreshEditor() }
        function onModelChanged() { root.refreshNavigation() }
    }

    Component.onCompleted: refreshNavigation()

    Rectangle { anchors.fill: parent; color: theme.background }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Compact tab strip: familiar engineering-tool navigation without
        // copying vendor branding or proprietary assets.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: theme.navigationDark

            RowLayout {
                anchors.fill: parent
                spacing: 0
                Repeater {
                    model: ["File", "Browser", "Simulator", "Diagnostics"]
                    delegate: Rectangle {
                        required property string modelData
                        required property int index
                        Layout.preferredWidth: index === 3 ? 96 : 82
                        Layout.fillHeight: true
                        color: modelData === "Simulator" ? theme.navigation : "transparent"
                        border.width: modelData === "Simulator" ? 1 : 0
                        border.color: "#5e9ed1"
                        Label {
                            anchors.centerIn: parent
                            text: modelData
                            color: theme.navigationText
                            font.pixelSize: 11
                            font.weight: modelData === "Simulator" ? Font.DemiBold : Font.Normal
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: "ARStack IEC 61850"
                    color: theme.navigationMuted
                    font.pixelSize: 10
                    Layout.rightMargin: 14
                }
            }
        }

        // Ribbon actions keep the primary workflow one click away:
        // Open SCL -> configure endpoint -> Start -> select a leaf -> Set value.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 92
            color: theme.chrome
            border.width: 1
            border.color: theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 0

                RowLayout {
                    Layout.preferredWidth: 330
                    Layout.fillHeight: true
                    spacing: 6

                    Button {
                        id: openButton
                        Layout.preferredWidth: 78; Layout.preferredHeight: 66
                        onClicked: root.openSclRequested()
                        contentItem: Column {
                            anchors.centerIn: parent; spacing: 5
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: "▰"; color: theme.textSoft; font.pixelSize: 24 }
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: "Open SCL"; color: theme.text; font.pixelSize: 10 }
                        }
                        background: Rectangle { color: openButton.hovered ? theme.surfaceRaised : "transparent"; border.width: 0 }
                    }
                    Button {
                        id: startButton
                        Layout.preferredWidth: 70; Layout.preferredHeight: 66
                        enabled: backend.imported && !backend.running && !backend.starting
                        onClicked: serverSettings.open()
                        contentItem: Column {
                            anchors.centerIn: parent; spacing: 5
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: "▶"; color: startButton.enabled ? theme.green : theme.line; font.pixelSize: 24 }
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: backend.starting ? "Starting" : "Start"; color: startButton.enabled ? theme.text : theme.muted; font.pixelSize: 10 }
                        }
                        background: Rectangle { color: startButton.hovered ? theme.surfaceRaised : "transparent"; border.width: 0 }
                    }
                    Button {
                        id: stopButton
                        Layout.preferredWidth: 70; Layout.preferredHeight: 66
                        enabled: backend.running || backend.starting
                        onClicked: backend.stopSimulation()
                        contentItem: Column {
                            anchors.centerIn: parent; spacing: 5
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: "■"; color: stopButton.enabled ? theme.red : theme.line; font.pixelSize: 21 }
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: "Stop"; color: stopButton.enabled ? theme.text : theme.muted; font.pixelSize: 10 }
                        }
                        background: Rectangle { color: stopButton.hovered ? theme.surfaceRaised : "transparent"; border.width: 0 }
                    }
                    Button {
                        id: closeButton
                        Layout.preferredWidth: 84; Layout.preferredHeight: 66
                        enabled: backend.imported && !backend.running && !backend.starting
                        onClicked: backend.clear()
                        contentItem: Column {
                            anchors.centerIn: parent; spacing: 5
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: "⊠"; color: closeButton.enabled ? theme.textSoft : theme.line; font.pixelSize: 23 }
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: "Close IED"; color: closeButton.enabled ? theme.text : theme.muted; font.pixelSize: 10 }
                        }
                        background: Rectangle { color: closeButton.hovered ? theme.surfaceRaised : "transparent"; border.width: 0 }
                    }
                }

                Rectangle { width: 1; Layout.fillHeight: true; Layout.topMargin: 8; Layout.bottomMargin: 18; color: theme.line }

                ColumnLayout {
                    Layout.preferredWidth: 205
                    Layout.fillHeight: true
                    spacing: 0
                    RowLayout {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        Button {
                            id: setValueButton
                            Layout.preferredWidth: 96; Layout.preferredHeight: 64
                            enabled: backend.running && backend.selectedValue.writable
                            onClicked: { root.refreshEditor(); valueEditor.open() }
                            contentItem: Column {
                                anchors.centerIn: parent; spacing: 5
                                Label { anchors.horizontalCenter: parent.horizontalCenter; text: "✎"; color: setValueButton.enabled ? theme.accent : theme.line; font.pixelSize: 23 }
                                Label { anchors.horizontalCenter: parent.horizontalCenter; text: "Set value"; color: setValueButton.enabled ? theme.text : theme.muted; font.pixelSize: 10 }
                            }
                            background: Rectangle { color: setValueButton.hovered ? theme.surfaceRaised : "transparent"; border.width: 0 }
                        }
                        Button {
                            id: clearButton
                            Layout.preferredWidth: 100; Layout.preferredHeight: 64
                            enabled: backend.activity.length > 0
                            onClicked: backend.clearActivity()
                            contentItem: Column {
                                anchors.centerIn: parent; spacing: 5
                                Label { anchors.horizontalCenter: parent.horizontalCenter; text: "⌫"; color: clearButton.enabled ? theme.textSoft : theme.line; font.pixelSize: 23 }
                                Label { anchors.horizontalCenter: parent.horizontalCenter; text: "Clear status"; color: clearButton.enabled ? theme.text : theme.muted; font.pixelSize: 10 }
                            }
                            background: Rectangle { color: clearButton.hovered ? theme.surfaceRaised : "transparent"; border.width: 0 }
                        }
                    }
                    Label { text: "Data"; color: theme.muted; font.pixelSize: 9; Layout.alignment: Qt.AlignHCenter; Layout.bottomMargin: 4 }
                }

                Rectangle { width: 1; Layout.fillHeight: true; Layout.topMargin: 8; Layout.bottomMargin: 18; color: theme.line }

                ColumnLayout {
                    Layout.preferredWidth: 186
                    Layout.fillHeight: true
                    spacing: 2
                    Item { Layout.fillHeight: true }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 12
                        Column {
                            spacing: 4
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: backend.reportCount.toString(); color: theme.accent; font.pixelSize: 20; font.weight: Font.DemiBold }
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: "Reports"; color: theme.textSoft; font.pixelSize: 10 }
                        }
                        Column {
                            spacing: 4
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: backend.dataSetCount.toString(); color: theme.accent; font.pixelSize: 20; font.weight: Font.DemiBold }
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: "DataSets"; color: theme.textSoft; font.pixelSize: 10 }
                        }
                        Column {
                            spacing: 4
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: backend.gooseCount.toString(); color: theme.muted; font.pixelSize: 20; font.weight: Font.DemiBold }
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: "GOOSE"; color: theme.textSoft; font.pixelSize: 10 }
                        }
                    }
                    Item { Layout.fillHeight: true }
                    Label { text: "Services"; color: theme.muted; font.pixelSize: 9; Layout.alignment: Qt.AlignHCenter; Layout.bottomMargin: 4 }
                }

                Rectangle { width: 1; Layout.fillHeight: true; Layout.topMargin: 8; Layout.bottomMargin: 18; color: theme.line }

                ColumnLayout {
                    Layout.preferredWidth: 250
                    Layout.fillHeight: true
                    spacing: 1
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        CheckBox {
                            checked: root.navigationVisible
                            onToggled: root.navigationVisible = checked
                            text: "Navigation"
                            font.pixelSize: 10
                        }
                        CheckBox {
                            checked: root.detailsVisible
                            onToggled: root.detailsVisible = checked
                            text: "Details"
                            font.pixelSize: 10
                        }
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        CheckBox {
                            checked: root.statusVisible
                            onToggled: root.statusVisible = checked
                            text: "Status history"
                            font.pixelSize: 10
                        }
                    }
                    Item { Layout.fillHeight: true }
                    Label { text: "Show"; color: theme.muted; font.pixelSize: 9; Layout.alignment: Qt.AlignHCenter; Layout.bottomMargin: 4 }
                }

                Item { Layout.fillWidth: true }

                ColumnLayout {
                    Layout.preferredWidth: 210
                    Layout.fillHeight: true
                    spacing: 2
                    Item { Layout.fillHeight: true }
                    StatusPill {
                        theme: root.theme
                        Layout.alignment: Qt.AlignRight
                        text: backend.running ? "MMS online" : (backend.starting ? "Starting" : (backend.imported ? "Ready" : "No model"))
                        tone: backend.running ? theme.green : (backend.starting ? theme.accent : theme.muted)
                        fill: backend.running ? theme.greenSoft : (backend.starting ? theme.accentSoft : theme.surfaceRaised)
                    }
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: backend.listenAddress + ":" + backend.port
                        color: theme.textSoft
                        font.pixelSize: 10
                    }
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: backend.imported ? backend.sourceName : "Open an SCL/CID/SCD/IID file"
                        color: theme.muted
                        font.pixelSize: 9
                        elide: Text.ElideMiddle
                        Layout.maximumWidth: 200
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 3

            // IEDScout-inspired navigation rail: service groups and full LD/LN
            // hierarchy live together so the operator never loses model context.
            Rectangle {
                visible: root.navigationVisible
                Layout.preferredWidth: root.navigationVisible ? 305 : 0
                Layout.fillHeight: true
                color: theme.navigation
                border.width: 1
                border.color: theme.navigationDark

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 7

                    Label {
                        text: "IEDs"
                        color: theme.navigationText
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#5a91bd" }

                    ComboBox {
                        id: iedBox
                        Layout.fillWidth: true
                        implicitHeight: 34
                        model: backend.ieds
                        textRole: "name"
                        currentIndex: backend.selectedIedIndex
                        onActivated: backend.selectIed(currentIndex)
                        contentItem: Label {
                            leftPadding: 10; rightPadding: 28
                            text: iedBox.displayText
                            color: theme.navigationText
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                        background: Rectangle {
                            color: theme.navigationDark
                            border.width: 1
                            border.color: "#6fa1c8"
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 6
                        rowSpacing: 3
                        Label { text: "IP address:"; color: theme.navigationMuted; font.pixelSize: 9 }
                        Label { text: backend.listenAddress; color: theme.navigationText; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideRight }
                        Label { text: "Port:"; color: theme.navigationMuted; font.pixelSize: 9 }
                        Label { text: backend.port.toString(); color: theme.navigationText; font.pixelSize: 9 }
                        Label { text: "SCL path:"; color: theme.navigationMuted; font.pixelSize: 9 }
                        Label { text: backend.sourcePath || "—"; color: theme.navigationText; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideMiddle }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: "#5a91bd" }

                    Flickable {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: width
                        contentHeight: navigationColumn.implicitHeight
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { }

                        Column {
                            id: navigationColumn
                            width: parent.width
                            spacing: 1

                            Rectangle {
                                width: parent.width; height: 28; color: "transparent"
                                Row {
                                    anchors.verticalCenter: parent.verticalCenter; spacing: 7
                                    Label { text: "▸"; color: theme.navigationText; font.pixelSize: 10 }
                                    Label { text: "GOOSE"; color: theme.navigationText; font.pixelSize: 10 }
                                    Label { text: backend.gooseCount > 0 ? "(" + backend.gooseCount + ")" : ""; color: theme.navigationMuted; font.pixelSize: 9 }
                                }
                            }
                            Rectangle {
                                width: parent.width; height: 28; color: "transparent"
                                Row {
                                    anchors.verticalCenter: parent.verticalCenter; spacing: 7
                                    Label { text: "▸"; color: theme.navigationText; font.pixelSize: 10 }
                                    Label { text: "Reports"; color: theme.navigationText; font.pixelSize: 10 }
                                    Label { text: backend.reportCount > 0 ? "(" + backend.reportCount + ")" : ""; color: theme.navigationMuted; font.pixelSize: 9 }
                                }
                            }
                            Rectangle {
                                width: parent.width; height: 28; color: "transparent"
                                Row {
                                    anchors.verticalCenter: parent.verticalCenter; spacing: 7
                                    Label { text: "▸"; color: theme.navigationText; font.pixelSize: 10 }
                                    Label { text: "Setting Groups"; color: theme.navigationText; font.pixelSize: 10 }
                                }
                            }
                            Rectangle {
                                width: parent.width; height: 28; color: "transparent"
                                Row {
                                    anchors.verticalCenter: parent.verticalCenter; spacing: 7
                                    Label { text: "▸"; color: theme.navigationText; font.pixelSize: 10 }
                                    Label { text: "DataSets"; color: theme.navigationText; font.pixelSize: 10 }
                                    Label { text: backend.dataSetCount > 0 ? "(" + backend.dataSetCount + ")" : ""; color: theme.navigationMuted; font.pixelSize: 9 }
                                }
                            }
                            Rectangle {
                                width: parent.width; height: 30; color: theme.navigationDark
                                Row {
                                    anchors.verticalCenter: parent.verticalCenter; spacing: 7
                                    Label { text: "▾"; color: theme.navigationText; font.pixelSize: 10 }
                                    Label { text: "Data Model"; color: theme.navigationText; font.pixelSize: 10; font.weight: Font.DemiBold }
                                    Label { text: backend.dataAttributeCount > 0 ? "(" + backend.dataAttributeCount + ")" : ""; color: theme.navigationMuted; font.pixelSize: 9 }
                                }
                            }

                            Repeater {
                                model: root.logicalDevices
                                delegate: Column {
                                    required property string modelData
                                    width: navigationColumn.width
                                    spacing: 0

                                    Rectangle {
                                        width: parent.width; height: 29
                                        color: root.selectedLd === modelData ? "#0e548d" : "transparent"
                                        Row {
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.left: parent.left; anchors.leftMargin: 16
                                            spacing: 7
                                            Label { text: root.selectedLd === modelData ? "▾" : "▸"; color: theme.navigationText; font.pixelSize: 9 }
                                            Rectangle {
                                                width: 18; height: 16; color: "#2c80bd"; border.width: 1; border.color: "#87b8dc"
                                                Label { anchors.centerIn: parent; text: "LD"; color: "white"; font.pixelSize: 8; font.weight: Font.DemiBold }
                                            }
                                            Label { text: modelData; color: theme.navigationText; font.pixelSize: 10; width: 205; elide: Text.ElideRight }
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.chooseLogicalDevice(modelData)
                                        }
                                    }

                                    Column {
                                        width: parent.width
                                        visible: root.selectedLd === modelData
                                        Repeater {
                                            model: root.logicalNodes
                                            delegate: Rectangle {
                                                required property string modelData
                                                width: navigationColumn.width; height: 27
                                                color: root.selectedLn === modelData ? "#2875ad" : "transparent"
                                                Row {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    anchors.left: parent.left; anchors.leftMargin: 43
                                                    spacing: 7
                                                    Rectangle {
                                                        width: 18; height: 16; color: "#317fc0"; border.width: 1; border.color: "#93c1e3"
                                                        Label { anchors.centerIn: parent; text: "LN"; color: "white"; font.pixelSize: 8; font.weight: Font.DemiBold }
                                                    }
                                                    Label { text: modelData; color: theme.navigationText; font.pixelSize: 10; width: 177; elide: Text.ElideRight }
                                                }
                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: root.chooseLogicalNode(modelData)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: theme.surface
                border.width: 1
                border.color: theme.line

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 27
                        color: theme.navigation
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            Label {
                                text: (backend.selectedIed.name || "IED") + " • Data Model • " +
                                      (root.selectedLd || "—") + " • " + (root.selectedLn || "—")
                                color: theme.navigationText
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                                Layout.fillWidth: true
                                elide: Text.ElideMiddle
                            }
                            Label {
                                text: backend.running ? "● ONLINE" : "● OFFLINE"
                                color: backend.running ? "#b8f0c8" : "#d5e3ef"
                                font.pixelSize: 9
                                font.weight: Font.DemiBold
                            }
                        }
                    }

                    Rectangle {
                        visible: root.detailsVisible
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.detailsVisible ? 42 : 0
                        color: "#fbfcfd"
                        border.width: 1
                        border.color: theme.lineSoft
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 10
                            Rectangle {
                                width: 20; height: 18; color: "#2b81c2"; border.width: 1; border.color: "#1d6396"
                                Label { anchors.centerIn: parent; text: "LN"; color: "white"; font.pixelSize: 8; font.weight: Font.DemiBold }
                            }
                            Label { text: root.selectedLn || "Select a logical node"; color: theme.text; font.pixelSize: 11; font.weight: Font.DemiBold }
                            Label { text: "IEC 61850 logical node"; color: theme.muted; font.pixelSize: 10 }
                            Item { Layout.fillWidth: true }
                            Label { text: root.tableRows.length + " rows"; color: theme.muted; font.pixelSize: 9 }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        color: "#e7eaed"
                        border.width: 1
                        border.color: theme.line
                        RowLayout {
                            anchors.fill: parent
                            spacing: 0
                            Label {
                                text: "Name"
                                color: theme.textSoft
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                                leftPadding: 8
                                Layout.preferredWidth: parent.width * 0.68
                            }
                            Rectangle { width: 1; Layout.fillHeight: true; color: theme.line }
                            Label {
                                text: "Value"
                                color: theme.textSoft
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                                leftPadding: 8
                                Layout.fillWidth: true
                            }
                        }
                    }

                    ListView {
                        id: modelTable
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: root.tableRows
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { }

                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: ListView.view.width
                            height: modelData.kind === "DO" ? 29 : 27
                            color: {
                                if (modelData.sourceIndex === backend.selectedValueIndex) return "#d8e9f7"
                                if (modelData.kind === "DO") return "#f1f3f5"
                                return index % 2 ? "#ffffff" : "#f8f9fa"
                            }
                            border.width: 0

                            RowLayout {
                                anchors.fill: parent
                                spacing: 0
                                Item {
                                    Layout.preferredWidth: parent.width * 0.68
                                    Layout.fillHeight: true
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: modelData.kind === "DO" ? 7 : 28
                                        anchors.rightMargin: 6
                                        spacing: 6
                                        Label {
                                            visible: modelData.kind === "DO"
                                            text: root.collapsedObjects[modelData.name] === true ? "▸" : "▾"
                                            color: theme.textSoft
                                            font.pixelSize: 9
                                        }
                                        Rectangle {
                                            width: 18; height: 16
                                            color: modelData.kind === "DO" ? "#2c7fbd" : "#4f94c7"
                                            border.width: 1; border.color: "#2a70a4"
                                            Label { anchors.centerIn: parent; text: modelData.kind; color: "white"; font.pixelSize: 8; font.weight: Font.DemiBold }
                                        }
                                        Label {
                                            text: modelData.name
                                            color: theme.text
                                            font.pixelSize: 10
                                            font.weight: modelData.kind === "DO" ? Font.DemiBold : Font.Normal
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                        }
                                        Label {
                                            text: modelData.fc.length ? "[" + modelData.fc + "]" : ""
                                            color: theme.muted
                                            font.pixelSize: 9
                                        }
                                        Label {
                                            text: modelData.type.length ? modelData.type : ""
                                            color: theme.muted
                                            font.pixelSize: 8
                                            Layout.maximumWidth: 84
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                                Rectangle { width: 1; Layout.fillHeight: true; color: theme.lineSoft }
                                Item {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 8
                                        spacing: 7
                                        Label {
                                            text: modelData.value
                                            color: modelData.sourceIndex === backend.selectedValueIndex ? theme.accent : theme.text
                                            font.pixelSize: 10
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                        }
                                        Rectangle {
                                            visible: modelData.kind === "DA" && modelData.quality.length > 0
                                            width: 6; height: 6; radius: 3
                                            color: modelData.quality === "Good" ? theme.green : theme.amber
                                        }
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (modelData.kind === "DO") root.toggleObject(modelData.name)
                                    if (modelData.sourceIndex >= 0) backend.selectValue(modelData.sourceIndex)
                                }
                                onDoubleClicked: {
                                    if (modelData.sourceIndex >= 0) backend.selectValue(modelData.sourceIndex)
                                    if (backend.running && backend.selectedValue.writable) {
                                        root.refreshEditor()
                                        valueEditor.open()
                                    }
                                }
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: root.tableRows.length === 0
                            text: backend.imported ? "Select a logical device and logical node." : "Open an SCL engineering file to begin."
                            color: theme.muted
                            font.pixelSize: 12
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 24
                        color: "#eef0f2"
                        border.width: 1
                        border.color: theme.lineSoft
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            Label {
                                text: backend.selectedValue.reference || "No data attribute selected"
                                color: theme.muted
                                font.pixelSize: 9
                                Layout.fillWidth: true
                                elide: Text.ElideMiddle
                            }
                            Label {
                                text: backend.selectedValue.fc ? "FC " + backend.selectedValue.fc : ""
                                color: theme.muted
                                font.pixelSize: 9
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            visible: root.statusVisible
            Layout.fillWidth: true
            Layout.preferredHeight: root.statusVisible ? 118 : 0
            color: theme.surface
            border.width: 1
            border.color: theme.line

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 25
                    color: theme.statusChrome
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 7
                        anchors.rightMargin: 7
                        Label { text: "Status History"; color: theme.statusText; font.pixelSize: 10; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        Label { text: "ⓘ " + root.activityCount("Info"); color: "#dce9f4"; font.pixelSize: 9 }
                        Label { text: "✓ " + root.activityCount("Success"); color: "#bde7c9"; font.pixelSize: 9 }
                        Label { text: "⚠ " + root.activityCount("Warning"); color: "#ffe49c"; font.pixelSize: 9 }
                        Label { text: "✖ " + root.activityCount("Error"); color: "#ffc2c7"; font.pixelSize: 9 }
                        Button {
                            text: "Copy"
                            implicitWidth: 48; implicitHeight: 20
                            onClicked: backend.copyDiagnostics()
                            contentItem: Label { text: parent.text; color: theme.text; font.pixelSize: 9; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { color: "#f5f5f5"; border.width: 1; border.color: "#8c8c8c" }
                        }
                        Button {
                            text: "Clear"
                            implicitWidth: 48; implicitHeight: 20
                            onClicked: backend.clearActivity()
                            contentItem: Label { text: parent.text; color: theme.text; font.pixelSize: 9; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { color: "#f5f5f5"; border.width: 1; border.color: "#8c8c8c" }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 22; color: "#f1f2f3"
                    RowLayout {
                        anchors.fill: parent; spacing: 0
                        Label { text: "Time"; color: theme.textSoft; font.pixelSize: 9; leftPadding: 8; Layout.preferredWidth: 92 }
                        Rectangle { width: 1; Layout.fillHeight: true; color: theme.lineSoft }
                        Label { text: "Description"; color: theme.textSoft; font.pixelSize: 9; leftPadding: 8; Layout.fillWidth: true }
                        Rectangle { width: 1; Layout.fillHeight: true; color: theme.lineSoft }
                        Label { text: "Category"; color: theme.textSoft; font.pixelSize: 9; leftPadding: 8; Layout.preferredWidth: 100 }
                    }
                }

                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true
                    model: backend.activity
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: ListView.view.width; height: 23
                        color: index % 2 ? "#ffffff" : "#f8f9fa"
                        RowLayout {
                            anchors.fill: parent; spacing: 0
                            Label { text: modelData.time; color: theme.muted; font.pixelSize: 9; leftPadding: 8; Layout.preferredWidth: 92 }
                            Rectangle { width: 1; Layout.fillHeight: true; color: theme.lineSoft }
                            RowLayout {
                                Layout.fillWidth: true; spacing: 6
                                Layout.leftMargin: 7; Layout.rightMargin: 7
                                Rectangle { width: 7; height: 7; radius: 4; color: root.severityColor(modelData.severity) }
                                Label { text: modelData.message; color: theme.textSoft; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideRight }
                            }
                            Rectangle { width: 1; Layout.fillHeight: true; color: theme.lineSoft }
                            Label { text: modelData.category; color: root.severityColor(modelData.severity); font.pixelSize: 9; leftPadding: 8; Layout.preferredWidth: 100; elide: Text.ElideRight }
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: serverSettings
        parent: Overlay.overlay
        modal: true
        focus: true
        width: 430
        height: 455
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        closePolicy: Popup.CloseOnEscape
        padding: 0

        background: Rectangle {
            color: theme.surface
            border.width: 2
            border.color: theme.navigation
        }

        contentItem: ColumnLayout {
            spacing: 0

            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 34; color: theme.navigation
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 9; anchors.rightMargin: 9
                    Label { text: "Simulate IED '" + (backend.selectedIed.name || "IED") + "'"; color: theme.navigationText; font.pixelSize: 11; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                    Label { text: "?"; color: theme.navigationText; font.pixelSize: 12; font.weight: Font.Bold }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true
                Layout.margins: 12
                spacing: 8

                Label { text: "Server settings"; color: theme.text; font.pixelSize: 11; font.weight: Font.DemiBold }
                Rectangle { Layout.fillWidth: true; height: 1; color: theme.textSoft }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 7
                    Label { text: "Enable server:"; color: theme.textSoft; font.pixelSize: 10 }
                    CheckBox { checked: true; enabled: false }
                    Label { text: "Listening on:"; color: theme.textSoft; font.pixelSize: 10 }
                    ComboBox {
                        id: settingsAddressBox
                        Layout.fillWidth: true; implicitHeight: 30
                        model: backend.availableAddresses
                        currentIndex: Math.max(0, backend.availableAddresses.indexOf(backend.listenAddress))
                        onActivated: backend.listenAddress = currentText
                        contentItem: Label { leftPadding: 8; rightPadding: 24; text: parent.displayText; color: theme.text; verticalAlignment: Text.AlignVCenter; font.pixelSize: 10 }
                        background: Rectangle { color: "#f7f8f9"; border.width: 1; border.color: theme.line }
                    }
                    Label { text: "Port:"; color: theme.textSoft; font.pixelSize: 10 }
                    TextField {
                        id: settingsPort
                        Layout.fillWidth: true; implicitHeight: 30
                        text: backend.port.toString()
                        validator: IntValidator { bottom: 1; top: 65535 }
                        color: theme.text; font.pixelSize: 10
                        background: Rectangle { color: "#ffffff"; border.width: 1; border.color: parent.activeFocus ? theme.accent : theme.line }
                    }
                    Label { text: "File transfer folder:"; color: theme.textSoft; font.pixelSize: 10 }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 4
                        TextField {
                            Layout.fillWidth: true; implicitHeight: 30
                            enabled: backend.fileServiceEnabled
                            text: backend.fileFolder
                            placeholderText: backend.fileServiceEnabled ? "Choose folder" : "File service disabled"
                            placeholderTextColor: theme.muted
                            color: theme.text; font.pixelSize: 9
                            onEditingFinished: backend.fileFolder = text
                            background: Rectangle { color: enabled ? "#ffffff" : "#f0f1f2"; border.width: 1; border.color: theme.line }
                        }
                        Button {
                            text: "…"; implicitWidth: 34; implicitHeight: 30
                            enabled: backend.fileServiceEnabled
                            onClicked: root.folderRequested()
                            contentItem: Label { text: parent.text; color: theme.text; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { color: "#f2f3f4"; border.width: 1; border.color: theme.line }
                        }
                    }
                }

                Item { Layout.preferredHeight: 4 }
                Label { text: "GOOSE publishing settings"; color: theme.text; font.pixelSize: 11; font.weight: Font.DemiBold }
                Rectangle { Layout.fillWidth: true; height: 1; color: theme.textSoft }
                GridLayout {
                    Layout.fillWidth: true; columns: 2; columnSpacing: 10; rowSpacing: 4
                    Label { text: "Enable GOOSE:"; color: theme.textSoft; font.pixelSize: 10 }
                    CheckBox { checked: false; enabled: false }
                    Label { text: "Simulation/Test:"; color: theme.textSoft; font.pixelSize: 10 }
                    CheckBox { checked: true; enabled: false }
                }
                Label { text: "GOOSE publishing is intentionally disabled until the native publisher milestone is wired to this GUI."; color: theme.muted; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }

                Item { Layout.preferredHeight: 4 }
                Label { text: "General settings"; color: theme.text; font.pixelSize: 11; font.weight: Font.DemiBold }
                Rectangle { Layout.fillWidth: true; height: 1; color: theme.textSoft }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Mode/Behavior:"; color: theme.textSoft; font.pixelSize: 10; Layout.preferredWidth: 126 }
                    ComboBox {
                        Layout.fillWidth: true; implicitHeight: 30
                        model: ["on"]
                        contentItem: Label { leftPadding: 8; text: parent.displayText; color: theme.text; verticalAlignment: Text.AlignVCenter; font.pixelSize: 10 }
                        background: Rectangle { color: "#f7f8f9"; border.width: 1; border.color: theme.line }
                    }
                }

                Item { Layout.fillHeight: true }
                Rectangle { Layout.fillWidth: true; height: 1; color: theme.lineSoft }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        theme: root.theme
                        text: "Start"
                        primary: true
                        Layout.preferredWidth: 96
                        onClicked: {
                            var requestedPort = Number(settingsPort.text)
                            if (requestedPort >= 1 && requestedPort <= 65535) backend.port = requestedPort
                            serverSettings.close()
                            backend.startSimulation()
                        }
                    }
                    ActionButton {
                        theme: root.theme
                        text: "Cancel"
                        Layout.preferredWidth: 96
                        onClicked: serverSettings.close()
                    }
                }
            }
        }
    }

    Popup {
        id: valueEditor
        parent: Overlay.overlay
        modal: true
        focus: true
        width: 470
        height: 390
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        closePolicy: Popup.CloseOnEscape
        padding: 0

        background: Rectangle {
            color: theme.surface
            border.width: 2
            border.color: theme.navigation
        }

        onOpened: root.refreshEditor()

        contentItem: ColumnLayout {
            spacing: 0
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 34; color: theme.navigation
                Label { anchors.fill: parent; leftPadding: 10; text: "Set simulated value"; color: theme.navigationText; font.pixelSize: 11; font.weight: Font.DemiBold; verticalAlignment: Text.AlignVCenter }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true
                Layout.margins: 14; spacing: 9
                Label { text: backend.selectedValue.reference || "No attribute selected"; color: theme.text; font.pixelSize: 11; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Type"; color: theme.muted; font.pixelSize: 9; Layout.preferredWidth: 70 }
                    Label { text: backend.selectedValue.type || "—"; color: theme.textSoft; font.pixelSize: 9; Layout.fillWidth: true }
                    Label { text: backend.selectedValue.fc ? "[" + backend.selectedValue.fc + "]" : ""; color: theme.muted; font.pixelSize: 9 }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: theme.lineSoft }
                Label { text: "Value"; color: theme.textSoft; font.pixelSize: 10 }
                ComboBox {
                    id: valueOptions
                    Layout.fillWidth: true; implicitHeight: 32
                    visible: (backend.selectedValue.options || []).length > 0
                    model: backend.selectedValue.options || []
                    currentIndex: Math.max(0, model.indexOf(root.pendingValue))
                    onActivated: root.pendingValue = currentText
                    contentItem: Label { leftPadding: 8; rightPadding: 24; text: parent.displayText; color: theme.text; verticalAlignment: Text.AlignVCenter; font.pixelSize: 10 }
                    background: Rectangle { color: "#ffffff"; border.width: 1; border.color: theme.line }
                }
                TextField {
                    Layout.fillWidth: true; implicitHeight: 32
                    visible: (backend.selectedValue.options || []).length === 0
                    text: root.pendingValue
                    onTextEdited: root.pendingValue = text
                    color: theme.text; font.pixelSize: 10
                    background: Rectangle { color: "#ffffff"; border.width: 1; border.color: parent.activeFocus ? theme.accent : theme.line }
                }
                Label { text: "Quality"; color: theme.textSoft; font.pixelSize: 10 }
                ComboBox {
                    id: editQuality
                    Layout.fillWidth: true; implicitHeight: 32
                    model: ["Good", "Invalid", "Questionable", "Reserved"]
                    currentIndex: Math.max(0, model.indexOf(root.pendingQuality))
                    onActivated: root.pendingQuality = currentText
                    contentItem: Label { leftPadding: 8; rightPadding: 24; text: parent.displayText; color: theme.text; verticalAlignment: Text.AlignVCenter; font.pixelSize: 10 }
                    background: Rectangle { color: "#ffffff"; border.width: 1; border.color: theme.line }
                }
                Label { text: "Origin"; color: theme.textSoft; font.pixelSize: 10 }
                ComboBox {
                    id: editOrigin
                    Layout.fillWidth: true; implicitHeight: 32
                    model: ["Simulator", "Process", "Operator", "Test"]
                    currentIndex: Math.max(0, model.indexOf(root.pendingOrigin))
                    onActivated: root.pendingOrigin = currentText
                    contentItem: Label { leftPadding: 8; rightPadding: 24; text: parent.displayText; color: theme.text; verticalAlignment: Text.AlignVCenter; font.pixelSize: 10 }
                    background: Rectangle { color: "#ffffff"; border.width: 1; border.color: theme.line }
                }
                Item { Layout.fillHeight: true }
                RowLayout {
                    Layout.fillWidth: true
                    ActionButton { theme: root.theme; text: "Undo last"; enabled: backend.running; onClicked: backend.undoLastChange() }
                    Item { Layout.fillWidth: true }
                    ActionButton { theme: root.theme; text: "Cancel"; onClicked: valueEditor.close() }
                    ActionButton {
                        theme: root.theme; text: "Apply"; primary: true
                        enabled: backend.running && backend.selectedValue.writable && root.pendingValue.length > 0
                        onClicked: {
                            if (backend.applySelectedValue(root.pendingValue, root.pendingQuality, root.pendingOrigin)) {
                                root.rebuildTable()
                                valueEditor.close()
                            }
                        }
                    }
                }
            }
        }
    }
}
