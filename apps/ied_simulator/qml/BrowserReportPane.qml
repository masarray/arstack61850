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
    readonly property bool selectedIsSubscribed: hasSelection && reports.active
        && reports.subscriptionRcbReference === rcb.reference
    readonly property bool selectedNeedsCleanup: hasSelection && reports.cleanupRequired
        && reports.subscriptionRcbReference === rcb.reference
    readonly property var latestReport: newestMatchingReport()
    readonly property bool hasLastReport: latestReport.reportId !== undefined
    readonly property var configuredMembers: membersForSelectedRcb()
    property bool attributesExpanded: true
    // Readable process values first; secondary bit tables and diagnostics
    // remain one click away, rather than consuming the opening viewport.
    property bool triggerExpanded: false
    property bool optionalExpanded: false
    property bool informationExpanded: false
    property bool dataExpanded: true
    property bool eventsExpanded: false
    property int selectedReportMemberIndex: -1
    readonly property int selectedRcbRow: reports.selectedRcbIndex
    onSelectedRcbRowChanged: {
        if (detailScroll.contentItem) detailScroll.contentItem.contentY = 0
        selectedReportMemberIndex = -1
    }
    // Preserve selection through repeat snapshots of the same report frame;
    // reset only when the actual ReportID/sequence/entry identity changes.
    readonly property string latestReportIdentity: !hasLastReport ? "" : [
        latestReport.rcbReference || "", latestReport.reportId || "", latestReport.sequence || "",
        latestReport.subSequence || "", latestReport.entryId || ""
    ].join("|")
    // A different indexed RCB can legitimately emit the same RptID/SqNum.
    // Reset member selection when provenance changes, not only wire sequence.
    onLatestReportIdentityChanged: selectedReportMemberIndex = -1
    function selectedReportMemberEvidence() {
        if (!hasLastReport || selectedReportMemberIndex < 0 ||
            !latestReport.valueEvidence) return ({})
        const entries = latestReport.valueEvidence
        for (let index = 0; index < entries.length; ++index) {
            const entry = entries[index]
            if (entry.index === selectedReportMemberIndex)
                return entry.evidence || ({})
        }
        return ({})
    }
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
        if (!hasSelection || !rcb.reference || !reports.receivedReports)
            return ({})
        const expectedRcb = String(rcb.reference)
        const expectedRptId = rcb.reportId ? String(rcb.reportId) : ""
        for (let index = 0; index < reports.receivedReports.length; ++index) {
            const frame = reports.receivedReports[index]
            // Do not attribute a frame by RptID alone: indexed RCBs can share it.
            if (!frame.rcbReference || String(frame.rcbReference) !== expectedRcb)
                continue
            if (expectedRptId.length > 0 && String(frame.reportId) !== expectedRptId)
                continue
            return frame
        }
        return ({})
    }

    function membersForSelectedRcb() {
        // Resolve through the canonical C++ DataSet matcher, never by a
        // potentially unrelated user-selected DataSet index.
        return root.hasSelection && root.rcb.dataSet
               ? reports.selectedRcbDataSetMembers : []
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

    readonly property var attributeLabels: [
        "Enabled", "Reserved", "Control Block Reference", "Report ID",
        "DataSet Reference", "Trigger Options", "Buffer Time (ms)",
        "Configuration Revision", "Integrity Period (ms)", "Sequence Number",
        "Reservation Time (s)", "Owner"
    ]
    // Exact IEC 61850 TrgOps / OptFlds bit names from the native field codec.
    // These are read-only observations. Authoring stays behind the explicit
    // guarded confirmation dialog in the Browser ribbon.
    readonly property var triggerFields: [
        { label: "Data change", name: "data-change" },
        { label: "Quality change", name: "quality-change" },
        { label: "Data update", name: "data-update" },
        { label: "Integrity", name: "integrity" },
        { label: "General interrogation", name: "general-interrogation" }
    ]
    readonly property var optionalFields: [
        { label: "Sequence number", name: "sequence-number" },
        { label: "Report timestamp", name: "report-time-stamp" },
        { label: "Reason for inclusion", name: "reason-for-inclusion" },
        { label: "DataSet name", name: "data-set-name" },
        { label: "Data reference", name: "data-reference" },
        { label: "Buffer overflow", name: "buffer-overflow" },
        { label: "Entry ID", name: "entry-id" },
        { label: "Configuration revision", name: "configuration-revision" },
        { label: "Segmentation", name: "segmentation" }
    ]

    function observedRcbInUse() {
        // Availability classification may be "dataset-unreadable" when a
        // bounded RCB Read succeeds without a DataSet-directory probe.
        // Positive verified RptEna, Resv or Owner evidence still forbids a
        // misleading "available" banner. Never infer other-client identity.
        return rcb.probeOk === true
               && (rcb.enabled === "true"
                   || rcb.reserved === "true"
                   || (rcb.owner && String(rcb.owner).length > 0))
    }

    function readStatusLabel(raw) {
        switch (String(raw || "not-read")) {
        case "verified": return "IED Read verified"
        case "not-exposed": return "Not exposed by IED"
        case "read-failed": return "IED Read failed"
        default: return "Not read"
        }
    }

    readonly property var informationLabels: [
        "Report ID", "Sequence Number", "Subsequence Number",
        "Configuration Revision", "Entry ID", "Decoder"
    ]

    function attributeValue(index) {
        switch (index) {
        case 0: return rcb.enabled
        case 1: return rcb.reserved
        case 2: return rcb.reference
        case 3: return rcb.reportId
        case 4: return rcb.dataSet
        case 5: return rcb.triggerOptions
        case 6: return rcb.bufTm
        case 7: return rcb.confRev
        case 8: return rcb.intgPd
        case 9: return rcb.sqNum
        case 10: return rcb.resvTms
        case 11: return rcb.owner
        default: return undefined
        }
    }

    function informationValue(index) {
        if (!hasLastReport) return undefined
        switch (index) {
        case 0: return latestReport.reportId
        case 1: return latestReport.sequence
        case 2: return latestReport.subSequence
        case 3: return latestReport.confRev
        case 4: return latestReport.entryId
        case 5: return latestReport.decoderMode
        default: return undefined
        }
    }

    component PropertyRow: Rectangle {
        id: propertyRow
        // Repeater supplies these model roles explicitly; lexical 'index'
        // in an inline component otherwise resolves to row zero.
        required property int index
        property bool lastReportSection: false
        readonly property string title: lastReportSection
                                       ? root.informationLabels[index] : root.attributeLabels[index]
        readonly property var rawValue: lastReportSection
                                        ? root.informationValue(index) : root.attributeValue(index)
        readonly property int stripe: index
        implicitHeight: 30
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
            text: propertyRow.title
            color: root.theme.textSoft
            font.pixelSize: 12
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
            font.pixelSize: 12
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
        implicitHeight: 30
        color: root.theme.surfaceRaised
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: root.theme.line }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 7
            Label {
                text: opened ? "▾" : "▸"
                font.pixelSize: 12
                color: root.theme.textSoft
            }
            Label {
                text: heading
                font.pixelSize: 12
                color: root.theme.text
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            Label {
                visible: evidence.length > 0
                text: evidence
                font.pixelSize: 11
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
        id: dataRow
        required property int index
        required property var modelData
        property bool received: false
        readonly property string reference: received
                                            ? root.memberName(modelData) : String(modelData)
        readonly property string displayValue: received
                                               ? root.memberValue(modelData) : "—"
        readonly property int stripe: index
        Layout.fillWidth: true
        implicitHeight: 30
        color: received && root.selectedReportMemberIndex === index
               ? root.theme.accentSoft
               : stripe % 2 ? root.theme.surfaceSoft : root.theme.surface
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 9
            anchors.rightMargin: 7
            spacing: 0
            Label {
                text: "DA"
                Layout.preferredWidth: 25
                color: root.theme.accent
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
            Label {
                text: dataRow.reference
                Layout.preferredWidth: Math.max(100, parent.width * 0.55 - 28)
                color: root.theme.text
                font.pixelSize: 12
                elide: Text.ElideMiddle
                ToolTip.visible: dataNameMouse.containsMouse && truncated
                ToolTip.text: text
                MouseArea { id: dataNameMouse; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
            }
            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: root.theme.lineSoft }
            Label {
                Layout.fillWidth: true
                leftPadding: 8
                text: dataRow.displayValue
                color: root.theme.text
                font.pixelSize: 12
                elide: Text.ElideMiddle
                ToolTip.visible: dataValueMouse.containsMouse && truncated
                ToolTip.text: text
                MouseArea { id: dataValueMouse; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
            }
        }
        MouseArea {
            anchors.fill: parent
            enabled: dataRow.received
            cursorShape: Qt.PointingHandCursor
            onClicked: root.selectedReportMemberIndex = dataRow.index
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: root.theme.lineSoft }
    }

    // Presentation-only tree over authoritative MMS-decoded child values.
    // Unknown child types retain positional names; SCL labels are never guessed.
    component TreeDataRow: ColumnLayout {
        id: treeRow
        required property int index
        required property var modelData
        readonly property bool topLevel: modelData.depth === 0
        readonly property bool expanded: root.selectedReportMemberIndex === modelData.memberIndex
        visible: topLevel || expanded
        Layout.fillWidth: true
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 32
            color: treeRow.topLevel && treeRow.expanded
                   ? root.theme.accentSoft
                   : treeRow.index % 2 ? root.theme.surfaceSoft : root.theme.surface
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 9 + Math.min(treeRow.modelData.depth, 4) * 16
                anchors.rightMargin: 8
                spacing: 6
                Label {
                    Layout.preferredWidth: 21
                    text: treeRow.topLevel
                          ? (treeRow.modelData.container
                             ? (treeRow.expanded ? "▾" : "▸") : "DA")
                          : "·"
                    color: root.theme.accent
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.preferredWidth: Math.max(100, parent.width * 0.49 - 35)
                    text: treeRow.modelData.name || "—"
                    color: root.theme.text
                    font.pixelSize: 12
                    font.weight: treeRow.topLevel ? Font.DemiBold : Font.Normal
                    elide: Text.ElideMiddle
                    ToolTip.visible: treeNameHover.containsMouse && truncated
                    ToolTip.text: treeRow.modelData.reference || text
                    MouseArea {
                        id: treeNameHover
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: treeRow.modelData.value || "—"
                    color: treeRow.modelData.received ? root.theme.text : root.theme.muted
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    ToolTip.visible: treeValueHover.containsMouse && truncated
                    ToolTip.text: text
                    MouseArea {
                        id: treeValueHover
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }
                }
                Label {
                    visible: !treeRow.topLevel
                    Layout.preferredWidth: 66
                    text: treeRow.modelData.kind || ""
                    color: root.theme.muted
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignRight
                }
            }
            MouseArea {
                anchors.fill: parent
                enabled: treeRow.topLevel
                cursorShape: Qt.PointingHandCursor
                onClicked: root.selectedReportMemberIndex = treeRow.expanded
                           ? -1 : treeRow.modelData.memberIndex
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: root.theme.lineSoft
            }
        }
        Label {
            visible: treeRow.topLevel && treeRow.expanded &&
                     Boolean(treeRow.modelData.reason)
            Layout.fillWidth: true
            leftPadding: 32
            topPadding: 4
            bottomPadding: 4
            color: root.theme.textSoft
            font.pixelSize: 11
            text: "Reason for inclusion: " + (treeRow.modelData.reason || "")
        }
        BrowserEngineeringEvidence {
            theme: root.theme
            // Native tree row carries exactly this member's typed decoder
            // evidence, so quality flags/time-quality stay visible on click.
            engineeringEvidence: treeRow.topLevel && treeRow.expanded
                                 ? treeRow.modelData.typedEvidence || ({}) : ({})
        }
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
                    Label { anchors.centerIn: parent; text: "R"; color: "#ffffff"; font.pixelSize: 12; font.weight: Font.DemiBold }
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
                          : reports.cleanupRequired
                            ? (root.selectedNeedsCleanup ? "CLEANUP REQUIRED" : "CLEANUP OTHER RCB")
                          : reports.active
                            ? (root.selectedIsSubscribed ? "SUBSCRIBED" : "OTHER RCB ACTIVE")
                          : root.rcb.probeOk === true ? "VERIFIED" : "ENGINEERING"
                    color: reports.cleanupRequired ? theme.red
                           : root.selectedIsSubscribed ? theme.green : theme.muted
                    font.pixelSize: 11
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
                    Label { text: modelData.buffered ? "BR" : "UR"; color: theme.accent; font.pixelSize: 11; Layout.preferredWidth: 22 }
                    Label { text: modelData.name || modelData.reference || "RCB"; Layout.fillWidth: true; font.pixelSize: 12; color: theme.text; elide: Text.ElideMiddle }
                    Label { text: modelData.logicalNode || "LLN0"; font.pixelSize: 11; color: theme.muted }
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

                Label {
                    visible: root.hasSelection && reports.active && !root.selectedIsSubscribed
                    Layout.fillWidth: true
                    leftPadding: 9
                    rightPadding: 9
                    topPadding: 5
                    bottomPadding: 5
                    color: theme.muted
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    text: "Another RCB is actively subscribed on this MMS association: "
                          + reports.subscriptionRcbReference
                          + ". The selected RCB is inspection-only; Stop active RCB acts on "
                          + "the recorded subscription, not this selection."
                }

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
                        model: root.attributeLabels.length
                        delegate: PropertyRow { lastReportSection: false }
                    }
                }

                SectionHeading {
                    heading: "Trigger Options (TrgOps)"
                    evidence: root.readStatusLabel(root.rcb.triggerOptionsReadStatus)
                    opened: root.triggerExpanded
                    onToggleRequested: root.triggerExpanded = !root.triggerExpanded
                }
                ColumnLayout {
                    visible: root.triggerExpanded
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        visible: root.rcb.triggerOptionsReadStatus !== "verified"
                        Layout.fillWidth: true
                        leftPadding: 10
                        topPadding: 6
                        bottomPadding: 6
                        color: theme.muted
                        font.pixelSize: 12
                        text: "TrgOps " + root.readStatusLabel(root.rcb.triggerOptionsReadStatus)
                              + " · no on/off bit state is inferred."
                    }
                    GridLayout {
                        visible: root.rcb.triggerOptionsReadStatus === "verified"
                        Layout.fillWidth: true
                        columns: width >= 550 ? 3 : width >= 330 ? 2 : 1
                        rowSpacing: 0
                        columnSpacing: 8
                        Repeater {
                            model: root.triggerFields
                            delegate: RowLayout {
                                id: bitRow
                                required property var modelData
                                readonly property bool bitSet: root.rcb.triggerOptionNames
                                    && root.rcb.triggerOptionNames.indexOf(modelData.name) >= 0
                                Layout.fillWidth: true
                                Layout.minimumHeight: 30
                                spacing: 6
                                // Passive bit observation: no CheckBox/MouseArea and
                                // no disabled-text styling or MMS Write affordance.
                                Rectangle {
                                    Layout.preferredWidth: 16
                                    Layout.preferredHeight: 16
                                    radius: 3
                                    color: bitRow.bitSet ? theme.accentSoft : theme.surface
                                    border.width: 1
                                    border.color: bitRow.bitSet ? theme.accent : theme.line
                                    Label {
                                        anchors.centerIn: parent
                                        text: "✓"
                                        visible: bitRow.bitSet
                                        color: theme.accent
                                        font.pixelSize: 12
                                        font.weight: Font.Bold
                                    }
                                }
                                Label {
                                    text: modelData.label
                                    color: theme.text
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }

                SectionHeading {
                    heading: "Optional Fields (OptFlds)"
                    evidence: root.readStatusLabel(root.rcb.optionalFieldsReadStatus)
                    opened: root.optionalExpanded
                    onToggleRequested: root.optionalExpanded = !root.optionalExpanded
                }
                ColumnLayout {
                    visible: root.optionalExpanded
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        visible: root.rcb.optionalFieldsReadStatus !== "verified"
                        Layout.fillWidth: true
                        leftPadding: 10
                        topPadding: 6
                        bottomPadding: 6
                        color: theme.muted
                        font.pixelSize: 12
                        text: "OptFlds " + root.readStatusLabel(root.rcb.optionalFieldsReadStatus)
                              + " · no on/off bit state is inferred."
                    }
                    GridLayout {
                        visible: root.rcb.optionalFieldsReadStatus === "verified"
                        Layout.fillWidth: true
                        columns: width >= 550 ? 3 : width >= 330 ? 2 : 1
                        rowSpacing: 0
                        columnSpacing: 8
                        Repeater {
                            model: root.optionalFields
                            delegate: RowLayout {
                                id: bitRow
                                required property var modelData
                                readonly property bool bitSet: root.rcb.optionalFieldNames
                                    && root.rcb.optionalFieldNames.indexOf(modelData.name) >= 0
                                Layout.fillWidth: true
                                Layout.minimumHeight: 30
                                spacing: 6
                                // Passive bit observation: no CheckBox/MouseArea and
                                // no disabled-text styling or MMS Write affordance.
                                Rectangle {
                                    Layout.preferredWidth: 16
                                    Layout.preferredHeight: 16
                                    radius: 3
                                    color: bitRow.bitSet ? theme.accentSoft : theme.surface
                                    border.width: 1
                                    border.color: bitRow.bitSet ? theme.accent : theme.line
                                    Label {
                                        anchors.centerIn: parent
                                        text: "✓"
                                        visible: bitRow.bitSet
                                        color: theme.accent
                                        font.pixelSize: 12
                                        font.weight: Font.Bold
                                    }
                                }
                                Label {
                                    text: modelData.label
                                    color: theme.text
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: rcbAvailabilityLabel.implicitHeight + 14
                    color: theme.surface
                    Label {
                        id: rcbAvailabilityLabel
                        anchors.fill: parent
                        anchors.margins: 7
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                        color: root.observedRcbInUse() ? theme.red : theme.muted
                        text: root.rcb.probeOk !== true
                              ? "RCB live state not verified · never infer an unused reservation."
                              : root.observedRcbInUse()
                                ? "RCB in use: enabled, reservation or Owner evidence is present. Do not take over another client."
                                : root.rcb.availability === "available"
                                  ? "No in-use evidence in RCB readback. Revalidate ownership before enabling."
                                  : "RCB availability not verified: " + root.asText(root.rcb.availabilityReason)
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
                        model: root.informationLabels.length
                        delegate: PropertyRow { lastReportSection: true }
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
                        implicitHeight: 30
                        color: theme.chrome
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            Label { text: "Name"; Layout.preferredWidth: Math.max(100, parent.width * 0.55 - 1); color: theme.textSoft; font.pixelSize: 12; font.weight: Font.DemiBold }
                            Label { text: "Value"; Layout.fillWidth: true; color: theme.textSoft; font.pixelSize: 12; font.weight: Font.DemiBold }
                        }
                    }
                    Repeater {
                        model: root.hasLastReport && root.latestReport.valueTree
                               ? root.latestReport.valueTree : []
                        delegate: TreeDataRow {}
                    }
                    Repeater {
                        model: root.hasLastReport &&
                               (!root.latestReport.valueTree ||
                                root.latestReport.valueTree.length === 0)
                               && root.latestReport.values ? root.latestReport.values : []
                        delegate: DataRow { received: true }
                    }
                    Repeater {
                        model: !root.hasLastReport ? root.configuredMembers.slice(0, 256) : []
                        delegate: DataRow { received: false }
                    }
                    Label {
                        visible: root.hasLastReport &&
                                 root.latestReport.treeNestedTruncated === true
                        Layout.fillWidth: true
                        leftPadding: 9
                        topPadding: 3
                        bottomPadding: 3
                        text: "Nested values exceed the bounded preview. Native MMS frame retained."
                        color: theme.muted
                        font.pixelSize: 11
                    }
                    BrowserEngineeringEvidence {
                        theme: root.theme
                        engineeringEvidence: root.hasLastReport &&
                                             (!root.latestReport.valueTree ||
                                              root.latestReport.valueTree.length === 0)
                                             ? root.selectedReportMemberEvidence() : ({})
                    }
                    Label {
                        visible: root.hasLastReport &&
                                 root.latestReport.treeTypedEvidenceOmitted === true
                        Layout.fillWidth: true
                        leftPadding: 9
                        topPadding: 3
                        bottomPadding: 3
                        text: "Quality/TimeQuality details withheld: member identity was truncated."
                        color: theme.muted
                        font.pixelSize: 11
                    }
                    Label {
                        visible: root.hasLastReport &&
                                 (!root.latestReport.valueTree ||
                                  root.latestReport.valueTree.length === 0) &&
                                 root.latestReport.valueEvidenceTruncated === true
                        Layout.fillWidth: true
                        leftPadding: 9
                        text: "Some report q/t details were omitted by the bounded projection."
                        color: theme.muted
                        font.pixelSize: 11
                    }
                    Label {
                        visible: root.hasLastReport &&
                                 root.latestReport.valueTextTruncated === true
                        Layout.fillWidth: true
                        leftPadding: 9
                        text: "Some values are preview-truncated; the report payload is preserved by MMS runtime."
                        color: theme.muted
                        font.pixelSize: 11
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
                              : !root.rcb.dataSet
                                ? "No bound DataSet reference is available for this RCB."
                                : "The configured DataSet has no exposed members. Offline values are not fabricated."
                        color: theme.muted
                        font.pixelSize: 12
                    }
                    Label {
                        visible: root.hasLastReport ? root.latestReport.valuesTruncated === true
                                                    : root.configuredMembers.length > 256
                        Layout.fillWidth: true
                        leftPadding: 10
                        text: "Showing first 256 members. Narrow the context for more."
                        color: theme.muted
                        font.pixelSize: 11
                    }
                }

                SectionHeading {
                    heading: "RCB lifecycle & InformationReport events"
                    evidence: reports.events.length + " bounded event(s)"
                    opened: root.eventsExpanded
                    visible: reports.events.length > 0
                    onToggleRequested: root.eventsExpanded = !root.eventsExpanded
                }
                Repeater {
                    model: root.eventsExpanded
                           ? reports.events.slice(Math.max(0, reports.events.length - 6))
                           : []
                    delegate: Label {
                        required property string modelData
                        Layout.fillWidth: true
                        leftPadding: 9
                        rightPadding: 9
                        topPadding: 3
                        bottomPadding: 3
                        text: modelData
                        color: theme.textSoft
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }

                Rectangle {
                    visible: reports.rejectedReportCount > 0 || reports.reportDecodeFailureCount > 0
                    Layout.fillWidth: true
                    implicitHeight: reportRejections.implicitHeight + 16
                    color: theme.surface
                    border.width: 1
                    border.color: theme.amber
                    Label {
                        id: reportRejections
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 8
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        color: theme.amber
                        text: reports.rejectedReportCount + " foreign/stale report(s) withheld from process values · "
                              + reports.reportDecodeFailureCount + " report decode failure(s). "
                              + "Inspect report events for exact diagnostics."
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
                        font.pixelSize: 12
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
