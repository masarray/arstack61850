// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ARStack.IedSimulator 1.0

Drawer {
    id: root

    required property var theme
    required property var backend

    edge: Qt.RightEdge
    modal: false
    interactive: true
    width: parent ? Math.min(1180, Math.max(820, parent.width * 0.86)) : 1080
    height: parent ? parent.height : 760
    padding: 0

    function display(value) {
        if (value === undefined || value === null || String(value).length === 0) return "—"
        return String(value)
    }

    background: Rectangle {
        color: root.theme.chrome
        border.width: 1
        border.color: root.theme.line
    }

    IedCommissioningModel {
        id: commissioningModel
        backend: root.opened ? root.backend : null
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
            font.pixelSize: 8
        }
        Label {
            Layout.fillWidth: true
            text: parent.value.length ? parent.value : "—"
            color: root.theme.textSoft
            font.pixelSize: 9
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideMiddle
        }
    }

    component SmallButton: Button {
        id: button
        implicitHeight: 29
        implicitWidth: Math.max(78, label.implicitWidth + 20)
        font.pixelSize: 9
        font.weight: Font.DemiBold
        property bool primary: false
        contentItem: Label {
            id: label
            text: button.text
            color: !button.enabled ? root.theme.muted
                                   : button.primary ? "#ffffff" : root.theme.textSoft
            font: button.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 5
            color: !button.enabled ? root.theme.surfaceRaised
                                   : button.primary ? root.theme.accent
                                                    : button.down ? root.theme.surfaceSoft : root.theme.surface
            border.width: 1
            border.color: button.primary ? root.theme.accent : root.theme.line
        }
    }

    Connections {
        target: commissioningModel
        function onFilterChanged() {
            var wanted = commissioningModel.kindFilter
            kindBox.currentIndex = Math.max(0, kindBox.model.indexOf(wanted))
            if (commissioningModel.filterText.length === 0 && searchField.text.length > 0) searchField.clear()
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 62
            color: root.theme.navigation

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 10
                spacing: 10

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Label {
                        text: "Commissioning Explorer"
                        color: root.theme.navigationText
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: root.backend.imported
                              ? (root.backend.selectedIed.name || "IED") + " · configured services and control semantics"
                              : "DataSet, Report, GOOSE and control topology"
                        color: root.theme.navigationMuted
                        font.pixelSize: 8
                    }
                }

                Label {
                    visible: root.backend.imported
                    text: commissioningModel.dataSetCount + " DS  ·  "
                          + commissioningModel.reportCount + " RPT  ·  "
                          + commissioningModel.gooseCount + " GOOSE  ·  "
                          + commissioningModel.controlCount + " CTRL"
                    color: root.theme.navigationMuted
                    font.pixelSize: 8
                }

                ToolButton {
                    text: "×"
                    font.pixelSize: 17
                    onClicked: root.close()
                    ToolTip.visible: hovered
                    ToolTip.text: "Close · Ctrl+Shift+C"
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                ComboBox {
                    id: kindBox
                    Layout.preferredWidth: 126
                    Layout.preferredHeight: 32
                    model: ["All", "DataSet", "Report", "GOOSE", "Control"]
                    font.pixelSize: 9
                    onActivated: commissioningModel.kindFilter = currentText
                }

                TextField {
                    id: searchField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    placeholderText: "Search service, reference, DataSet, APPID, goID or control model"
                    selectByMouse: true
                    font.pixelSize: 9
                    enabled: root.backend.imported
                    onTextChanged: commissioningModel.filterText = text
                }

                Label {
                    text: commissioningModel.itemCount + " item" + (commissioningModel.itemCount === 1 ? "" : "s")
                    color: root.theme.muted
                    font.pixelSize: 9
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.preferredWidth: 310
                Layout.minimumWidth: 270
                Layout.fillHeight: true
                color: root.theme.chrome
                border.width: 1
                border.color: root.theme.lineSoft

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        color: root.theme.surfaceRaised
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 11
                            anchors.rightMargin: 9
                            Label {
                                Layout.fillWidth: true
                                text: "CONFIGURED SERVICES"
                                color: root.theme.muted
                                font.pixelSize: 8
                                font.weight: Font.DemiBold
                            }
                            Label {
                                text: "Selected IED"
                                color: root.theme.muted
                                font.pixelSize: 7
                            }
                        }
                    }

                    ListView {
                        id: serviceList
                        objectName: "iedCommissioningServiceList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: commissioningModel.itemCount
                        reuseItems: true
                        cacheBuffer: 0
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        delegate: Rectangle {
                            id: serviceRow
                            required property int index
                            property var entry: {
                                var version = commissioningModel.revision
                                return commissioningModel.item(index)
                            }

                            width: serviceList.width
                            height: 64
                            color: entry.selected ? root.theme.accentSoft
                                                  : serviceMouse.containsMouse ? root.theme.surfaceRaised
                                                                               : root.theme.surface

                            Rectangle {
                                visible: serviceRow.entry.selected === true
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: 3
                                color: root.theme.accent
                            }

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 11
                                anchors.rightMargin: 9
                                anchors.topMargin: 7
                                anchors.bottomMargin: 7
                                spacing: 3

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Rectangle {
                                        implicitWidth: typeLabel.implicitWidth + 12
                                        implicitHeight: 17
                                        radius: 8
                                        color: serviceRow.entry.kind === "GOOSE" ? root.theme.greenSoft
                                             : serviceRow.entry.kind === "Report" ? root.theme.accentSoft
                                             : serviceRow.entry.kind === "Control" ? root.theme.amberSoft
                                                                                   : root.theme.surfaceRaised
                                        Label {
                                            id: typeLabel
                                            anchors.centerIn: parent
                                            text: serviceRow.entry.kind || "Service"
                                            color: serviceRow.entry.kind === "GOOSE" ? root.theme.green
                                                 : serviceRow.entry.kind === "Control" ? root.theme.amber
                                                                                      : root.theme.accent
                                            font.pixelSize: 7
                                            font.weight: Font.Bold
                                        }
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: serviceRow.entry.name || "—"
                                        color: root.theme.text
                                        font.pixelSize: 10
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        text: serviceRow.entry.status || ""
                                        color: serviceRow.entry.status === "Unresolved" ? root.theme.red : root.theme.muted
                                        font.pixelSize: 7
                                        font.weight: Font.DemiBold
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: serviceRow.entry.summary || serviceRow.entry.reference || ""
                                    color: root.theme.muted
                                    font.pixelSize: 8
                                    elide: Text.ElideMiddle
                                }
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                color: root.theme.lineSoft
                            }

                            MouseArea {
                                id: serviceMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: commissioningModel.select(serviceRow.index)
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: commissioningModel.itemCount === 0
                            text: root.backend.imported ? "No services match this filter" : "Open an SCL model first"
                            color: root.theme.muted
                            font.pixelSize: 10
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

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        color: root.theme.surfaceRaised
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 11
                            anchors.rightMargin: 11
                            Label {
                                Layout.fillWidth: true
                                text: "CANONICAL MEMBERS"
                                color: root.theme.muted
                                font.pixelSize: 8
                                font.weight: Font.DemiBold
                            }
                            Label {
                                text: commissioningModel.memberCount + " member" + (commissioningModel.memberCount === 1 ? "" : "s")
                                color: root.theme.muted
                                font.pixelSize: 8
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        color: root.theme.chrome
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 0
                            Label { Layout.fillWidth: true; text: "FCDA / object"; color: root.theme.muted; font.pixelSize: 8; font.weight: Font.DemiBold }
                            Label { Layout.preferredWidth: 44; text: "FC"; color: root.theme.muted; font.pixelSize: 8; font.weight: Font.DemiBold }
                            Label { Layout.preferredWidth: 64; text: "CDC"; color: root.theme.muted; font.pixelSize: 8; font.weight: Font.DemiBold }
                            Label { Layout.preferredWidth: 95; text: "Type"; color: root.theme.muted; font.pixelSize: 8; font.weight: Font.DemiBold }
                        }
                    }

                    ListView {
                        id: memberList
                        objectName: "iedCommissioningMemberList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: commissioningModel.memberCount
                        reuseItems: true
                        cacheBuffer: 0
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        delegate: Rectangle {
                            id: memberRow
                            required property int index
                            property var entry: {
                                var version = commissioningModel.revision
                                return commissioningModel.member(index)
                            }

                            width: memberList.width
                            height: 36
                            color: memberMouse.containsMouse ? root.theme.surfaceRaised : root.theme.surface

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 0
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Label {
                                        Layout.fillWidth: true
                                        text: (memberRow.entry.dataObject || "")
                                              + (memberRow.entry.dataAttribute ? "." + memberRow.entry.dataAttribute : "")
                                        color: root.theme.textSoft
                                        font.pixelSize: 9
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: memberRow.entry.reference || memberRow.entry.mmsItem || ""
                                        color: root.theme.muted
                                        font.pixelSize: 7
                                        elide: Text.ElideMiddle
                                    }
                                }
                                Label { Layout.preferredWidth: 44; text: memberRow.entry.fc || ""; color: root.theme.muted; font.pixelSize: 8 }
                                Label { Layout.preferredWidth: 64; text: memberRow.entry.cdc || ""; color: root.theme.muted; font.pixelSize: 8 }
                                Label { Layout.preferredWidth: 95; text: memberRow.entry.type || ""; color: root.theme.muted; font.pixelSize: 8; elide: Text.ElideRight }
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                color: root.theme.lineSoft
                            }
                            MouseArea {
                                id: memberMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                            }
                        }

                        Column {
                            anchors.centerIn: parent
                            visible: commissioningModel.memberCount === 0
                            spacing: 4
                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: commissioningModel.selectedItem.kind === "Control"
                                      ? "Control object has no DataSet member list"
                                      : "No canonical members resolved"
                                color: root.theme.textSoft
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                            }
                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: commissioningModel.selectedItem.status === "Unresolved"
                                      ? "The configured DataSet reference is unresolved in this SCL model."
                                      : "Select another configured service."
                                color: root.theme.muted
                                font.pixelSize: 8
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 320
                Layout.minimumWidth: 286
                Layout.fillHeight: true
                color: root.theme.chrome
                border.width: 1
                border.color: root.theme.lineSoft

                ScrollView {
                    anchors.fill: parent
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
                            spacing: 3
                            Label {
                                text: commissioningModel.selectedItem.kind || "INSPECTOR"
                                color: root.theme.muted
                                font.pixelSize: 8
                                font.weight: Font.Bold
                            }
                            Label {
                                Layout.fillWidth: true
                                text: commissioningModel.selectedItem.name || "No service selected"
                                color: root.theme.text
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: commissioningModel.selectedItem.reference || "Select a configured service in the catalog."
                                color: root.theme.muted
                                font.pixelSize: 8
                                wrapMode: Text.WrapAnywhere
                            }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.theme.lineSoft }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            spacing: 5
                            MetaRow { title: "Status"; value: root.display(commissioningModel.selectedItem.status) }
                            MetaRow { title: "DataSet"; value: root.display(commissioningModel.selectedItem.dataSetReference) }
                            MetaRow { title: "Members"; value: commissioningModel.selectedItem.kind ? String(commissioningModel.selectedItem.memberCount || 0) : "" }
                        }

                        ColumnLayout {
                            visible: commissioningModel.selectedItem.kind === "Report"
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            spacing: 5
                            Label { text: "REPORT CONTROL"; color: root.theme.muted; font.pixelSize: 8; font.weight: Font.Bold }
                            MetaRow { title: "Mode"; value: commissioningModel.selectedItem.buffered ? "BRCB" : "URCB" }
                            MetaRow { title: "RptID"; value: root.display(commissioningModel.selectedItem.reportId) }
                            MetaRow { title: "ConfRev"; value: root.display(commissioningModel.selectedItem.confRev) }
                            MetaRow { title: "BufTm"; value: root.display(commissioningModel.selectedItem.bufferTimeMs) + " ms" }
                            MetaRow { title: "IntgPd"; value: root.display(commissioningModel.selectedItem.integrityMs) + " ms" }
                        }

                        ColumnLayout {
                            visible: commissioningModel.selectedItem.kind === "GOOSE"
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            spacing: 5
                            Label { text: "GOOSE CONTROL"; color: root.theme.muted; font.pixelSize: 8; font.weight: Font.Bold }
                            MetaRow { title: "goID"; value: root.display(commissioningModel.selectedItem.goId) }
                            MetaRow { title: "APPID"; value: root.display(commissioningModel.selectedItem.appId) }
                            MetaRow { title: "MAC"; value: root.display(commissioningModel.selectedItem.mac) }
                            MetaRow { title: "VLAN"; value: root.display(commissioningModel.selectedItem.vlanId) }
                            MetaRow { title: "Priority"; value: root.display(commissioningModel.selectedItem.vlanPriority) }
                            MetaRow { title: "Min/Max"; value: root.display(commissioningModel.selectedItem.minTimeMs) + " / " + root.display(commissioningModel.selectedItem.maxTimeMs) + " ms" }
                            MetaRow { title: "ConfRev"; value: root.display(commissioningModel.selectedItem.confRev) }
                        }

                        ColumnLayout {
                            visible: commissioningModel.selectedItem.kind === "Control"
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            spacing: 5
                            Label { text: "CONTROL SEMANTICS"; color: root.theme.muted; font.pixelSize: 8; font.weight: Font.Bold }
                            MetaRow { title: "CDC"; value: root.display(commissioningModel.selectedItem.cdc) }
                            MetaRow { title: "ctlModel"; value: root.display(commissioningModel.selectedItem.controlModel) }
                            MetaRow { title: "LN"; value: root.display(commissioningModel.selectedItem.logicalNode) }
                            Label {
                                Layout.fillWidth: true
                                text: "Configured semantics are shown here; live Operate/SBO execution remains on the simulator runtime path."
                                color: root.theme.muted
                                font.pixelSize: 8
                                wrapMode: Text.WordWrap
                            }
                        }

                        Rectangle {
                            visible: commissioningModel.selectedItem.kind === "Report" || commissioningModel.selectedItem.kind === "GOOSE"
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            Layout.preferredHeight: visible ? 1 : 0
                            color: root.theme.lineSoft
                        }

                        SmallButton {
                            visible: commissioningModel.selectedItem.kind === "Report" || commissioningModel.selectedItem.kind === "GOOSE"
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            text: "Open bound DataSet"
                            primary: true
                            enabled: commissioningModel.selectedItem.dataSetReference !== undefined
                                     && String(commissioningModel.selectedItem.dataSetReference).length > 0
                                     && commissioningModel.selectedItem.status !== "Unresolved"
                            onClicked: {
                                if (commissioningModel.focusBoundDataSet()) searchField.clear()
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            Layout.bottomMargin: 14
                            text: "Member rows are materialized on demand from the canonical parsed SCL document; this drawer does not keep a second full FCDA catalog."
                            color: root.theme.muted
                            font.pixelSize: 7
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }
}
