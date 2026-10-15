// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Cross-IED presentation of the existing P6A per-IED watchlists. Every value
// retains its source slot; refresh routes to that slot's original MMS client.
// No timer, hidden polling, service sharing, or cross-IED write path exists.
Rectangle {
    id: root

    required property var theme
    required property var fleet
    required property var browserViews

    signal iedRequested(int index)

    readonly property int maximumRows: 2048
    readonly property int totalRows: rows.count
    property string statusMessage: "Combined, event-driven view of each IED's Global Data."

    color: theme.background

    ListModel { id: rows }

    // Presentation only. Each IED retains its canonical watchlist, MMS
    // association, and report authority; this pane never owns measurements.
    property string filterText: ""
    property int sourceCount: 0
    readonly property int maximumPreviewLength: 256

    function preview(value) {
        const text = value === undefined || value === null ? "" : String(value)
        return text.length <= maximumPreviewLength ? text
            : text.slice(0, maximumPreviewLength - 1) + "…"
    }

    function requestRefresh() {
        // Event-driven coalescing: a multi-value MMS update is one UI refresh.
        Qt.callLater(root.refreshRows)
    }

    function refreshRows() {
        const next = []
        const needle = filterText.trim().toLowerCase()
        let sourceTotal = 0
        let truncated = false
        for (let index = 0; index < fleet.workspaceCount; ++index) {
            const view = browserViews.itemAt(index)
            const context = fleet.contextAt(index)
            if (!view || !context) continue
            const entries = view.watchedRows()
            const ied = context.iedName || ("IED " + String(index + 1))
            for (let i = 0; i < entries.length; ++i) {
                const row = entries[i]
                ++sourceTotal
                const item = {
                    "slotIndex": index,
                    "iedName": ied,
                    "kind": row.kind || "",
                    "reference": row.reference || "",
                    "detail": preview(row.detail),
                    "value": preview(row.value),
                    "quality": preview(row.quality),
                    "timestamp": preview(row.timestamp),
                    "status": row.status || "OFFLINE"
                }
                // Search a bounded presentation preview, not a potentially
                // megabyte-long MMS scalar or a second indexing/cache system.
                if (needle.length > 0 &&
                    [item.iedName, item.kind, preview(item.reference),
                     item.detail, item.value, item.quality, item.timestamp,
                     item.status].join(" ").toLowerCase().indexOf(needle) < 0)
                    continue
                if (next.length >= maximumRows) {
                    truncated = true
                    continue
                }
                next.push(item)
            }
        }

        sourceCount = sourceTotal
        // Keep virtualized delegates and scroll position stable for frequent
        // real MMS value updates. Structural change is the only reset path.
        let stable = rows.count === next.length
        if (stable) {
            for (let i = 0; i < next.length; ++i) {
                const old = rows.get(i)
                if (old.slotIndex !== next[i].slotIndex ||
                    old.kind !== next[i].kind ||
                    old.reference !== next[i].reference) {
                    stable = false
                    break
                }
            }
        }
        if (stable) {
            const dynamicRoles = ["iedName", "detail", "value", "quality", "timestamp", "status"]
            for (let i = 0; i < next.length; ++i) {
                const before = rows.get(i)
                for (let k = 0; k < dynamicRoles.length; ++k) {
                    const role = dynamicRoles[k]
                    if (before[role] !== next[i][role])
                        rows.setProperty(i, role, next[i][role])
                }
            }
        } else {
            rows.clear()
            for (let i = 0; i < next.length; ++i)
                rows.append(next[i])
        }
        statusMessage = truncated
            ? "Visible result limit reached (2048). Refine search."
            : needle.length > 0
              ? String(rows.count) + " matches across " + String(sourceTotal) + " watched entries."
              : String(sourceTotal) + " watched entries from "
                    + String(fleet.workspaceCount) + " independent IED(s)."
    }

    function refreshSelected() {
        var index = selectedIed.currentIndex
        if (index < 0 || index >= fleet.workspaceCount)
            return
        var view = browserViews.itemAt(index)
        if (!view) return
        var accepted = view.refreshWatchedGlobalData()
        statusMessage = accepted
            ? "Explicit bounded refresh requested on " + (fleet.contextAt(index).iedName || "selected IED") + "."
            : "Refresh was not started; inspect that IED's Global Data for details."
    }

    Connections {
        target: fleet
        function onWorkspacesChanged() { root.requestRefresh() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 68
            color: theme.chrome
            border.width: 1
            border.color: theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 13
                anchors.rightMargin: 13
                spacing: 9

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        text: "All IEDs · Global Data"
                        color: theme.text
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: root.statusMessage
                        color: theme.muted
                        font.pixelSize: 8
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                TextField {
                    id: signalSearch
                    objectName: "fleetGlobalDataSearch"
                    Layout.preferredWidth: 215
                    Layout.maximumWidth: 270
                    maximumLength: 128
                    placeholderText: "Search IED, signal, value, quality…"
                    selectByMouse: true
                    onTextChanged: {
                        root.filterText = text
                        root.requestRefresh()
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: "Filter this view only. Never changes watched signals or starts MMS traffic."
                }
                Button {
                    text: "Clear"
                    visible: signalSearch.length > 0
                    onClicked: signalSearch.clear()
                }
                ComboBox {
                    id: selectedIed
                    Layout.preferredWidth: 160
                    model: fleet
                    textRole: "label"
                }
                Button {
                    text: "Refresh selected IED"
                    enabled: selectedIed.currentIndex >= 0
                             && fleet.clientAt(selectedIed.currentIndex)
                             && fleet.clientAt(selectedIed.currentIndex).connected
                             && !fleet.clientAt(selectedIed.currentIndex).operationBusy
                    onClicked: root.refreshSelected()
                    ToolTip.visible: hovered
                    ToolTip.text: "Explicit bounded MMS Read on one IED association only; no automatic polling."
                }
                Button {
                    text: "Open selected IED"
                    enabled: selectedIed.currentIndex >= 0
                    onClicked: root.iedRequested(selectedIed.currentIndex)
                }
                Label {
                    text: String(rows.count) + " / " + String(root.maximumRows)
                    color: theme.muted
                    font.pixelSize: 8
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: theme.surface
            border.width: 1
            border.color: theme.lineSoft
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 9
                Label { Layout.preferredWidth: 96; text: "IED"; color: theme.muted; font.pixelSize: 8 }
                Label { Layout.preferredWidth: 58; text: "TYPE"; color: theme.muted; font.pixelSize: 8 }
                Label { Layout.fillWidth: true; text: "IEC REFERENCE"; color: theme.muted; font.pixelSize: 8 }
                Label { Layout.preferredWidth: 120; text: "VALUE"; color: theme.muted; font.pixelSize: 8 }
                Label { Layout.preferredWidth: 110; text: "QUALITY"; color: theme.muted; font.pixelSize: 8 }
                Label { Layout.preferredWidth: 145; text: "IED TIMESTAMP"; color: theme.muted; font.pixelSize: 8 }
                Label { Layout.preferredWidth: 90; text: "STATE"; color: theme.muted; font.pixelSize: 8 }
            }
        }

        ListView {
            id: combinedList
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: rows
            clip: true
            reuseItems: true
            spacing: 1
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                required property int index
                required property int slotIndex
                required property string iedName
                required property string kind
                required property string reference
                required property string detail
                required property string value
                required property string quality
                required property string timestamp
                required property string status

                width: combinedList.width
                height: 37
                color: index % 2 ? theme.surfaceSoft : theme.surface

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 9
                    Label {
                        Layout.preferredWidth: 96
                        text: iedName
                        color: theme.text
                        font.pixelSize: 8
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.preferredWidth: 58
                        text: kind
                        color: theme.muted
                        font.pixelSize: 8
                    }
                    Label {
                        Layout.fillWidth: true
                        text: reference
                        color: theme.textSoft
                        font.pixelSize: 8
                        elide: Text.ElideMiddle
                    }
                    Label {
                        Layout.preferredWidth: 120
                        text: value.length ? value : "—"
                        color: theme.text
                        font.pixelSize: 8
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.preferredWidth: 110
                        text: quality.length ? quality : "—"
                        color: quality.length ? theme.textSoft : theme.muted
                        font.pixelSize: 8
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.preferredWidth: 145
                        text: timestamp.length ? timestamp : "—"
                        color: timestamp.length ? theme.textSoft : theme.muted
                        font.pixelSize: 8
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.preferredWidth: 90
                        text: status
                        color: status === "LIVE" || status === "ACTIVE"
                               ? theme.green : theme.muted
                        font.pixelSize: 8
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    onDoubleClicked: root.iedRequested(slotIndex)
                    ToolTip.visible: containsMouse
                    ToolTip.text: reference + "\n" + detail
                                    + (quality.length ? "\nQuality: " + quality : "")
                                    + (timestamp.length ? "\nTimestamp: " + timestamp : "")
                    hoverEnabled: true
                }
            }

            Label {
                anchors.centerIn: parent
                visible: rows.count === 0
                width: Math.max(250, parent.width - 80)
                text: root.filterText.length > 0
                    ? "No watched signals match this search. Clear the filter to see all IEDs."
                    : "No watched signals yet. Open an IED tab and add DataAttributes, DataSets, Reports, or GOOSE to its Global Data."
                color: theme.muted
                font.pixelSize: 9
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
        }
    }
}
