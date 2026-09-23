// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root

    required property var theme
    required property var reports

    property string pendingDeleteReference: ""
    readonly property int maximumMembers: 64

    title: "Dynamic DataSet Authoring"
    modal: true
    width: 700
    height: 620
    anchors.centerIn: Overlay.overlay
    standardButtons: Dialog.Close

    ListModel { id: memberModel }

    function findMember(domain, item) {
        for (var i = 0; i < memberModel.count; ++i) {
            var row = memberModel.get(i)
            if (row.mmsDomain === domain && row.mmsItem === item)
                return i
        }
        return -1
    }

    function resetDraft() {
        memberModel.clear()
        referenceField.text = ""
        statusLabel.text = "Draft is local until Create is explicitly confirmed."
    }

    function newDraft() {
        resetDraft()
        root.open()
    }

    function addSelected(node) {
        if (!node || node.kind !== "DA" || node.readable !== true
                || !node.mmsDomain || !node.mmsItem || !node.reference) {
            statusLabel.text = "Select one readable canonical DataAttribute first."
            root.open()
            return false
        }
        if (findMember(node.mmsDomain, node.mmsItem) >= 0) {
            statusLabel.text = "That MMS member is already in the draft."
            root.open()
            return true
        }
        if (memberModel.count >= root.maximumMembers) {
            statusLabel.text = "Draft is full (64 members)."
            root.open()
            return false
        }
        memberModel.append({
            "reference": node.reference,
            "mmsDomain": node.mmsDomain,
            "mmsItem": node.mmsItem,
            "functionalConstraint": node.functionalConstraint || ""
        })
        if (referenceField.text.length === 0)
            referenceField.text = node.mmsDomain + "/LLN0.ARStackDynamic1"
        statusLabel.text = "Added canonical member " + node.reference
        root.open()
        return true
    }

    function memberPayload() {
        var result = []
        for (var i = 0; i < memberModel.count; ++i) {
            var row = memberModel.get(i)
            result.push({
                "reference": row.reference,
                "mmsDomain": row.mmsDomain,
                "mmsItem": row.mmsItem,
                "functionalConstraint": row.functionalConstraint
            })
        }
        return result
    }

    function createConfirmed() {
        var accepted = reports.createDynamicDataSet(
            referenceField.text, memberPayload())
        if (accepted) {
            statusLabel.text = "DefineNamedVariableList started; verification is mandatory."
            root.close()
        } else {
            statusLabel.text = reports.lastError.length
                ? reports.lastError : "Dynamic DataSet create was not started."
        }
    }

    function requestDelete(dataSet) {
        if (!dataSet || dataSet.dynamicOwned !== true) {
            pendingDeleteReference = ""
            deleteError.text =
                "Only DataSets created by this live report association can be deleted here."
            deleteDialog.open()
            return false
        }
        pendingDeleteReference = dataSet.reference || ""
        deleteError.text = ""
        deleteDialog.open()
        return true
    }

    function deleteConfirmed() {
        if (pendingDeleteReference.length === 0)
            return
        reports.deleteDynamicDataSet(pendingDeleteReference)
        pendingDeleteReference = ""
    }

    contentItem: ColumnLayout {
        spacing: 8

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 74
            radius: 6
            color: theme.surface
            border.width: 1
            border.color: theme.lineSoft

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 3
                Label {
                    text: "DATASET REFERENCE"
                    color: theme.muted
                    font.pixelSize: 7
                    font.weight: Font.Bold
                }
                TextField {
                    id: referenceField
                    Layout.fillWidth: true
                    placeholderText: "LogicalDevice/LLN0.ARStackDynamic1"
                    selectByMouse: true
                    enabled: !reports.busy
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: "Ordered members · " + memberModel.count
                      + " / " + root.maximumMembers
                color: theme.text
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }
            Button {
                text: "Clear draft"
                enabled: memberModel.count > 0 && !reports.busy
                onClicked: memberModel.clear()
            }
        }

        ListView {
            id: memberList
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: memberModel
            clip: true
            reuseItems: true
            spacing: 1
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                required property int index
                required property string reference
                required property string mmsDomain
                required property string mmsItem
                required property string functionalConstraint

                width: memberList.width
                height: 54
                color: index % 2 ? theme.surfaceSoft : theme.surface
                border.width: 1
                border.color: theme.lineSoft

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 9
                    anchors.rightMargin: 8
                    spacing: 7

                    Label {
                        text: String(index + 1)
                        color: theme.muted
                        font.pixelSize: 8
                        Layout.preferredWidth: 24
                        horizontalAlignment: Text.AlignHCenter
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Label {
                            Layout.fillWidth: true
                            text: reference
                            color: theme.text
                            font.pixelSize: 9
                            font.weight: Font.DemiBold
                            elide: Text.ElideMiddle
                        }
                        Label {
                            Layout.fillWidth: true
                            text: mmsDomain + "/" + mmsItem
                                  + (functionalConstraint.length
                                     ? " · FC=" + functionalConstraint : "")
                            color: theme.muted
                            font.pixelSize: 7
                            elide: Text.ElideMiddle
                        }
                    }

                    Button {
                        text: "↑"
                        enabled: index > 0 && !reports.busy
                        onClicked: memberModel.move(index, index - 1, 1)
                    }
                    Button {
                        text: "↓"
                        enabled: index + 1 < memberModel.count && !reports.busy
                        onClicked: memberModel.move(index, index + 1, 1)
                    }
                    Button {
                        text: "Remove"
                        enabled: !reports.busy
                        onClicked: memberModel.remove(index)
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: memberModel.count === 0
                width: Math.max(220, parent.width - 60)
                text: "Add readable DataAttributes from Data Model. Member order is preserved exactly in DefineNamedVariableList."
                color: theme.muted
                font.pixelSize: 9
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
        }

        Label {
            id: statusLabel
            Layout.fillWidth: true
            text: "Draft is local until Create is explicitly confirmed."
            color: reports.lastError.length ? theme.red : theme.muted
            font.pixelSize: 8
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: "Create uses the existing MMS association and verifies the resulting directory before ownership is recorded."
                color: theme.muted
                font.pixelSize: 8
                wrapMode: Text.WordWrap
            }
            Button {
                text: reports.busy ? "Working…" : "Create…"
                enabled: reports.connected && !reports.busy
                         && referenceField.text.trim().length > 0
                         && memberModel.count > 0
                onClicked: createConfirm.open()
            }
        }
    }

    Dialog {
        id: createConfirm
        title: "Confirm dynamic DataSet create"
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.createConfirmed()
        contentItem: Label {
            width: 470
            text: "Create " + referenceField.text
                  + " with " + memberModel.count
                  + " ordered member(s)? This sends one DefineNamedVariableList request and requires directory read-back verification."
            color: theme.text
            font.pixelSize: 9
            wrapMode: Text.WordWrap
        }
    }

    Dialog {
        id: deleteDialog
        title: "Confirm dynamic DataSet delete"
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: root.pendingDeleteReference.length > 0
                         ? Dialog.Ok | Dialog.Cancel : Dialog.Close
        onAccepted: root.deleteConfirmed()
        contentItem: ColumnLayout {
            width: 470
            spacing: 6
            Label {
                Layout.fillWidth: true
                text: root.pendingDeleteReference.length > 0
                      ? "Delete owned dynamic DataSet " + root.pendingDeleteReference + "?"
                      : "Delete refused."
                color: theme.text
                font.pixelSize: 9
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            Label {
                id: deleteError
                Layout.fillWidth: true
                text: ""
                color: deleteError.text.length ? theme.red : theme.muted
                font.pixelSize: 8
                wrapMode: Text.WordWrap
            }
            Label {
                visible: root.pendingDeleteReference.length > 0
                Layout.fillWidth: true
                text: "ARStack uses owned-only DeleteNamedVariableList. Static and foreign dynamic DataSets cannot be deleted from this workflow."
                color: theme.muted
                font.pixelSize: 8
                wrapMode: Text.WordWrap
            }
        }
    }
}
