// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ARStack.Studio 1.0

Rectangle {
    id: panel
    property var theme
    property var device
    property real signalFrequency: 50.0
    property string uiFont: "Inter"
    property string monoFont: "Inter"
    color: theme.surface2
    radius: 8
    border.width: 1
    border.color: theme.lineSoft
    implicitHeight: 390

    SvCaptureController { id: capture }

    // Read already-published Studio state passively. This never queues serial
    // commands; sample it at 1Hz so PTP/SV transitions can be time-correlated.
    function telemetrySnapshot() {
        return {
            "captured_utc": new Date().toISOString(),
            "operator_frequency_hz_UNVERIFIED": panel.signalFrequency,
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
            "device_tx_failures": panel.device ? panel.device.txFailures : "",
            "onboard_sync_flight_UNVERIFIED": panel.device ? panel.device.syncFlightLatest : ""

        }
    }
    Timer {
        interval: 1000
        repeat: true
        running: capture.capturing
        onTriggered: capture.sampleTelemetry(panel.telemetrySnapshot())
    }


    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 9

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "CAPTURE · SV + PTP"
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
            text: "Record independent SV + PTP Ethernet while injection continues. Choose an Ethernet adapter and duration, then press Capture. One-time Windows capture support (Wireshark/Npcap) and a traffic-visible TAP/SPAN may be required."
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
            CalmButton {
                theme: panel.theme
                uiFont: panel.uiFont
                text: "Install capture support"
                visible: !capture.captureSupportAvailable
                enabled: !capture.capturing && !capture.processing
                onClicked: capture.openCaptureSupportHelp()
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
                      capture.processing ? "Exporting…" : "Capture SV + PTP"
                tone: "accent"
                enabled: !capture.capturing && !capture.processing && adapter.currentIndex >= 0
                onClicked: {
                    var seconds = captureDuration.currentIndex === 2 ? 3600
                                : captureDuration.currentIndex === 0 ? 30 : 120
                    capture.startCapture(adapter.currentIndex, seconds, panel.telemetrySnapshot())
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
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 92
            visible: capture.summary.sv_frames !== undefined
            radius: 8
            color: panel.theme.surface
            border.width: 1
            border.color: panel.theme.lineSoft
            GridLayout {
                anchors.fill: parent
                anchors.margins: 10
                columns: 4
                rowSpacing: 5
                columnSpacing: 10
                Repeater {
                    model: [
                        { label: "SV packets", value: capture.summary.sv_frames },
                        { label: "PTP packets", value: capture.summary.ptp_frames },
                        { label: "Missing samples", value: capture.summary.missing_samples },
                        { label: "smpSynch changes", value: capture.summary.sync_transitions }
                    ]
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            text: modelData.value === undefined ? "—" : String(modelData.value)
                            color: panel.theme.text
                            font.family: panel.monoFont
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                        }
                        Label {
                            text: modelData.label
                            color: panel.theme.muted
                            font.family: panel.uiFont
                            font.pixelSize: 9
                        }
                    }
                }
                Label {
                    Layout.columnSpan: 4
                    text: "Wire capture saved · Physical synchronization is not yet verified"
                    color: panel.theme.textSoft
                    font.family: panel.uiFont
                    font.pixelSize: 9
                }
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
            text: "Studio saves the raw SV+PTP PCAP, 4I+4V CSV, event transitions and report JSON automatically. Open files to share the evidence. The 3600 s CSV is sampled 1:20; PCAP retains all captured packets. Host capture is not hardware phase proof."
            wrapMode: Text.WordWrap
            color: panel.theme.muted
            font.family: panel.uiFont
            font.pixelSize: 8
        }
        Item { Layout.fillHeight: true }
    }
}
