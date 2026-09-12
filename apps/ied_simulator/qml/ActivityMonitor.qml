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
                    enabled: root.backend.activityModel.retainedCount > 0
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
                    onTextChanged: root.backend.activityModel.filterText = text
                }

                ComboBox {
                    id: severityBox
                    Layout.preferredWidth: 105
                    Layout.preferredHeight: 31
                    model: ["All", "Info", "Success", "Warning", "Error"]
                    font.pixelSize: 9
                    onCurrentTextChanged: root.backend.activityModel.severityFilter = currentText
                }
            }
        }

        ListView {
            id: activityList
            objectName: "iedActivityMonitorList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.opened ? root.backend.activityModel : null
            reuseItems: true
            cacheBuffer: 0
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                id: eventRow
                required property string time
                required property string category
                required property string message
                required property string severity
                required property string ied

                width: activityList.width
                height: 70
                color: rowMouse.containsMouse ? root.theme.surfaceRaised : root.theme.surface

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 3
                    color: {
                        if (eventRow.severity === "Error") return root.theme.red
                        if (eventRow.severity === "Warning") return root.theme.amber
                        if (eventRow.severity === "Success") return root.theme.green
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
                            text: eventRow.time
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
                                text: eventRow.category.length ? eventRow.category : "Event"
                                color: root.theme.textSoft
                                font.pixelSize: 7
                                font.weight: Font.DemiBold
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: eventRow.ied
                            color: root.theme.accent
                            font.pixelSize: 8
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideLeft
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: eventRow.message
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
                visible: root.opened && root.backend.activityModel.visibleCount === 0
                text: root.backend.activityModel.retainedCount === 0
                      ? "No activity yet"
                      : "No activity matches this filter"
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
                    text: {
                        var retained = root.backend.activityModel.retainedCount
                        var visible = root.backend.activityModel.visibleCount
                        return visible === retained
                               ? retained + " retained events · newest first"
                               : visible + " shown · " + retained + " retained"
                    }
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
        root.backend.activityModel.filterText = searchField.text
        root.backend.activityModel.severityFilter = severityBox.currentText
        searchField.forceActiveFocus()
        activityList.positionViewAtBeginning()
    }

    Connections {
        target: root.backend.activityModel
        function onRetainedCountChanged() {
            if (root.opened)
                activityList.positionViewAtBeginning()
        }
    }
}
