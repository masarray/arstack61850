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
    property bool large: false
    property bool iconOnly: false
    property string toolTipText: ""

    implicitWidth: iconOnly ? 32 : large ? Math.max(74, label.implicitWidth + 40) : Math.max(54, label.implicitWidth + (icon.visible ? 32 : 20))
    implicitHeight: large ? 38 : 32
    hoverEnabled: true
    activeFocusOnTab: true
    font.family: uiFont
    font.pixelSize: large ? 11 : 9
    font.weight: Font.DemiBold

    readonly property color fillColor: !enabled ? "transparent"
        : tone === "success" ? (hovered ? "#194433" : "#14372a")
        : tone === "danger" ? (hovered ? "#46252d" : "#351e24")
        : tone === "accent" ? (hovered ? "#15324d" : "#11283d")
        : tone === "warning" ? (hovered ? "#41351f" : "#312919")
        : checked ? "#132b42"
        : hovered ? theme.raisedHover : "transparent"
    readonly property color edgeColor: !enabled ? "transparent"
        : tone === "success" ? "#2b684e"
        : tone === "danger" ? "#713843"
        : tone === "accent" ? "#2b567f"
        : tone === "warning" ? "#715a2f"
        : checked ? "#294f74"
        : activeFocus ? theme.accent : "transparent"
    readonly property color textColor: !enabled ? theme.muted2
        : tone === "success" ? "#d9f7e9"
        : tone === "danger" ? "#ffdce1"
        : tone === "accent" ? "#ddebff"
        : tone === "warning" ? "#f0d49a"
        : checked ? theme.text : theme.textSoft

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight

        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: control.iconOnly ? 0 : 6

            Image {
                id: icon
                visible: control.iconSource.toString().length > 0
                source: control.iconSource
                Layout.preferredWidth: control.large ? 17 : 14
                Layout.preferredHeight: Layout.preferredWidth
                sourceSize.width: Layout.preferredWidth * 2
                sourceSize.height: Layout.preferredHeight * 2
                opacity: control.enabled ? 0.92 : 0.28
            }

            Text {
                id: label
                visible: !control.iconOnly
                text: control.text
                color: control.textColor
                font: control.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
    }

    background: Rectangle {
        radius: 6
        color: control.fillColor
        border.width: control.edgeColor === "transparent" ? 0 : 1
        border.color: control.edgeColor
        Behavior on color { ColorAnimation { duration: 90 } }
        Behavior on border.color { ColorAnimation { duration: 90 } }
    }

    ToolTip.visible: hovered && toolTipText.length > 0
    ToolTip.text: toolTipText
    ToolTip.delay: 420
}
