// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ARStack.Studio 1.0

SurfacePanel {
    id: panel
    property var device
    property var firmware
    property var session
    property string uiFont: "Inter"
    property string monoFont: "Inter"
    property bool technicalDetailsVisible: false
    readonly property string firmwareDisplayStatus:
        panel.session && panel.session.firmwareUpdateResult.length > 0 &&
        !panel.session.updatingFirmware
        ? panel.session.firmwareUpdateResult : panel.firmware.status
    readonly property bool verificationFailed:
        panel.session && panel.session.firmwareUpdateResult.length > 0 &&
        !panel.session.firmwareUpdateResult.startsWith("Exact bundled firmware build verified")

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
                    text: "Firmware & recovery"
                    color: panel.theme.text
                    font.family: panel.uiFont
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
            }


        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 46
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
                        Layout.fillWidth: true
                        text: panel.firmware.bundleReady
                            ? "Bundled firmware v" + panel.firmware.firmwareVersion + " · ready"
                            : "Firmware package unavailable"
                        elide: Text.ElideRight
                        color: panel.theme.text
                        font.family: panel.uiFont
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 112
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
                        text: "Device"
                        color: panel.theme.text
                        font.family: panel.uiFont
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.fillWidth: true
                        text: panel.device.deviceVerified
                            ? panel.device.deviceProduct + " · firmware v" + panel.device.firmwareVersion
                            : (panel.hasPort ? firmwarePort.currentText + " · not verified"
                                             : "Connect ESP32-P4 via USB.")
                        color: panel.device.deviceVerified ? panel.theme.green : panel.theme.muted
                        font.family: panel.uiFont
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        StudioDarkComboBox {
                            theme: panel.theme
                            uiFont: panel.uiFont
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
            implicitHeight: 99
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
                        text: panel.firmware.busy ? "Installing firmware" : "Firmware action"
                        color: panel.theme.text
                        font.family: panel.uiFont
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.fillWidth: true
                        text: panel.firmware.busy
                            ? "Keep USB connected until verification completes."
                            : (panel.session && panel.session.firmwareUpdateAvailable
                                ? "Current firmware works; update is optional."
                                : "Installation and recovery are supervised automatically.")
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
                    text: panel.session && (panel.session.firmwareUpdateRequired || panel.session.firmwareUpdateAvailable)
                        ? (panel.session.firmwareUpdateRequired ? "Update firmware" : "Install bundled build")
                        : "Reinstall current"
                    visible: panel.session &&
                             (panel.session.firmwareUpdateRequired || panel.session.firmwareUpdateAvailable || panel.session.firmwareReinstallAvailable)
                    enabled: visible && !panel.firmware.busy
                    tone: panel.session && panel.session.firmwareUpdateRequired ? "accent" : "normal"
                    onClicked: {
                        if (panel.session.firmwareUpdateRequired || panel.session.firmwareUpdateAvailable)
                            panel.session.beginFirmwareUpdate()
                        else
                            panel.session.beginFirmwareReinstall()
                    }
                }


            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: panel.firmwareDisplayStatus.length > 0 ? 52 : 0
            visible: panel.firmwareDisplayStatus.length > 0
            radius: 7
            color: panel.verificationFailed ? "#2a171c" : (panel.firmware.targetVerified ? "#10251d" : (panel.firmware.bootloaderHelpNeeded ? "#261f13" : "#171d25"))
            border.width: 1
            border.color: panel.verificationFailed ? panel.theme.red : (panel.firmware.targetVerified ? "#2c674e" : (panel.firmware.bootloaderHelpNeeded ? "#705827" : panel.theme.lineSoft))
            Label {
                anchors.fill: parent
                anchors.margins: 11
                text: panel.firmwareDisplayStatus
                color: panel.verificationFailed ? panel.theme.red : (panel.firmware.targetVerified ? panel.theme.green : (panel.firmware.bootloaderHelpNeeded ? panel.theme.amber : panel.theme.textSoft))
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
            CalmButton {
                theme: panel.theme
                uiFont: panel.uiFont
                text: "Copy diagnostics"
                toolTipText: "Logs may include device identifiers; review before sharing."
                enabled: panel.session !== null
                onClicked: StudioClipboard.copyText(panel.session.diagnosticsText())
            }
            Item { Layout.fillWidth: true }
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
