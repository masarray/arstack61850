// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ARStack.IedSimulator 1.0

Item {
    id: root
    required property var theme
    required property var client

    property bool showConnectionHeader: true
    property bool showNavigationPanel: true

    property var selected: client.treeModel.selectedNode
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

    function firstVisibleRow() {
        var index = tree.indexAt(4, tree.contentY + 4)
        return index >= 0 ? index : 0
    }

    function lastVisibleRow() {
        var index = tree.indexAt(4, tree.contentY + tree.height - 4)
        return index >= 0 ? index : Math.max(0, tree.count - 1)
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
                            enabled: client.connected
                            onTextChanged: searchDebounce.restart()
                        }
                        Button {
                            text: "Refresh visible"
                            enabled: client.connected && !client.operationBusy && tree.count > 0
                            onClicked: client.refreshVisible(root.firstVisibleRow(), root.lastVisibleRow())
                        }
                        Timer {
                            id: searchDebounce
                            interval: 100
                            repeat: false
                            onTriggered: client.treeModel.filterText = searchField.text
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
                        model: client.treeModel
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
                                    client.treeModel.selectRow(index)
                                    if (model.hasChildren && event.x < 38 + model.depth * 14) client.treeModel.toggle(index)
                                }
                                onDoubleClicked: {
                                    if (model.hasChildren) client.treeModel.toggle(index)
                                    else client.readSelected()
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: theme.background

                ScrollView {
                    anchors.fill: parent
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: Math.max(0, parent.width - 36)
                        x: 18
                        spacing: 10

                        Label {
                            text: root.text(root.selected.kind) + "  " + root.text(root.selected.label)
                            color: theme.text
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                        }
                        Label {
                            Layout.fillWidth: true
                            text: root.text(root.selected.reference)
                            color: theme.muted
                            font.pixelSize: 9
                            wrapMode: Text.WrapAnywhere
                        }
                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 16
                            rowSpacing: 8
                            Label { text: "MMS item"; color: theme.muted; font.pixelSize: 9 }
                            Label { Layout.fillWidth: true; text: root.text(root.selected.mmsDomain) + " / " + root.text(root.selected.mmsItem); color: theme.textSoft; font.pixelSize: 9; elide: Text.ElideMiddle }
                            Label { text: "FC"; color: theme.muted; font.pixelSize: 9 }
                            Label { text: root.text(root.selected.functionalConstraint); color: theme.textSoft; font.pixelSize: 9 }
                            Label { text: "MMS type"; color: theme.muted; font.pixelSize: 9 }
                            Label { text: root.text(root.selected.mmsType); color: theme.textSoft; font.pixelSize: 9 }
                            Label { text: "SCL type"; color: theme.muted; font.pixelSize: 9 }
                            Label { text: root.text(root.selected.sclType); color: theme.textSoft; font.pixelSize: 9 }
                            Label { text: "Type evidence"; color: theme.muted; font.pixelSize: 9 }
                            Label { text: root.text(root.selected.typeStatus); color: root.selected.typeStatus === "Exact" ? theme.green : theme.amber; font.pixelSize: 9 }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 16
                            rowSpacing: 8
                            Label { text: "Value"; color: theme.muted; font.pixelSize: 9 }
                            Label { text: root.text(root.selected.value); color: theme.text; font.pixelSize: 11; font.weight: Font.DemiBold }
                            Label { text: "Quality"; color: theme.muted; font.pixelSize: 9 }
                            Label { text: root.text(root.selected.quality); color: theme.textSoft; font.pixelSize: 9 }
                            Label { text: "Timestamp"; color: theme.muted; font.pixelSize: 9 }
                            Label { text: root.text(root.selected.timestamp); color: theme.textSoft; font.pixelSize: 9 }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Button {
                                text: client.operationBusy ? "Reading…" : "Read"
                                enabled: client.connected && !client.operationBusy && root.selected.readable === true
                                onClicked: client.readSelected()
                            }
                            TextField {
                                id: writeValue
                                Layout.fillWidth: true
                                placeholderText: root.selected.writable === true ? "New scalar value" : "Read-only / unsupported Write type"
                                enabled: client.connected && !client.operationBusy && root.selected.writable === true
                                onTextChanged: root.pendingWrite = text
                            }
                            Button {
                                text: "Write…"
                                enabled: writeValue.enabled && writeValue.text.length > 0
                                onClicked: writeConfirm.open()
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: root.selected.writable !== true && root.selected.kind === "DA"
                            text: "Write is fail-closed: only exact scalar MMS types in FC SP/CF/DC/SE are enabled. ST/MX/CO and unknown/structured types are never written through this generic path."
                            color: theme.muted
                            font.pixelSize: 8
                            wrapMode: Text.WordWrap
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

                        Label {
                            text: "SESSION"
                            color: theme.muted
                            font.pixelSize: 8
                            font.weight: Font.DemiBold
                        }
                        Label { text: client.endpoint; color: theme.textSoft; font.pixelSize: 9 }
                        Label { text: "Association: " + root.text(client.associationProfile); color: theme.muted; font.pixelSize: 8 }
                        Label {
                            visible: client.trustedSclHealth.length > 0
                            text: "Trusted SCL health: " + client.trustedSclHealth.toUpperCase()
                            color: root.sclHealthColor()
                            font.pixelSize: 8
                            font.weight: Font.DemiBold
                        }
                        Label { Layout.fillWidth: true; text: root.text(client.modelSummary); color: theme.muted; font.pixelSize: 8; wrapMode: Text.WordWrap }
                        Label {
                            Layout.fillWidth: true
                            text: client.lastError.length ? client.lastError : client.lastDiagnostic
                            color: client.lastError.length ? theme.red : theme.muted
                            font.pixelSize: 8
                            wrapMode: Text.WordWrap
                        }
                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }
    }

    Dialog {
        id: writeConfirm
        title: "Confirm guarded MMS Write"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(480, Math.max(320, root.width - 32))
        implicitWidth: 480
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: client.writeSelected(root.pendingWrite)
        contentItem: ColumnLayout {
            spacing: 8
            Label { Layout.fillWidth: true; text: root.text(root.selected.reference); color: theme.text; font.pixelSize: 10; wrapMode: Text.WrapAnywhere }
            Label { Layout.fillWidth: true; text: "FC " + root.text(root.selected.functionalConstraint) + " · " + root.text(root.selected.mmsType); color: theme.muted; font.pixelSize: 9 }
            Label { Layout.fillWidth: true; text: "Write value: " + root.pendingWrite; color: theme.textSoft; font.pixelSize: 10 }
            Label { Layout.fillWidth: true; text: "No automatic retry is performed."; color: theme.muted; font.pixelSize: 8 }
        }
    }
}