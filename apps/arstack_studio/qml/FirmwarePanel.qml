// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SurfacePanel {
    id: panel
    property var device
    property var firmware
    property var session
    property string uiFont: "Inter"
    property string monoFont: "Inter"
    property bool technicalDetailsVisible: false

    readonly property bool hasPort: firmwarePort.currentText.length > 0

    component StepBadge: Rectangle {
        property string stepText: "1"
        property bool complete: false
        implicitWidth: 30
        implicitHeight: 30
        radius: 15
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
        implicitHeight: 40
        font.family: panel.uiFont
        font.pixelSize: 11
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

    Component.onCompleted: if (panel.session) panel.session.requestRefreshPorts()

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
                    font.pixelSize: 9
                    font.weight: Font.Bold
                    font.letterSpacing: 0.9
                }
                Label {
                    text: "Session-controlled firmware service"
                    color: panel.theme.text
                    font.family: panel.uiFont
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.fillWidth: true
                    text: "Firmware probing, writing and reset are owned by the guided device session. Advanced view is diagnostic only, preventing a second path from competing for the serial port."
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
                            ? "ARStack firmware v" + panel.firmware.firmwareVersion + " · build " + panel.firmware.firmwareBuildId + " is verified in this Studio package"
                            : "Firmware package is not ready"
                        color: panel.theme.text
                        font.family: panel.uiFont
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.fillWidth: true
                        text: panel.firmware.bundleReady
                            ? "Manifest target, revision policy and SHA-256 are checked before the guided session can write."
                            : panel.firmware.bundleStatus
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
            implicitHeight: 126
            radius: 9
            color: panel.theme.surface2
            border.width: 1
            border.color: panel.theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 13

                StepBadge {
                    stepText: "1"
                    complete: panel.device.deviceVerified
                    Layout.alignment: Qt.AlignTop
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 7
                    Label {
                        text: "Observe the connected device"
                        color: panel.theme.text
                        font.family: panel.uiFont
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.fillWidth: true
                        text: panel.device.deviceVerified
                            ? panel.device.deviceProduct + " · " + panel.device.deviceTarget + " · firmware v" + panel.device.firmwareVersion
                            : (panel.hasPort
                                ? firmwarePort.currentText + " is visible. Device identity and recovery decisions remain owned by the guided session."
                                : "Connect the ESP32-P4 programming USB cable, then refresh the port list.")
                        color: panel.device.deviceVerified ? panel.theme.green : panel.theme.muted
                        font.family: panel.uiFont
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        DarkComboBox {
                            id: firmwarePort
                            Layout.preferredWidth: 205
                            model: panel.device.ports
                            enabled: panel.session && !panel.firmware.busy
                            onPressedChanged: if (pressed && panel.session) panel.session.requestRefreshPorts()
                        }
                        CalmButton {
                            theme: panel.theme
                            uiFont: panel.uiFont
                            text: "Refresh"
                            enabled: panel.session && !panel.firmware.busy
                            onClicked: panel.session.requestRefreshPorts()
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
            color: panel.theme.surface2
            border.width: 1
            border.color: panel.firmware.busy ? panel.theme.accent : panel.theme.lineSoft

            RowLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 13

                StepBadge {
                    stepText: "2"
                    complete: panel.device.deviceVerified &&
                              panel.device.protocolVersion === panel.firmware.expectedProtocol
                    Layout.alignment: Qt.AlignTop
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Label {
                        text: panel.firmware.busy ? "Guided firmware operation in progress" : "Use the guided firmware action"
                        color: panel.theme.text
                        font.family: panel.uiFont
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.fillWidth: true
                        text: panel.firmware.busy
                            ? "The session supervisor currently owns the firmware tool. Keep USB connected until Studio reports a terminal result."
                            : "When Studio reports Firmware required or Firmware update, use that guided action from the main workflow. It will stop output, obtain an acknowledged serial release, verify the ESP32-P4, then write."
                        color: panel.theme.textSoft
                        font.family: panel.uiFont
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                }

                CalmButton {
                    id: firmwareAction
                    theme: panel.theme
                    uiFont: panel.uiFont
                    implicitWidth: 172
                    text: panel.session && panel.session.firmwareUpdateRequired
                        ? "Update firmware"
                        : "Reinstall current"
                    visible: panel.session &&
                             (panel.session.firmwareUpdateRequired || panel.session.firmwareReinstallAvailable)
                    enabled: visible && !panel.firmware.busy
                    tone: panel.session && panel.session.firmwareUpdateRequired ? "accent" : "normal"
                    onClicked: {
                        if (panel.session.firmwareUpdateRequired)
                            panel.session.beginFirmwareUpdate()
                        else
                            panel.session.beginFirmwareReinstall()
                    }
                }

                Rectangle {
                    visible: !firmwareAction.visible
                    implicitWidth: 150
                    implicitHeight: 34
                    radius: 7
                    color: panel.firmware.busy ? panel.theme.accentSoft : panel.theme.raised
                    border.width: 1
                    border.color: panel.firmware.busy ? panel.theme.accent : panel.theme.line
                    Label {
                        anchors.centerIn: parent
                        text: panel.firmware.busy ? "SESSION OWNED" : "GUIDED ONLY"
                        color: panel.firmware.busy ? panel.theme.accent : panel.theme.textSoft
                        font.family: panel.uiFont
                        font.pixelSize: 9
                        font.weight: Font.Bold
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: panel.firmware.status.length > 0 ? 52 : 0
            visible: panel.firmware.status.length > 0
            radius: 7
            color: panel.firmware.targetVerified ? "#10251d" : (panel.firmware.bootloaderHelpNeeded ? "#261f13" : "#171d25")
            border.width: 1
            border.color: panel.firmware.targetVerified ? "#2c674e" : (panel.firmware.bootloaderHelpNeeded ? "#705827" : panel.theme.lineSoft)
            Label {
                anchors.fill: parent
                anchors.margins: 11
                text: panel.firmware.status
                color: panel.firmware.targetVerified ? panel.theme.green : (panel.firmware.bootloaderHelpNeeded ? panel.theme.amber : panel.theme.textSoft)
                font.family: panel.uiFont
                font.pixelSize: 10
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
                text: "Protocol, target revision, SHA-256 and firmware-service log"
                color: panel.theme.muted
                font.family: panel.uiFont
                font.pixelSize: 9
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
                Label { text: "FIRMWARE SERVICE LOG"; color: panel.theme.muted; font.family: panel.uiFont; font.pixelSize: 8; font.weight: Font.Bold; font.letterSpacing: 0.7 }
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
                    font.pixelSize: 9
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
