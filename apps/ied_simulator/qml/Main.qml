// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ARStack.IedSimulator 1.0

ApplicationWindow {
    id: root
    width: 1440
    height: 900
    minimumWidth: 1120
    minimumHeight: 700
    visible: true
    title: "ARStack IED Lab"
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

    property bool appendImport: false

    FileDialog {
        id: sclDialog
        title: appendImport ? "Add IEC 61850 engineering model" : "Open IEC 61850 engineering model"
        nameFilters: ["IEC 61850 engineering files (*.scl *.cid *.scd *.iid *.icd)", "All files (*)"]
        onAccepted: appendImport ? simulator.addFile(selectedFile) : simulator.loadFile(selectedFile)
    }

    FolderDialog {
        id: folderDialog
        title: "Choose file-service folder"
        onAccepted: simulator.fileFolder = selectedFolder.toString().replace("file:///", "")
    }

    function importModel(append) {
        appendImport = append === true
        sclDialog.open()
    }

    FleetWorkspace {
        anchors.fill: parent
        theme: appTheme
        backend: simulator
        onOpenSclRequested: root.importModel(false)
        onAddIedRequested: root.importModel(true)
        onFolderRequested: folderDialog.open()
    }
}
