// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

QtObject {
    readonly property color bg: "#090d12"
    readonly property color chrome: "#0c1117"
    readonly property color surface: "#11171f"
    readonly property color surface2: "#0e141b"
    readonly property color raised: "#151d27"
    readonly property color raisedHover: "#1a2430"
    readonly property color line: "#26313d"
    readonly property color lineSoft: "#1c252f"
    readonly property color text: "#edf2f7"
    readonly property color textSoft: "#c2ccd7"
    readonly property color muted: "#778493"
    readonly property color muted2: "#566271"
    readonly property color accent: "#69a9ff"
    readonly property color accentSoft: "#18314c"
    readonly property color green: "#58d49d"
    readonly property color greenSoft: "#153a2d"
    readonly property color amber: "#e1b25a"
    readonly property color amberSoft: "#3b301c"
    readonly property color red: "#ff727f"
    readonly property color redSoft: "#46252b"

    // Shared density and typography tokens. Keep engineering data compact,
    // but never depend on tiny text to make the workspace fit.
    readonly property int captionSize: 10
    readonly property int labelSize: 11
    readonly property int bodySize: 12
    readonly property int subtitleSize: 15
    readonly property int titleSize: 20
    readonly property int controlHeight: 36
    readonly property int panelRadius: 10
    readonly property int controlRadius: 7
}
