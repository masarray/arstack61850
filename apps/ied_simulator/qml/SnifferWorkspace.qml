// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ARStack.IedSimulator 1.0

Item {
    id: root
    required property var theme
    required property GooseMonitorController monitor

    MmsPassiveSnifferController {
        id: mmsMonitor
        objectName: "passiveMmsSnifferBackend"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TabBar {
            id: protocolTabs
            Layout.fillWidth: true
            currentIndex: 0
            TabButton { text: "GOOSE · Layer 2" }
            TabButton { text: "MMS · TCP/102" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: protocolTabs.currentIndex

            GooseWorkspace {
                theme: root.theme
                monitor: root.monitor
            }

            MmsSnifferWorkspace {
                theme: root.theme
                sniffer: mmsMonitor
            }
        }
    }
}
