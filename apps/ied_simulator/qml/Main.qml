// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ARStack.IedSimulator 1.0

ApplicationWindow {
    id: root
    width: 1360
    height: 860
    minimumWidth: 1024
    minimumHeight: 680
    visible: true
    title: "ARStack IEC 61850 Workbench"
    color: appTheme.background
    font.family: interFont.status === FontLoader.Ready ? interFont.name : "Segoe UI"

    property int workspaceIndex: simulator.imported ? 1 : 0

    AppTheme { id: appTheme }
    IedFleetController {
        id: simulator
        objectName: "simulatorBackend"
    }
    MmsClientController {
        id: mmsClient
        objectName: "mmsClientBackend"
    }
    GooseMonitorController {
        id: gooseMonitor
        objectName: "gooseMonitorBackend"
    }

    FontLoader {
        id: interFont
        source: "qrc:/iedsim/assets/InterVariable.ttf"
    }

    FileDialog {
        id: sclDialog
        title: "Open IEC 61850 engineering model"
        nameFilters: ["IEC 61850 engineering files (*.scl *.cid *.scd *.iid *.icd)", "All files (*)"]
        onAccepted: simulator.loadFileAsync(selectedFile)
    }

    function importModel() { sclDialog.open() }

    Shortcut {
        sequence: "Ctrl+1"
        onActivated: root.workspaceIndex = 0
    }
    Shortcut {
        sequence: "Ctrl+2"
        onActivated: root.workspaceIndex = 1
    }
    Shortcut {
        sequence: "Ctrl+3"
        onActivated: root.workspaceIndex = 2
    }
    Shortcut {
        sequence: "Ctrl+Shift+A"
        enabled: root.workspaceIndex === 1
        onActivated: activityMonitor.opened ? activityMonitor.close() : activityMonitor.open()
    }
    Shortcut {
        sequence: "Ctrl+Shift+C"
        enabled: root.workspaceIndex === 1 && simulator.imported
        onActivated: commissioningWorkspace.opened ? commissioningWorkspace.close() : commissioningWorkspace.open()
    }

    component WorkspaceButton: Button {
        id: control
        required property int workspace
        implicitHeight: 28
        implicitWidth: Math.max(110, label.implicitWidth + 24)
        checkable: true
        checked: root.workspaceIndex === workspace
        onClicked: root.workspaceIndex = workspace
        contentItem: Label {
            id: label
            text: control.text
            color: control.checked ? "#ffffff" : appTheme.navigationMuted
            font.pixelSize: 9
            font.weight: control.checked ? Font.DemiBold : Font.Normal
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 4
            color: control.checked ? appTheme.accent : control.hovered ? "#263833" : "transparent"
            border.width: 0
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            color: appTheme.navigationDark

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 4

                Label {
                    text: "ARStack IEC 61850"
                    color: appTheme.navigationText
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    Layout.rightMargin: 12
                }

                WorkspaceButton { workspace: 0; text: "IED Connection" }
                WorkspaceButton { workspace: 1; text: "Simulator" }
                WorkspaceButton { workspace: 2; text: "GOOSE" }
                Item { Layout.fillWidth: true }
                Label {
                    text: root.workspaceIndex === 0
                          ? mmsClient.stateText
                          : root.workspaceIndex === 2
                            ? (gooseMonitor.capturing ? "GOOSE MONITOR LIVE" : "GOOSE")
                            : (simulator.running ? "SIMULATOR LIVE" : "SIMULATOR")
                    color: root.workspaceIndex === 0 && mmsClient.connected
                           ? "#9ff0c1"
                           : root.workspaceIndex === 2 && gooseMonitor.capturing
                             ? "#9ff0c1" : appTheme.navigationMuted
                    font.pixelSize: 8
                    font.weight: Font.DemiBold
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.workspaceIndex

            MmsClientWorkspace {
                theme: appTheme
                client: mmsClient
            }

            Item {
                IedScoutWorkspace {
                    anchors.fill: parent
                    theme: appTheme
                    backend: simulator
                    onOpenSclRequested: root.importModel()
                }
            }

            GooseWorkspace {
                theme: appTheme
                simulator: simulator
                monitor: gooseMonitor
                onOpenSimulatorRequested: root.workspaceIndex = 1
            }
        }
    }

    Rectangle {
        id: commissioningLauncher
        z: 20
        visible: root.workspaceIndex === 1 && simulator.imported && !commissioningWorkspace.opened
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 12
        anchors.bottomMargin: 72
        width: 142
        height: 27
        radius: 5
        color: commissioningMouse.containsMouse ? appTheme.surfaceRaised : appTheme.statusChrome
        border.width: 1
        border.color: appTheme.line

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 9
            anchors.rightMargin: 8
            spacing: 6
            Rectangle {
                width: 6
                height: 6
                radius: 3
                color: simulator.reportCount > 0 || simulator.gooseCount > 0 ? appTheme.green : appTheme.accent
            }
            Label {
                Layout.fillWidth: true
                text: "Commissioning"
                color: appTheme.statusText
                font.pixelSize: 9
                font.weight: Font.DemiBold
            }
            Label {
                text: String(simulator.dataSetCount + simulator.reportCount + simulator.gooseCount)
                color: appTheme.navigationMuted
                font.pixelSize: 8
            }
        }

        MouseArea {
            id: commissioningMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: commissioningWorkspace.open()
        }

        ToolTip.visible: commissioningMouse.containsMouse
        ToolTip.text: "Commissioning Explorer · Ctrl+Shift+C"
    }

    Rectangle {
        id: activityLauncher
        z: 20
        visible: root.workspaceIndex === 1 && !activityMonitor.opened
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 12
        anchors.bottomMargin: 38
        width: 116
        height: 27
        radius: 5
        color: launcherMouse.containsMouse ? appTheme.surfaceRaised : appTheme.statusChrome
        border.width: 1
        border.color: appTheme.line

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 9
            anchors.rightMargin: 8
            spacing: 6
            Rectangle {
                width: 6
                height: 6
                radius: 3
                color: simulator.running ? appTheme.green
                                         : simulator.fatalError.length ? appTheme.red
                                                                       : appTheme.accent
            }
            Label {
                Layout.fillWidth: true
                text: "Activity"
                color: appTheme.statusText
                font.pixelSize: 9
                font.weight: Font.DemiBold
            }
            Label {
                text: String(simulator.activityModel.retainedCount)
                color: appTheme.navigationMuted
                font.pixelSize: 8
            }
        }

        MouseArea {
            id: launcherMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: activityMonitor.open()
        }

        ToolTip.visible: launcherMouse.containsMouse
        ToolTip.text: "Activity Monitor · Ctrl+Shift+A"
    }

    CommissioningWorkspace {
        id: commissioningWorkspace
        z: 45
        theme: appTheme
        backend: simulator
    }

    ActivityMonitor {
        id: activityMonitor
        z: 50
        theme: appTheme
        backend: simulator
    }
}
