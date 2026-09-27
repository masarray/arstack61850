// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Read-only entry point for both Open SCL and live MMS discovery. The catalog
// derives solely from the canonical engineering context; it never creates a
// DataSet, connects another association or enables reporting implicitly.
Dialog {
    id: root
    objectName: "iedSignalCatalog"
    required property var theme
    required property var context
    signal dataSetRequested(string reference)
    signal browseRequested()

    title: "Signal Catalog"
    modal: true
    width: Math.min(650, Overlay.overlay ? Overlay.overlay.width - 32 : 650)
    height: Math.min(520, Overlay.overlay ? Overlay.overlay.height - 32 : 520)
    anchors.centerIn: Overlay.overlay
    standardButtons: Dialog.Close

    readonly property var canonicalDataSets: context.loaded ? context.dataSets : []

    contentItem: ColumnLayout {
        spacing: 10

        Label {
            Layout.fillWidth: true
            text: root.context.iedName + "  ·  " + root.context.authority
            color: root.theme.text
            font.pixelSize: root.theme.labelSize
            font.weight: Font.DemiBold
            elide: Text.ElideMiddle
        }
        Label {
            Layout.fillWidth: true
            text: "Choose exact DataSet members or browse the complete canonical model."
            color: root.theme.textSoft
            font.pixelSize: root.theme.captionSize
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Button {
                objectName: "iedCatalogDatasetSignals"
                text: "Dataset Signals"
                enabled: root.canonicalDataSets.length > 0
                onClicked: {
                    root.dataSetRequested(root.canonicalDataSets[0].reference)
                    root.close()
                }
            }
            Button {
                objectName: "iedCatalogBrowseSignals"
                text: "Browse All Signals"
                onClicked: {
                    root.browseRequested()
                    root.close()
                }
            }
            Item { Layout.fillWidth: true }
            Label {
                text: root.canonicalDataSets.length + " DataSet(s)"
                color: root.theme.muted
                font.pixelSize: root.theme.captionSize
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.theme.lineSoft }
        Label {
            text: "DATASET SIGNALS · ORDERED IED MEMBERSHIP"
            color: root.theme.muted
            font.pixelSize: root.theme.captionSize
            font.weight: Font.DemiBold
        }

        ListView {
            id: dataSetsView
            objectName: "iedSignalCatalogDataSets"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            reuseItems: true
            model: root.canonicalDataSets
            spacing: 2
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                required property int index
                required property var modelData
                width: ListView.view.width
                height: 48
                radius: 5
                color: dsMouse.containsMouse ? root.theme.surfaceRaised : root.theme.surface
                border.width: 1
                border.color: root.theme.lineSoft
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: modelData.reference || "DataSet"
                            color: root.theme.text
                            font.pixelSize: root.theme.labelSize
                            elide: Text.ElideMiddle
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.usedByReports && modelData.usedByReports.length
                                  ? "RCB: " + modelData.usedByReports.join(", ")
                                  : "RCB binding not identified in model"
                            color: root.theme.muted
                            font.pixelSize: root.theme.captionSize
                            elide: Text.ElideMiddle
                        }
                    }
                    Label {
                        text: (modelData.memberCount || 0) + " members"
                        color: root.theme.textSoft
                        font.pixelSize: root.theme.captionSize
                    }
                    Label {
                        text: "›"
                        color: root.theme.accent
                        font.pixelSize: root.theme.labelSize
                    }
                }
                MouseArea {
                    id: dsMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        root.dataSetRequested(modelData.reference)
                        root.close()
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: dataSetsView.count === 0
                text: "No DataSets in this canonical model. Browse All Signals instead."
                color: root.theme.muted
                font.pixelSize: root.theme.labelSize
            }
        }
        Label {
            Layout.fillWidth: true
            text: "Viewing never changes IED membership or writes an RCB. Report enable and GI remain explicit actions."
            color: root.theme.muted
            font.pixelSize: root.theme.captionSize
            wrapMode: Text.WordWrap
        }
    }
}
