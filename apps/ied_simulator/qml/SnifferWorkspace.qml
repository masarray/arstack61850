// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ARStack.IedSimulator 1.0

Item {
    id: root
    required property var theme
    required property IedFleetController simulator
    required property GooseMonitorController monitor

    signal openSimulatorRequested()

    GooseWorkspace {
        anchors.fill: parent
        theme: root.theme
        simulator: root.simulator
        monitor: root.monitor
        onOpenSimulatorRequested: root.openSimulatorRequested()
    }
}
