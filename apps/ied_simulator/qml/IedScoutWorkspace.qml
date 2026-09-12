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
                                    : root.theme.surface
            border.width: 1
            border.color: control.primary ? root.theme.accent
                                          : control.danger ? root.theme.red
                                          : root.theme.line
        }
    }

    Shortcut { sequence: "Ctrl+O"; onActivated: root.openSclRequested() }
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
            Layout.preferredHeight: 52
            color: theme.navigation

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 8

                ColumnLayout {
                    Layout.preferredWidth: 210
                    spacing: 0
                    Label {
                        text: "ARStack IED Simulator"
                        color: theme.navigationText
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: "IEC 61850 engineering simulator"
                        color: theme.navigationMuted
                        font.pixelSize: 9
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    Layout.topMargin: 10
                    Layout.bottomMargin: 10
                    color: "#32423d"
                }

                CommandButton {
                    text: "Open SCL"
                    onClicked: root.openSclRequested()
                }
                CommandButton {
                    text: "Start"
                    primary: true
                    enabled: backend.imported && !backend.running && !backend.starting && backend.selectedIed.enabled
                    onClicked: startDialog.open()
                }
                CommandButton {
                    text: "Stop"
                    danger: true
                    enabled: backend.running || backend.starting
                    onClicked: backend.stopSimulation()
                }
                CommandButton {
                    text: "Set values"
                    enabled: backend.running && backend.selectedValue.writable === true
                    onClicked: valueDialog.open()
                }

                Item { Layout.fillWidth: true }

                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: backend.running ? theme.green : backend.starting ? theme.amber : theme.muted
                }
                Label {
                    text: backend.running ? "LIVE" : backend.starting ? "STARTING" : "READY"
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
                Layout.preferredWidth: 292
                Layout.minimumWidth: 250
                Layout.fillHeight: true
                color: theme.chrome
                border.width: 1
                border.color: theme.lineSoft

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Label {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 31
                        Layout.leftMargin: 12
                        text: "IEDs"
                        verticalAlignment: Text.AlignVCenter
                        color: theme.text
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }

                    ComboBox {
                        id: iedSelector
                        Layout.fillWidth: true
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        Layout.preferredHeight: 32
                        model: backend.ieds
                        textRole: "name"
                        currentIndex: backend.selectedIedIndex
                        enabled: backend.ieds.length > 0 && !backend.anyRunning
                        font.pixelSize: 10
                        onActivated: backend.selectIed(currentIndex)
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        Layout.topMargin: 8
                        Layout.bottomMargin: 8
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 2
                        Label { text: "IP"; color: theme.muted; font.pixelSize: 9 }
                        Label {
                            Layout.fillWidth: true
                            text: backend.listenAddress || "Not assigned"
                            color: theme.textSoft
                            font.pixelSize: 9
                            elide: Text.ElideMiddle
                        }
                        Label { text: "Port"; color: theme.muted; font.pixelSize: 9 }
                        Label { text: backend.port; color: theme.textSoft; font.pixelSize: 9 }
                        Label { text: "Model"; color: theme.muted; font.pixelSize: 9 }
                        Label {
                            Layout.fillWidth: true
                            text: backend.sourceName || "—"
                            color: theme.textSoft
                            font.pixelSize: 9
                            elide: Text.ElideMiddle
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 31
                        Layout.leftMargin: 12
                        Layout.rightMargin: 8
                        Label {
                            Layout.fillWidth: true
                            text: "Navigation"
                            color: theme.text
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                        }
                        ToolButton {
                            text: "−"
                            font.pixelSize: 12
                            onClicked: navigationModel.collapseAll()
                        }
                        ToolButton {
                            text: "+"
                            font.pixelSize: 12
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
                        ScrollBar.vertical: ScrollBar { }

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
                            height: kind === "SECTION" ? 30 : 27
                            color: selected ? theme.accentSoft
                                            : mouse.containsMouse ? theme.surfaceRaised
                                                                 : "transparent"

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
                                    width: 21
                                    height: 16
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
                                id: mouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: navigationModel.activate(navRow.index)
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }
                    Label {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        Layout.leftMargin: 12
                        text: backend.reportCount + " reports  ·  " + backend.dataSetCount + " data sets"
                        verticalAlignment: Text.AlignVCenter
                        color: theme.muted
                        font.pixelSize: 9
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
                        Layout.preferredHeight: 48
                        color: theme.surface

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 10

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
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
                                    color: theme.muted
                                    font.pixelSize: 9
                                }
                            }

                            TextField {
                                id: searchField
                                Layout.preferredWidth: 250
                                Layout.preferredHeight: 31
                                placeholderText: "Search object, value, FC, reference"
                                selectByMouse: true
                                font.pixelSize: 10
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 29
                        color: theme.surfaceRaised
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 0
                            Label { Layout.fillWidth: true; text: "Object / attribute"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                            Label { Layout.preferredWidth: 58; text: "FC"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                            Label { Layout.preferredWidth: 112; text: "Type"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                            Label { Layout.preferredWidth: 180; text: "Value"; color: theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
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
                        ScrollBar.vertical: ScrollBar { }

                        delegate: Rectangle {
                            id: signalRow
                            required property int index
                            required property string kind
                            required property string name
                            required property int depth
                            required property var value
                            required property var fc
                            required property var type
                            required property bool writable
                            required property bool changed
                            required property bool selected

                            width: signalList.width
                            height: signalRow.kind === "DO" ? 31 : 28
                            color: signalRow.selected ? theme.accentSoft
                                                     : signalRow.kind === "DO" ? theme.chrome
                                                     : mouseArea.containsMouse ? "#f6f9f8"
                                                                                 : theme.surface

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
                                    }
                                    Label {
                                        visible: signalRow.changed
                                        text: "●"
                                        color: theme.amber
                                        font.pixelSize: 8
                                    }
                                }

                                Label {
                                    Layout.preferredWidth: 58
                                    text: signalRow.fc ? "[" + signalRow.fc + "]" : ""
                                    color: theme.muted
                                    font.pixelSize: 9
                                }
                                Label {
                                    Layout.preferredWidth: 112
                                    text: signalRow.type || ""
                                    color: theme.muted
                                    font.pixelSize: 9
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.preferredWidth: 180
                                    text: signalRow.value === undefined ? "" : String(signalRow.value)
                                    color: signalRow.writable && backend.running ? theme.accent : theme.textSoft
                                    font.pixelSize: 10
                                    font.weight: signalRow.kind === "DA" ? Font.DemiBold : Font.Normal
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
                                id: mouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: signalModel.activate(signalRow.index)
                                onDoubleClicked: {
                                    signalModel.activate(signalRow.index)
                                    if (backend.running && backend.selectedValue.writable === true) valueDialog.open()
                                }
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: signalModel.visibleRowCount === 0
                            text: backend.imported ? "Select a logical node or adjust the search" : "Open an SCL/CID/SCD file"
                            color: theme.muted
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 29
            color: theme.statusChrome
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10
                Label {
                    Layout.fillWidth: true
                    text: backend.fatalError.length ? backend.fatalError
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

    Dialog {
        id: startDialog
        modal: true
        anchors.centerIn: parent
        width: 430
        title: "Start simulated IED"
        closePolicy: Popup.CloseOnEscape

        onOpened: {
            var addressIndex = backend.availableAddresses.indexOf(backend.listenAddress)
            addressBox.currentIndex = Math.max(0, addressIndex)
            portField.text = String(backend.port)
        }

        contentItem: ColumnLayout {
            spacing: 10

            Label {
                Layout.fillWidth: true
                text: backend.selectedIed.name || "Selected IED"
                color: theme.text
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

            Label { text: "Listening on"; color: theme.muted; font.pixelSize: 9 }
            ComboBox {
                id: addressBox
                Layout.fillWidth: true
                model: backend.availableAddresses
                font.pixelSize: 10
            }
            Label { text: "Port"; color: theme.muted; font.pixelSize: 9 }
            TextField {
                id: portField
                Layout.fillWidth: true
                inputMethodHints: Qt.ImhDigitsOnly
                selectByMouse: true
                font.pixelSize: 10
            }
            Label {
                Layout.fillWidth: true
                text: "The MMS endpoint starts only after these settings are confirmed."
                color: theme.muted
                wrapMode: Text.WordWrap
                font.pixelSize: 9
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                CommandButton { text: "Cancel"; onClicked: startDialog.close() }
                CommandButton {
                    text: "Start"
                    primary: true
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
        width: 500
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
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

            Label { text: "Value"; color: theme.muted; font.pixelSize: 9 }
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
                    text: "Set values"
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
