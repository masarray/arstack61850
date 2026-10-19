// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ARStack.Studio 1.0

Rectangle {
    id: panel
    property var theme
    property var device
    property string uiFont: "Inter"
    property string monoFont: "Inter"
    color: theme.surface2
    radius: 8
    border.width: 1
    border.color: theme.lineSoft
    implicitHeight: 258

    SvCaptureController { id: capture }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 9

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "SV WIRE CAPTURE"
                color: panel.theme.text
                font.family: panel.uiFont
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            Item { Layout.fillWidth: true }
            Label {
                text: capture.capturing ? "RECORDING" : capture.processing ? "EXPORTING" : "READ-ONLY"
                color: capture.capturing ? panel.theme.amber : panel.theme.green
                font.family: panel.uiFont
                font.pixelSize: 9
                font.weight: Font.DemiBold
            }
        }
        Label {
            Layout.fillWidth: true
            text: "Capture genuine IEC 61850 SV Ethernet frames. No injector commands are sent. Requires Wireshark dumpcap + Npcap and a TAP/SPAN receiving the injector traffic."
            wrapMode: Text.WordWrap
            color: panel.theme.textSoft
            font.family: panel.uiFont
            font.pixelSize: 9
            lineHeight: 1.15
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: "Ethernet"
                color: panel.theme.muted
                font.family: panel.uiFont
                font.pixelSize: 9
            }
            StudioDarkComboBox {
                id: adapter
                theme: panel.theme
                uiFont: panel.uiFont
                Layout.fillWidth: true
                model: capture.interfaces
                font.pixelSize: 9
                enabled: !capture.capturing && !capture.processing
                Connections {
                    target: capture
                    function onInterfacesChanged() {
                        adapter.currentIndex = capture.suggestedInterface >= 0
                            ? capture.suggestedInterface : -1
                    }
                }
            }
            CalmButton {
                theme: panel.theme
                uiFont: panel.uiFont
                text: "Refresh"
                enabled: !capture.capturing && !capture.processing
                onClicked: capture.refreshInterfaces()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: "Duration"
                color: panel.theme.muted
                font.family: panel.uiFont
                font.pixelSize: 9
            }
            StudioDarkComboBox {
                id: captureDuration
                theme: panel.theme
                uiFont: panel.uiFont
                Layout.preferredWidth: 170
                model: ["30 s · quick check", "120 s · acceptance", "3600 s · endurance"]
                currentIndex: 1
                enabled: !capture.capturing && !capture.processing
                font.pixelSize: 9
            }
            CalmButton {
                theme: panel.theme
                uiFont: panel.uiFont
                text: capture.capturing ? "Capturing…" :
                      capture.processing ? "Exporting…" : "Capture SV + CSV"
                tone: "accent"
                enabled: !capture.capturing && !capture.processing && adapter.currentIndex >= 0
                onClicked: {
                    var seconds = captureDuration.currentIndex === 2 ? 3600
                                : captureDuration.currentIndex === 0 ? 30 : 120
                    capture.startCapture(adapter.currentIndex, seconds, {
                        "captured_utc": new Date().toISOString(),
                        "firmware_version": panel.device ? panel.device.firmwareVersion : "",
                        "firmware_build_id": panel.device ? panel.device.firmwareBuildId : "",
                        "boot_id": panel.device ? panel.device.bootId : "",
                        "device_id": panel.device ? panel.device.deviceId : "",
                        "serial_port": panel.device ? panel.device.portName : "",
                        "running": panel.device ? panel.device.running : false,
                        "profile_generation": panel.device ? panel.device.profileGeneration : "",
                        "signal_generation": panel.device ? panel.device.signalGeneration : "",
                        "smpSynch_value": panel.device ? panel.device.smpSynchValue : "",
                        "smpSynch_source": panel.device ? panel.device.smpSynchSource : "",
                        "ptp_role": panel.device ? panel.device.ptpRole : "",
                        "ptp_running": panel.device ? panel.device.ptpRunning : false,
                        "ptp_discipline": panel.device ? panel.device.ptpDiscipline : "",
                        "ptp_source": panel.device ? panel.device.ptpSource : "",
                        "ptp_offset_ns": panel.device ? panel.device.ptpOffsetNs : "",
                        "device_fps": panel.device ? panel.device.fps : "",
                        "device_missed": panel.device ? panel.device.missed : "",
                        "device_tx_failures": panel.device ? panel.device.txFailures : ""
                    })
                }
            }
            CalmButton {
                theme: panel.theme
                uiFont: panel.uiFont
                text: "Abort"
                tone: "danger"
                visible: capture.capturing
                onClicked: capture.abortCapture()
            }
            Item { Layout.fillWidth: true }
            CalmButton {
                theme: panel.theme
                uiFont: panel.uiFont
                text: "Open files"
                enabled: !capture.capturing
                onClicked: capture.openOutputFolder()
            }
        }
        Label {
            Layout.fillWidth: true
            text: capture.status
            wrapMode: Text.WordWrap
            color: capture.capturing ? panel.theme.amber : panel.theme.textSoft
            font.family: panel.uiFont
            font.pixelSize: 9
            maximumLineCount: 3
            elide: Text.ElideRight
        }
        Label {
            Layout.fillWidth: true
            text: "Exports: filtered .pcap + 8-channel samples.csv + seconds.csv + report.json. CSV is 1:20 sampled only for 3600 s; the PCAP always retains all packets. A capture is not a physical PASS."
            wrapMode: Text.WordWrap
            color: panel.theme.muted
            font.family: panel.uiFont
            font.pixelSize: 8
        }
        Item { Layout.fillHeight: true }
    }
}
