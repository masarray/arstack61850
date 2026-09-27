// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ARStack.IedSimulator 1.0

Item {
    id: root
    required property var theme
    required property var client

    property var modelProvider: client
    property bool showConnectionHeader: true
    property bool showNavigationPanel: true

    property var selected: modelProvider.treeModel.selectedNode
    property bool modelAvailable: modelProvider && modelProvider.treeModel
                                  && modelProvider.treeModel.totalNodeCount > 0
    property string pendingWrite: ""

    function text(value) {
        return value === undefined || value === null || String(value).length === 0 ? "—" : String(value)
    }

    function sclHealthLabel() {
        if (client.trustedSclHealth === "matched") return "SCL MATCHED"
        if (client.trustedSclHealth === "degraded") return "SCL DEGRADED"
        if (client.trustedSclHealth === "incompatible") return "SCL INCOMPATIBLE"
        return ""
    }

    function sclHealthColor() {
        if (client.trustedSclHealth === "matched") return theme.green
        if (client.trustedSclHealth === "degraded") return theme.amber
        if (client.trustedSclHealth === "incompatible") return theme.red
        return theme.muted
    }

    function usingEngineeringModel() {
        return modelProvider !== client
    }

    function readCurrentSelection() {
        if (usingEngineeringModel())
            return client.readEngineeringSelected()
        return client.readSelected()
    }

    function refreshCurrentVisible(firstRow, lastRow) {
        if (usingEngineeringModel())
            return client.refreshEngineeringVisible(firstRow, lastRow)
        return client.refreshVisible(firstRow, lastRow)
    }

    function writeCurrentSelection(value) {
        if (usingEngineeringModel())
            return client.writeEngineeringSelected(value)
        return client.writeSelected(value)
    }

    function firstVisibleRow() {
        var activeView = root.showNavigationPanel ? tree : valueTable
        var index = activeView.indexAt(4, activeView.contentY + 4)
        return index >= 0 ? index : 0
    }

    function lastVisibleRow() {
        var activeView = root.showNavigationPanel ? tree : valueTable
        var index = activeView.indexAt(4, activeView.contentY + activeView.height - 4)
        return index >= 0 ? index : Math.max(0, activeView.count - 1)
    }

    function revealSelection() {
        if (!valueTable || valueTable.count <= 0)
            return
        const row = root.modelProvider.treeModel.selectedRow
        if (row >= 0)
            valueTable.positionViewAtIndex(row, ListView.Contain)
    }

    Rectangle { anchors.fill: parent; color: theme.background }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            visible: root.showConnectionHeader
            Layout.fillWidth: true
            Layout.preferredHeight: root.showConnectionHeader ? 54 : 0
            color: theme.chrome
            border.width: 1
            border.color: theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 8

                TextField {
                    id: hostField
                    Layout.preferredWidth: 190
                    text: client.host
                    placeholderText: "IED IP / hostname"
                    selectByMouse: true
                    enabled: !client.connected && !client.busy
                    onEditingFinished: client.host = text
                }
                SpinBox {
                    id: portField
                    Layout.preferredWidth: 100
                    from: 1
                    to: 65535
                    value: client.port
                    editable: true
                    enabled: !client.connected && !client.busy
                    onValueModified: client.port = value
                }
                Button {
                    text: client.connected ? "Reconnect" : "Connect"
                    enabled: !client.busy
                    onClicked: {
                        client.host = hostField.text
                        client.port = portField.value
                        if (client.connected) client.reconnect()
                        else client.connectToIed()
                    }
                }
                Button {
                    text: "Disconnect"
                    enabled: client.connected || client.busy
                    onClicked: client.disconnectFromIed()
                }

                Rectangle {
                    width: 8; height: 8; radius: 4
                    color: client.trustedSclIncompatible ? theme.red
                                                        : client.trustedSclDegraded ? theme.amber
                                                        : client.connected ? theme.green
                                                                           : client.busy ? theme.amber
                                                                                         : client.lastError.length ? theme.red : theme.muted
                }
                Label {
                    text: client.stateText
                    color: theme.textSoft
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                }

                Rectangle {
                    visible: client.trustedSclHealth.length > 0
                    implicitWidth: healthLabel.implicitWidth + 14
                    implicitHeight: 20
                    radius: 10
                    color: Qt.rgba(root.sclHealthColor().r,
                                   root.sclHealthColor().g,
                                   root.sclHealthColor().b,
                                   0.12)
                    border.width: 1
                    border.color: root.sclHealthColor()
                    Label {
                        id: healthLabel
                        anchors.centerIn: parent
                        text: root.sclHealthLabel()
                        color: root.sclHealthColor()
                        font.pixelSize: 8
                        font.weight: Font.DemiBold
                    }
                }

                Item { Layout.fillWidth: true }

                ColumnLayout {
                    visible: client.connected
                    spacing: 0
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: client.iedName.length ? client.iedName : client.endpoint
                        color: theme.text
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: client.logicalDeviceCount + " LD · " + client.logicalNodeCount + " LN · " + client.dataAttributeCount + " DA"
                        color: theme.muted
                        font.pixelSize: 8
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                visible: root.showNavigationPanel
                Layout.preferredWidth: root.showNavigationPanel ? Math.max(390, root.width * 0.44) : 0
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
                        Layout.rightMargin: 10
                        spacing: 6
                        TextField {
                            id: searchField
                            Layout.fillWidth: true
                            placeholderText: "Search reference, FC, type or value"
                            enabled: root.modelAvailable
                            onTextChanged: searchDebounce.restart()
                        }
                        Button {
                            text: "Refresh visible"
                            enabled: client.connected && !client.operationBusy && tree.count > 0
                            onClicked: root.refreshCurrentVisible(root.firstVisibleRow(), root.lastVisibleRow())
                        }
                        Timer {
                            id: searchDebounce
                            interval: 100
                            repeat: false
                            onTriggered: root.modelProvider.treeModel.filterText = searchField.text
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

                    ListView {
                        id: tree
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        reuseItems: true
                        cacheBuffer: 0
                        model: root.modelProvider.treeModel
                        ScrollBar.vertical: ScrollBar { }

                        delegate: Rectangle {
                            id: row
                            width: ListView.view.width
                            height: 30
                            color: model.selected ? theme.accentSoft
                                                  : mouse.containsMouse ? theme.surfaceRaised
                                                                        : "transparent"
                            border.width: model.selected ? 1 : 0
                            border.color: theme.accent

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8 + model.depth * 14
                                anchors.rightMargin: 10
                                spacing: 5
                                Label {
                                    Layout.preferredWidth: 12
                                    text: model.hasChildren ? (model.expanded ? "▾" : "▸") : ""
                                    color: theme.muted
                                    font.pixelSize: 10
                                }
                                Label {
                                    Layout.preferredWidth: 24
                                    text: model.kind
                                    color: model.kind === "DA" ? theme.accent : theme.muted
                                    font.pixelSize: 7
                                    font.weight: Font.DemiBold
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: model.label
                                    color: theme.textSoft
                                    font.pixelSize: 9
                                    elide: Text.ElideMiddle
                                }
                                Label {
                                    visible: model.functionalConstraint.length > 0
                                    text: model.functionalConstraint
                                    color: theme.muted
                                    font.pixelSize: 8
                                }
                                Label {
                                    visible: model.value.length > 0
                                    Layout.maximumWidth: 116
                                    text: model.value
                                    color: theme.text
                                    font.pixelSize: 8
                                    elide: Text.ElideRight
                                }
                            }

                            MouseArea {
                                id: mouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: function(event) {
                                    root.modelProvider.treeModel.selectRow(index)
                                    if (model.hasChildren && event.x < 38 + model.depth * 14)
                                        root.modelProvider.treeModel.toggle(index)
                                }
                                onDoubleClicked: {
                                    if (model.hasChildren) root.modelProvider.treeModel.toggle(index)
                                    else root.readCurrentSelection()
                                }
                            }
                        }
                    }
                }
            }

            SplitView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                orientation: Qt.Horizontal

                Rectangle {
                    SplitView.fillWidth: true
                    SplitView.fillHeight: true
                    SplitView.minimumWidth: 420
                    color: theme.surface
                    border.width: 1
                    border.color: theme.lineSoft

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 48
                            color: theme.chrome
                            border.width: 1
                            border.color: theme.lineSoft

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 10
                                spacing: 8

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Label {
                                        text: "Model values"
                                        color: theme.text
                                        font.pixelSize: theme.labelSize
                                        font.weight: Font.DemiBold
                                    }
                                    Label {
                                        text: root.modelAvailable
                                              ? root.modelProvider.treeModel.visibleNodeCount + " visible · "
                                                + root.modelProvider.treeModel.totalNodeCount + " model nodes"
                                              : "Open an engineering model or discover an IED"
                                        color: theme.muted
                                        font.pixelSize: theme.captionSize
                                    }
                                }
                                ActionButton {
                                    theme: root.theme
                                    text: root.client.operationBusy ? "Reading…" : "Read visible"
                                    enabled: root.client.connected && !root.client.operationBusy && valueTable.count > 0
                                    onClicked: root.refreshCurrentVisible(root.firstVisibleRow(), root.lastVisibleRow())
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Read the visible attributes only. The request is bounded to 64 targets."
                                }
                                ActionButton {
                                    theme: root.theme
                                    text: "Locate selection"
                                    enabled: root.modelAvailable && root.modelProvider.treeModel.selectedRow >= 0
                                    onClicked: root.revealSelection()
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 30
                            color: theme.surfaceRaised
                            border.width: 1
                            border.color: theme.lineSoft

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 10
                                spacing: 0
                                Label {
                                    Layout.fillWidth: true
                                    text: "Name"
                                    color: theme.textSoft
                                    font.pixelSize: theme.captionSize
                                    font.weight: Font.DemiBold
                                }
                                Label {
                                    Layout.preferredWidth: 48
                                    text: "FC"
                                    color: theme.textSoft
                                    font.pixelSize: theme.captionSize
                                    font.weight: Font.DemiBold
                                }
                                Label {
                                    Layout.preferredWidth: Math.max(120, valueTable.width * 0.24)
                                    text: "Value"
                                    color: theme.textSoft
                                    font.pixelSize: theme.captionSize
                                    font.weight: Font.DemiBold
                                }
                                Label {
                                    Layout.preferredWidth: Math.max(104, valueTable.width * 0.18)
                                    text: "Type"
                                    color: theme.textSoft
                                    font.pixelSize: theme.captionSize
                                    font.weight: Font.DemiBold
                                }
                            }
                        }

                        ListView {
                            id: valueTable
                            objectName: "iedBrowserValueTable"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            reuseItems: true
                            cacheBuffer: 0
                            model: root.modelProvider.treeModel
                            currentIndex: root.modelProvider.treeModel.selectedRow
                            boundsBehavior: Flickable.StopAtBounds
                            keyNavigationEnabled: true
                            activeFocusOnTab: true
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                            delegate: Rectangle {
                                id: valueRow
                                required property int index
                                // Bind the complete model role object alongside the index.
                                required property var model
                                width: valueTable.width
                                height: 31
                                color: model.selected
                                       ? theme.accentSoft
                                       : valueMouse.containsMouse ? theme.surfaceRaised
                                                                  : index % 2 ? theme.chrome : theme.surface
                                border.width: model.selected ? 1 : 0
                                border.color: theme.accent

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 0

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.leftMargin: Math.max(0, model.depth - 1) * 14
                                        spacing: 6

                                        Label {
                                            Layout.preferredWidth: 12
                                            text: model.hasChildren ? (model.expanded ? "▾" : "▸") : ""
                                            color: theme.muted
                                            font.pixelSize: 10
                                        }
                                        Rectangle {
                                            Layout.preferredWidth: 25
                                            Layout.preferredHeight: 18
                                            radius: 3
                                            color: model.kind === "DA" ? theme.accentSoft
                                                   : model.kind === "DO" ? theme.surfaceSoft
                                                                         : theme.surfaceRaised
                                            border.width: 1
                                            border.color: model.kind === "DA" ? theme.accent : theme.line
                                            Label {
                                                anchors.centerIn: parent
                                                text: model.kind
                                                color: model.kind === "DA" ? theme.accent : theme.textSoft
                                                font.pixelSize: 8
                                                font.weight: Font.DemiBold
                                            }
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: model.label
                                            color: theme.text
                                            font.pixelSize: theme.labelSize
                                            font.weight: model.kind === "IED" || model.kind === "LD" || model.kind === "LN"
                                                         ? Font.DemiBold : Font.Normal
                                            elide: Text.ElideMiddle
                                        }
                                    }

                                    Label {
                                        Layout.preferredWidth: 48
                                        text: model.functionalConstraint || ""
                                        color: theme.muted
                                        font.pixelSize: theme.captionSize
                                    }
                                    Label {
                                        Layout.preferredWidth: Math.max(120, valueTable.width * 0.24)
                                        text: model.value && model.value.length ? model.value : "—"
                                        color: model.value && model.value.length ? theme.text : theme.muted
                                        font.pixelSize: theme.labelSize
                                        font.weight: model.value && model.value.length ? Font.Medium : Font.Normal
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        Layout.preferredWidth: Math.max(104, valueTable.width * 0.18)
                                        text: model.sclType || model.mmsType || ""
                                        color: model.typeStatus === "Exact" ? theme.textSoft : theme.muted
                                        font.pixelSize: theme.captionSize
                                        elide: Text.ElideRight
                                    }
                                }

                                MouseArea {
                                    id: valueMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: function(event) {
                                        const wasExpanded = model.expanded
                                        root.modelProvider.treeModel.selectRow(index)
                                        if (model.hasChildren
                                                && (!wasExpanded
                                                    || event.x < 58 + Math.max(0, model.depth - 1) * 14))
                                            root.modelProvider.treeModel.toggle(index)
                                    }
                                    onDoubleClicked: {
                                        if (model.hasChildren)
                                            root.modelProvider.treeModel.toggle(index)
                                        else if (model.kind === "DA")
                                            root.readCurrentSelection()
                                    }
                                }

                                ToolTip.visible: valueMouse.containsMouse
                                ToolTip.text: model.reference || model.label
                                ToolTip.delay: 700
                            }

                            Label {
                                anchors.centerIn: parent
                                visible: valueTable.count === 0
                                width: Math.min(420, parent.width - 48)
                                text: root.modelAvailable
                                      ? "No nodes match the current filter."
                                      : "Load or discover an IED to inspect its canonical model and live values."
                                color: theme.muted
                                font.pixelSize: theme.bodySize
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                            }

                            Keys.onReturnPressed: {
                                if (currentIndex >= 0) {
                                    root.modelProvider.treeModel.selectRow(currentIndex)
                                    if (root.selected.kind === "DA")
                                        root.readCurrentSelection()
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    SplitView.preferredWidth: 318
                    SplitView.minimumWidth: 278
                    SplitView.maximumWidth: 430
                    SplitView.fillHeight: true
                    color: theme.chrome
                    border.width: 1
                    border.color: theme.lineSoft

                    ScrollView {
                        anchors.fill: parent
                        contentWidth: availableWidth

                        ColumnLayout {
                            width: Math.max(0, parent.width - 32)
                            x: 16
                            spacing: 12

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: 14
                                spacing: 8
                                Rectangle {
                                    Layout.preferredWidth: 32
                                    Layout.preferredHeight: 24
                                    radius: 4
                                    color: root.selected.kind === "DA" ? theme.accentSoft : theme.surfaceRaised
                                    border.width: 1
                                    border.color: root.selected.kind === "DA" ? theme.accent : theme.line
                                    Label {
                                        anchors.centerIn: parent
                                        text: root.text(root.selected.kind)
                                        color: root.selected.kind === "DA" ? theme.accent : theme.textSoft
                                        font.pixelSize: theme.captionSize
                                        font.weight: Font.DemiBold
                                    }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: root.text(root.selected.label)
                                    color: theme.text
                                    font.pixelSize: theme.subtitleSize
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                text: root.text(root.selected.reference)
                                color: theme.muted
                                font.pixelSize: theme.captionSize
                                wrapMode: Text.WrapAnywhere
                            }

                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

                            Label {
                                text: "OBSERVED VALUE"
                                color: theme.muted
                                font.pixelSize: theme.captionSize
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.5
                            }
                            Label {
                                Layout.fillWidth: true
                                text: root.text(root.selected.value)
                                color: theme.text
                                font.pixelSize: 18
                                font.weight: Font.DemiBold
                                wrapMode: Text.WrapAnywhere
                            }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 12
                                rowSpacing: 7
                                Label { text: "Quality"; color: theme.muted; font.pixelSize: theme.captionSize }
                                Label { Layout.fillWidth: true; text: root.text(root.selected.quality); color: root.selected.quality === "good" ? theme.green : theme.textSoft; font.pixelSize: theme.labelSize; elide: Text.ElideRight }
                                Label { text: "Timestamp"; color: theme.muted; font.pixelSize: theme.captionSize }
                                Label { Layout.fillWidth: true; text: root.text(root.selected.timestamp); color: theme.textSoft; font.pixelSize: theme.labelSize; wrapMode: Text.WrapAnywhere }
                            }

                            ActionButton {
                                theme: root.theme
                                Layout.fillWidth: true
                                text: root.client.operationBusy ? "Reading selected…" : "Read selected"
                                primary: root.selected.readable === true
                                enabled: root.client.connected && !root.client.operationBusy && root.selected.readable === true
                                onClicked: root.readCurrentSelection()
                            }

                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

                            Label {
                                text: "ENGINEERING IDENTITY"
                                color: theme.muted
                                font.pixelSize: theme.captionSize
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.5
                            }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 12
                                rowSpacing: 7
                                Label { text: "FC"; color: theme.muted; font.pixelSize: theme.captionSize }
                                Label { Layout.fillWidth: true; text: root.text(root.selected.functionalConstraint); color: theme.textSoft; font.pixelSize: theme.labelSize }
                                Label { text: "MMS type"; color: theme.muted; font.pixelSize: theme.captionSize }
                                Label { Layout.fillWidth: true; text: root.text(root.selected.mmsType); color: theme.textSoft; font.pixelSize: theme.labelSize; elide: Text.ElideRight }
                                Label { text: "SCL type"; color: theme.muted; font.pixelSize: theme.captionSize }
                                Label { Layout.fillWidth: true; text: root.text(root.selected.sclType); color: theme.textSoft; font.pixelSize: theme.labelSize; elide: Text.ElideRight }
                                Label { text: "Evidence"; color: theme.muted; font.pixelSize: theme.captionSize }
                                Label { Layout.fillWidth: true; text: root.text(root.selected.typeStatus); color: root.selected.typeStatus === "Exact" ? theme.green : theme.amber; font.pixelSize: theme.labelSize }
                                Label { text: "MMS item"; color: theme.muted; font.pixelSize: theme.captionSize }
                                Label { Layout.fillWidth: true; text: root.text(root.selected.mmsDomain) + " / " + root.text(root.selected.mmsItem); color: theme.textSoft; font.pixelSize: theme.captionSize; wrapMode: Text.WrapAnywhere }
                            }

                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

                            Label {
                                text: "WRITE"
                                color: theme.muted
                                font.pixelSize: theme.captionSize
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.5
                            }
                            TextField {
                                id: writeValue
                                Layout.fillWidth: true
                                placeholderText: root.selected.writable === true
                                                 ? "Enter a scalar value"
                                                 : "Read-only or unsupported type"
                                enabled: client.connected && !client.operationBusy && root.selected.writable === true
                                selectByMouse: true
                                onTextChanged: root.pendingWrite = text
                            }
                            ActionButton {
                                theme: root.theme
                                Layout.fillWidth: true
                                text: "Review write…"
                                enabled: writeValue.enabled && writeValue.text.length > 0
                                onClicked: writeConfirm.open()
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: root.selected.writable !== true && root.selected.kind === "DA"
                                text: "Generic write remains fail-closed. Only exact scalar SP/CF/DC/SE attributes are eligible."
                                color: theme.muted
                                font.pixelSize: theme.captionSize
                                wrapMode: Text.WordWrap
                            }

                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

                            Label {
                                text: "SESSION"
                                color: theme.muted
                                font.pixelSize: theme.captionSize
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.5
                            }
                            Label { Layout.fillWidth: true; text: client.endpoint; color: theme.textSoft; font.pixelSize: theme.labelSize; elide: Text.ElideMiddle }
                            Label { Layout.fillWidth: true; text: "Association · " + root.text(client.associationProfile); color: theme.muted; font.pixelSize: theme.captionSize; wrapMode: Text.WordWrap }
                            Label {
                                visible: client.trustedSclHealth.length > 0
                                text: "Trusted SCL · " + client.trustedSclHealth.toUpperCase()
                                color: root.sclHealthColor()
                                font.pixelSize: theme.captionSize
                                font.weight: Font.DemiBold
                            }
                            Label { Layout.fillWidth: true; text: root.text(client.modelSummary); color: theme.muted; font.pixelSize: theme.captionSize; wrapMode: Text.WordWrap }
                            Label {
                                Layout.fillWidth: true
                                text: client.lastError.length ? client.lastError : client.lastDiagnostic
                                color: client.lastError.length ? theme.red : theme.muted
                                font.pixelSize: theme.captionSize
                                wrapMode: Text.WordWrap
                            }
                            Item { Layout.preferredHeight: 14 }
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: root.modelProvider.treeModel
        function onSelectionChanged() { Qt.callLater(root.revealSelection) }
    }

    Dialog {
        id: writeConfirm
        title: "Confirm guarded MMS Write"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(480, Math.max(320, root.width - 32))
        implicitWidth: 480
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.writeCurrentSelection(root.pendingWrite)
        contentItem: ColumnLayout {
            spacing: 8
            Label { Layout.fillWidth: true; text: root.text(root.selected.reference); color: theme.text; font.pixelSize: 10; wrapMode: Text.WrapAnywhere }
            Label { Layout.fillWidth: true; text: "FC " + root.text(root.selected.functionalConstraint) + " · " + root.text(root.selected.mmsType); color: theme.muted; font.pixelSize: 9 }
            Label { Layout.fillWidth: true; text: "Write value: " + root.pendingWrite; color: theme.textSoft; font.pixelSize: 10 }
            Label { Layout.fillWidth: true; text: "No automatic retry is performed."; color: theme.muted; font.pixelSize: 8 }
        }
    }
}
