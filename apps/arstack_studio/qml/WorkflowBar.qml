// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SurfacePanel {
    id: workflow

    property var controller
    property var device
    property var profiles
    property string uiFont: "Inter"
    property string monoFont: "Inter"
    property bool compact: false

    function withAlpha(value, alpha) {
        return Qt.rgba(value.r, value.g, value.b, alpha)
    }

    Menu {
        id: viewMenu
        MenuItem {
            text: "Signal preview"
            checkable: true
            checked: workflow.controller.previewDockVisible || workflow.controller.previewDetached
            onTriggered: {
                workflow.controller.previewDetached = false
                workflow.controller.previewDockVisible = checked
            }
        }
        MenuItem {
            text: "Status & output monitor"
            checkable: true
            checked: workflow.controller.telemetryDockVisible
            onTriggered: workflow.controller.telemetryDockVisible = checked
        }
        MenuSeparator { }
        MenuItem {
            text: "Detach signal preview"
            enabled: workflow.controller.previewDockVisible && !workflow.controller.previewDetached
            onTriggered: workflow.controller.detachPreview()
        }
    }

    implicitHeight: 82
    color: theme.raised
    border.color: theme.line

    component StepMarker: Rectangle {
        required property string numberText
        required property string titleText
        required property string statusText
        required property color statusColor

        implicitWidth: workflow.compact ? 94 : 112
        implicitHeight: 48
        radius: workflow.theme.controlRadius
        color: "#101820"
        border.width: 1
        border.color: statusColor === workflow.theme.muted ? workflow.theme.lineSoft : workflow.withAlpha(statusColor, 0.55)

        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 7

            Rectangle {
                width: 23
                height: 23
                radius: 6
                color: workflow.withAlpha(statusColor, 0.16)
                border.width: 1
                border.color: workflow.withAlpha(statusColor, 0.48)
                Label {
                    anchors.centerIn: parent
                    text: numberText
                    color: statusColor
                    font.family: workflow.monoFont
                    font.pixelSize: workflow.theme.captionSize
                    font.weight: Font.Bold
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    text: titleText
                    color: workflow.theme.textSoft
                    font.family: workflow.uiFont
                    font.pixelSize: workflow.theme.labelSize
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: statusText
                    color: statusColor
                    font.family: workflow.monoFont
                    font.pixelSize: workflow.theme.captionSize - 1
                    elide: Text.ElideRight
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: workflow.compact ? 10 : 13
        anchors.rightMargin: workflow.compact ? 10 : 13
        spacing: workflow.compact ? 7 : 9

        ColumnLayout {
            visible: !workflow.compact
            Layout.preferredWidth: 92
            spacing: 1
            Label {
                text: "FAST WORKFLOW"
                color: workflow.theme.muted
                font.family: workflow.uiFont
                font.pixelSize: workflow.theme.captionSize - 1
                font.weight: Font.DemiBold
                font.letterSpacing: 0.9
            }
            Label {
                text: "Manual"
                color: workflow.theme.text
                font.family: workflow.uiFont
                font.pixelSize: workflow.theme.subtitleSize
                font.weight: Font.DemiBold
            }
            Label {
                text: "live setpoints"
                color: workflow.theme.muted
                font.family: workflow.uiFont
                font.pixelSize: workflow.theme.captionSize
            }
        }

        StepMarker {
            numberText: "1"
            titleText: "Source"
            statusText: workflow.profiles.hasProfiles ? "READY" : "DEV PROFILE"
            statusColor: workflow.profiles.hasProfiles ? workflow.theme.green : workflow.theme.muted
        }

        StepMarker {
            numberText: "2"
            titleText: "Device"
            statusText: workflow.device.connected ? workflow.device.portName : "OFFLINE"
            statusColor: workflow.device.connected ? workflow.theme.green : workflow.theme.muted
        }

        StepMarker {
            numberText: "3"
            titleText: "Output"
            statusText: workflow.controller.instrumentState
            statusColor: workflow.controller.instrumentStateColor
        }

        Rectangle { width: 1; height: 40; color: workflow.theme.lineSoft }

        RowLayout {
            spacing: 6

            CalmButton {
                theme: workflow.theme
                uiFont: workflow.uiFont
                text: "Open SCL"
                enabled: !workflow.device.running
                onClicked: workflow.controller.openEngineeringFile()
                ToolTip.visible: hovered
                ToolTip.text: "Open SCL, CID, SCD or IID  ·  Ctrl+O"
            }
            CalmButton {
                visible: !workflow.compact
                theme: workflow.theme
                uiFont: workflow.uiFont
                text: "Balanced"
                onClicked: workflow.controller.balanced()
                ToolTip.visible: hovered
                ToolTip.text: "Apply balanced 3-phase defaults  ·  Ctrl+B"
            }
            CalmButton {
                theme: workflow.theme
                uiFont: workflow.uiFont
                text: "Zero"
                onClicked: workflow.controller.zeroAll()
                ToolTip.visible: hovered
                ToolTip.text: "Set all magnitudes to zero  ·  Ctrl+0"
            }
            CalmButton {
                visible: !workflow.compact
                theme: workflow.theme
                uiFont: workflow.uiFont
                text: "Check"
                onClicked: workflow.controller.runReadinessCheck()
                ToolTip.visible: hovered
                ToolTip.text: "Check profile and device readiness  ·  Ctrl+K"
            }
            CalmButton {
                id: viewsButton
                theme: workflow.theme
                uiFont: workflow.uiFont
                text: "Views"
                onClicked: viewMenu.popup(viewsButton, 0, viewsButton.height)
                ToolTip.visible: hovered
                ToolTip.text: "Show, hide, or detach engineering views"
            }
        }

        Item { Layout.fillWidth: true }

        CalmButton {
            visible: !workflow.compact
            theme: workflow.theme
            uiFont: workflow.uiFont
            tone: "accent"
            text: workflow.device.profileDeploying ? "Deploying…" : "Deploy"
            enabled: workflow.controller.canDeploy
            onClicked: workflow.controller.deploySelectedProfile()
            ToolTip.visible: hovered
            ToolTip.text: "Commit the selected immutable profile  ·  Ctrl+D"
        }

        CalmButton {
            theme: workflow.theme
            uiFont: workflow.uiFont
            tone: "danger"
            text: "Stop"
            implicitWidth: 72
            implicitHeight: 42
            enabled: workflow.device.connected && workflow.device.running
            onClicked: workflow.device.stop()
            ToolTip.visible: hovered
            ToolTip.text: "Stop output  ·  F6"
        }

        CalmButton {
            theme: workflow.theme
            uiFont: workflow.uiFont
            tone: "success"
            text: "Start"
            implicitWidth: 82
            implicitHeight: 42
            enabled: workflow.controller.canStart
            onClicked: workflow.device.start()
            ToolTip.visible: hovered
            ToolTip.text: "Start armed output  ·  F5"
        }
    }
}
