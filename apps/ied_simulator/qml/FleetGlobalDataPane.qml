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

    function refreshRows() {
        rows.clear()
        for (var index = 0; index < fleet.workspaceCount; ++index) {
            var view = browserViews.itemAt(index)
            var context = fleet.contextAt(index)
            if (!view || !context)
                continue
            var entries = view.watchedRows()
            for (var i = 0; i < entries.length; ++i) {
                if (rows.count >= maximumRows) {
                    statusMessage = "Combined watchlist bound reached (2048); no more rows retained."
                    return
                }
                var row = entries[i]
                rows.append({
                    "slotIndex": index,
                    "iedName": context.iedName || ("IED " + String(index + 1)),
                    "kind": row.kind || "",
                    "reference": row.reference || "",
                    "detail": row.detail || "",
                    "value": row.value || "",
                    "quality": row.quality || "",
                    "timestamp": row.timestamp || "",
                    "status": row.status || "OFFLINE"
                })
            }
        }
        statusMessage = String(rows.count) + " entries from "
                        + String(fleet.workspaceCount) + " separate IED workspace(s)."
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
        function onWorkspacesChanged() { root.refreshRows() }
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

                ComboBox {
                    id: selectedIed
                    Layout.preferredWidth: 185
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
                Label { Layout.preferredWidth: 110; text: "IED"; color: theme.muted; font.pixelSize: 8 }
                Label { Layout.preferredWidth: 70; text: "TYPE"; color: theme.muted; font.pixelSize: 8 }
                Label { Layout.fillWidth: true; text: "CANONICAL REFERENCE"; color: theme.muted; font.pixelSize: 8 }
                Label { Layout.preferredWidth: 150; text: "VALUE"; color: theme.muted; font.pixelSize: 8 }
                Label { Layout.preferredWidth: 90; text: "STATUS"; color: theme.muted; font.pixelSize: 8 }
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
                        Layout.preferredWidth: 110
                        text: iedName
                        color: theme.text
                        font.pixelSize: 8
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.preferredWidth: 70
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
                        Layout.preferredWidth: 150
                        text: value
                        color: theme.text
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
                text: "No watched signals yet. Open an IED tab and add DataAttributes, DataSets, Reports, or GOOSE to its Global Data."
                color: theme.muted
                font.pixelSize: 9
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
        }
    }
}
