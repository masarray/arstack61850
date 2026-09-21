// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var theme
    required property var session
    required property var client
    required property var context
    required property var reports
    required property var utilities

    property bool expanded: false
    readonly property int maximumEntries: 80
    property string lastEventKey: ""

    Layout.fillWidth: true
    Layout.preferredHeight: expanded ? 152 : 26
    color: theme.statusChrome
    border.width: 1
    border.color: session.lastError.length ? theme.red : theme.lineSoft

    ListModel { id: historyModel }

    function eventSeverity(errorText, busy, connected) {
        if (errorText && String(errorText).length > 0) return "error"
        if (busy) return "busy"
        if (connected) return "success"
        return "info"
    }

    function appendEvent(category, message, severity) {
        const normalized = String(message || "").trim()
        if (!normalized.length)
            return
        const key = category + "|" + normalized + "|" + severity
        if (key === lastEventKey)
            return
        lastEventKey = key
        historyModel.insert(0, {
            time: Qt.formatTime(new Date(), "HH:mm:ss"),
            category: category,
            message: normalized,
            severity: severity
        })
        while (historyModel.count > maximumEntries)
            historyModel.remove(historyModel.count - 1)
    }

    function captureSession() {
        appendEvent(
            "SESSION",
            session.lastError.length ? session.lastError : session.stateText + " · " + session.endpoint,
            eventSeverity(session.lastError, session.busy, session.connected))
    }

    function captureContext() {
        if (!context.loaded) {
            appendEvent("MODEL", "No active engineering model", "info")
            return
        }
        appendEvent(
            "MODEL",
            (context.iedName.length ? context.iedName : "IED")
                + " · " + context.authority
                + " · " + (context.online ? "ONLINE" : "OFFLINE"),
            context.online ? "success" : "info")
    }

    function captureReports() {
        if (reports.lastError.length) {
            appendEvent("REPORT", reports.lastError, "error")
            return
        }
        if (reports.connected)
            appendEvent("REPORT", reports.stateText, reports.active ? "success" : "info")
    }

    function captureUtilities() {
        if (utilities.lastError.length) {
            appendEvent("UTILITY", utilities.lastError, "error")
            return
        }
        if (utilities.connected)
            appendEvent("UTILITY", utilities.stateText, "info")
    }

    Component.onCompleted: {
        appendEvent("BROWSER", "Engineering Browser ready", "info")
        captureContext()
        captureSession()
    }

    Connections {
        target: root.session
        function onStateChanged() { root.captureSession() }
    }
    Connections {
        target: root.context
        function onContextChanged() { root.captureContext() }
        function onRuntimeChanged() { root.captureContext() }
    }
    Connections {
        target: root.reports
        function onStateChanged() { root.captureReports() }
    }
    Connections {
        target: root.utilities
        function onStateChanged() { root.captureUtilities() }
    }
    Connections {
        target: root.client
        function onDiagnosticsChanged() {
            if (root.client.lastError.length)
                root.appendEvent("MMS", root.client.lastError, "error")
            else if (root.client.lastDiagnostic.length)
                root.appendEvent("MMS", root.client.lastDiagnostic, "info")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 26
            color: session.lastError.length ? theme.redSoft : theme.statusChrome

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 9
                anchors.rightMargin: 7
                spacing: 7

                Rectangle {
                    width: 6
                    height: 6
                    radius: 3
                    color: session.lastError.length ? theme.red
                                                   : session.connected ? theme.green
                                                                       : session.busy ? theme.amber
                                                                                      : theme.muted
                }

                Label {
                    text: "STATUS"
                    color: session.lastError.length ? theme.red : theme.navigationMuted
                    font.pixelSize: 7
                    font.weight: Font.DemiBold
                }

                Label {
                    Layout.fillWidth: true
                    text: session.lastError.length
                          ? session.lastError
                          : session.stateText + " · " + (context.loaded ? (context.iedName || "IED") : "no model")
                    color: session.lastError.length ? theme.red : theme.navigationText
                    font.pixelSize: 8
                    elide: Text.ElideRight
                }

                Label {
                    text: historyModel.count + "/" + maximumEntries
                    color: session.lastError.length ? theme.red : theme.navigationMuted
                    font.pixelSize: 7
                }

                ToolButton {
                    text: root.expanded ? "Hide history" : "History"
                    font.pixelSize: 8
                    onClicked: root.expanded = !root.expanded
                }
            }
        }

        ListView {
            id: historyList
            visible: root.expanded
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: historyModel
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                required property string time
                required property string category
                required property string message
                required property string severity

                width: historyList.width
                height: 25
                color: index % 2 ? root.theme.chrome : root.theme.surface

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 9
                    anchors.rightMargin: 9
                    spacing: 8

                    Label {
                        Layout.preferredWidth: 46
                        text: time
                        color: root.theme.muted
                        font.pixelSize: 7
                    }
                    Label {
                        Layout.preferredWidth: 52
                        text: category
                        color: severity === "error" ? root.theme.red
                                                   : severity === "success" ? root.theme.green
                                                                            : severity === "busy" ? root.theme.amber
                                                                                                  : root.theme.accent
                        font.pixelSize: 7
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.fillWidth: true
                        text: message
                        color: root.theme.textSoft
                        font.pixelSize: 8
                        elide: Text.ElideRight
                    }
                }
            }
        }

        RowLayout {
            visible: root.expanded
            Layout.fillWidth: true
            Layout.preferredHeight: root.expanded ? 24 : 0
            Layout.leftMargin: 9
            Layout.rightMargin: 7
            spacing: 6

            Label {
                Layout.fillWidth: true
                text: "Bounded Browser history · no background polling"
                color: root.theme.navigationMuted
                font.pixelSize: 7
            }
            ToolButton {
                text: "Clear"
                font.pixelSize: 8
                enabled: historyModel.count > 0
                onClicked: {
                    historyModel.clear()
                    root.lastEventKey = ""
                }
            }
        }
    }
}
