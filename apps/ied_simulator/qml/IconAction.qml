// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

Button {
    id: control

    required property var theme
    property url iconSource: ""
    property bool primary: false
    property bool danger: false
    property bool compact: false

    hoverEnabled: true
    implicitHeight: compact ? 32 : 36
    implicitWidth: Math.max(compact ? 36 : 84, implicitContentWidth + (compact ? 16 : 24))
    leftPadding: compact ? 8 : 12
    rightPadding: compact ? 8 : 12
    spacing: 7

    icon.source: iconSource
    icon.width: compact ? 15 : 16
    icon.height: compact ? 15 : 16
    icon.color: {
        if (!enabled) return theme.muted
        if (primary) return "#ffffff"
        if (danger) return theme.red
        return theme.textSoft
    }
    display: text.length === 0 ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon

    palette.buttonText: {
        if (!enabled) return theme.muted
        if (primary) return "#ffffff"
        if (danger) return theme.red
        return theme.text
    }

    font.pixelSize: theme.labelSize
    font.weight: Font.DemiBold

    background: Rectangle {
        radius: 8
        border.width: 1
        border.color: {
            if (!control.enabled) return control.theme.lineSoft
            if (control.primary) return control.hovered ? control.theme.accentHover : control.theme.accent
            if (control.danger) return control.hovered ? control.theme.red : control.theme.redSoft
            return control.hovered ? control.theme.accent : control.theme.lineSoft
        }
        color: {
            if (!control.enabled) return control.theme.surfaceRaised
            if (control.primary) return control.pressed ? control.theme.navigationDark : (control.hovered ? control.theme.accentHover : control.theme.accent)
            if (control.danger) return control.hovered ? control.theme.redSoft : control.theme.surface
            return control.pressed ? control.theme.surfaceSoft : (control.hovered ? control.theme.accentSoft : control.theme.surface)
        }
    }
}
