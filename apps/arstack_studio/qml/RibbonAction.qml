// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: control
    property var theme
    property string uiFont: "Inter"
    property string tone: "neutral"
    property url iconSource
    property bool large: true
    property bool iconOnly: false
    property string toolTipText: ""
    readonly property string hoverPath: iconSource.toString().replace("/assets/material/", "/assets/material_dark/")
    implicitWidth: iconOnly ? 60 : Math.max(67, caption.implicitWidth + 20)
    implicitHeight: 65
    hoverEnabled: true
    activeFocusOnTab: true
    font.family: uiFont
    font.pixelSize: 10
    font.weight: Font.DemiBold
    readonly property color fillColor: !enabled ? "transparent"
        : hovered ? "#e9f2fd"
        : tone === "success" ? "#173d30"
        : tone === "danger" ? "#3b2027"
        : tone === "accent" || checked ? "#163451"
        : tone === "warning" ? "#45351b" : "transparent"
    readonly property color edgeColor: !enabled ? "transparent"
        : hovered ? "#a6c9f2"
        : tone === "success" ? "#367e5e"
        : tone === "danger" ? "#8b4b56"
        : tone === "accent" || checked ? "#34638a"
        : tone === "warning" ? "#806536" : "transparent"
    readonly property color textColor: !enabled ? theme.muted2
        : hovered ? "#142334"
        : tone === "success" ? "#dbfaec"
        : tone === "warning" ? "#f5daaa"
        : tone === "danger" ? "#ffe3e6"
        : tone === "accent" || checked ? "#e1f1ff" : theme.textSoft
    contentItem: ColumnLayout {
        anchors.centerIn: parent
        spacing: 4
        Image {
            visible: control.iconSource.toString().length > 0
            source: control.hovered && control.enabled ? control.hoverPath : control.iconSource
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 24
            Layout.preferredHeight: 24
            sourceSize.width: 48
            sourceSize.height: 48
            opacity: control.enabled ? 1 : 0.3
        }
        Text {
            id: caption
            visible: !control.iconOnly
            text: control.text
            color: control.textColor
            font: control.font
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
    background: Rectangle {
        radius: 9
        color: control.fillColor
        border.width: control.edgeColor === "transparent" ? 0 : 1
        border.color: control.edgeColor
        Behavior on color { ColorAnimation { duration: 90 } }
    }
    ToolTip.visible: hovered && toolTipText.length > 0
    ToolTip.text: toolTipText
    ToolTip.delay: theme.tooltipDelay
}
