// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var theme
    required property var context
    required property var client
    required property var reports

    readonly property int maximumEntries: 256
    readonly property int watchCount: watchModel.count
    signal watchSnapshotChanged()
    property int boundContextGeneration: context.contextGeneration
    property string statusMessage: "Add DataAttributes, DataSets, Reports, or configured GOOSE from the Browser."

    color: theme.background

    ListModel { id: watchModel }

    function snapshotRows() {
        var entries = []
        for (var i = 0; i < watchModel.count; ++i) {
            var row = watchModel.get(i)
            entries.push({
                "kind": row.kind,
                "reference": row.reference,
                "detail": row.detail,
                "value": row.value,
                "quality": row.quality,
                "timestamp": row.timestamp,
                "status": row.status
            })
        }
        return entries
    }

    function indexFor(kind, reference) {
        for (var i = 0; i < watchModel.count; ++i) {
            var row = watchModel.get(i)
            if (row.kind === kind && row.reference === reference)
                return i
        }
        return -1
    }

    function addEntry(kind, reference, detail, membersText) {
        if (!reference || reference.length === 0) {
            statusMessage = "The selected object has no canonical reference."
            return false
        }
        if (indexFor(kind, reference) >= 0) {
            statusMessage = reference + " is already monitored."
            return true
        }
        if (watchModel.count >= maximumEntries) {
            statusMessage = "Global Data is full (" + maximumEntries + " entries). Remove an item before adding another."
            return false
        }
        watchModel.append({
            "kind": kind,
            "reference": reference,
            "detail": detail || "",
            "membersText": membersText || "",
            "value": "",
            "quality": "",
            "timestamp": "",
            "status": context.online ? "READY" : "OFFLINE"
        })
        syncRows()
        statusMessage = "Added " + reference + " to Global Data."
        return true
    }

    function addSelectedData(node) {
        if (!node || node.kind !== "DA" || node.readable !== true) {
            statusMessage = "Global Data accepts a readable DataAttribute selection; structural nodes fail closed."
            return false
        }
        return addEntry(
            "DATA",
            node.reference || "",
            "FC " + (node.functionalConstraint || "—") + " · " + (node.mmsType || "unknown"),
            "")
    }

    function addDataSet(reference, members) {
        var list = members || []
        return addEntry(
            "DATASET",
            reference || "",
            String(list.length) + " canonical member(s)",
            list.join("\n"))
    }

    function addReport(rcb) {
        if (!rcb || !rcb.reference)
            return false
        return addEntry(
            "REPORT",
            rcb.reference,
            (rcb.mode || "RCB") + " · " + (rcb.dataSet || "no DataSet"),
            "")
    }

    function addGoose(stream) {
        if (!stream || !stream.reference)
            return false
        return addEntry(
            "GOOSE",
            stream.reference,
            (stream.dataSet || "configured stream") + (stream.appId ? " · AppID " + stream.appId : ""),
            "")
    }

    function removeAt(row) {
        if (row < 0 || row >= watchModel.count)
            return
        watchModel.remove(row)
        root.watchSnapshotChanged()
        statusMessage = watchModel.count > 0
            ? String(watchModel.count) + " monitored object(s)."
            : "Global Data is empty."
    }

    function clearAll() {
        watchModel.clear()
        root.watchSnapshotChanged()
        statusMessage = "Global Data cleared."
    }

    function refreshReferences() {
        var refs = []
        var seen = ({})
        for (var i = 0; i < watchModel.count; ++i) {
            var row = watchModel.get(i)
            var candidates = []
            if (row.kind === "DATA") {
                candidates = [row.reference]
            } else if (row.kind === "DATASET" && row.membersText.length > 0) {
                candidates = row.membersText.split("\n")
            }
            for (var j = 0; j < candidates.length; ++j) {
                var reference = candidates[j]
                if (reference.length === 0 || seen[reference])
                    continue
                seen[reference] = true
                refs.push(reference)
            }
        }
        return refs
    }

    function refreshWatched() {
        if (!client.connected) {
            statusMessage = "Offline: monitored engineering context is retained; connect explicitly before refreshing values."
            syncRows()
            return false
        }
        if (client.operationBusy) {
            statusMessage = "The MMS client is busy; no overlapping Global Data read was started."
            return false
        }
        var refs = refreshReferences()
        if (refs.length === 0) {
            statusMessage = "No monitored DataAttribute/DataSet members require an MMS Read."
            return false
        }
        var accepted = client.refreshEngineeringReferences(refs)
        statusMessage = accepted
            ? "Refreshing monitored values (bounded to 64 MMS targets for this action)."
            : (client.lastError.length ? client.lastError : "Global Data refresh was not started.")
        return accepted
    }

    function syncRows() {
        for (var i = 0; i < watchModel.count; ++i) {
            var row = watchModel.get(i)
            if (row.kind === "DATA") {
                var node = context.treeModel.nodeForReference(row.reference)
                watchModel.setProperty(i, "value", node.value || "")
                watchModel.setProperty(i, "quality", node.quality || "")
                watchModel.setProperty(i, "timestamp", node.timestamp || "")
                watchModel.setProperty(i, "status", context.online ? (node.readable ? "LIVE" : "UNRESOLVED") : "OFFLINE")
            } else if (row.kind === "DATASET") {
                var members = row.membersText.length > 0 ? row.membersText.split("\n").length : 0
                watchModel.setProperty(i, "status", context.online ? "READY" : "OFFLINE")
                watchModel.setProperty(i, "value", String(members) + " member(s)")
            } else if (row.kind === "REPORT") {
                watchModel.setProperty(i, "status", reports.active && reports.selectedRcb.reference === row.reference
                    ? "ACTIVE" : (context.online ? "CONFIGURED" : "OFFLINE"))
                watchModel.setProperty(i, "value", reports.receivedReportCount > 0
                    ? String(reports.receivedReportCount) + " received" : "")
            } else if (row.kind === "GOOSE") {
                watchModel.setProperty(i, "status", context.online ? "CONFIGURED" : "OFFLINE")
            }
        }
        root.watchSnapshotChanged()
    }

    Connections {
        target: context
        function onContextChanged() {
            if (root.boundContextGeneration !== context.contextGeneration) {
                watchModel.clear()
                root.boundContextGeneration = context.contextGeneration
                root.statusMessage = context.loaded
                    ? "New engineering context: Global Data starts empty."
                    : "No active engineering context."
            }
            root.syncRows()
        }
        function onRuntimeChanged() { root.syncRows() }
    }

    Connections {
        target: context.treeModel
        function onDataChanged() { root.syncRows() }
        function onSelectionChanged() { root.syncRows() }
    }

    Connections {
        target: reports
        function onReportsChanged() { root.syncRows() }
        function onStateChanged() { root.syncRows() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: theme.surface
            border.width: 1
            border.color: theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 10
                spacing: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        text: "Global Data"
                        color: theme.text
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root.statusMessage
                        color: theme.muted
                        font.pixelSize: 8
                        elide: Text.ElideRight
                    }
                }

                Label {
                    text: watchModel.count + " / " + root.maximumEntries
                    color: theme.muted
                    font.pixelSize: 8
                }
                Button {
                    text: client.operationBusy ? "Refreshing…" : "Refresh watched"
                    enabled: client.connected && !client.operationBusy && root.refreshReferences().length > 0
                    onClicked: root.refreshWatched()
                    ToolTip.visible: hovered
                    ToolTip.text: "Explicit bounded refresh; at most 64 MMS targets. No background polling."
                }
                Button {
                    text: "Clear"
                    enabled: watchModel.count > 0
                    onClicked: root.clearAll()
                }
            }
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Vertical

            ListView {
                id: watchList
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.minimumHeight: 180
                model: watchModel
                clip: true
                reuseItems: true
                spacing: 1
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    required property int index
                    required property string kind
                    required property string reference
                    required property string detail
                    required property string value
                    required property string quality
                    required property string timestamp
                    required property string status

                    width: watchList.width
                    height: 54
                    color: index % 2 ? theme.surfaceSoft : theme.surface
                    border.width: 1
                    border.color: theme.lineSoft

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 9
                        anchors.rightMargin: 8
                        spacing: 8

                        Rectangle {
                            Layout.preferredWidth: 50
                            Layout.preferredHeight: 20
                            radius: 4
                            color: theme.surfaceRaised
                            Label {
                                anchors.centerIn: parent
                                text: kind
                                color: kind === "DATA" ? theme.accent : theme.textSoft
                                font.pixelSize: 7
                                font.weight: Font.Bold
                            }
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
                                text: detail
                                color: theme.muted
                                font.pixelSize: 7
                                elide: Text.ElideRight
                            }
                        }

                        ColumnLayout {
                            Layout.preferredWidth: 230
                            spacing: 1
                            Label {
                                Layout.fillWidth: true
                                text: value.length ? value : "—"
                                color: theme.textSoft
                                font.pixelSize: 9
                                horizontalAlignment: Text.AlignRight
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: quality.length || timestamp.length
                                    ? ((quality.length ? "q=" + quality : "") + (timestamp.length ? " · " + timestamp : ""))
                                    : status
                                color: theme.muted
                                font.pixelSize: 7
                                horizontalAlignment: Text.AlignRight
                                elide: Text.ElideRight
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: 68
                            Layout.preferredHeight: 20
                            radius: 10
                            color: status === "LIVE" || status === "ACTIVE" ? theme.greenSoft : theme.surfaceRaised
                            Label {
                                anchors.centerIn: parent
                                text: status
                                color: status === "LIVE" || status === "ACTIVE" ? theme.green : theme.muted
                                font.pixelSize: 7
                                font.weight: Font.Bold
                            }
                        }

                        Button {
                            text: "Remove"
                            flat: true
                            onClicked: root.removeAt(index)
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: watchModel.count === 0
                    width: Math.max(220, parent.width - 60)
                    text: "Select a DataAttribute, DataSet, Report, or configured GOOSE object in the Browser and choose Add to Global Data."
                    color: theme.muted
                    font.pixelSize: 9
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            Rectangle {
                SplitView.fillWidth: true
                SplitView.preferredHeight: 150
                SplitView.minimumHeight: 90
                color: theme.chrome
                border.width: 1
                border.color: theme.lineSoft

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        Label {
                            Layout.fillWidth: true
                            text: "REPORT ACTIVITY"
                            color: theme.muted
                            font.pixelSize: 8
                            font.weight: Font.DemiBold
                        }
                        Label {
                            text: String(reports.receivedReportCount)
                            color: theme.textSoft
                            font.pixelSize: 8
                        }
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: reports.events
                        clip: true
                        reuseItems: true
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Label {
                            required property string modelData
                            width: ListView.view.width
                            height: 24
                            leftPadding: 10
                            rightPadding: 10
                            verticalAlignment: Text.AlignVCenter
                            text: modelData
                            color: theme.textSoft
                            font.pixelSize: 8
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
}
