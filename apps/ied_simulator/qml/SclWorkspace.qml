// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property var workspace

    function targetEditionKey(index) {
        if (index === 1) return "ed1"
        if (index === 2) return "ed2"
        if (index === 3) return "ed2.1"
        return "preserve"
    }

    FileDialog {
        id: openDialog
        title: "Open SCL engineering source"
        fileMode: FileDialog.OpenFile
        nameFilters: ["IEC 61850 engineering files (*.scl *.cid *.scd *.iid *.icd)", "All files (*)"]
        onAccepted: workspace.openFile(selectedFile)
    }

    FileDialog {
        id: saveDialog
        property bool canonicalRequested: false
        title: canonicalRequested ? "Export canonical SCL" : "Save exact SCL copy"
        fileMode: FileDialog.SaveFile
        nameFilters: canonicalRequested
                     ? ["Canonical SCL (*.scd *.icd *.cid)", "All files (*)"]
                     : ["IEC 61850 engineering files (*.scl *.cid *.scd *.iid *.icd)", "All files (*)"]
        onAccepted: {
            if (canonicalRequested)
                workspace.exportCanonical(selectedFile, root.targetEditionKey(editionPicker.currentIndex))
            else
                workspace.saveAs(selectedFile, "preserve")
        }
    }

    Rectangle { anchors.fill: parent; color: theme.background }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        SurfaceCard {
            Layout.fillWidth: true
            Layout.preferredHeight: 78
            theme: root.theme

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        text: "SCL WORKSPACE"
                        color: theme.text
                        font.pixelSize: theme.subtitleSize
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.fillWidth: true
                        text: workspace.loaded
                              ? workspace.sourceName + " · " + workspace.editionText
                              : "Exact source preservation + verified canonical reconstruction/export"
                        color: theme.textSoft
                        font.pixelSize: theme.captionSize
                        elide: Text.ElideMiddle
                    }
                }

                ActionButton {
                    theme: root.theme
                    text: "Open SCL"
                    primary: !workspace.loaded
                    enabled: !workspace.busy
                    onClicked: openDialog.open()
                }
                ActionButton {
                    theme: root.theme
                    text: "Cancel"
                    danger: true
                    enabled: workspace.busy
                    onClicked: workspace.cancelOperation()
                }
                ColumnLayout {
                    Layout.preferredWidth: 160
                    spacing: 1
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: workspace.stateText
                        color: workspace.lastError.length ? theme.red : (workspace.loaded ? theme.green : theme.textSoft)
                        font.pixelSize: theme.labelSize
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: workspace.sourceFormat.length ? workspace.sourceFormat.toUpperCase() : "NO SOURCE"
                        color: theme.muted
                        font.pixelSize: 9
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10

                SurfaceCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 190
                    theme: root.theme

                    GridLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        columns: 4
                        columnSpacing: 14
                        rowSpacing: 8

                        Label { text: "Edition"; color: theme.muted; font.pixelSize: theme.captionSize }
                        Label { text: workspace.editionText; color: theme.text; font.pixelSize: theme.labelSize; font.weight: Font.DemiBold }
                        Label { text: "Header ID"; color: theme.muted; font.pixelSize: theme.captionSize }
                        Label { text: workspace.headerId || "—"; color: theme.text; font.pixelSize: theme.labelSize; elide: Text.ElideRight; Layout.fillWidth: true }

                        Label { text: "Namespace"; color: theme.muted; font.pixelSize: theme.captionSize }
                        Label {
                            text: workspace.namespaceUri || "—"
                            color: theme.textSoft
                            font.pixelSize: 9
                            elide: Text.ElideMiddle
                            Layout.columnSpan: 3
                            Layout.fillWidth: true
                        }

                        Label { text: "Header version"; color: theme.muted; font.pixelSize: theme.captionSize }
                        Label { text: workspace.headerVersion || "—"; color: theme.text; font.pixelSize: theme.labelSize }
                        Label { text: "Header revision"; color: theme.muted; font.pixelSize: theme.captionSize }
                        Label { text: workspace.headerRevision || "—"; color: theme.text; font.pixelSize: theme.labelSize }

                        Repeater {
                            model: [
                                {label: "IED", value: workspace.iedCount},
                                {label: "Logical nodes", value: workspace.logicalNodeCount},
                                {label: "Model leaves", value: workspace.modelLeafCount},
                                {label: "DataSets", value: workspace.dataSetCount},
                                {label: "Reports", value: workspace.reportCount},
                                {label: "GOOSE", value: workspace.gooseCount},
                                {label: "SMV", value: workspace.smvCount}
                            ]
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 1
                                Label { text: modelData.label; color: theme.muted; font.pixelSize: 9 }
                                Label {
                                    text: String(modelData.value)
                                    color: theme.text
                                    font.pixelSize: theme.subtitleSize
                                    font.weight: Font.DemiBold
                                }
                            }
                        }
                    }
                }

                SurfaceCard {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    theme: root.theme

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: "Preservation / reconstruction report"
                                color: theme.text
                                font.pixelSize: theme.labelSize
                                font.weight: Font.DemiBold
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: workspace.reconstructionSupported ? "CANONICAL READY" : "CANONICAL GUARDED"
                                color: workspace.reconstructionSupported ? theme.green : theme.amber
                                font.pixelSize: 9
                                font.weight: Font.Bold
                            }
                        }

                        ListView {
                            id: reportList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 4
                            model: workspace.preservationReport
                            delegate: Rectangle {
                                required property string modelData
                                width: reportList.width
                                height: reportText.implicitHeight + 14
                                radius: 5
                                color: modelData.indexOf("fail-closed") >= 0 || modelData.indexOf("not vendor-lossless") >= 0
                                       ? theme.amberSoft : theme.chrome
                                border.width: 1
                                border.color: theme.lineSoft
                                Label {
                                    id: reportText
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    text: modelData
                                    wrapMode: Text.Wrap
                                    color: theme.textSoft
                                    font.pixelSize: theme.captionSize
                                }
                            }
                            ScrollBar.vertical: ScrollBar {}
                        }
                    }
                }
            }

            SurfaceCard {
                Layout.preferredWidth: 410
                Layout.fillHeight: true
                theme: root.theme

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    Label {
                        text: "Export policy"
                        color: theme.text
                        font.pixelSize: theme.subtitleSize
                        font.weight: Font.DemiBold
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 82
                        radius: 6
                        color: workspace.exactSourceSaveSupported ? theme.greenSoft : theme.surfaceSoft
                        border.width: 1
                        border.color: workspace.exactSourceSaveSupported ? theme.green : theme.line
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 9
                            spacing: 2
                            Label {
                                text: "EXACT SOURCE SAVE"
                                color: workspace.exactSourceSaveSupported ? theme.green : theme.textSoft
                                font.pixelSize: 9
                                font.weight: Font.Bold
                            }
                            Label {
                                Layout.fillWidth: true
                                text: "Byte-identical source copy. Same edition and same extension/profile only; vendor XML and unmodeled content remain untouched."
                                wrapMode: Text.Wrap
                                color: theme.textSoft
                                font.pixelSize: theme.captionSize
                            }
                        }
                    }

                    ActionButton {
                        Layout.fillWidth: true
                        theme: root.theme
                        text: "Save exact source copy"
                        enabled: workspace.exactSourceSaveSupported && !workspace.busy
                        onClicked: {
                            saveDialog.canonicalRequested = false
                            saveDialog.open()
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: theme.lineSoft }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 108
                        radius: 6
                        color: workspace.reconstructionSupported ? theme.greenSoft : theme.amberSoft
                        border.width: 1
                        border.color: workspace.reconstructionSupported ? theme.green : theme.amber
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 9
                            spacing: 2
                            Label {
                                text: "CANONICAL RECONSTRUCTION"
                                color: workspace.reconstructionSupported ? theme.green : theme.amber
                                font.pixelSize: 9
                                font.weight: Font.Bold
                            }
                            Label {
                                Layout.fillWidth: true
                                text: workspace.reconstructionSupported
                                      ? "Rebuilds modeled DTT/references/DataSets/Reports/GOOSE/SMV and Communication. Output is reparsed + semantic-verified; normalized, not vendor-lossless."
                                      : "This source contains a model that cannot be reconstructed safely. Exact source preservation remains available."
                                wrapMode: Text.Wrap
                                color: theme.textSoft
                                font.pixelSize: theme.captionSize
                            }
                        }
                    }

                    Label { text: "Canonical target edition"; color: theme.textSoft; font.pixelSize: theme.captionSize }
                    ComboBox {
                        id: editionPicker
                        Layout.fillWidth: true
                        model: ["Preserve source edition", "Edition 1", "Edition 2", "Edition 2.1"]
                        enabled: workspace.loaded && workspace.editionConversionSupported && !workspace.busy
                    }

                    ActionButton {
                        Layout.fillWidth: true
                        theme: root.theme
                        text: "Export canonical SCL / ICD / CID"
                        primary: true
                        enabled: workspace.reconstructionSupported && !workspace.busy
                        onClicked: {
                            saveDialog.canonicalRequested = true
                            saveDialog.open()
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: workspace.profileConversionSupported
                              ? "Profile conversion: SCD + single-IED ICD/CID enabled."
                              : "ICD/CID requires exactly one modeled IED; SCD remains the canonical multi-IED target."
                        wrapMode: Text.Wrap
                        color: workspace.profileConversionSupported ? theme.green : theme.textSoft
                        font.pixelSize: 9
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: theme.lineSoft }

                    Label {
                        visible: workspace.lastExportVerified
                        Layout.fillWidth: true
                        text: "VERIFIED · " + workspace.lastExportMode + " · " + workspace.lastExportPath
                        wrapMode: Text.WrapAnywhere
                        color: theme.green
                        font.pixelSize: theme.captionSize
                        font.weight: Font.DemiBold
                    }
                    Label {
                        visible: workspace.lastError.length > 0
                        Layout.fillWidth: true
                        text: workspace.lastError
                        wrapMode: Text.Wrap
                        color: theme.red
                        font.pixelSize: theme.captionSize
                    }

                    Item { Layout.fillHeight: true }

                    Label {
                        Layout.fillWidth: true
                        text: "Safety boundary: generic Enum ordinal domains and ambiguous nested-SDO parent typing fail closed instead of inventing engineering values. Exact source preservation is the fidelity path for vendor-specific XML."
                        wrapMode: Text.Wrap
                        color: theme.muted
                        font.pixelSize: 9
                    }
                }
            }
        }
    }
}
