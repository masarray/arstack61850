// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ARStack.IedSimulator 1.0

Item {
    id: root
    required property var theme
    required property GooseMonitorController monitor

    GooseWorkspace {
        anchors.fill: parent
        theme: root.theme
        monitor: root.monitor
    }
}
