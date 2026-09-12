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

    IedScoutWorkspace {
        anchors.fill: parent
        theme: appTheme
        backend: simulator
        onOpenSclRequested: root.importModel()
    }
}
