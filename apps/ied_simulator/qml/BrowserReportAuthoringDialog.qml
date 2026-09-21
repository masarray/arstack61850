// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root

    required property var theme
    required property var reports

    property bool pendingGi: false

    title: "Advanced Report Authoring"
    modal: true
    width: 720
    height: 650
    anchors.centerIn: Overlay.overlay
    standardButtons: Dialog.Close

    function containsName(text, name) {
        return text && String(text).indexOf(name) >= 0
    }

    function selectedDataSetMap() {
        if (dataSetBox.currentIndex < 0
                || dataSetBox.currentIndex >= reports.dataSets.length)
            return ({})
        return reports.dataSets[dataSetBox.currentIndex]
    }

    function bindingAllowed() {
        var ds = selectedDataSetMap()
        if (!ds || !ds.reference)
            return false
        if (reports.selectedRcb.dataSet === ds.reference)
            return true
        return ds.dynamicOwned === true
    }

    function triggerNames() {
        var result = []
        if (dchg.checked) result.push("data-change")
        if (qchg.checked) result.push("quality-change")
        if (dupd.checked) result.push("data-update")
        if (integrity.checked) result.push("integrity")
        if (giTrigger.checked) result.push("general-interrogation")
        return result
    }

    function optionalNames() {
        var result = []
        if (seqNum.checked) result.push("sequence-number")
        if (timeStamp.checked) result.push("report-time-stamp")
        if (reason.checked) result.push("reason-for-inclusion")
        if (dataSetName.checked) result.push("data-set-name")
        if (dataReference.checked) result.push("data-reference")
        if (bufferOverflow.checked) result.push("buffer-overflow")
        if (entryId.checked) result.push("entry-id")
        if (confRev.checked) result.push("configuration-revision")
        if (segmentation.checked) result.push("segmentation")
        return result
    }

    function findDataSet(reference) {
        for (var i = 0; i < reports.dataSets.length; ++i) {
            if (reports.dataSets[i].reference === reference)
                return i
        }
        return reports.selectedDataSetIndex >= 0
            ? reports.selectedDataSetIndex : 0
    }

    function openForSelected() {
        if (reports.selectedRcbIndex < 0)
            return false
        var rcb = reports.selectedRcb
        dataSetBox.currentIndex = findDataSet(rcb.dataSet || "")

        var trg = rcb.triggerOptions || ""
        dchg.checked = containsName(trg, "data-change")
        qchg.checked = containsName(trg, "quality-change")
        dupd.checked = containsName(trg, "data-update")
        integrity.checked = containsName(trg, "integrity")
        giTrigger.checked = containsName(trg, "general-interrogation")

        var opt = rcb.optionalFields || ""
        seqNum.checked = containsName(opt, "sequence-number")
        timeStamp.checked = containsName(opt, "report-time-stamp")
        reason.checked = containsName(opt, "reason-for-inclusion")
        dataSetName.checked = containsName(opt, "data-set-name")
        dataReference.checked = containsName(opt, "data-reference")
        bufferOverflow.checked = containsName(opt, "buffer-overflow")
        entryId.checked = containsName(opt, "entry-id")
        confRev.checked = containsName(opt, "configuration-revision")
        segmentation.checked = containsName(opt, "segmentation")

        if (!trg.length) {
            dchg.checked = true
            qchg.checked = true
            integrity.checked = true
            giTrigger.checked = true
        }
        if (!opt.length) {
            seqNum.checked = true
            timeStamp.checked = true
            reason.checked = true
            dataSetName.checked = true
            dataReference.checked = true
            confRev.checked = true
        }
        root.open()
        return true
    }

    function requestEnable(gi) {
        pendingGi = gi
        if (gi && !giTrigger.checked)
            giTrigger.checked = true
        confirmEnable.open()
    }

    function executeEnable() {
        var ds = selectedDataSetMap()
        if (!ds || !ds.reference)
            return
        var accepted = reports.enableSelectedAuthored(
            ds.reference,
            triggerNames(),
            optionalNames(),
            pendingGi)
        if (accepted)
            root.close()
    }

    contentItem: ScrollView {
        contentWidth: availableWidth

        ColumnLayout {
            width: Math.max(0, parent.width - 30)
            x: 15
            spacing: 10

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 76
                radius: 6
                color: theme.surface
                border.width: 1
                border.color: theme.lineSoft

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 2
                    Label {
                        Layout.fillWidth: true
                        text: reports.selectedRcb.reference || "—"
                        color: theme.text
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        elide: Text.ElideMiddle
                    }
                    Label {
                        Layout.fillWidth: true
                        text: (reports.selectedRcb.mode || "RCB")
                              + " · currently " + (reports.selectedRcb.dataSet || "no DataSet")
                        color: theme.muted
                        font.pixelSize: 8
                        elide: Text.ElideMiddle
                    }
                }
            }

            Label {
                text: "DataSet binding"
                color: theme.text
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }

            ComboBox {
                id: dataSetBox
                Layout.fillWidth: true
                model: reports.dataSets
                textRole: "reference"
                valueRole: "reference"
                enabled: !reports.busy && !reports.active
                delegate: ItemDelegate {
                    required property var modelData
                    width: dataSetBox.width
                    text: (modelData.dynamicOwned === true ? "DYNAMIC · " : "STATIC · ")
                          + (modelData.reference || "—")
                          + " · " + (modelData.memberCount || 0) + " member(s)"
                }
            }

            Rectangle {
                visible: !root.bindingAllowed()
                Layout.fillWidth: true
                implicitHeight: bindingWarning.implicitHeight + 16
                radius: 5
                color: theme.redSoft
                border.width: 1
                border.color: theme.red
                Label {
                    id: bindingWarning
                    anchors.fill: parent
                    anchors.margins: 8
                    text: "Binding to a different static/non-owned DataSet is blocked. "
                          + "Only an association-owned dynamic DataSet may replace RCB DatSet."
                    color: theme.red
                    font.pixelSize: 8
                    wrapMode: Text.WordWrap
                }
            }

            Label {
                text: "Trigger Options (TrgOps)"
                color: theme.text
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 3
                CheckBox { id: dchg; text: "Data change"; enabled: !reports.busy }
                CheckBox { id: qchg; text: "Quality change"; enabled: !reports.busy }
                CheckBox { id: dupd; text: "Data update"; enabled: !reports.busy }
                CheckBox { id: integrity; text: "Integrity"; enabled: !reports.busy }
                CheckBox { id: giTrigger; text: "General interrogation"; enabled: !reports.busy }
            }

            Label {
                text: "Optional Fields (OptFlds)"
                color: theme.text
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 3
                CheckBox { id: seqNum; text: "Sequence number"; enabled: !reports.busy }
                CheckBox { id: timeStamp; text: "Report timestamp"; enabled: !reports.busy }
                CheckBox { id: reason; text: "Reason for inclusion"; enabled: !reports.busy }
                CheckBox { id: dataSetName; text: "DataSet name"; enabled: !reports.busy }
                CheckBox { id: dataReference; text: "Data reference"; enabled: !reports.busy }
                CheckBox { id: bufferOverflow; text: "Buffer overflow"; enabled: !reports.busy }
                CheckBox { id: entryId; text: "EntryID"; enabled: !reports.busy }
                CheckBox { id: confRev; text: "Configuration revision"; enabled: !reports.busy }
                CheckBox { id: segmentation; text: "Segmentation"; enabled: !reports.busy }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: note.implicitHeight + 16
                radius: 5
                color: theme.surface
                border.width: 1
                border.color: theme.lineSoft
                Label {
                    id: note
                    anchors.fill: parent
                    anchors.margins: 8
                    text: "Draft-only until Enable is confirmed. Static binding never rewrites DatSet. "
                          + "For an owned dynamic DataSet, DatSet + TrgOps + OptFlds are written only through "
                          + "the proven report runtime, read back, and only then RptEna is written."
                    color: theme.muted
                    font.pixelSize: 8
                    wrapMode: Text.WordWrap
                }
            }

            Rectangle {
                visible: reports.lastError.length > 0
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
                    text: reports.lastError
                    color: theme.red
                    font.pixelSize: 8
                    wrapMode: Text.WordWrap
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: reports.busy ? "Working…" : "Enable authored…"
                    enabled: reports.connected && !reports.busy && !reports.active
                             && root.bindingAllowed()
                             && dataSetBox.currentIndex >= 0
                    onClicked: root.requestEnable(false)
                }
                Button {
                    text: reports.busy ? "Working…" : "Enable authored + GI…"
                    enabled: reports.connected && !reports.busy && !reports.active
                             && root.bindingAllowed()
                             && dataSetBox.currentIndex >= 0
                    onClicked: root.requestEnable(true)
                }
            }
        }
    }

    Dialog {
        id: confirmEnable
        title: "Confirm authored RCB enable"
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.executeEnable()
        contentItem: Label {
            width: 500
            text: {
                var ds = root.selectedDataSetMap()
                return "Apply authored configuration to "
                    + (reports.selectedRcb.reference || "selected RCB")
                    + " using " + (ds.reference || "—")
                    + (root.pendingGi ? " and request GI?" : "?")
                    + "\n\nNo automatic retry is performed."
            }
            color: theme.text
            font.pixelSize: 9
            wrapMode: Text.WordWrap
        }
    }
}
