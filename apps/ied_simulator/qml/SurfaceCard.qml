// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

Rectangle {
    property var theme
    color: theme.surface
    radius: theme.panelRadius
    border.width: 1
    border.color: theme.line
}
