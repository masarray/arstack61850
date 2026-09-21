// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root

    required property var theme
    required property var session
    required property var controls

    property var targetNode: ({})
    property string pendingAction: ""
    property string pendingSummary: ""

    title: "IEC 61850 Control"
    modal: true
    width: 660
    height: 620
    anchors.centerIn: Overlay.overlay
    standardButtons: Dialog.Close

    function text(value) {
        return value === undefined || value === null || String(value).length === 0 ? "—" : String(value)
    }

    function openFor(node) {
        if (!node || node.controlCandidate !== true || !node.objectReference) {
            return false
        }
        targetNode = node
        valueField.text = ""
        pendingAction = ""
        pendingSummary = ""
        root.open()
        session.prepareControlObject(node.objectReference)
        return true
    }

    function requestAction(action, summary) {
        pendingAction = action
        pendingSummary = summary
        confirmDialog.open()
    }

    function executePending() {
        var kind = valueKind.currentText
        var value = valueField.text
        var originCategory = origin.currentIndex
        var originId = originIdentifier.text
        var test = testFlag.checked
        var interlock = interlockFlag.checked
        var synchro = synchroFlag.checked

        if (pendingAction === "select") {
            controls.select(kind, value, originCategory, originId, test, interlock, synchro)
        } else if (pendingAction === "operate") {
            controls.operate(kind, value, originCategory, originId, test, interlock, synchro, autoSelect.checked)
        } else if (pendingAction === "select-operate") {
            controls.selectAndOperate(kind, value, originCategory, originId, test, interlock, synchro)
        } else if (pendingAction === "cancel") {
            controls.cancel()
        }
        pendingAction = ""
    }

    onClosed: {
        pendingAction = ""
    }

    contentItem: ScrollView {
        contentWidth: availableWidth

        ColumnLayout {
            width: Math.max(0, parent.width - 30)
            x: 15
            spacing: 10

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 68
                radius: 6
                color: theme.surface
                border.width: 1
                border.color: theme.lineSoft

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 3
                    Label {
                        Layout.fillWidth: true
                        text: root.text(root.targetNode.objectReference)
                        color: theme.text
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        elide: Text.ElideMiddle
                    }
                    Label {
                        Layout.fillWidth: true
                        text: "Canonical selection · " + root.text(root.targetNode.reference)
                        color: theme.muted
                        font.pixelSize: 8
                        elide: Text.ElideMiddle
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: descriptorGrid.implicitHeight + 18
                radius: 6
                color: theme.surface
                border.width: 1
                border.color: theme.lineSoft

                GridLayout {
                    id: descriptorGrid
                    anchors.fill: parent
                    anchors.margins: 9
                    columns: 4
                    columnSpacing: 12
                    rowSpacing: 5

                    Label { text: "State"; color: theme.muted; font.pixelSize: 8 }
                    Label {
                        text: controls.busy ? "PREPARING / BUSY"
                              : controls.prepared ? "READY" : "NOT READY"
                        color: controls.prepared ? theme.green
                                               : controls.lastError.length ? theme.red : theme.muted
                        font.pixelSize: 8
                        font.weight: Font.DemiBold
                    }
                    Label { text: "ctlModel"; color: theme.muted; font.pixelSize: 8 }
                    Label { text: root.text(controls.modelName); color: theme.textSoft; font.pixelSize: 8 }
                    Label { text: "CDC"; color: theme.muted; font.pixelSize: 8 }
                    Label { text: root.text(controls.cdc); color: theme.textSoft; font.pixelSize: 8 }
                    Label { text: "Selection"; color: theme.muted; font.pixelSize: 8 }
                    Label {
                        text: controls.activeSelection ? "ACTIVE"
                              : controls.requiresSelect ? "REQUIRED" : "NOT REQUIRED"
                        color: controls.activeSelection ? theme.green : theme.textSoft
                        font.pixelSize: 8
                    }
                    Label { text: "Security"; color: theme.muted; font.pixelSize: 8 }
                    Label {
                        text: controls.enhanced ? "ENHANCED" : "NORMAL"
                        color: theme.textSoft
                        font.pixelSize: 8
                    }
                    Label { text: "Termination"; color: theme.muted; font.pixelSize: 8 }
                    Label {
                        text: controls.supportsCommandTermination ? "CommandTermination" : "MMS acceptance"
                        color: theme.textSoft
                        font.pixelSize: 8
                    }
                }
            }

            Label {
                visible: controls.busy
                text: controls.lastStatus
                color: theme.muted
                font.pixelSize: 9
            }

            Rectangle {
                visible: controls.lastError.length > 0
                Layout.fillWidth: true
                implicitHeight: errorLabel.implicitHeight + 16
                radius: 5
                color: theme.redSoft
                border.width: 1
                border.color: theme.red
                Label {
                    id: errorLabel
                    anchors.fill: parent
                    anchors.margins: 8
                    text: controls.lastError
                    color: theme.red
                    font.pixelSize: 8
                    wrapMode: Text.WordWrap
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 8

                Label { text: "Value type"; color: theme.muted; font.pixelSize: 9 }
                ComboBox {
                    id: valueKind
                    Layout.fillWidth: true
                    model: ["auto", "bool", "dpc", "int", "uint", "float", "step"]
                    enabled: controls.prepared && !controls.busy
                }

                Label { text: "ctlVal"; color: theme.muted; font.pixelSize: 9 }
                TextField {
                    id: valueField
                    Layout.fillWidth: true
                    placeholderText: controls.cdc === "DPC" ? "on / off"
                                     : controls.cdc === "SPC" ? "true / false"
                                     : "Exact typed value"
                    selectByMouse: true
                    enabled: controls.prepared && !controls.busy
                }

                Label { text: "Origin"; color: theme.muted; font.pixelSize: 9 }
                ComboBox {
                    id: origin
                    Layout.fillWidth: true
                    currentIndex: 2
                    model: [
                        "not-supported", "bay-control", "station-control",
                        "remote-control", "automatic-bay", "automatic-station",
                        "automatic-remote", "maintenance", "process"
                    ]
                    enabled: controls.prepared && !controls.busy
                }

                Label { text: "Origin identifier"; color: theme.muted; font.pixelSize: 9 }
                TextField {
                    id: originIdentifier
                    Layout.fillWidth: true
                    text: "ARSTACK-HMI"
                    maximumLength: 64
                    selectByMouse: true
                    enabled: controls.prepared && !controls.busy
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                CheckBox {
                    id: testFlag
                    text: "Test"
                    checked: false
                    enabled: controls.prepared && !controls.busy
                }
                CheckBox {
                    id: interlockFlag
                    text: "Interlock-check"
                    checked: true
                    enabled: controls.prepared && !controls.busy
                }
                CheckBox {
                    id: synchroFlag
                    text: "Synchro-check"
                    checked: true
                    enabled: controls.prepared && !controls.busy
                }
                CheckBox {
                    id: autoSelect
                    text: "Auto-select for Operate"
                    checked: true
                    visible: controls.requiresSelect
                    enabled: controls.prepared && !controls.busy
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: theme.lineSoft
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Button {
                    text: controls.enhanced ? "Select with value…" : "Select…"
                    visible: controls.requiresSelect
                    enabled: controls.prepared && !controls.busy && !controls.activeSelection
                             && valueField.text.length > 0
                    onClicked: root.requestAction(
                        "select",
                        "SELECT " + controls.objectReference + " with ctlVal=" + valueField.text)
                }

                Button {
                    text: "Select + Operate…"
                    visible: controls.requiresSelect
                    enabled: controls.prepared && !controls.busy && !controls.activeSelection
                             && valueField.text.length > 0
                    onClicked: root.requestAction(
                        "select-operate",
                        "SELECT + OPERATE " + controls.objectReference + " with ctlVal=" + valueField.text)
                }

                Button {
                    text: controls.requiresSelect && autoSelect.checked
                          ? "Operate (auto-select)…" : "Operate…"
                    enabled: controls.prepared && !controls.busy
                             && valueField.text.length > 0
                             && (!controls.requiresSelect || controls.activeSelection || autoSelect.checked)
                    onClicked: root.requestAction(
                        "operate",
                        "OPERATE " + controls.objectReference + " with ctlVal=" + valueField.text)
                }

                Button {
                    text: "Cancel selection…"
                    visible: controls.supportsCancel
                    enabled: controls.prepared && !controls.busy && controls.activeSelection
                    onClicked: root.requestAction(
                        "cancel",
                        "CANCEL active selection for " + controls.objectReference)
                }

                Item { Layout.fillWidth: true }
            }

            Rectangle {
                visible: controls.lastResult.completion !== undefined
                Layout.fillWidth: true
                implicitHeight: resultColumn.implicitHeight + 18
                radius: 6
                color: theme.surface
                border.width: 1
                border.color: controls.lastResult.success === true ? theme.green : theme.lineSoft

                ColumnLayout {
                    id: resultColumn
                    anchors.fill: parent
                    anchors.margins: 9
                    spacing: 3
                    Label {
                        text: "LAST CONTROL RESULT"
                        color: theme.muted
                        font.pixelSize: 7
                        font.weight: Font.Bold
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root.text(controls.lastResult.completion).toUpperCase()
                              + " · ctlNum " + root.text(controls.lastResult.controlNumber)
                        color: controls.lastResult.success === true ? theme.green : theme.text
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root.text(controls.lastResult.message)
                        color: theme.textSoft
                        font.pixelSize: 8
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: controls.lastResult.commandTerminationReceived === true
                        text: controls.lastResult.positiveTermination === true
                              ? "Correlated positive CommandTermination received."
                              : "Correlated negative CommandTermination / LastApplError received."
                        color: controls.lastResult.positiveTermination === true ? theme.green : theme.red
                        font.pixelSize: 8
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: controls.lastResult.addCause !== undefined
                        text: "ControlError=" + root.text(controls.lastResult.controlErrorName)
                              + " · AddCause=" + root.text(controls.lastResult.addCauseName)
                        color: theme.muted
                        font.pixelSize: 7
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: controls.lastResult.statusValue !== undefined
                        text: "Status read-back"
                              + (controls.lastResult.statusFunctionalConstraint !== undefined
                                 ? " [" + controls.lastResult.statusFunctionalConstraint + "]" : "")
                              + ": " + root.text(controls.lastResult.statusValue)
                        color: theme.textSoft
                        font.pixelSize: 8
                        font.weight: Font.DemiBold
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                text: "Safety boundary: there is no automatic retry. Enhanced-security success is not declared from MMS Write acceptance alone; correlated CommandTermination is required by the proven control runtime."
                color: theme.muted
                font.pixelSize: 8
                wrapMode: Text.WordWrap
            }
        }
    }

    Dialog {
        id: confirmDialog
        title: "Confirm IEC 61850 control action"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(520, Math.max(360, root.width - 60))
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.executePending()

        contentItem: ColumnLayout {
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: root.pendingSummary
                color: theme.text
                font.pixelSize: 10
                font.weight: Font.DemiBold
                wrapMode: Text.WrapAnywhere
            }
            Label {
                Layout.fillWidth: true
                text: "This action may change the remote IED/process state. The command is sent once only; ARStack will not automatically retry it."
                color: theme.muted
                font.pixelSize: 8
                wrapMode: Text.WordWrap
            }
        }
    }
}
