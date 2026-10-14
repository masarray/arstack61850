// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Pure presentation over canonical typed evidence. No hex parsing or MMS reads.
ColumnLayout {
    id: root
    required property var theme
    property var engineeringEvidence: ({})
    readonly property bool hasEvidence: Boolean(engineeringEvidence)
        && (engineeringEvidence.kind === "quality"
            || engineeringEvidence.kind === "timestamp")
    visible: hasEvidence
    spacing: 0
    Layout.fillWidth: true

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 27
        color: theme.surfaceRaised
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            Label {
                Layout.fillWidth: true
                text: root.engineeringEvidence.kind === "quality"
                      ? "Quality Details · MMS BIT STRING"
                      : "Timestamp & TimeQuality · MMS UTC"
                color: theme.text
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }
            Label {
                text: "READ EVIDENCE"
                color: theme.muted
                font.pixelSize: 9
            }
        }
    }
    Repeater {
        model: root.hasEvidence && root.engineeringEvidence.rows
               ? root.engineeringEvidence.rows : []
        delegate: Rectangle {
            id: evidenceRow
            required property int index
            required property var modelData
            Layout.fillWidth: true
            implicitHeight: 23
            color: index % 2 ? theme.surfaceSoft : theme.surface
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 9
                anchors.rightMargin: 9
                spacing: 6
                Label {
                    Layout.preferredWidth: Math.max(154, root.width * 0.4)
                    text: evidenceRow.modelData.label || ""
                    color: theme.textSoft
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: evidenceRow.modelData.value === undefined
                          || evidenceRow.modelData.value === null
                          ? "Unknown" : String(evidenceRow.modelData.value)
                    color: theme.text
                    font.pixelSize: 10
                    elide: Text.ElideMiddle
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                height: 1
                width: parent.width
                color: theme.lineSoft
            }
        }
    }
}
