// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ARStack.IedSimulator 1.0

Item {
    id: root

    property var theme
    property var backend
    signal openSclRequested()

    property string activeSection: "DataModel"
    property var activeService: ({})
    property int activeMemberCount: 0
    readonly property bool showInspector: width >= 1260 && activeSection === "DataModel"

    function catalogFor(section) {
        if (section === "GOOSE") return gooseCatalog
        if (section === "Reports") return reportCatalog
        if (section === "SettingGroups") return settingGroupCatalog
        if (section === "DataSets") return dataSetCatalog
        return null
    }

    function refreshService() {
        var model = catalogFor(activeSection)
        activeService = model ? model.selectedItem : ({})
        activeMemberCount = model ? model.memberCount : 0
    }

    function activateSection(section) {
        activeSection = section
        var model = catalogFor(section)
        if (model && model.itemCount > 0 && model.selectedCatalogIndex < 0) model.select(0)
        refreshService()
    }

    function serviceTitle() {
        if (activeSection === "SettingGroups") return "Setting Groups"
        if (activeSection === "DataSets") return "DataSets"
        return activeSection
    }

    function serviceEmptyText() {
        if (activeSection === "GOOSE") return "No configured GSEControl blocks in this IED."
        if (activeSection === "Reports") return "No ReportControl blocks in this IED."
        if (activeSection === "SettingGroups") return "No FC=SE values or SGCB object found in this IED."
        if (activeSection === "DataSets") return "No configured DataSets in this IED."
        return "No configured service objects."
    }

    IedScoutWorkspace {
        id: stableShell
        anchors.fill: parent
        theme: root.theme
        backend: root.backend
        onOpenSclRequested: root.openSclRequested()
    }

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

    IedCommissioningModel {
        id: gooseCatalog
        backend: root.backend
        kindFilter: "GOOSE"
        onSelectionChanged: if (root.activeSection === "GOOSE") root.refreshService()
        onModelChanged: if (root.activeSection === "GOOSE") root.refreshService()
    }
    IedCommissioningModel {
        id: reportCatalog
        backend: root.backend
        kindFilter: "Report"
        onSelectionChanged: if (root.activeSection === "Reports") root.refreshService()
        onModelChanged: if (root.activeSection === "Reports") root.refreshService()
    }
    IedCommissioningModel {
        id: settingGroupCatalog
        backend: root.backend
        kindFilter: "SettingGroup"
        onSelectionChanged: if (root.activeSection === "SettingGroups") root.refreshService()
        onModelChanged: if (root.activeSection === "SettingGroups") root.refreshService()
    }
    IedCommissioningModel {
        id: dataSetCatalog
        backend: root.backend
        kindFilter: "DataSet"
        onSelectionChanged: if (root.activeSection === "DataSets") root.refreshService()
        onModelChanged: if (root.activeSection === "DataSets") root.refreshService()
    }

    Connections {
        target: root.backend
        function onSelectionChanged() { root.refreshService() }
        function onModelChanged() { root.refreshService() }
    }

    component MetaRow: RowLayout {
        property string title: ""
        property string value: ""
        Layout.fillWidth: true
        spacing: 8
        Label {
            Layout.preferredWidth: 86
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

    component SectionButton: Rectangle {
        required property string section
        required property string title
        required property int count
        property string badge: ""
        Layout.fillWidth: true
        Layout.preferredHeight: 34
        color: root.activeSection === section ? root.theme.accentSoft
                                              : sectionMouse.containsMouse ? root.theme.surfaceRaised : "transparent"

        Rectangle {
            visible: root.activeSection === parent.section
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 3
            color: root.theme.accent
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 11
            anchors.rightMargin: 9
            spacing: 7
            Label {
                Layout.preferredWidth: 12
                text: root.activeSection === parent.parent.section ? "⌄" : "›"
                color: root.theme.muted
                font.pixelSize: 12
            }
            Label {
                Layout.fillWidth: true
                text: parent.parent.title
                color: root.activeSection === parent.parent.section ? root.theme.accent : root.theme.textSoft
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }
            Label {
                visible: parent.parent.badge.length > 0
                text: parent.parent.badge
                color: root.theme.muted
                font.pixelSize: 8
            }
            Rectangle {
                implicitWidth: countLabel.implicitWidth + 12
                implicitHeight: 18
                radius: 9
                color: root.activeSection === parent.parent.section ? root.theme.surface : root.theme.surfaceRaised
                Label {
                    id: countLabel
                    anchors.centerIn: parent
                    text: String(parent.parent.parent.count)
                    color: root.theme.muted
                    font.pixelSize: 8
                    font.weight: Font.DemiBold
                }
            }
        }

        MouseArea {
            id: sectionMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.activateSection(parent.section)
        }
    }

    component CatalogList: Item {
        required property var catalog
        required property string emptyText
        required property string section

        ListView {
            id: catalogList
            anchors.fill: parent
            clip: true
            model: parent.catalog ? parent.catalog.itemCount : 0
            reuseItems: true
            cacheBuffer: 0
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                required property int index
                property var rowData: catalogList.parent.catalog.item(index)
                width: catalogList.width
                height: 31
                color: rowData.selected ? root.theme.accentSoft
                                        : catalogMouse.containsMouse ? root.theme.surfaceRaised : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 22
                    anchors.rightMargin: 9
                    spacing: 6
                    Rectangle {
                        width: 23
                        height: 17
                        radius: 3
                        color: root.theme.surfaceSoft
                        Label {
                            anchors.centerIn: parent
                            text: section === "GOOSE" ? "G" : section === "Reports" ? "R" :
                                  section === "SettingGroups" ? "SG" : "DS"
                            color: root.theme.accent
                            font.pixelSize: 7
                            font.weight: Font.Bold
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            text: rowData.name || rowData.reference || "Configured object"
                            color: rowData.selected ? root.theme.accent : root.theme.textSoft
                            font.pixelSize: 9
                            font.weight: rowData.selected ? Font.DemiBold : Font.Normal
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: rowData.summary || rowData.status || ""
                            color: root.theme.muted
                            font.pixelSize: 7
                            elide: Text.ElideRight
                        }
                    }
                }

                MouseArea {
                    id: catalogMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        catalogList.parent.catalog.select(index)
                        root.activeSection = catalogList.parent.section
                        root.refreshService()
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: Math.max(120, parent.width - 28)
                visible: parent.model === 0
                text: catalogList.parent.emptyText
                color: root.theme.muted
                font.pixelSize: 9
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    Rectangle {
        id: workspaceBody
        z: 10
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 56
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 30
        color: root.theme.background

        RowLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                Layout.preferredWidth: 296
                Layout.minimumWidth: 272
                Layout.maximumWidth: 326
                Layout.fillHeight: true
                color: root.theme.chrome
                border.width: 1
                border.color: root.theme.lineSoft

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
                            color: root.theme.muted
                            font.pixelSize: 8
                            font.weight: Font.DemiBold
                        }
                        Rectangle {
                            visible: root.backend && root.backend.imported
                            implicitWidth: runtimeStateLabel.implicitWidth + 14
                            implicitHeight: 18
                            radius: 9
                            color: root.backend.running ? root.theme.greenSoft : root.theme.surfaceRaised
                            Label {
                                id: runtimeStateLabel
                                anchors.centerIn: parent
                                text: root.backend && root.backend.running ? "RUNNING" : "OFFLINE"
                                color: root.backend && root.backend.running ? root.theme.green : root.theme.muted
                                font.pixelSize: 7
                                font.weight: Font.Bold
                            }
                        }
                    }

                    ComboBox {
                        Layout.fillWidth: true
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        Layout.preferredHeight: 33
                        model: root.backend ? root.backend.ieds : []
                        textRole: "name"
                        currentIndex: root.backend ? root.backend.selectedIedIndex : -1
                        enabled: root.backend && root.backend.ieds.length > 0 && !root.backend.anyRunning && !root.backend.importing
                        font.pixelSize: 10
                        onActivated: root.backend.selectIed(currentIndex)
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        Layout.topMargin: 8
                        Layout.bottomMargin: 8
                        Layout.preferredHeight: 67
                        radius: 6
                        color: root.theme.surface
                        border.width: 1
                        border.color: root.theme.lineSoft
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 2
                            MetaRow {
                                title: "Endpoint"
                                value: root.backend && root.backend.imported
                                       ? (root.backend.listenAddress + ":" + root.backend.port) : ""
                            }
                            MetaRow { title: "Model"; value: root.backend ? root.backend.sourceName : "" }
                            MetaRow {
                                title: "Profile"
                                value: root.backend
                                       ? (root.backend.selectedIed.type || root.backend.selectedIed.manufacturer || "") : ""
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.theme.lineSoft }
                    Label {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        Layout.leftMargin: 12
                        text: "IED CONTENTS"
                        verticalAlignment: Text.AlignVCenter
                        color: root.theme.muted
                        font.pixelSize: 8
                        font.weight: Font.Bold
                    }

                    SectionButton {
                        section: "GOOSE"
                        title: "GOOSE"
                        count: gooseCatalog.itemCount
                    }
                    SectionButton {
                        section: "Reports"
                        title: "Reports"
                        count: reportCatalog.itemCount
                    }
                    SectionButton {
                        section: "SettingGroups"
                        title: "Setting Groups"
                        count: settingGroupCatalog.itemCount
                    }
                    SectionButton {
                        section: "DataSets"
                        title: "DataSets"
                        count: dataSetCatalog.itemCount
                    }
                    SectionButton {
                        section: "DataModel"
                        title: "Data Model"
                        count: root.backend ? root.backend.logicalDeviceCount : 0
                        badge: "LD"
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.theme.lineSoft }

                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: root.activeSection === "GOOSE" ? 0
                                      : root.activeSection === "Reports" ? 1
                                      : root.activeSection === "SettingGroups" ? 2
                                      : root.activeSection === "DataSets" ? 3 : 4

                        CatalogList {
                            catalog: gooseCatalog
                            section: "GOOSE"
                            emptyText: "No configured GOOSE control blocks."
                        }
                        CatalogList {
                            catalog: reportCatalog
                            section: "Reports"
                            emptyText: "No configured report control blocks."
                        }
                        CatalogList {
                            catalog: settingGroupCatalog
                            section: "SettingGroups"
                            emptyText: "No setting-group capable Logical Device found."
                        }
                        CatalogList {
                            catalog: dataSetCatalog
                            section: "DataSets"
                            emptyText: "No configured DataSets."
                        }

                        ListView {
                            id: navigationList
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
                                color: selected ? root.theme.accentSoft
                                                : navMouse.containsMouse ? root.theme.surfaceRaised : "transparent"

                                Rectangle {
                                    visible: navRow.selected
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: 3
                                    color: root.theme.accent
                                }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 11 + navRow.depth * 15
                                    anchors.rightMargin: 8
                                    spacing: 5
                                    Label {
                                        Layout.preferredWidth: 12
                                        text: navRow.expandable ? (navRow.expanded ? "⌄" : "›") : ""
                                        color: root.theme.muted
                                        font.pixelSize: 12
                                    }
                                    Rectangle {
                                        visible: navRow.kind !== "SECTION"
                                        width: 23
                                        height: 17
                                        radius: 3
                                        color: navRow.kind === "LD" ? root.theme.surfaceSoft : root.theme.accentSoft
                                        Label {
                                            anchors.centerIn: parent
                                            text: navRow.kind
                                            color: navRow.kind === "LN" ? root.theme.accent : root.theme.textSoft
                                            font.pixelSize: 7
                                            font.weight: Font.Bold
                                        }
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: navRow.name
                                        color: navRow.selected ? root.theme.accent : root.theme.textSoft
                                        font.pixelSize: navRow.kind === "SECTION" ? 10 : 9
                                        font.weight: navRow.kind === "SECTION" || navRow.selected ? Font.DemiBold : Font.Normal
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
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.theme.lineSoft }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 29
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        Label {
                            Layout.fillWidth: true
                            text: (root.backend ? root.backend.reportCount : 0) + " reports"
                            color: root.theme.muted
                            font.pixelSize: 8
                        }
                        Label {
                            text: (root.backend ? root.backend.dataSetCount : 0) + " data sets"
                            color: root.theme.muted
                            font.pixelSize: 8
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: root.theme.surface
                border.width: 1
                border.color: root.theme.lineSoft

                StackLayout {
                    anchors.fill: parent
                    currentIndex: root.activeSection === "DataModel" ? 0 : 1

                    Item {
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 56
                                color: root.theme.surface
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
                                            text: (root.backend && root.backend.selectedIed.name || "IED") + "  ›  Data Model  ›  " +
                                                  (navigationModel.selectedLogicalDevice || "—") + "  ›  " +
                                                  (navigationModel.selectedLogicalNode || "—")
                                            color: root.theme.text
                                            font.pixelSize: 11
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideMiddle
                                        }
                                        Label {
                                            text: signalModel.visibleRowCount + " visible rows" +
                                                  (searchField.text.length ? " · filtered" : "")
                                            color: root.theme.muted
                                            font.pixelSize: 9
                                        }
                                    }
                                    TextField {
                                        id: searchField
                                        Layout.preferredWidth: Math.min(360, Math.max(250, root.width * 0.24))
                                        Layout.preferredHeight: 32
                                        placeholderText: "Search object, value, FC or reference"
                                        selectByMouse: true
                                        font.pixelSize: 10
                                        enabled: root.backend && root.backend.imported
                                    }
                                }
                            }

                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.theme.lineSoft }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 30
                                color: root.theme.surfaceRaised
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 0
                                    Label { Layout.fillWidth: true; text: "Object / attribute"; color: root.theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                                    Label { Layout.preferredWidth: 54; text: "FC"; color: root.theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                                    Label { Layout.preferredWidth: 96; text: "Type"; color: root.theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                                    Label { Layout.preferredWidth: 88; text: "Quality"; color: root.theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                                    Label { Layout.preferredWidth: 150; text: "Value"; color: root.theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignRight }
                                }
                            }

                            ListView {
                                id: signalList
                                objectName: "iedLiveSignalTableV2"
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
                                    color: signalRow.selected ? root.theme.accentSoft
                                                             : signalRow.kind === "DO" ? root.theme.chrome
                                                             : signalMouse.containsMouse ? root.theme.surfaceRaised : root.theme.surface

                                    Rectangle {
                                        visible: signalRow.selected
                                        anchors.left: parent.left
                                        anchors.top: parent.top
                                        anchors.bottom: parent.bottom
                                        width: 3
                                        color: root.theme.accent
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
                                                color: signalRow.kind === "DO" ? root.theme.accentSoft : root.theme.surfaceRaised
                                                Label {
                                                    anchors.centerIn: parent
                                                    text: signalRow.kind
                                                    color: signalRow.kind === "DO" ? root.theme.accent : root.theme.muted
                                                    font.pixelSize: 7
                                                    font.weight: Font.Bold
                                                }
                                            }
                                            Label {
                                                Layout.fillWidth: true
                                                text: signalRow.name
                                                color: signalRow.kind === "DO" ? root.theme.text : root.theme.textSoft
                                                font.pixelSize: 10
                                                font.weight: signalRow.kind === "DO" ? Font.DemiBold : Font.Normal
                                                elide: Text.ElideRight
                                            }
                                            Label { visible: signalRow.changed; text: "●"; color: root.theme.amber; font.pixelSize: 8 }
                                        }
                                        Label { Layout.preferredWidth: 54; text: signalRow.fc ? "[" + signalRow.fc + "]" : ""; color: root.theme.muted; font.pixelSize: 9 }
                                        Label { Layout.preferredWidth: 96; text: signalRow.type || ""; color: root.theme.muted; font.pixelSize: 9; elide: Text.ElideRight }
                                        Label {
                                            Layout.preferredWidth: 88
                                            text: signalRow.kind === "DA" && signalRow.quality ? String(signalRow.quality) : ""
                                            color: String(signalRow.quality).toLowerCase() === "good" ? root.theme.green : root.theme.muted
                                            font.pixelSize: 9
                                            elide: Text.ElideRight
                                        }
                                        Label {
                                            Layout.preferredWidth: 150
                                            text: signalRow.value === undefined ? "" : String(signalRow.value)
                                            color: signalRow.writable && root.backend && root.backend.running ? root.theme.accent : root.theme.textSoft
                                            font.pixelSize: 10
                                            font.weight: signalRow.kind === "DA" ? Font.DemiBold : Font.Normal
                                            elide: Text.ElideLeft
                                            horizontalAlignment: Text.AlignRight
                                        }
                                    }
                                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: root.theme.lineSoft }
                                    MouseArea {
                                        id: signalMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: signalModel.activate(signalRow.index)
                                        onDoubleClicked: signalModel.activate(signalRow.index)
                                    }
                                }

                                Label {
                                    anchors.centerIn: parent
                                    visible: signalModel.visibleRowCount === 0
                                    text: root.backend && root.backend.imported ? "No matching data attributes" : "Open an IEC 61850 model"
                                    color: root.theme.muted
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }

                    Item {
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 62
                                color: root.theme.surface
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 14
                                    anchors.rightMargin: 14
                                    spacing: 10
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1
                                        Label {
                                            Layout.fillWidth: true
                                            text: (root.backend && root.backend.selectedIed.name || "IED") + "  ›  " + root.serviceTitle() +
                                                  (root.activeService.name ? "  ›  " + root.activeService.name : "")
                                            color: root.theme.text
                                            font.pixelSize: 12
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideMiddle
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: root.activeService.summary || root.serviceEmptyText()
                                            color: root.theme.muted
                                            font.pixelSize: 9
                                            elide: Text.ElideRight
                                        }
                                    }
                                    Rectangle {
                                        visible: root.activeService.status !== undefined
                                        implicitWidth: serviceStatusLabel.implicitWidth + 16
                                        implicitHeight: 22
                                        radius: 11
                                        color: root.theme.accentSoft
                                        Label {
                                            id: serviceStatusLabel
                                            anchors.centerIn: parent
                                            text: root.activeService.status || "Configured"
                                            color: root.theme.accent
                                            font.pixelSize: 8
                                            font.weight: Font.DemiBold
                                        }
                                    }
                                }
                            }

                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.theme.lineSoft }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.activeService.name ? 116 : 0
                                visible: root.activeService.name !== undefined
                                color: root.theme.chrome
                                GridLayout {
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    columns: 4
                                    columnSpacing: 14
                                    rowSpacing: 6
                                    Label { text: "Reference"; color: root.theme.muted; font.pixelSize: 8 }
                                    Label { Layout.columnSpan: 3; Layout.fillWidth: true; text: root.activeService.reference || "—"; color: root.theme.textSoft; font.pixelSize: 9; elide: Text.ElideMiddle }
                                    Label { text: "DataSet"; color: root.theme.muted; font.pixelSize: 8 }
                                    Label { Layout.columnSpan: 3; Layout.fillWidth: true; text: root.activeService.dataSetReference || "—"; color: root.theme.textSoft; font.pixelSize: 9; elide: Text.ElideMiddle }
                                    Label { text: "Members"; color: root.theme.muted; font.pixelSize: 8 }
                                    Label { text: String(root.activeMemberCount); color: root.theme.text; font.pixelSize: 9; font.weight: Font.DemiBold }
                                    Label { text: root.activeSection === "GOOSE" ? "APPID" : root.activeSection === "Reports" ? "ConfRev" : "Logical Device"; color: root.theme.muted; font.pixelSize: 8 }
                                    Label {
                                        Layout.fillWidth: true
                                        text: root.activeSection === "GOOSE" ? (root.activeService.appId || "—")
                                              : root.activeSection === "Reports" ? String(root.activeService.confRev === undefined ? "—" : root.activeService.confRev)
                                              : (root.activeService.logicalDevice || "—")
                                        color: root.theme.textSoft
                                        font.pixelSize: 9
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 30
                                color: root.theme.surfaceRaised
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 0
                                    Label { Layout.fillWidth: true; text: "Member / setting reference"; color: root.theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                                    Label { Layout.preferredWidth: 56; text: "FC"; color: root.theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                                    Label { Layout.preferredWidth: 92; text: "CDC"; color: root.theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                                    Label { Layout.preferredWidth: 128; text: "Configured"; color: root.theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignRight }
                                }
                            }

                            ListView {
                                id: memberList
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: root.activeMemberCount
                                reuseItems: true
                                cacheBuffer: 0
                                boundsBehavior: Flickable.StopAtBounds
                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                                delegate: Rectangle {
                                    required property int index
                                    property var memberData: {
                                        var model = root.catalogFor(root.activeSection)
                                        return model ? model.member(index) : ({})
                                    }
                                    width: memberList.width
                                    height: 31
                                    color: memberMouse.containsMouse ? root.theme.surfaceRaised : root.theme.surface
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 12
                                        anchors.rightMargin: 12
                                        spacing: 0
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 0
                                            Label {
                                                Layout.fillWidth: true
                                                text: memberData.reference || memberData.mmsItem || memberData.dataObject || "Member"
                                                color: root.theme.textSoft
                                                font.pixelSize: 9
                                                elide: Text.ElideMiddle
                                            }
                                            Label {
                                                Layout.fillWidth: true
                                                text: (memberData.logicalDevice || "") + (memberData.logicalNode ? " / " + memberData.logicalNode : "")
                                                color: root.theme.muted
                                                font.pixelSize: 7
                                                elide: Text.ElideRight
                                            }
                                        }
                                        Label { Layout.preferredWidth: 56; text: memberData.fc || ""; color: root.theme.muted; font.pixelSize: 9 }
                                        Label { Layout.preferredWidth: 92; text: memberData.cdc || memberData.type || ""; color: root.theme.muted; font.pixelSize: 9; elide: Text.ElideRight }
                                        Label { Layout.preferredWidth: 128; text: memberData.configuredValue || ""; color: root.theme.textSoft; font.pixelSize: 9; elide: Text.ElideLeft; horizontalAlignment: Text.AlignRight }
                                    }
                                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: root.theme.lineSoft }
                                    MouseArea { id: memberMouse; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                                }

                                Column {
                                    anchors.centerIn: parent
                                    visible: root.activeMemberCount === 0
                                    spacing: 5
                                    Label {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: root.activeService.name ? "No members exposed for this object" : root.serviceEmptyText()
                                        color: root.theme.textSoft
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                    }
                                    Label {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: root.activeSection === "SettingGroups"
                                              ? "Setting-group entries are derived only from real FC=SE / SGCB model evidence."
                                              : "The loaded SCL does not expose additional member detail."
                                        color: root.theme.muted
                                        font.pixelSize: 9
                                    }
                                }
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
                color: root.theme.chrome
                border.width: visible ? 1 : 0
                border.color: root.theme.lineSoft

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        Label { Layout.fillWidth: true; text: "INSPECTOR"; color: root.theme.muted; font.pixelSize: 8; font.weight: Font.DemiBold }
                        Rectangle {
                            visible: root.backend && root.backend.selectedValue.reference !== undefined
                            implicitWidth: accessLabel.implicitWidth + 14
                            implicitHeight: 18
                            radius: 9
                            color: root.backend && root.backend.selectedValue.writable === true ? root.theme.accentSoft : root.theme.surfaceRaised
                            Label {
                                id: accessLabel
                                anchors.centerIn: parent
                                text: root.backend && root.backend.selectedValue.writable === true ? "WRITABLE" : "READ ONLY"
                                color: root.backend && root.backend.selectedValue.writable === true ? root.theme.accent : root.theme.muted
                                font.pixelSize: 7
                                font.weight: Font.Bold
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.theme.lineSoft }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: availableWidth
                        ColumnLayout {
                            width: parent.width
                            spacing: 9
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                Layout.topMargin: 12
                                spacing: 4
                                Label {
                                    Layout.fillWidth: true
                                    text: root.backend ? (root.backend.selectedValue.dataAttribute || root.backend.selectedValue.dataObject || "No value selected") : "No value selected"
                                    color: root.theme.text
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: root.backend ? (root.backend.selectedValue.reference || "Select a data attribute to inspect it.") : ""
                                    color: root.theme.muted
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
                                color: root.theme.surface
                                border.width: 1
                                border.color: root.theme.lineSoft
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 2
                                    Label { text: "CURRENT VALUE"; color: root.theme.muted; font.pixelSize: 8; font.weight: Font.DemiBold }
                                    Label {
                                        Layout.fillWidth: true
                                        text: root.backend && root.backend.selectedValue.value !== undefined ? String(root.backend.selectedValue.value) : "—"
                                        color: root.backend && root.backend.selectedValue.writable === true && root.backend.running ? root.theme.accent : root.theme.text
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
                                MetaRow { title: "FC"; value: root.backend && root.backend.selectedValue.fc ? String(root.backend.selectedValue.fc) : "" }
                                MetaRow { title: "Type"; value: root.backend && root.backend.selectedValue.type ? String(root.backend.selectedValue.type) : "" }
                                MetaRow { title: "Quality"; value: root.backend && root.backend.selectedValue.quality ? String(root.backend.selectedValue.quality) : "" }
                                MetaRow { title: "Unit"; value: root.backend && root.backend.selectedValue.unit ? String(root.backend.selectedValue.unit) : "" }
                                MetaRow { title: "Origin"; value: root.backend && root.backend.selectedValue.origin ? String(root.backend.selectedValue.origin) : "" }
                                MetaRow { title: "Updated"; value: root.backend && root.backend.selectedValue.updated ? String(root.backend.selectedValue.updated) : "" }
                            }
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.theme.lineSoft }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                Layout.bottomMargin: 12
                                spacing: 5
                                Label { text: "MODEL SUMMARY"; color: root.theme.muted; font.pixelSize: 8; font.weight: Font.DemiBold }
                                MetaRow { title: "Endpoint"; value: root.backend && root.backend.imported ? (root.backend.listenAddress + ":" + root.backend.port) : "" }
                                MetaRow { title: "DataSets"; value: root.backend && root.backend.imported ? String(root.backend.dataSetCount) : "" }
                                MetaRow { title: "Reports"; value: root.backend && root.backend.imported ? String(root.backend.reportCount) : "" }
                                MetaRow { title: "GOOSE"; value: root.backend && root.backend.imported ? String(root.backend.gooseCount) : "" }
                                MetaRow { title: "Setting LDs"; value: String(settingGroupCatalog.itemCount) }
                                Label {
                                    Layout.fillWidth: true
                                    text: "Use the top Set value command or Ctrl+E for writable simulator points."
                                    color: root.theme.muted
                                    font.pixelSize: 8
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Component.onCompleted: refreshService()
}
