// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ARStack.IedSimulator 1.0

Item {
    id: root

    required property var theme
    required property var backend

    signal openSclRequested()

    property string pendingValue: ""
    property string pendingQuality: "Good"
    property string pendingOrigin: "Simulator"
    readonly property bool showInspector: width >= 1260

    IedNavigationModel {
        id: navigationModel
        backend: root.backend
    }

    IedSignalModel {
        id: signalModel
        backend: root.backend
        logicalDevice: navigationModel.selectedLogicalDevice
        logicalNode: navigationModel.selectedLogicalNode
        filterText: searchField.text
    }

    component CommandButton: Button {
        id: control
        implicitHeight: 32
        implicitWidth: Math.max(76, label.implicitWidth + 24)
        font.pixelSize: 11
        font.weight: Font.DemiBold
        property bool primary: false
        property bool danger: false
        property bool quiet: false
        property color foreground: !enabled ? root.theme.muted
                                           : primary ? "#ffffff"
                                           : danger ? root.theme.red
                                           : root.theme.textSoft
        contentItem: Text {
            id: label
            text: control.text
            color: control.foreground
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 5
            color: !control.enabled ? root.theme.surfaceRaised
                                    : control.primary ? root.theme.accent
                                    : control.down ? root.theme.surfaceSoft
                                    : control.quiet ? "transparent"
                                    : root.theme.surface
            border.width: control.quiet ? 0 : 1
            border.color: control.primary ? root.theme.accent
                                          : control.danger ? root.theme.red
                                          : root.theme.line
        }
    }

    component MetaRow: RowLayout {
        property string title: ""
        property string value: ""
        Layout.fillWidth: true
        spacing: 8
        Label {
            Layout.preferredWidth: 72
            text: parent.title
            color: root.theme.muted
            font.pixelSize: 9
        }
        Label {
            Layout.fillWidth: true
            text: parent.value.length ? parent.value : "—"
            color: root.theme.textSoft
            font.pixelSize: 9
            elide: Text.ElideMiddle
            horizontalAlignment: Text.AlignRight
        }
    }

    Shortcut { sequence: "Ctrl+O"; onActivated: root.openSclRequested() }
    Shortcut {
        sequence: "Ctrl+F"
        enabled: backend.imported
        onActivated: {
            searchField.forceActiveFocus()
            searchField.selectAll()
        }
    }
    Shortcut {
        sequence: "Ctrl+E"
        enabled: backend.running && backend.selectedValue.writable === true
        onActivated: valueDialog.open()
    }
    Shortcut {
        sequence: "F5"
        enabled: backend.imported && !backend.running && !backend.starting
        onActivated: startDialog.open()
    }
    Shortcut {
        sequence: "Shift+F5"
        enabled: backend.running || backend.starting
        onActivated: backend.stopSimulation()
    }

    Rectangle { anchors.fill: parent; color: theme.background }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: theme.navigation

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 8

                ColumnLayout {
                    Layout.preferredWidth: 216
                    spacing: 0
                    Label {
                        text: "ARStack IED Simulator"
                        color: theme.navigationText
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: "IEC 61850 commissioning workspace"
                        color: theme.navigationMuted
                        font.pixelSize: 9
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    Layout.topMargin: 11
                    Layout.bottomMargin: 11
                    color: "#32423d"
                }

                CommandButton {
                    text: "Open SCL"
                    enabled: !backend.importing
                    onClicked: root.openSclRequested()
                }
                CommandButton {
                    text: "Start"
                    primary: true
                    enabled: backend.imported && !backend.importing && !backend.running && !backend.starting && backend.selectedIed.enabled
                    onClicked: startDialog.open()
                }
                CommandButton {
                    text: "Stop"
                    danger: true
                    enabled: backend.running || backend.starting
                    onClicked: backend.stopSimulation()
                }
                CommandButton {
                    text: "Set value"
                    enabled: backend.running && backend.selectedValue.writable === true
                    onClicked: valueDialog.open()
                }

                Item { Layout.fillWidth: true }

                ColumnLayout {
                    visible: backend.imported
                    Layout.maximumWidth: 260
                    spacing: 0
                    Label {
                        Layout.fillWidth: true
                        text: backend.selectedIed.name || "IED"
                        color: theme.navigationText
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        text: (backend.selectedIed.endpoint || (backend.listenAddress + ":" + backend.port))
                        color: theme.navigationMuted
                        font.pixelSize: 8
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideMiddle
                    }
                }

                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: backend.running ? theme.green
                                           : backend.starting || backend.importing ? theme.amber
                                                                                  : theme.muted
                }
                Label {
                    text: backend.importing ? "IMPORTING"
                                            : backend.running ? "LIVE"
                                                              : backend.starting ? "STARTING" : "READY"
                    color: backend.running ? "#9ff0c1" : theme.navigationMuted
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.preferredWidth: 286
                Layout.minimumWidth: 246
                Layout.maximumWidth: 320
                Layout.fillHeight: true
                color: theme.chrome
                border.width: 1
                border.color: theme.lineSoft

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34
                        Layout.leftMargin: 12
                        Layout.rightMargin: 10
                        Label {
                            Layout.fillWidth: true
                            text: "ACTIVE IED"
                            color: theme.muted
                            font.pixelSize: 8
                            font.weight: Font.DemiBold
                        }
                        Rectangle {
                            visible: backend.imported
                            implicitWidth: runtimeLabel.implicitWidth + 14
                            implicitHeight: 18
                            radius: 9
                            color: backend.running ? theme.greenSoft
                                                   : backend.starting ? theme.amberSoft
                                                                      : theme.surfaceRaised
                            Label {
                                id: runtimeLabel
                                anchors.centerIn: parent
                                text: backend.running ? "RUNNING" : backend.starting ? "STARTING" : "OFFLINE"
                                color: backend.running ? theme.green
                                                       : backend.starting ? theme.amber
                                                                          : theme.muted
                                font.pixelSize: 7
                                font.weight: Font.Bold
                            }
                        }
                    }

                    ComboBox {
                        id: iedSelector
                        Layout.fillWidth: true
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        Layout.preferredHeight: 33
                        model: backend.ieds
                        textRole: "name"
                        currentIndex: backend.selectedIedIndex
                        enabled: backend.ieds.length > 0 && !backend.anyRunning && !backend.importing
                        font.pixelSize: 10
                        onActivated: backend.selectIed(currentIndex)
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        Layout.topMargin: 8
                        Layout.bottomMargin: 9
                        Layout.preferredHeight: 76
                        radius: 6
                        color: theme.surface
                        border.width: 1
                        border.color: theme.lineSoft

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 9
                            spacing: 3
                            MetaRow { title: "Endpoint"; value: backend.imported ? (backend.listenAddress + ":" + backend.port) : "—" }
                            MetaRow { title: "Model"; value: backend.sourceName || "—" }
                            MetaRow {
                                title: "Profile"
                                value: backend.selectedIed.type || backend.selectedIed.manufacturer || "—"
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34
                        Layout.leftMargin: 12
                        Layout.rightMargin: 7
                        Label {
                            Layout.fillWidth: true
                            text: "DATA MODEL"
                            color: theme.muted
                            font.pixelSize: 8
                            font.weight: Font.DemiBold
                        }
                        ToolButton {
                            text: "−"
                            font.pixelSize: 12
                            ToolTip.visible: hovered
                            ToolTip.text: "Collapse all"
                            onClicked: navigationModel.collapseAll()
                        }
                        ToolButton {
                            text: "+"
                            font.pixelSize: 12
                            ToolTip.visible: hovered
                            ToolTip.text: "Expand all"
                            onClicked: navigationModel.expandAll()
                        }
                    }

                    ListView {
                        id: navigationList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: navigationModel
                        reuseItems: true
                        cacheBuffer: 0
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        delegate: Rectangle {
                            id: navRow
                            required property int index
                            required property string kind
                            required property string name
                            required property int depth
                            required property bool expanded
                            required property bool expandable
                            required property bool selected

                            width: navigationList.width
                            height: kind === "SECTION" ? 31 : 28
                            color: selected ? theme.accentSoft
                                            : navMouse.containsMouse ? theme.surfaceRaised
                                                                    : "transparent"

                            Rectangle {
                                visible: navRow.selected
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: 3
                                color: theme.accent
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10 + navRow.depth * 15
                                anchors.rightMargin: 8
                                spacing: 5

                                Label {
                                    Layout.preferredWidth: 12
                                    text: navRow.expandable ? (navRow.expanded ? "⌄" : "›") : ""
                                    color: theme.muted
                                    font.pixelSize: 12
                                }
                                Rectangle {
                                    visible: navRow.kind !== "SECTION"
                                    width: 23
                                    height: 17
                                    radius: 3
                                    color: navRow.kind === "LD" ? theme.surfaceSoft : theme.accentSoft
                                    Label {
                                        anchors.centerIn: parent
                                        text: navRow.kind
                                        color: navRow.kind === "LN" ? theme.accent : theme.textSoft
                                        font.pixelSize: 7
                                        font.weight: Font.Bold
                                    }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: navRow.name
                                    color: navRow.selected ? theme.accent : theme.textSoft
                                    font.pixelSize: navRow.kind === "SECTION" ? 10 : 9
                                    font.weight: navRow.kind === "SECTION" || navRow.selected
                                                 ? Font.DemiBold : Font.Normal
                                    elide: Text.ElideRight
                                }
                            }

                            MouseArea {
                                id: navMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: navigationModel.activate(navRow.index)
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        Label {
                            Layout.fillWidth: true
                            text: backend.reportCount + " reports"
                            color: theme.muted
                            font.pixelSize: 9
                        }
                        Label {
                            text: backend.dataSetCount + " data sets"
                            color: theme.muted
                            font.pixelSize: 9
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
                        Layout.preferredHeight: 56
                        color: theme.surface

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 10
                            spacing: 10

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Label {
                                    Layout.fillWidth: true
                                    text: (backend.selectedIed.name || "IED") + "  ›  " +
                                          (navigationModel.selectedLogicalDevice || "Data Model") + "  ›  " +
                                          (navigationModel.selectedLogicalNode || "—")
                                    color: theme.text
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideMiddle
                                }
                                Label {
                                    text: signalModel.visibleRowCount + " visible rows"
                                          + (searchField.text.length ? " · filtered" : "")
                                    color: theme.muted
                                    font.pixelSize: 9
                                }
                            }

                            RowLayout {
                                Layout.preferredWidth: Math.min(360, Math.max(250, root.width * 0.25))
                                spacing: 4
                                TextField {
                                    id: searchField
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 32
                                    placeholderText: "Search object, value, FC or reference"
                                    selectByMouse: true
                                    font.pixelSize: 10
                                    enabled: backend.imported
                                }
                                ToolButton {
                                    visible: searchField.text.length > 0
                                    text: "×"
                                    font.pixelSize: 13
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Clear search"
                                    onClicked: {
                                        searchField.clear()
                                        searchField.forceActiveFocus()
                                    }
                                }
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        color: theme.surfaceRaised
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 0
                            Label { Layout.fillWidth: true; text: "Object / attribute"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                            Label { Layout.preferredWidth: 54; text: "FC"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                            Label { Layout.preferredWidth: 96; text: "Type"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                            Label { Layout.preferredWidth: 88; text: "Quality"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                            Label { Layout.preferredWidth: 150; text: "Value"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignRight }
                        }
                    }

                    ListView {
                        id: signalList
                        objectName: "iedLiveSignalTable"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: signalModel
                        reuseItems: true
                        cacheBuffer: 0
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        delegate: Rectangle {
                            id: signalRow
                            required property int index
                            required property string kind
                            required property string name
                            required property int depth
                            required property var value
                            required property var fc
                            required property var type
                            required property var quality
                            required property bool writable
                            required property bool changed
                            required property bool selected
                            required property var reference

                            width: signalList.width
                            height: signalRow.kind === "DO" ? 32 : 29
                            color: signalRow.selected ? theme.accentSoft
                                                     : signalRow.kind === "DO" ? theme.chrome
                                                     : signalMouse.containsMouse ? "#f6f9f8"
                                                                                 : theme.surface

                            Rectangle {
                                visible: signalRow.selected
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: 3
                                color: theme.accent
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 0

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Item { Layout.preferredWidth: signalRow.depth * 18 }
                                    Rectangle {
                                        width: 22
                                        height: 16
                                        radius: 3
                                        color: signalRow.kind === "DO" ? theme.accentSoft : theme.surfaceRaised
                                        Label {
                                            anchors.centerIn: parent
                                            text: signalRow.kind
                                            color: signalRow.kind === "DO" ? theme.accent : theme.muted
                                            font.pixelSize: 7
                                            font.weight: Font.Bold
                                        }
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: signalRow.name
                                        color: signalRow.kind === "DO" ? theme.text : theme.textSoft
                                        font.pixelSize: 10
                                        font.weight: signalRow.kind === "DO" ? Font.DemiBold : Font.Normal
                                        elide: Text.ElideRight
                                        ToolTip.visible: nameMouse.containsMouse && implicitWidth > width
                                        ToolTip.text: signalRow.reference ? String(signalRow.reference) : signalRow.name
                                        MouseArea { id: nameMouse; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                                    }
                                    Label {
                                        visible: signalRow.changed
                                        text: "●"
                                        color: theme.amber
                                        font.pixelSize: 8
                                    }
                                }

                                Label {
                                    Layout.preferredWidth: 54
                                    text: signalRow.fc ? "[" + signalRow.fc + "]" : ""
                                    color: theme.muted
                                    font.pixelSize: 9
                                }
                                Label {
                                    Layout.preferredWidth: 96
                                    text: signalRow.type || ""
                                    color: theme.muted
                                    font.pixelSize: 9
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.preferredWidth: 88
                                    text: signalRow.kind === "DA" && signalRow.quality ? String(signalRow.quality) : ""
                                    color: {
                                        var q = signalRow.quality ? String(signalRow.quality).toLowerCase() : ""
                                        return q === "good" ? theme.green : q === "invalid" ? theme.red : theme.muted
                                    }
                                    font.pixelSize: 9
                                    font.weight: signalRow.kind === "DA" ? Font.DemiBold : Font.Normal
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.preferredWidth: 150
                                    text: signalRow.value === undefined ? "" : String(signalRow.value)
                                    color: signalRow.writable && backend.running ? theme.accent : theme.textSoft
                                    font.pixelSize: 10
                                    font.weight: signalRow.kind === "DA" ? Font.DemiBold : Font.Normal
                                    elide: Text.ElideLeft
                                    horizontalAlignment: Text.AlignRight
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
                                id: signalMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: signalModel.activate(signalRow.index)
                                onDoubleClicked: {
                                    signalModel.activate(signalRow.index)
                                    if (backend.running && backend.selectedValue.writable === true) valueDialog.open()
                                }
                            }
                        }

                        Column {
                            anchors.centerIn: parent
                            visible: signalModel.visibleRowCount === 0
                            spacing: 5
                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: backend.imported ? "No matching data attributes" : "Open an IEC 61850 model"
                                color: theme.textSoft
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: backend.imported
                                      ? "Select another logical node or clear the search filter."
                                      : "CID, SCD, ICD, IID and SCL files are supported."
                                color: theme.muted
                                font.pixelSize: 9
                            }
                        }
                    }
                }
            }

            Rectangle {
                visible: root.showInspector
                Layout.preferredWidth: visible ? 296 : 0
                Layout.minimumWidth: visible ? 276 : 0
                Layout.fillHeight: true
                color: theme.chrome
                border.width: visible ? 1 : 0
                border.color: theme.lineSoft

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        Label {
                            Layout.fillWidth: true
                            text: "INSPECTOR"
                            color: theme.muted
                            font.pixelSize: 8
                            font.weight: Font.DemiBold
                        }
                        Rectangle {
                            visible: backend.selectedValue.reference !== undefined
                            implicitWidth: inspectorAccess.implicitWidth + 14
                            implicitHeight: 18
                            radius: 9
                            color: backend.selectedValue.writable === true ? theme.accentSoft : theme.surfaceRaised
                            Label {
                                id: inspectorAccess
                                anchors.centerIn: parent
                                text: backend.selectedValue.writable === true ? "WRITABLE" : "READ ONLY"
                                color: backend.selectedValue.writable === true ? theme.accent : theme.muted
                                font.pixelSize: 7
                                font.weight: Font.Bold
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: availableWidth

                        ColumnLayout {
                            width: parent.width
                            spacing: 10

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                Layout.topMargin: 12
                                spacing: 4
                                Label {
                                    Layout.fillWidth: true
                                    text: backend.selectedValue.dataAttribute || backend.selectedValue.dataObject || "No value selected"
                                    color: theme.text
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: backend.selectedValue.reference || "Select a data attribute in the table to inspect it."
                                    color: theme.muted
                                    font.pixelSize: 9
                                    wrapMode: Text.WrapAnywhere
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                Layout.preferredHeight: 76
                                radius: 7
                                color: theme.surface
                                border.width: 1
                                border.color: theme.lineSoft
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 2
                                    Label { text: "CURRENT VALUE"; color: theme.muted; font.pixelSize: 8; font.weight: Font.DemiBold }
                                    Label {
                                        Layout.fillWidth: true
                                        text: backend.selectedValue.value === undefined ? "—" : String(backend.selectedValue.value)
                                        color: backend.selectedValue.writable === true && backend.running ? theme.accent : theme.text
                                        font.pixelSize: 20
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                spacing: 5
                                MetaRow { title: "FC"; value: backend.selectedValue.fc ? String(backend.selectedValue.fc) : "" }
                                MetaRow { title: "Type"; value: backend.selectedValue.type ? String(backend.selectedValue.type) : "" }
                                MetaRow { title: "Quality"; value: backend.selectedValue.quality ? String(backend.selectedValue.quality) : "" }
                                MetaRow { title: "Unit"; value: backend.selectedValue.unit ? String(backend.selectedValue.unit) : "" }
                                MetaRow { title: "Origin"; value: backend.selectedValue.origin ? String(backend.selectedValue.origin) : "" }
                                MetaRow { title: "Updated"; value: backend.selectedValue.updated ? String(backend.selectedValue.updated) : "" }
                            }

                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                spacing: 6
                                Label { text: "ACTIONS"; color: theme.muted; font.pixelSize: 8; font.weight: Font.DemiBold }
                                CommandButton {
                                    Layout.fillWidth: true
                                    text: "Set value  Ctrl+E"
                                    primary: true
                                    enabled: backend.running && backend.selectedValue.writable === true
                                    onClicked: valueDialog.open()
                                }
                                CommandButton {
                                    Layout.fillWidth: true
                                    text: "Undo last change"
                                    enabled: backend.running && backend.selectedValue.changed === true
                                    onClicked: backend.undoLastChange()
                                }
                            }

                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                Layout.bottomMargin: 12
                                spacing: 5
                                Label { text: "MODEL SUMMARY"; color: theme.muted; font.pixelSize: 8; font.weight: Font.DemiBold }
                                MetaRow { title: "Endpoint"; value: backend.imported ? (backend.listenAddress + ":" + backend.port) : "" }
                                MetaRow { title: "Data sets"; value: backend.imported ? String(backend.dataSetCount) : "" }
                                MetaRow { title: "Reports"; value: backend.imported ? String(backend.reportCount) : "" }
                                MetaRow { title: "GOOSE"; value: backend.imported ? String(backend.gooseCount) : "" }
                                CommandButton {
                                    Layout.fillWidth: true
                                    Layout.topMargin: 4
                                    text: "Copy diagnostics"
                                    enabled: backend.imported
                                    onClicked: backend.copyDiagnostics()
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: theme.statusChrome
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10
                Rectangle {
                    width: 6
                    height: 6
                    radius: 3
                    color: backend.fatalError.length ? theme.red
                                                    : backend.running ? theme.green
                                                                      : backend.importing ? theme.amber : theme.navigationMuted
                }
                Label {
                    Layout.fillWidth: true
                    text: backend.fatalError.length ? backend.fatalError
                                                    : backend.importing ? "Importing and indexing engineering model…"
                                                                        : backend.imported ? backend.modelStatus
                                                                                           : "Ready. Open an IEC 61850 engineering model."
                    color: backend.fatalError.length ? "#ffb2b8" : theme.statusText
                    font.pixelSize: 9
                    elide: Text.ElideRight
                }
                Label {
                    text: backend.imported
                          ? backend.logicalDeviceCount + " LD  ·  " + backend.dataObjectCount + " DO  ·  " + backend.dataAttributeCount + " DA/BDA"
                          : ""
                    color: theme.navigationMuted
                    font.pixelSize: 9
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: backend.importing
        z: 100
        color: "#660c1714"

        Rectangle {
            anchors.centerIn: parent
            width: 350
            height: 116
            radius: 10
            color: theme.surface
            border.width: 1
            border.color: theme.line

            RowLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 14
                BusyIndicator { running: backend.importing; implicitWidth: 34; implicitHeight: 34 }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Label { text: "Loading engineering model"; color: theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
                    Label {
                        Layout.fillWidth: true
                        text: "Parsing is running on a bounded worker. The workspace remains responsive."
                        color: theme.muted
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    Dialog {
        id: startDialog
        modal: true
        anchors.centerIn: parent
        width: 460
        title: "Start simulated IED"
        closePolicy: Popup.CloseOnEscape

        onOpened: {
            var addressIndex = backend.availableAddresses.indexOf(backend.listenAddress)
            addressBox.currentIndex = Math.max(0, addressIndex)
            portField.text = String(backend.port)
        }

        contentItem: ColumnLayout {
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Label {
                        text: backend.selectedIed.name || "Selected IED"
                        color: theme.text
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: "MMS server endpoint"
                        color: theme.muted
                        font.pixelSize: 9
                    }
                }
                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: theme.accent
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

            Label { text: "Listening address"; color: theme.muted; font.pixelSize: 9 }
            RowLayout {
                Layout.fillWidth: true
                ComboBox {
                    id: addressBox
                    Layout.fillWidth: true
                    model: backend.availableAddresses
                    font.pixelSize: 10
                }
                CommandButton {
                    text: "Refresh"
                    onClicked: {
                        backend.refreshNetworkInterfaces()
                        var addressIndex = backend.availableAddresses.indexOf(backend.listenAddress)
                        addressBox.currentIndex = Math.max(0, addressIndex)
                    }
                }
            }
            Label { text: "TCP port"; color: theme.muted; font.pixelSize: 9 }
            TextField {
                id: portField
                Layout.fillWidth: true
                inputMethodHints: Qt.ImhDigitsOnly
                selectByMouse: true
                font.pixelSize: 10
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 52
                radius: 6
                color: theme.surfaceRaised
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 1
                    Label { text: "ENDPOINT PREVIEW"; color: theme.muted; font.pixelSize: 7; font.weight: Font.Bold }
                    Label {
                        text: (addressBox.currentText || "0.0.0.0") + ":" + (portField.text || "102")
                        color: theme.text
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                text: "Each simulated IED uses its own local IPv4 endpoint. Multiple IEDs may share TCP 102 when bound to different local addresses."
                color: theme.muted
                wrapMode: Text.WordWrap
                font.pixelSize: 9
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                CommandButton { text: "Cancel"; onClicked: startDialog.close() }
                CommandButton {
                    text: "Start IED"
                    primary: true
                    enabled: addressBox.currentText.length > 0 && Number(portField.text) >= 1 && Number(portField.text) <= 65535
                    onClicked: {
                        var requestedPort = Number(portField.text)
                        if (backend.configureIedEndpoint(backend.selectedIedIndex, addressBox.currentText, requestedPort)) {
                            backend.startSimulation()
                            startDialog.close()
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: valueDialog
        modal: true
        anchors.centerIn: parent
        width: 520
        title: "Set IED value"
        closePolicy: Popup.CloseOnEscape
        property var availableOptions: backend.selectedValue.options || []

        onOpened: {
            root.pendingValue = backend.selectedValue.value === undefined ? "" : String(backend.selectedValue.value)
            root.pendingQuality = backend.selectedValue.quality || "Good"
            root.pendingOrigin = backend.selectedValue.origin || "Simulator"
        }

        contentItem: ColumnLayout {
            spacing: 9

            Label {
                Layout.fillWidth: true
                text: backend.selectedValue.reference || "Select a writable data attribute"
                color: theme.textSoft
                font.pixelSize: 10
                wrapMode: Text.WrapAnywhere
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: backend.selectedValue.fc ? "FC " + backend.selectedValue.fc : ""
                    color: theme.muted
                    font.pixelSize: 8
                }
                Label {
                    text: backend.selectedValue.type || ""
                    color: theme.muted
                    font.pixelSize: 8
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: "Current: " + (backend.selectedValue.value === undefined ? "—" : String(backend.selectedValue.value))
                    color: theme.muted
                    font.pixelSize: 8
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

            Label { text: "New value"; color: theme.muted; font.pixelSize: 9 }
            Loader {
                Layout.fillWidth: true
                sourceComponent: valueDialog.availableOptions.length > 0 ? optionValueEditor : textValueEditor
            }
            Component {
                id: textValueEditor
                TextField {
                    text: root.pendingValue
                    selectByMouse: true
                    font.pixelSize: 10
                    onTextEdited: root.pendingValue = text
                }
            }
            Component {
                id: optionValueEditor
                ComboBox {
                    model: valueDialog.availableOptions
                    currentIndex: Math.max(0, valueDialog.availableOptions.indexOf(root.pendingValue))
                    font.pixelSize: 10
                    onActivated: root.pendingValue = currentText
                }
            }

            Label { text: "Quality"; color: theme.muted; font.pixelSize: 9 }
            ComboBox {
                Layout.fillWidth: true
                model: ["Good", "Questionable", "Invalid"]
                currentIndex: Math.max(0, model.indexOf(root.pendingQuality))
                font.pixelSize: 10
                onActivated: root.pendingQuality = currentText
            }

            Label { text: "Origin"; color: theme.muted; font.pixelSize: 9 }
            TextField {
                Layout.fillWidth: true
                text: root.pendingOrigin
                selectByMouse: true
                font.pixelSize: 10
                onTextEdited: root.pendingOrigin = text
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                CommandButton { text: "Cancel"; onClicked: valueDialog.close() }
                CommandButton {
                    text: "Apply value"
                    primary: true
                    enabled: backend.running && backend.selectedValue.writable === true
                    onClicked: {
                        if (backend.applySelectedValue(root.pendingValue, root.pendingQuality, root.pendingOrigin)) {
                            valueDialog.close()
                        }
                    }
                }
            }
        }
    }
}
