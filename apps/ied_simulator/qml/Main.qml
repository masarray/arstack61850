// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ARStack.IedSimulator 1.0

ApplicationWindow {
    id: root
    width: 1480
    height: 940
    minimumWidth: 1180
    minimumHeight: 760
    visible: true
    title: "ARStack IED Simulator"
    color: appTheme.background
    font.family: interFont.status === FontLoader.Ready ? interFont.name : "Segoe UI"

    AppTheme { id: appTheme }
    IedSimulatorController {
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
        title: appendImport ? "Add an IED engineering file" : "Import an engineering file"
        nameFilters: ["IEC 61850 engineering files (*.scl *.cid *.scd *.iid *.icd)", "All files (*)"]
        onAccepted: appendImport ? simulator.addFile(selectedFile) : simulator.loadFile(selectedFile)
    }

    FolderDialog {
        id: folderDialog
        title: "Choose the file-service folder"
        onAccepted: simulator.fileFolder = selectedFolder.toString().replace("file:///", "")
    }

    function importModel(append) {
        appendImport = append === true
        sclDialog.open()
    }

    Loader {
        anchors.fill: parent
        sourceComponent: simulator.running ? runtimeComponent : launchComponent
    }

    Component {
        id: launchComponent
        LaunchWorkspace {
            anchors.fill: parent
            theme: appTheme
            backend: simulator
            onImportRequested: root.importModel(false)
            onFolderRequested: folderDialog.open()
        }
    }

    Component {
        id: runtimeComponent
        RuntimeWorkspace {
            anchors.fill: parent
            theme: appTheme
            backend: simulator
            onAddIedRequested: root.importModel(true)
            onFolderRequested: folderDialog.open()
        }
    }
}
