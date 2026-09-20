// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    required property var theme
    required property var client

    color: theme.background

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 10

        Label {
            text: "Configured GOOSE"
            color: theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }

        Label {
            Layout.fillWidth: true
            text: "This Browser node is reserved for configured GOOSE control-block inspection from the engineering model. Network capture remains in Sniffer and publication remains in IED Simulator."
            color: theme.textSoft
            font.pixelSize: 10
            wrapMode: Text.WordWrap
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.lineSoft }

        Label {
            Layout.fillWidth: true
            text: client.connected
                  ? "IED model connected. Configured GOOSE inventory is not synthesized from wire traffic; it will be populated only from explicit model evidence."
                  : "Connect or open an engineering model to inspect configured GOOSE."
            color: theme.muted
            font.pixelSize: 9
            wrapMode: Text.WordWrap
        }

        Item { Layout.fillHeight: true }
    }
}
