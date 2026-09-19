// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property var theme
    required property var session
    required property var client
    required property var reports
    required property var utilities

    property int activeSection: 0

    function ensureActiveService() {
        if (!session.connected)
            return
        if (activeSection === 1)
            session.ensureReportsConnected()
        else if (activeSection === 2 || activeSection === 3)
            session.ensureUtilitiesConnected()
    }

    onActiveSectionChanged: ensureActiveService()

    Connections {
        target: session
        function onStateChanged() {
            if (session.connected)
                root.ensureActiveService()
        }
    }

    component SectionButton: Button {
        id: control
        required property int section
        checkable: true
        checked: root.activeSection === section
        implicitHeight: 30
        implicitWidth: Math.max(112, label.implicitWidth + 24)
        onClicked: root.activeSection = section

        contentItem: Label {
            id: label
            text: control.text
            color: control.checked ? "#ffffff" : root.theme.textSoft
            font.pixelSize: 9
            font.weight: control.checked ? Font.DemiBold : Font.Normal
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 5
            color: control.checked ? root.theme.accent
                                   : control.hovered ? root.theme.surfaceRaised : "transparent"
            border.width: control.checked ? 0 : 1
            border.color: root.theme.lineSoft
        }
    }

    Rectangle { anchors.fill: parent; color: root.theme.background }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            color: root.theme.chrome
            border.width: 1
            border.color: root.theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 8

                ColumnLayout {
                    Layout.preferredWidth: 155
                    spacing: 1
                    Label {
                        text: "IED BROWSER"
                        color: root.theme.text
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: "One endpoint · contextual IEC 61850 services"
                        color: root.theme.muted
                        font.pixelSize: 8
                    }
                }

                TextField {
                    id: hostField
                    Layout.preferredWidth: 205
                    text: session.host
                    placeholderText: "IED IP / hostname"
                    selectByMouse: true
                    enabled: !session.configurationLocked
                    onEditingFinished: session.host = text
                }

                SpinBox {
                    id: portField
                    Layout.preferredWidth: 102
                    from: 1
                    to: 65535
                    value: session.port
                    editable: true
                    enabled: !session.configurationLocked
                    onValueModified: session.port = value
                }

                Button {
                    text: session.connected ? "Connected" : session.busy ? "Connecting..." : "Connect"
                    enabled: !session.connected && !session.busy
                    onClicked: {
                        session.host = hostField.text
                        session.port = portField.value
                        session.connectToIed()
                    }
                }

                Button {
                    text: "Disconnect"
                    enabled: session.connected || session.busy
                    onClicked: session.disconnectFromIed()
                }

                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: session.lastError.length ? root.theme.red
                                                   : session.connected ? root.theme.green
                                                                       : session.busy ? root.theme.amber
                                                                                      : root.theme.muted
                }

                ColumnLayout {
                    spacing: 0
                    Label {
                        text: session.stateText
                        color: session.lastError.length ? root.theme.red : root.theme.textSoft
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: session.endpoint
                        color: root.theme.muted
                        font.pixelSize: 8
                    }
                }

                Item { Layout.fillWidth: true }

                ColumnLayout {
                    visible: session.connected
                    spacing: 0
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: client.iedName.length ? client.iedName : "Online IED"
                        color: root.theme.text
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: client.logicalDeviceCount + " LD · " + client.logicalNodeCount + " LN · "
                              + client.dataAttributeCount + " DA"
                        color: root.theme.muted
                        font.pixelSize: 8
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: root.theme.surface
            border.width: 1
            border.color: root.theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 6

                SectionButton { section: 0; text: "Data Model" }
                SectionButton { section: 1; text: "Reports" }
                SectionButton { section: 2; text: "Files" }
                SectionButton { section: 3; text: "Setting Groups" }
                Item { Layout.fillWidth: true }
                Label {
                    text: activeSection === 0 ? "Model discovery / read / guarded write"
                          : activeSection === 1 ? "URCB / BRCB service"
                          : activeSection === 2 ? "Read-only MMS file service"
                                                : "SGCB inspection / guarded ActSG"
                    color: root.theme.muted
                    font.pixelSize: 8
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.activeSection

            MmsClientWorkspace {
                theme: root.theme
                client: root.client
                showConnectionHeader: false
            }

            ReportsWorkspace {
                theme: root.theme
                reports: root.reports
                showConnectionHeader: false
            }

            FilesWorkspace {
                theme: root.theme
                files: root.utilities
                showConnectionHeader: false
            }

            SettingsWorkspace {
                theme: root.theme
                settings: root.utilities
                showConnectionHeader: false
            }
        }
    }
}
