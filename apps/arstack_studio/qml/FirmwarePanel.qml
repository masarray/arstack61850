// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SurfacePanel {
    id: panel
    property var device
    property var firmware
    property string uiFont: "Inter"
    property string monoFont: "Inter"
    property bool technicalDetailsVisible: false

    readonly property bool hasPort: firmwarePort.currentText.length > 0
    readonly property bool canInstall:
        panel.firmware.bundleReady && panel.firmware.targetVerified &&
        !panel.firmware.busy && panel.hasPort &&
        firmwarePort.currentText === panel.firmware.selectedPort

    Timer {
        id: reconnectTimer
        interval: 1600
        repeat: false
        onTriggered: panel.device.autoDetectAndConnect()
    }

    Connections {
        target: panel.firmware
        function onInstallationFinished(resetSucceeded) {
            if (resetSucceeded)
                reconnectTimer.restart()
        }
    }

    component StepBadge: Rectangle {
        property string stepText: "1"
        property bool complete: false
        implicitWidth: 28
        implicitHeight: 28
        radius: 14
        color: complete ? "#173c2c" : panel.theme.accentSoft
        border.width: 1
        border.color: complete ? "#347a59" : "#315f8d"
        Label {
            anchors.centerIn: parent
            text: parent.complete ? "✓" : parent.stepText
            color: parent.complete ? panel.theme.green : panel.theme.text
            font.family: panel.uiFont
            font.pixelSize: 11
            font.weight: Font.Bold
        }
    }

    component DarkComboBox: ComboBox {
        id: combo
        implicitHeight: 38
        font.family: panel.uiFont
        font.pixelSize: 10
        leftPadding: 12
        rightPadding: 34
        contentItem: Text {
            leftPadding: 2
            rightPadding: 2
            text: combo.displayText
            color: combo.enabled ? panel.theme.text : panel.theme.muted2
            font: combo.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Label {
            x: combo.width - width - 12
            y: (combo.height - height) / 2
            text: "⌄"
            color: combo.enabled ? panel.theme.textSoft : panel.theme.muted2
            font.family: panel.uiFont
            font.pixelSize: 13
        }
        background: Rectangle {
            radius: 7
            color: combo.hovered ? panel.theme.raisedHover : panel.theme.surface2
            border.width: 1
            border.color: combo.activeFocus ? panel.theme.accent : panel.theme.line
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    text: "FIRMWARE RECOVERY"
                    color: panel.theme.muted
                    font.family: panel.uiFont
                    font.pixelSize: 8
                    font.weight: Font.Bold
                    font.letterSpacing: 0.9
                }
                Label {
                    text: "Install ARStack firmware"
                    color: panel.theme.text
                    font.family: panel.uiFont
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.fillWidth: true
                    text: panel.device.deviceVerified
                        ? "The connected injector is recognized. Use this page to reinstall or recover firmware when required."
                        : "Use this when the board is new, blank, or still running older firmware. It does not need to identify as an ARStack injector first."
                    color: panel.theme.textSoft
                    font.family: panel.uiFont
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }
            }

            Rectangle {
                implicitWidth: 126
                implicitHeight: 30
                radius: 7
                color: panel.firmware.bundleReady ? "#112a20" : "#2a2112"
                border.width: 1
                border.color: panel.firmware.bundleReady ? "#2c674e" : "#705827"
                Label {
                    anchors.centerIn: parent
                    text: panel.firmware.bundleReady ? "PACKAGE READY" : "PACKAGE BLOCKED"
                    color: panel.firmware.bundleReady ? panel.theme.green : panel.theme.amber
                    font.family: panel.uiFont
                    font.pixelSize: 8
                    font.weight: Font.Bold
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 62
            radius: 8
            color: panel.theme.surface2
            border.width: 1
            border.color: panel.theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 11
                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: panel.firmware.bundleReady ? panel.theme.green : panel.theme.amber
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Label {
                        text: panel.firmware.bundleReady
                            ? "ARStack firmware v" + panel.firmware.firmwareVersion + " is bundled and verified"
                            : "Firmware package is not ready"
                        color: panel.theme.text
                        font.family: panel.uiFont
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.fillWidth: true
                        text: panel.firmware.bundleStatus
                        color: panel.theme.muted
                        font.family: panel.uiFont
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 122
            radius: 9
            color: panel.theme.surface2
            border.width: 1
            border.color: panel.firmware.targetVerified ? "#2c674e" : panel.theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 13

                StepBadge {
                    stepText: "1"
                    complete: panel.firmware.targetVerified
                    Layout.alignment: Qt.AlignTop
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 7
                    Label {
                        text: "Choose the ESP32-P4 board"
                        color: panel.theme.text
                        font.family: panel.uiFont
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.fillWidth: true
                        text: panel.firmware.targetVerified
                            ? panel.firmware.targetChip + " verified on " + panel.firmware.selectedPort
                            : "Select the USB COM port, then check the chip directly through its ROM bootloader. Old ARStack firmware is not required for this check."
                        color: panel.firmware.targetVerified ? panel.theme.green : panel.theme.muted
                        font.family: panel.uiFont
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        DarkComboBox {
                            id: firmwarePort
                            Layout.preferredWidth: 190
                            model: panel.device.ports
                            enabled: !panel.firmware.busy
                            onPressedChanged: if (pressed) panel.device.refreshPorts()
                        }
                        CalmButton {
                            theme: panel.theme
                            uiFont: panel.uiFont
                            text: "Refresh"
                            enabled: !panel.firmware.busy
                            onClicked: panel.device.refreshPorts()
                        }
                        CalmButton {
                            theme: panel.theme
                            uiFont: panel.uiFont
                            text: panel.firmware.busy ? "Checking…" : (panel.firmware.targetVerified ? "Board verified" : "Check board")
                            tone: panel.firmware.targetVerified ? "success" : "accent"
                            enabled: !panel.firmware.busy && panel.hasPort
                            toolTipText: "Read ESP32-P4 chip identity and silicon revision from the selected USB port"
                            onClicked: {
                                if (panel.device.connected)
                                    panel.device.disconnectPort()
                                panel.firmware.probeTarget(firmwarePort.currentText)
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 118
            radius: 9
            color: panel.canInstall ? "#10251d" : panel.theme.surface2
            border.width: 1
            border.color: panel.canInstall ? "#347a59" : panel.theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 13

                StepBadge {
                    stepText: "2"
                    complete: panel.device.deviceVerified && panel.device.protocolVersion === panel.firmware.expectedProtocol
                    Layout.alignment: Qt.AlignTop
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Label {
                        text: "Install firmware"
                        color: panel.theme.text
                        font.family: panel.uiFont
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.fillWidth: true
                        text: panel.canInstall
                            ? "Board safety check passed. Install the bundled ARStack firmware, then Studio will reset and reconnect automatically."
                            : "Check the board first. Installation stays locked until an ESP32-P4 target allowed by this release is verified."
                        color: panel.canInstall ? panel.theme.textSoft : panel.theme.muted
                        font.family: panel.uiFont
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                    }
                }

                CalmButton {
                    theme: panel.theme
                    uiFont: panel.uiFont
                    implicitWidth: 178
                    implicitHeight: 42
                    text: panel.firmware.busy ? "Working…" : "Install ARStack v" + panel.firmware.firmwareVersion
                    tone: "success"
                    enabled: panel.canInstall
                    toolTipText: panel.canInstall
                        ? "Flash the verified ARStack recovery image to " + firmwarePort.currentText
                        : "Complete Check board first"
                    onClicked: panel.firmware.installFirmware(firmwarePort.currentText)
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: panel.firmware.status.length > 0 ? 50 : 0
            visible: panel.firmware.status.length > 0
            radius: 7
            color: panel.firmware.targetVerified ? "#10251d" : "#171d25"
            border.width: 1
            border.color: panel.firmware.targetVerified ? "#2c674e" : panel.theme.lineSoft
            Label {
                anchors.fill: parent
                anchors.margins: 11
                text: panel.firmware.status
                color: panel.firmware.targetVerified ? panel.theme.green : panel.theme.textSoft
                font.family: panel.uiFont
                font.pixelSize: 9
                wrapMode: Text.WordWrap
                verticalAlignment: Text.AlignVCenter
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            CalmButton {
                theme: panel.theme
                uiFont: panel.uiFont
                text: panel.technicalDetailsVisible ? "Hide technical details" : "Technical details"
                onClicked: panel.technicalDetailsVisible = !panel.technicalDetailsVisible
            }
            Label {
                Layout.fillWidth: true
                text: "Protocol, SHA-256 and raw flash log"
                color: panel.theme.muted
                font.family: panel.uiFont
                font.pixelSize: 9
            }
            CalmButton {
                visible: panel.firmware.busy
                theme: panel.theme
                uiFont: panel.uiFont
                text: "Cancel"
                tone: "danger"
                onClicked: panel.firmware.cancel()
            }
        }

        Rectangle {
            visible: panel.technicalDetailsVisible
            Layout.fillWidth: true
            implicitHeight: 64
            radius: 7
            color: panel.theme.surface2
            border.width: 1
            border.color: panel.theme.lineSoft
            GridLayout {
                anchors.fill: parent
                anchors.margins: 10
                columns: 4
                columnSpacing: 12
                rowSpacing: 4
                Label { text: "Protocol"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                Label { text: panel.firmware.expectedProtocol === "-" ? "-" : "v" + panel.firmware.expectedProtocol; color: panel.theme.text; font.family: panel.uiFont; font.pixelSize: 9 }
                Label { text: "Target"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                Label { text: panel.firmware.targetChip; color: panel.theme.textSoft; font.family: panel.uiFont; font.pixelSize: 9 }
                Label { text: "SHA-256"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8 }
                Label { Layout.columnSpan: 3; text: panel.firmware.firmwareSha256.length ? panel.firmware.firmwareSha256 : "-"; color: panel.theme.textSoft; font.family: panel.monoFont; font.pixelSize: 8; elide: Text.ElideMiddle }
            }
        }

        ColumnLayout {
            visible: panel.technicalDetailsVisible
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                Label { text: "FLASH LOG"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8; font.weight: Font.Bold; font.letterSpacing: 0.7 }
                Item { Layout.fillWidth: true }
                CalmButton { theme: panel.theme; uiFont: panel.uiFont; text: "Clear"; onClicked: panel.firmware.clearLog() }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 120
                TextArea {
                    readOnly: true
                    text: panel.firmware.logText
                    color: panel.theme.textSoft
                    selectionColor: panel.theme.accent
                    font.family: panel.monoFont
                    font.pixelSize: 8
                    wrapMode: TextEdit.WrapAnywhere
                    background: Rectangle { color: "#090e14"; radius: 6; border.width: 1; border.color: panel.theme.lineSoft }
                }
            }
        }

        Item {
            visible: !panel.technicalDetailsVisible
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
