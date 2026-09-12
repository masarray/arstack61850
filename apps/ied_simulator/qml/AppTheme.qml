// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

QtObject {
    // Dense engineering-workbench palette.  The simulator intentionally uses
    // a light data canvas with a strong navigation blue so long IEC 61850
    // references remain readable during commissioning sessions.
    readonly property color background: "#e7ebef"
    readonly property color chrome: "#f4f6f8"
    readonly property color surface: "#ffffff"
    readonly property color surfaceRaised: "#eef1f4"
    readonly property color surfaceSoft: "#e3e7eb"
    readonly property color line: "#b8c0c8"
    readonly property color lineSoft: "#d4d9de"
    readonly property color text: "#17212b"
    readonly property color textSoft: "#3e4b57"
    readonly property color muted: "#6d7984"
    readonly property color accent: "#1769aa"
    readonly property color accentHover: "#0f5c99"
    readonly property color accentSoft: "#dbeaf7"
    readonly property color navigation: "#155f9f"
    readonly property color navigationDark: "#0f4f87"
    readonly property color navigationText: "#f7fbff"
    readonly property color navigationMuted: "#c5ddf1"
    readonly property color green: "#17864b"
    readonly property color greenSoft: "#e0f3e8"
    readonly property color amber: "#b77800"
    readonly property color amberSoft: "#fff1cf"
    readonly property color red: "#c63d48"
    readonly property color redSoft: "#fbe3e6"
    readonly property color statusChrome: "#2b2d2f"
    readonly property color statusText: "#f7f7f7"
    readonly property int panelRadius: 3
    readonly property int controlRadius: 2
    readonly property int controlHeight: 32
    readonly property int captionSize: 11
    readonly property int labelSize: 12
    readonly property int bodySize: 13
    readonly property int subtitleSize: 15
    readonly property int titleSize: 20
}
