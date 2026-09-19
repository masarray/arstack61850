// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

Item {
    id: root
    required property var theme
    required property var workspace

    SclWorkspace {
        anchors.fill: parent
        theme: root.theme
        workspace: root.workspace
    }
}
