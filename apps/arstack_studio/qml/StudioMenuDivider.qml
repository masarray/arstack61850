// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

MenuSeparator {
    id: separator
    property var theme
    topPadding: 2
    bottomPadding: 2
    leftPadding: 10
    rightPadding: 10
    contentItem: Rectangle {
        implicitHeight: 1
        color: separator.theme.line
    }
}
