// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Drawer {
    id: root

    required property var theme
    required property var backend

    edge: Qt.RightEdge
    modal: false
    interactive: true
    width: parent ? Math.min(520, Math.max(390, parent.width * 0.42)) : 480
    height: parent ? parent.height : 760
    padding: 0

    property string searchText: ""
    property string severityFilter: "All"

    function eventMatches(item) {
        if (!item)
            return false
        if (severityFilter !== "All" && String(item.severity || "Info") !== severityFilter)
            return false
        var query = searchText.trim().toLowerCase()
        if (query.length === 0)
            return true
        var haystack = [item.time, item.severity, item.ied, item.category, item.message]
                .map(function(value) { return String(value || "").toLowerCase() })
                .join(" ")
        return haystack.indexOf(query) >= 0
    }

    background: Rectangle {
        color: root.theme.chrome
        border.width: 1
        border.color: root.theme.line
    }

    contentItem: ColumnLayout {
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            color: root.theme.navigation

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 10
                spacing: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        text: "Activity Monitor"
                        color: root.theme.navigationText
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: "IEC 61850 runtime, reports and diagnostics"
                        color: root.theme.navigationMuted
                        font.pixelSize: 8
                    }
                }

                ToolButton {
                    text: "Copy"
                    enabled: root.backend.imported
                    onClicked: root.backend.copyDiagnostics()
                    ToolTip.visible: hovered
                    ToolTip.text: "Copy full diagnostics"
                }
                ToolButton {
                    text: "Clear"
                    enabled: root.backend.activity.length > 0
                    onClicked: root.backend.clearActivity()
                }
                ToolButton {
                    text: "×"
                    font.pixelSize: 16
                    onClicked: root.close()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 7

                TextField {
                    id: searchField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 31
                    placeholderText: "Filter IED, service, report or message"
                    selectByMouse: true
                    font.pixelSize: 9
                    onTextChanged: root.searchText = text
                }

                ComboBox {
                    Layout.preferredWidth: 105
                    Layout.preferredHeight: 31
                    model: ["All", "Info", "Success", "Warning", "Error"]
                    font.pixelSize: 9
                    onCurrentTextChanged: root.severityFilter = currentText
                }
            }
        }

        ListView {
            id: activityList
            objectName: "iedActivityMonitorList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.opened ? root.backend.activity : []
            reuseItems: true
            cacheBuffer: 0
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                id: eventRow
                required property var modelData

                readonly property bool matches: root.eventMatches(modelData)
                width: activityList.width
                height: matches ? 70 : 0
                visible: matches
                color: rowMouse.containsMouse ? root.theme.surfaceRaised : root.theme.surface

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 3
                    color: {
                        var severity = String(eventRow.modelData.severity || "Info")
                        if (severity === "Error") return root.theme.red
                        if (severity === "Warning") return root.theme.amber
                        if (severity === "Success") return root.theme.green
                        return root.theme.accent
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 11
                    anchors.rightMargin: 10
                    anchors.topMargin: 7
                    anchors.bottomMargin: 7
                    spacing: 3

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 7
                        Label {
                            text: String(eventRow.modelData.time || "")
                            color: root.theme.muted
                            font.pixelSize: 8
                        }
                        Rectangle {
                            implicitWidth: categoryLabel.implicitWidth + 12
                            implicitHeight: 17
                            radius: 8
                            color: root.theme.surfaceRaised
                            Label {
                                id: categoryLabel
                                anchors.centerIn: parent
                                text: String(eventRow.modelData.category || "Event")
                                color: root.theme.textSoft
                                font.pixelSize: 7
                                font.weight: Font.DemiBold
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: String(eventRow.modelData.ied || "")
                            color: root.theme.accent
                            font.pixelSize: 8
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideLeft
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: String(eventRow.modelData.message || "")
                        color: root.theme.textSoft
                        font.pixelSize: 9
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: root.theme.lineSoft
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                }
            }

            Label {
                anchors.centerIn: parent
                visible: root.backend.activity.length === 0
                text: "No activity yet"
                color: root.theme.muted
                font.pixelSize: 11
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: root.theme.statusChrome

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                Label {
                    Layout.fillWidth: true
                    text: root.backend.activity.length + " retained events · newest first"
                    color: root.theme.statusText
                    font.pixelSize: 8
                }
                Label {
                    text: "Ctrl+Shift+A"
                    color: root.theme.navigationMuted
                    font.pixelSize: 8
                }
            }
        }
    }

    onOpened: {
        searchField.forceActiveFocus()
        activityList.positionViewAtBeginning()
    }

    Connections {
        target: root.backend
        function onActivityChanged() {
            if (root.opened)
                activityList.positionViewAtBeginning()
        }
    }
}
