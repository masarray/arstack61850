// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property var settings

    property bool showConnectionHeader: true

    function valueOrDash(name) {
        var value = settings.selectedSettingGroup[name]
        if (value === undefined || value === null || value === "")
            return "—"
        return String(value)
    }

    function activationReady() {
        return settings.selectedSettingGroup.canActivate === true
    }

    Rectangle {
        anchors.fill: parent
        color: theme.background
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        SurfaceCard {
            visible: root.showConnectionHeader
            Layout.fillWidth: true
            Layout.preferredHeight: root.showConnectionHeader ? 72 : 0
            theme: root.theme

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                ColumnLayout {
                    Layout.preferredWidth: 235
                    spacing: 2
                    Label {
                        text: "SETTING GROUPS"
                        color: theme.text
                        font.pixelSize: theme.subtitleSize
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: "SGCB inspection + guarded ActSG activation"
                        color: theme.muted
                        font.pixelSize: theme.captionSize
                    }
                }

                TextField {
                    Layout.preferredWidth: 210
                    text: settings.host
                    placeholderText: "IED host"
                    enabled: !settings.busy && !settings.connected
                    onEditingFinished: settings.host = text
                }
                SpinBox {
                    Layout.preferredWidth: 105
                    from: 1
                    to: 65535
                    editable: true
                    value: settings.port
                    enabled: !settings.busy && !settings.connected
                    onValueModified: settings.port = value
                }
                ActionButton {
                    theme: root.theme
                    text: settings.connected ? "Connected" : "Connect"
                    primary: !settings.connected
                    enabled: !settings.busy && !settings.connected
                    onClicked: settings.connectToIed()
                }
                ActionButton {
                    theme: root.theme
                    text: "Refresh"
                    enabled: settings.connected && !settings.operationBusy
                    onClicked: settings.refreshSettingGroups()
                }
                ActionButton {
                    theme: root.theme
                    text: "Disconnect"
                    danger: true
                    enabled: settings.connected
                    onClicked: settings.disconnectFromIed()
                }
                Item { Layout.fillWidth: true }
                ColumnLayout {
                    spacing: 1
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: settings.stateText
                        color: settings.connected ? theme.green : (settings.lastError.length ? theme.red : theme.textSoft)
                        font.pixelSize: theme.labelSize
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: settings.associationProfile.length ? settings.associationProfile : settings.endpoint
                        color: theme.muted
                        font.pixelSize: 9
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            SurfaceCard {
                Layout.preferredWidth: 340
                Layout.fillHeight: true
                theme: root.theme

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "SGCB inventory"
                            color: theme.text
                            font.pixelSize: theme.labelSize
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: settings.settingGroupCount + " block(s)"
                            color: theme.muted
                            font.pixelSize: theme.captionSize
                        }
                    }

                    ListView {
                        id: groupList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: settings.settingGroups
                        spacing: 3

                        delegate: Rectangle {
                            id: groupRow
                            required property var modelData
                            required property int index
                            width: groupList.width
                            height: 60
                            radius: 6
                            color: settings.selectedSettingGroupIndex === index
                                   ? theme.accentSoft
                                   : groupMouse.containsMouse ? theme.surfaceRaised : theme.surface
                            border.width: 1
                            border.color: settings.selectedSettingGroupIndex === index ? theme.accent : theme.lineSoft

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 2
                                Label {
                                    Layout.fillWidth: true
                                    text: groupRow.modelData.reference || "SGCB"
                                    elide: Text.ElideMiddle
                                    color: theme.text
                                    font.pixelSize: theme.labelSize
                                    font.weight: Font.DemiBold
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        text: "ActSG " + (groupRow.modelData.actSG === undefined ? "—" : groupRow.modelData.actSG)
                                        color: theme.textSoft
                                        font.pixelSize: theme.captionSize
                                    }
                                    Label {
                                        text: "Num " + (groupRow.modelData.numOfSG === undefined ? "—" : groupRow.modelData.numOfSG)
                                        color: theme.textSoft
                                        font.pixelSize: theme.captionSize
                                    }
                                    Item { Layout.fillWidth: true }
                                    Label {
                                        text: groupRow.modelData.canActivate === true ? "ACTIVATE READY" : "READ ONLY"
                                        color: groupRow.modelData.canActivate === true ? theme.green : theme.amber
                                        font.pixelSize: 9
                                        font.weight: Font.Bold
                                    }
                                }
                            }

                            MouseArea {
                                id: groupMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: settings.selectSettingGroup(groupRow.index)
                            }
                        }

                        ScrollBar.vertical: ScrollBar {}
                    }
                }
            }

            SurfaceCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                theme: root.theme

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 9

                    Label {
                        text: settings.selectedSettingGroup.reference || "Select a Setting Group"
                        color: theme.text
                        font.pixelSize: theme.subtitleSize
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: settings.selectedSettingGroup.functionalConstraints
                              ? "Exact FC paths: " + settings.selectedSettingGroup.functionalConstraints
                              : ""
                        color: theme.muted
                        font.pixelSize: theme.captionSize
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 5
                        columnSpacing: 8
                        rowSpacing: 5

                        Repeater {
                            model: [
                                {label: "NumOfSG", key: "numOfSG"},
                                {label: "ActSG", key: "actSG"},
                                {label: "EditSG", key: "editSG"},
                                {label: "CnfEdit", key: "cnfEdit"},
                                {label: "LActTm", key: "lActTm"}
                            ]
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 58
                                radius: 6
                                color: theme.chrome
                                border.width: 1
                                border.color: theme.lineSoft
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 7
                                    spacing: 2
                                    Label {
                                        text: modelData.label
                                        color: theme.muted
                                        font.pixelSize: 9
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: root.valueOrDash(modelData.key)
                                        elide: Text.ElideRight
                                        color: theme.text
                                        font.pixelSize: theme.labelSize
                                        font.weight: Font.DemiBold
                                    }
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            text: "Activate group"
                            color: theme.text
                            font.pixelSize: theme.labelSize
                            font.weight: Font.DemiBold
                        }
                        SpinBox {
                            id: groupTarget
                            from: 1
                            to: Math.max(1, Number(settings.selectedSettingGroup.numOfSG || 1))
                            value: Math.max(1, Number(settings.selectedSettingGroup.actSG || 1))
                            editable: true
                            enabled: root.activationReady() && !settings.operationBusy
                        }
                        ActionButton {
                            theme: root.theme
                            text: "Activate + verify"
                            primary: true
                            enabled: settings.connected && root.activationReady() && !settings.operationBusy
                            onClicked: settings.activateSelectedSettingGroup(groupTarget.value)
                        }
                        ActionButton {
                            theme: root.theme
                            text: "Cancel"
                            danger: true
                            enabled: settings.operationBusy
                            onClicked: settings.cancelOperation()
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: settings.selectedSettingGroup.complete === true ? "5/5 READ PATH READY" : "PARTIAL READ"
                            color: settings.selectedSettingGroup.complete === true ? theme.green : theme.amber
                            font.pixelSize: 9
                            font.weight: Font.Bold
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.activationReady() ? 50 : 66
                        radius: 6
                        color: root.activationReady() ? theme.greenSoft : theme.amberSoft
                        border.width: 1
                        border.color: root.activationReady() ? theme.green : theme.amber
                        Label {
                            anchors.fill: parent
                            anchors.margins: 8
                            text: root.activationReady()
                                  ? "Guard: one exact-type ActSG Write followed by verification Read. No automatic retry."
                                  : (settings.selectedSettingGroup.activationBlockedReason ||
                                     "Activation unavailable until SGCB values are read safely.")
                            wrapMode: Text.Wrap
                            color: theme.textSoft
                            font.pixelSize: theme.captionSize
                        }
                    }

                    Label {
                        text: "Exact attributes"
                        color: theme.text
                        font.pixelSize: theme.labelSize
                        font.weight: Font.DemiBold
                    }

                    ListView {
                        id: attributeList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: settings.selectedSettingGroup.attributes || []
                        spacing: 2
                        delegate: Rectangle {
                            required property var modelData
                            width: attributeList.width
                            height: 43
                            radius: 5
                            color: theme.chrome
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 8
                                Label {
                                    text: modelData.path || ""
                                    color: theme.text
                                    font.pixelSize: theme.captionSize
                                    font.weight: Font.DemiBold
                                    Layout.preferredWidth: 90
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.reference || ""
                                    elide: Text.ElideMiddle
                                    color: theme.muted
                                    font.pixelSize: 9
                                }
                                Label {
                                    text: modelData.success === true ? modelData.value : "READ FAILED"
                                    color: modelData.success === true ? theme.textSoft : theme.red
                                    font.pixelSize: theme.captionSize
                                    Layout.preferredWidth: 135
                                    horizontalAlignment: Text.AlignRight
                                    elide: Text.ElideRight
                                }
                            }
                        }
                        ScrollBar.vertical: ScrollBar {}
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 58
                        radius: 6
                        color: theme.surfaceSoft
                        border.width: 1
                        border.color: theme.line
                        Label {
                            anchors.fill: parent
                            anchors.margins: 8
                            text: "Full Setting Group editing is NOT claimed in Milestone Q. EditSG → SE value mutation → CnfEdit remains disabled until that complete transaction is implemented and proven."
                            wrapMode: Text.Wrap
                            color: theme.textSoft
                            font.pixelSize: theme.captionSize
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: settings.lastError.length > 0
                        text: settings.lastError
                        wrapMode: Text.Wrap
                        color: theme.red
                        font.pixelSize: theme.captionSize
                    }
                }
            }
        }
    }
}
