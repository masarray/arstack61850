// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

QtObject {
    // ARStack Lab uses its own calm graphite + teal engineering palette.  The
    // data canvas stays light for long IEC 61850 references while navigation
    // and actions are visually distinct from vendor commissioning tools.
    readonly property color background: "#f2f6f4"
    readonly property color chrome: "#f8faf9"
    readonly property color surface: "#ffffff"
    readonly property color surfaceRaised: "#edf3f0"
    readonly property color surfaceSoft: "#e3ebe8"
    readonly property color line: "#b9c9c3"
    readonly property color lineSoft: "#d7e2de"
    readonly property color text: "#17241f"
    readonly property color textSoft: "#40534d"
    readonly property color muted: "#71827c"
    readonly property color accent: "#0f766e"
    readonly property color accentHover: "#0b625c"
    readonly property color accentSoft: "#d9f1ec"
    readonly property color navigation: "#182522"
    readonly property color navigationDark: "#101b18"
    readonly property color navigationText: "#f5faf8"
    readonly property color navigationMuted: "#b7cbc5"
    readonly property color green: "#16834b"
    readonly property color greenSoft: "#e1f5e8"
    readonly property color amber: "#b77900"
    readonly property color amberSoft: "#fff1cf"
    readonly property color red: "#c23f4b"
    readonly property color redSoft: "#fbe5e7"
    readonly property color statusChrome: "#182522"
    readonly property color statusText: "#f7fbf9"
    readonly property int panelRadius: 10
    readonly property int controlRadius: 8
    readonly property int controlHeight: 34
    readonly property int captionSize: 11
    readonly property int labelSize: 12
    readonly property int bodySize: 13
    readonly property int subtitleSize: 15
    readonly property int titleSize: 20
}
