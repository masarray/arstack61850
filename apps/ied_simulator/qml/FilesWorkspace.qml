// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property var files

    property bool showConnectionHeader: true

    property string selectedRemotePath: ""
    property bool selectedIsDirectory: false

    function parentDirectory(path) {
        var normalized = path || ""
        while (normalized.length > 0 && normalized.endsWith("/"))
            normalized = normalized.slice(0, -1)
        var slash = normalized.lastIndexOf("/")
        if (slash <= 0)
            return ""
        return normalized.slice(0, slash)
    }

    FileDialog {
        id: saveDialog
        title: "Save downloaded MMS file"
        fileMode: FileDialog.SaveFile
        onAccepted: files.downloadFile(root.selectedRemotePath, selectedFile.toString())
    }

    Rectangle {
        anchors.fill: parent
        color: theme.background
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        SurfaceCard {
            visible: root.showConnectionHeader
            Layout.fillWidth: true
            Layout.preferredHeight: root.showConnectionHeader ? 72 : 0
            theme: root.theme

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                ColumnLayout {
                    Layout.preferredWidth: 235
                    spacing: 2
                    Label {
                        text: "MMS FILES"
                        color: theme.text
                        font.pixelSize: theme.subtitleSize
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: "Read-only FileDirectory + streaming download"
                        color: theme.muted
                        font.pixelSize: theme.captionSize
                    }
                }

                TextField {
                    Layout.preferredWidth: 210
                    text: files.host
                    placeholderText: "IED host"
                    enabled: !files.busy && !files.connected
                    onEditingFinished: files.host = text
                }
                SpinBox {
                    Layout.preferredWidth: 105
                    from: 1
                    to: 65535
                    editable: true
                    value: files.port
                    enabled: !files.busy && !files.connected
                    onValueModified: files.port = value
                }
                ActionButton {
                    theme: root.theme
                    text: files.connected ? "Connected" : "Connect"
                    primary: !files.connected
                    enabled: !files.busy && !files.connected
                    onClicked: files.connectToIed()
                }
                ActionButton {
                    theme: root.theme
                    text: "Reconnect"
                    enabled: !files.busy && files.connected
                    onClicked: files.reconnect()
                }
                ActionButton {
                    theme: root.theme
                    text: "Disconnect"
                    danger: true
                    enabled: files.connected
                    onClicked: files.disconnectFromIed()
                }
                Item { Layout.fillWidth: true }
                ColumnLayout {
                    spacing: 1
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: files.stateText
                        color: files.connected ? theme.green : (files.lastError.length ? theme.red : theme.textSoft)
                        font.pixelSize: theme.labelSize
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: files.endpoint
                        color: theme.muted
                        font.pixelSize: theme.captionSize
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TextField {
                id: directoryField
                Layout.fillWidth: true
                placeholderText: "Remote directory (blank = root)"
                text: files.currentDirectory
                enabled: files.connected && !files.operationBusy
                onAccepted: files.browseDirectory(text)
            }
            ActionButton {
                theme: root.theme
                text: "Root"
                enabled: files.connected && !files.operationBusy
                onClicked: files.browseDirectory("")
            }
            ActionButton {
                theme: root.theme
                text: "Up"
                enabled: files.connected && !files.operationBusy && files.currentDirectory.length > 0
                onClicked: files.browseDirectory(root.parentDirectory(files.currentDirectory))
            }
            ActionButton {
                theme: root.theme
                text: "Browse"
                primary: true
                enabled: files.connected && !files.operationBusy
                onClicked: files.browseDirectory(directoryField.text)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            SurfaceCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                theme: root.theme

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "Directory"
                            color: theme.text
                            font.pixelSize: theme.labelSize
                            font.weight: Font.DemiBold
                        }
                        Label {
                            Layout.fillWidth: true
                            text: files.currentDirectory.length ? files.currentDirectory : "/"
                            elide: Text.ElideMiddle
                            color: theme.textSoft
                            font.pixelSize: theme.captionSize
                        }
                        Label {
                            text: files.fileEntryCount + " entries · " + files.filePageCount + " page(s)"
                            color: theme.muted
                            font.pixelSize: theme.captionSize
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: theme.chrome
                        radius: 6
                        border.width: 1
                        border.color: theme.lineSoft

                        ListView {
                            id: fileList
                            anchors.fill: parent
                            anchors.margins: 1
                            clip: true
                            model: files.fileEntries
                            spacing: 1

                            delegate: Rectangle {
                                id: row
                                required property var modelData
                                width: fileList.width
                                height: 46
                                color: root.selectedRemotePath === modelData.path
                                       ? theme.accentSoft
                                       : mouse.containsMouse ? theme.surfaceRaised : theme.surface

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 10
                                    Label {
                                        text: row.modelData.directory ? "DIR" : "FILE"
                                        color: row.modelData.directory ? theme.accent : theme.textSoft
                                        font.pixelSize: 9
                                        font.weight: Font.Bold
                                        Layout.preferredWidth: 34
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1
                                        Label {
                                            Layout.fillWidth: true
                                            text: row.modelData.name
                                            elide: Text.ElideMiddle
                                            color: theme.text
                                            font.pixelSize: theme.labelSize
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: row.modelData.path
                                            elide: Text.ElideMiddle
                                            color: theme.muted
                                            font.pixelSize: 9
                                        }
                                    }
                                    Label {
                                        text: row.modelData.directory || row.modelData.sizeBytes === undefined
                                              ? ""
                                              : Number(row.modelData.sizeBytes).toLocaleString(Qt.locale(), 'f', 0) + " B"
                                        color: theme.textSoft
                                        font.pixelSize: theme.captionSize
                                        Layout.preferredWidth: 95
                                        horizontalAlignment: Text.AlignRight
                                    }
                                    Label {
                                        text: row.modelData.modified || ""
                                        color: theme.muted
                                        font.pixelSize: 9
                                        Layout.preferredWidth: 120
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }

                                MouseArea {
                                    id: mouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        root.selectedRemotePath = row.modelData.path
                                        root.selectedIsDirectory = row.modelData.directory
                                    }
                                    onDoubleClicked: {
                                        root.selectedRemotePath = row.modelData.path
                                        root.selectedIsDirectory = row.modelData.directory
                                        if (row.modelData.directory && files.connected && !files.operationBusy)
                                            files.browseDirectory(row.modelData.path)
                                    }
                                }
                            }

                            ScrollBar.vertical: ScrollBar {}
                        }
                    }
                }
            }

            SurfaceCard {
                Layout.preferredWidth: 360
                Layout.fillHeight: true
                theme: root.theme

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 9

                    Label {
                        text: "Download"
                        color: theme.text
                        font.pixelSize: theme.subtitleSize
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root.selectedRemotePath.length
                              ? root.selectedRemotePath
                              : "Select a remote file"
                        wrapMode: Text.WrapAnywhere
                        color: root.selectedRemotePath.length ? theme.textSoft : theme.muted
                        font.pixelSize: theme.captionSize
                    }

                    ActionButton {
                        Layout.fillWidth: true
                        theme: root.theme
                        text: "Download / Save As"
                        primary: true
                        enabled: files.connected && !files.operationBusy &&
                                 root.selectedRemotePath.length > 0 && !root.selectedIsDirectory
                        onClicked: saveDialog.open()
                    }
                    ActionButton {
                        Layout.fillWidth: true
                        theme: root.theme
                        text: "Cancel transfer"
                        danger: true
                        enabled: files.downloadActive
                        onClicked: files.cancelOperation()
                    }

                    ProgressBar {
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        indeterminate: files.downloadActive && !files.downloadExpectedKnown
                        value: files.downloadPercent >= 0 ? files.downloadPercent : 0
                    }
                    Label {
                        Layout.fillWidth: true
                        text: files.downloadExpectedKnown
                              ? Number(files.downloadBytes).toLocaleString(Qt.locale(), 'f', 0) +
                                " / " + Number(files.downloadExpectedBytes).toLocaleString(Qt.locale(), 'f', 0) + " B"
                              : Number(files.downloadBytes).toLocaleString(Qt.locale(), 'f', 0) + " B"
                        color: theme.textSoft
                        font.pixelSize: theme.captionSize
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: files.lastDownloadLocalPath.length > 0
                        text: files.lastDownloadLocalPath
                        wrapMode: Text.WrapAnywhere
                        color: theme.muted
                        font.pixelSize: 9
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: theme.lineSoft
                    }
                    Label {
                        text: "Safety contract"
                        color: theme.text
                        font.pixelSize: theme.labelSize
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.fillWidth: true
                        text: "Read-only remote access. Directory continuation is bounded to 64 pages / 4096 entries. Downloads are capped at 512 MiB, use canonical FileOpen/FileRead/FileClose, and partial local files are removed on failure or cancellation. Upload/delete/rename are intentionally not exposed."
                        wrapMode: Text.Wrap
                        color: theme.textSoft
                        font.pixelSize: theme.captionSize
                    }

                    Item { Layout.fillHeight: true }
                    Label {
                        Layout.fillWidth: true
                        visible: files.lastError.length > 0
                        text: files.lastError
                        wrapMode: Text.Wrap
                        color: theme.red
                        font.pixelSize: theme.captionSize
                    }
                }
            }
        }
    }
}
