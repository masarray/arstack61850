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
    title: "ARStack IED Simulator"
    color: appTheme.background
    font.family: interFont.status === FontLoader.Ready ? interFont.name : "Segoe UI"

    AppTheme { id: appTheme }
    IedFleetController {
        id: simulator
        objectName: "simulatorBackend"
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

    function importModel() {
        sclDialog.open()
    }

    Shortcut {
        sequence: "Ctrl+Shift+A"
        onActivated: activityMonitor.open()
    }

    IedScoutWorkspace {
        anchors.fill: parent
        theme: appTheme
        backend: simulator
        onOpenSclRequested: root.importModel()
    }

    Rectangle {
        id: activityLauncher
        z: 20
        visible: !activityMonitor.opened
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
                text: String(simulator.activity.length)
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

    ActivityMonitor {
        id: activityMonitor
        z: 50
        theme: appTheme
        backend: simulator
    }
}
