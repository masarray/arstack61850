// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

Rectangle {
    property var theme
    color: theme ? theme.surface : "#11171f"
    radius: 8
    border.width: 1
    border.color: theme ? theme.lineSoft : "#1c252f"
}