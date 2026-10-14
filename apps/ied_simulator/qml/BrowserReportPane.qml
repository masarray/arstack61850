// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Browser-only presentation of the existing MMS report service.
// Engineering inventory is not proof of a successful live read or report.
Rectangle {
    id: root
    required property var theme
    required property var reports

    readonly property var rcb: reports.selectedRcb
    readonly property bool hasSelection: reports.selectedRcbIndex >= 0
    readonly property var latestReport: newestMatchingReport()
    readonly property bool hasLastReport: latestReport.reportId !== undefined
    readonly property var configuredMembers: membersForSelectedRcb()
    property bool attributesExpanded: true
    property bool informationExpanded: true
    property bool dataExpanded: true
    color: theme.background

    function asText(value) {
        return value === null || value === undefined || String(value).length === 0
               ? "—" : String(value)
    }

    function rcbName() {
        return rcb.name || (rcb.reference ? String(rcb.reference).split(".").pop() : "Report control block")
    }

    function newestMatchingReport() {
        // A DataSet can be shared by multiple RCBs. Never attribute a received
        // frame to an RCB using only its DataSet or its list position.
        if (!hasSelection || !rcb.reportId || !reports.receivedReports)
            return ({})
        const wanted = String(rcb.reportId)
        for (let index = 0; index < reports.receivedReports.length; ++index) {
            const frame = reports.receivedReports[index]
            if (frame.reportId && String(frame.reportId) === wanted)
                return frame
        }
        return ({})
    }

    function membersForSelectedRcb() {
        const reference = hasSelection && rcb.dataSet ? String(rcb.dataSet) : ""
        if (!reference || !reports.dataSets)
            return []
        for (let index = 0; index < reports.dataSets.length; ++index) {
            const entry = reports.dataSets[index]
            if (String(entry.reference || "") === reference)
                return entry.members || []
        }
        return []
    }

    function memberName(raw) {
        const text = String(raw)
        const at = text.indexOf(" = ")
        return at >= 0 ? text.slice(0, at) : text
    }

    function memberValue(raw) {
        const text = String(raw)
        const at = text.indexOf(" = ")
        return at >= 0 ? text.slice(at + 3) : "—"
    }

    component PropertyRow: Rectangle {
        required property string title
        property var rawValue
        property int stripe: 0
        implicitHeight: 25
        Layout.fillWidth: true
        color: stripe % 2 ? root.theme.surfaceSoft : root.theme.surface
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: Math.min(228, Math.max(136, parent.width * 0.34))
            color: root.theme.chrome
            opacity: 0.4
        }
        Rectangle {
            x: Math.min(228, Math.max(136, parent.width * 0.34))
            width: 1
            height: parent.height
            color: root.theme.lineSoft
        }
        Label {
            width: Math.min(228, Math.max(136, parent.width * 0.34)) - 10
            height: parent.height
            x: 7
            verticalAlignment: Text.AlignVCenter
            text: title
            color: root.theme.textSoft
            font.pixelSize: 10
            elide: Text.ElideRight
            ToolTip.visible: titleMouse.containsMouse && truncated
            ToolTip.text: title
            MouseArea { id: titleMouse; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
        }
        Label {
            x: Math.min(228, Math.max(136, parent.width * 0.34)) + 8
            width: Math.max(0, parent.width - x - 9)
            height: parent.height
            verticalAlignment: Text.AlignVCenter
            text: root.asText(rawValue)
            color: root.theme.text
            font.pixelSize: 10
            elide: Text.ElideMiddle
            ToolTip.visible: valueMouse.containsMouse && truncated
            ToolTip.text: text
            MouseArea { id: valueMouse; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: root.theme.lineSoft }
    }

    component SectionHeading: Rectangle {
        required property string heading
        property string evidence: ""
        property bool opened: true
        signal toggleRequested()
        Layout.fillWidth: true
        implicitHeight: 26
        color: root.theme.surfaceRaised
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: root.theme.line }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 7
            Label {
                text: opened ? "▾" : "▸"
                font.pixelSize: 10
                color: root.theme.textSoft
            }
            Label {
                text: heading
                font.pixelSize: 10
                color: root.theme.text
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            Label {
                visible: evidence.length > 0
                text: evidence
                font.pixelSize: 9
                color: root.theme.muted
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: toggleRequested()
        }
    }

    component DataRow: Rectangle {
        required property string reference
        property string displayValue: "—"
        property int stripe: 0
        Layout.fillWidth: true
        implicitHeight: 25
        color: stripe % 2 ? root.theme.surfaceSoft : root.theme.surface
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 9
            anchors.rightMargin: 7
            spacing: 0
            Label {
                text: "DA"
                Layout.preferredWidth: 25
                color: root.theme.accent
                font.pixelSize: 9
                font.weight: Font.DemiBold
            }
            Label {
                text: reference
                Layout.preferredWidth: Math.max(100, parent.width * 0.55 - 28)
                color: root.theme.text
                font.pixelSize: 10
                elide: Text.ElideMiddle
                ToolTip.visible: dataNameMouse.containsMouse && truncated
                ToolTip.text: text
                MouseArea { id: dataNameMouse; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
            }
            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: root.theme.lineSoft }
            Label {
                Layout.fillWidth: true
                leftPadding: 8
                text: displayValue
                color: root.theme.text
                font.pixelSize: 10
                elide: Text.ElideMiddle
                ToolTip.visible: dataValueMouse.containsMouse && truncated
                ToolTip.text: text
                MouseArea { id: dataValueMouse; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: root.theme.lineSoft }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 29
            color: theme.surface
            border.width: 1
            border.color: theme.lineSoft
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 7
                Rectangle {
                    implicitWidth: 20; implicitHeight: 17; radius: 2
                    color: theme.accent
                    Label { anchors.centerIn: parent; text: "R"; color: "#ffffff"; font.pixelSize: 10; font.weight: Font.DemiBold }
                }
                Label {
                    Layout.fillWidth: true
                    text: root.hasSelection ? root.rcbName() : "Reports"
                    color: theme.text
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    elide: Text.ElideMiddle
                }
                Label {
                    text: !root.hasSelection ? reports.reportControls.length + " RCB(s)"
                          : reports.cleanupRequired ? "CLEANUP REQUIRED"
                          : reports.active ? "SUBSCRIBED"
                          : root.rcb.probeOk === true ? "VERIFIED" : "ENGINEERING"
                    color: reports.cleanupRequired ? theme.red
                           : reports.active ? theme.green : theme.muted
                    font.pixelSize: 9
                }
            }
        }

        ListView {
            objectName: "browserReportsOverview"
            visible: !root.hasSelection
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            reuseItems: true
            model: reports.reportControls
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                required property int index
                required property var modelData
                width: ListView.view.width
                height: 25
                color: reportMouse.containsMouse ? theme.accentSoft
                       : index % 2 ? theme.surfaceSoft : theme.surface
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8
                    Label { text: modelData.buffered ? "BR" : "UR"; color: theme.accent; font.pixelSize: 9; Layout.preferredWidth: 22 }
                    Label { text: modelData.name || modelData.reference || "RCB"; Layout.fillWidth: true; font.pixelSize: 10; color: theme.text; elide: Text.ElideMiddle }
                    Label { text: modelData.logicalNode || "LLN0"; font.pixelSize: 9; color: theme.muted }
                }
                MouseArea { id: reportMouse; anchors.fill: parent; hoverEnabled: true; onClicked: reports.selectRcb(index) }
            }
        }

        ScrollView {
            id: detailScroll
            objectName: "browserReportUnifiedInspector"
            visible: root.hasSelection
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: Math.max(0, detailScroll.availableWidth)
                spacing: 0

                SectionHeading {
                    heading: "Control Block Attributes"
                    evidence: root.rcb.probeOk === true ? "IED verified" : "Engineering model"
                    opened: root.attributesExpanded
                    onToggleRequested: root.attributesExpanded = !root.attributesExpanded
                }
                ColumnLayout {
                    visible: root.attributesExpanded
                    Layout.fillWidth: true
                    spacing: 0
                    Repeater {
                        model: [
                            ["Enabled", root.rcb.enabled],
                            ["Reserved", root.rcb.reserved],
                            ["Control Block Reference", root.rcb.reference],
                            ["Report ID", root.rcb.reportId],
                            ["DataSet Reference", root.rcb.dataSet],
                            ["Trigger Options", root.rcb.triggerOptions],
                            ["Buffer Time (ms)", root.rcb.bufTm],
                            ["Configuration Revision", root.rcb.confRev],
                            ["Integrity Period (ms)", root.rcb.intgPd],
                            ["Sequence Number", root.rcb.sqNum],
                            ["Reservation Time (s)", root.rcb.resvTms],
                            ["Owner", root.rcb.owner]
                        ]
                        delegate: PropertyRow {
                            title: modelData[0]
                            rawValue: modelData[1]
                            stripe: index
                        }
                    }
                }

                SectionHeading {
                    heading: "Information Received in Last Report"
                    evidence: root.hasLastReport ? "Received" : "Not received"
                    opened: root.informationExpanded
                    onToggleRequested: root.informationExpanded = !root.informationExpanded
                }
                ColumnLayout {
                    visible: root.informationExpanded
                    Layout.fillWidth: true
                    spacing: 0
                    Repeater {
                        model: [
                            ["Report ID", root.hasLastReport ? root.latestReport.reportId : undefined],
                            ["Sequence Number", root.hasLastReport ? root.latestReport.sequence : undefined],
                            ["Subsequence Number", root.hasLastReport ? root.latestReport.subSequence : undefined],
                            ["Configuration Revision", root.hasLastReport ? root.latestReport.confRev : undefined],
                            ["Entry ID", root.hasLastReport ? root.latestReport.entryId : undefined],
                            ["Decoder", root.hasLastReport ? root.latestReport.decoderMode : undefined]
                        ]
                        delegate: PropertyRow { title: modelData[0]; rawValue: modelData[1]; stripe: index }
                    }
                }

                SectionHeading {
                    heading: "Data"
                    evidence: root.hasLastReport
                              ? root.latestReport.valueCount + " received value(s)"
                              : root.configuredMembers.length + " configured member(s)"
                    opened: root.dataExpanded
                    onToggleRequested: root.dataExpanded = !root.dataExpanded
                }
                ColumnLayout {
                    visible: root.dataExpanded
                    Layout.fillWidth: true
                    spacing: 0
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 25
                        color: theme.chrome
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            Label { text: "Name"; Layout.preferredWidth: Math.max(100, parent.width * 0.55 - 1); color: theme.textSoft; font.pixelSize: 10; font.weight: Font.DemiBold }
                            Label { text: "Value"; Layout.fillWidth: true; color: theme.textSoft; font.pixelSize: 10; font.weight: Font.DemiBold }
                        }
                    }
                    Repeater {
                        model: root.hasLastReport && root.latestReport.values
                               ? root.latestReport.values.slice(0, 256) : []
                        delegate: DataRow {
                            reference: root.memberName(modelData)
                            displayValue: root.memberValue(modelData)
                            stripe: index
                        }
                    }
                    Repeater {
                        model: !root.hasLastReport ? root.configuredMembers.slice(0, 256) : []
                        delegate: DataRow {
                            reference: String(modelData)
                            displayValue: "—"
                            stripe: index
                        }
                    }
                    Label {
                        visible: root.hasLastReport ? !root.latestReport.values || root.latestReport.values.length === 0
                                                    : root.configuredMembers.length === 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        leftPadding: 10
                        verticalAlignment: Text.AlignVCenter
                        text: root.hasLastReport
                              ? "The report contains no exposed member values."
                              : "No matched DataSet members. Offline values are not fabricated."
                        color: theme.muted
                        font.pixelSize: 10
                    }
                    Label {
                        visible: root.hasLastReport ? root.latestReport.values.length > 256
                                                    : root.configuredMembers.length > 256
                        Layout.fillWidth: true
                        leftPadding: 10
                        text: "Showing first 256 members. Narrow the context for more."
                        color: theme.muted
                        font.pixelSize: 9
                    }
                }

                Rectangle {
                    visible: reports.cleanupRequired || (reports.lastError && reports.lastError.length > 0)
                    Layout.fillWidth: true
                    implicitHeight: issueLabel.implicitHeight + 16
                    color: theme.redSoft
                    Label {
                        id: issueLabel
                        anchors.fill: parent
                        anchors.margins: 8
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                        color: theme.red
                        text: reports.cleanupRequired
                              ? "RCB cleanup is unconfirmed. Verify ownership before retrying."
                              : reports.lastError
                    }
                }
            }
        }
    }
}
