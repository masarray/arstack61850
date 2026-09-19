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

    property int workspaceIndex: 0
    property bool persistenceReady: false
    property bool mmsWasConnected: false
    property bool simulatorAutoSelectPending: true

    onWorkspaceIndexChanged: {
        if (root.persistenceReady) hardening.workspaceIndex = root.workspaceIndex
    }

    AppTheme { id: appTheme }
    ProductHardeningController {
        id: hardening
        objectName: "productHardeningBackend"
    }
    IedFleetController {
        id: simulator
        objectName: "simulatorBackend"
    }
    MmsClientController {
        id: mmsClient
        objectName: "mmsClientBackend"
    }
    MmsReportController {
        id: reports
        objectName: "mmsReportBackend"
    }
    MmsFileSettingsController {
        id: utilities
        objectName: "mmsFileSettingsBackend"
    }
    SclWorkspaceController {
        id: sclWorkspace
        objectName: "sclWorkspaceBackend"
    }
    GooseMonitorController {
        id: gooseMonitor
        objectName: "gooseMonitorBackend"
    }

    Component.onCompleted: {
        if (hardening.lastHost.length > 0) {
            mmsClient.host = hardening.lastHost
            mmsClient.port = hardening.lastPort
        }
        root.workspaceIndex = hardening.workspaceIndex
        root.mmsWasConnected = mmsClient.connected
        root.persistenceReady = true
    }

    Connections {
        target: mmsClient
        function onStateChanged() {
            if (mmsClient.connected && !root.mmsWasConnected)
                hardening.rememberEndpoint(mmsClient.host, mmsClient.port)
            root.mmsWasConnected = mmsClient.connected
        }
    }

    Connections {
        target: sclWorkspace
        function onWorkspaceChanged() {
            // Arm the SCL-assisted online path only after the bounded parser has
            // accepted the engineering file. Failed/unfinished imports never
            // become a trusted connection source.
            mmsClient.trustedSclPath = sclWorkspace.loaded ? sclWorkspace.sourcePath : ""
        }
    }

    Connections {
        target: simulator
        function onModelChanged() {
            if (root.simulatorAutoSelectPending && simulator.imported) {
                root.workspaceIndex = 5
                root.simulatorAutoSelectPending = false
            }
        }
    }

    FontLoader {
        id: interFont
        source: "qrc:/iedsim/assets/InterVariable.ttf"
    }

    FileDialog {
        id: sclDialog
        title: "Open IEC 61850 engineering model"
        nameFilters: ["IEC 61850 engineering files (*.scl *.cid *.scd *.iid *.icd)", "All files (*)"]
        onAccepted: {
            simulator.loadFileAsync(selectedFile)
            sclWorkspace.openFile(selectedFile)
        }
    }

    function importModel() { sclDialog.open() }

    Shortcut { sequence: "Ctrl+1"; onActivated: root.workspaceIndex = 0 }
    Shortcut { sequence: "Ctrl+2"; onActivated: root.workspaceIndex = 1 }
    Shortcut { sequence: "Ctrl+3"; onActivated: root.workspaceIndex = 2 }
    Shortcut { sequence: "Ctrl+4"; onActivated: root.workspaceIndex = 3 }
    Shortcut { sequence: "Ctrl+5"; onActivated: root.workspaceIndex = 4 }
    Shortcut { sequence: "Ctrl+6"; onActivated: root.workspaceIndex = 5 }
    Shortcut { sequence: "Ctrl+7"; onActivated: root.workspaceIndex = 6 }
    Shortcut {
        sequence: "Ctrl+Shift+A"
        enabled: root.workspaceIndex === 5
        onActivated: activityMonitor.opened ? activityMonitor.close() : activityMonitor.open()
    }
    Shortcut {
        sequence: "Ctrl+Shift+C"
        enabled: root.workspaceIndex === 5 && simulator.imported
        onActivated: commissioningWorkspace.opened ? commissioningWorkspace.close() : commissioningWorkspace.open()
    }

    component WorkspaceButton: Button {
        id: control
        required property int workspace
        implicitHeight: 28
        implicitWidth: Math.max(96, label.implicitWidth + 20)
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

                Image {
                    source: "qrc:/iedsim/assets/app-icon.png"
                    sourceSize.width: 22
                    sourceSize.height: 22
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                    Layout.rightMargin: 3
                }

                Label {
                    text: "ARStack IEC 61850"
                    color: appTheme.navigationText
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    Layout.rightMargin: 8
                }

                WorkspaceButton { workspace: 0; text: "IED Connection" }
                WorkspaceButton { workspace: 1; text: "Reports" }
                WorkspaceButton { workspace: 2; text: "Files" }
                WorkspaceButton { workspace: 3; text: "Settings" }
                WorkspaceButton { workspace: 4; text: "SCL" }
                WorkspaceButton { workspace: 5; text: "Simulator" }
                WorkspaceButton { workspace: 6; text: "GOOSE" }
                Item { Layout.fillWidth: true }

                ComboBox {
                    id: recentConnectionPicker
                    visible: root.workspaceIndex === 0 && hardening.recentEndpoints.length > 0
                    Layout.preferredWidth: 160
                    model: hardening.recentEndpoints
                    enabled: !mmsClient.connected && !mmsClient.busy
                    onActivated: {
                        const recentHost = hardening.recentHost(currentIndex)
                        const recentPort = hardening.recentPort(currentIndex)
                        if (recentHost.length > 0 && recentPort > 0) {
                            mmsClient.host = recentHost
                            mmsClient.port = recentPort
                        }
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: "Recent MMS endpoints · restored without auto-connect"
                }

                Label {
                    visible: !hardening.settingsHealthy
                    text: "STATE RESET"
                    color: appTheme.amber
                    font.pixelSize: 8
                    font.weight: Font.Bold
                    ToolTip.visible: stateResetMouse.containsMouse
                    ToolTip.text: hardening.settingsStatus
                    MouseArea { id: stateResetMouse; anchors.fill: parent; hoverEnabled: true }
                }

                Label {
                    visible: hardening.npcapRequired && !hardening.npcapAvailable
                    text: "NPCAP REQUIRED"
                    color: appTheme.amber
                    font.pixelSize: 8
                    font.weight: Font.Bold
                    ToolTip.visible: npcapMouse.containsMouse
                    ToolTip.text: hardening.npcapStatus
                    MouseArea { id: npcapMouse; anchors.fill: parent; hoverEnabled: true }
                }

                Label {
                    text: root.workspaceIndex === 0
                          ? mmsClient.stateText
                          : root.workspaceIndex === 1
                            ? reports.stateText
                            : (root.workspaceIndex === 2 || root.workspaceIndex === 3)
                              ? utilities.stateText
                              : root.workspaceIndex === 4
                                ? sclWorkspace.stateText
                                : root.workspaceIndex === 6
                                  ? (gooseMonitor.capturing ? "GOOSE MONITOR LIVE" : "GOOSE")
                                  : (simulator.running ? "SIMULATOR LIVE" : "SIMULATOR")
                    color: root.workspaceIndex === 0 && mmsClient.connected
                           ? "#9ff0c1"
                           : root.workspaceIndex === 1 && reports.active
                             ? "#9ff0c1"
                             : root.workspaceIndex === 1 && reports.cleanupRequired
                               ? "#ff9ca5"
                               : (root.workspaceIndex === 2 || root.workspaceIndex === 3) && utilities.connected
                                 ? "#9ff0c1"
                                 : root.workspaceIndex === 4 && sclWorkspace.loaded && !sclWorkspace.lastError.length
                                   ? "#9ff0c1"
                                   : root.workspaceIndex === 6 && gooseMonitor.capturing
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

            ReportsWorkspace {
                theme: appTheme
                reports: reports
            }

            FilesWorkspace {
                theme: appTheme
                files: utilities
            }

            SettingsWorkspace {
                theme: appTheme
                settings: utilities
            }

            SclWorkspace {
                theme: appTheme
                workspace: sclWorkspace
            }

            Item {
                Loader {
                    id: simulatorWorkspaceLoader
                    anchors.fill: parent
                    source: "qrc:/iedsim/InteropWorkspaceV2.qml"
                    onLoaded: {
                        item.theme = appTheme
                        item.backend = simulator
                    }
                }
                Connections {
                    target: simulatorWorkspaceLoader.item
                    function onOpenSclRequested() { root.importModel() }
                }
            }

            GooseWorkspace {
                theme: appTheme
                simulator: simulator
                monitor: gooseMonitor
                onOpenSimulatorRequested: root.workspaceIndex = 5
            }
        }
    }

    Rectangle {
        id: commissioningLauncher
        z: 20
        visible: root.workspaceIndex === 5 && simulator.imported && !commissioningWorkspace.opened
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
        visible: root.workspaceIndex === 5 && !activityMonitor.opened
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