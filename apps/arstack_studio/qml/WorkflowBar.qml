// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import ARStack.Studio 1.0

SurfacePanel {
    id: ribbon

    property var controller
    property var device
    property var profiles
    property string uiFont: "Inter"
    property string monoFont: "Inter"
    property bool compact: false
    property bool updatePromptDeferred: false
    property bool installPromptDeferred: false
    property bool firmwareResultSuccess: false
    property string firmwareResultTitle: ""
    property string firmwareResultMessage: ""

    readonly property alias session: smartSession
    readonly property bool firmwareGateActive:
        smartSession.firmwareInstallRequired || smartSession.firmwareUpdateRequired ||
        smartSession.updatingFirmware || smartSession.updateNeedsBootloaderHelp

    Settings {
        id: operatorSettings
        category: "operator-ui"
        property string lastEngineeringUrl: ""
        property int lastSelectedIndex: 0
        property bool phasorVisible: false
        property bool waveformVisible: false
        property bool telemetryVisible: true
        property bool phasorDetached: false
        property bool waveformDetached: false
        property bool telemetryExpanded: false
    }

    SmartSessionController {
        id: smartSession
        device: ribbon.device
        profiles: ribbon.profiles
        firmware: FirmwareService
    }

    Component.onCompleted: {
        controller.phasorDockVisible = operatorSettings.phasorVisible
        controller.waveformDockVisible = operatorSettings.waveformVisible
        controller.telemetryDockVisible = operatorSettings.telemetryVisible
        controller.phasorDetached = operatorSettings.phasorDetached
        controller.waveformDetached = operatorSettings.waveformDetached
        controller.telemetryExpanded = operatorSettings.telemetryExpanded

        if (operatorSettings.lastEngineeringUrl.length > 0) {
            if (profiles.loadFile(operatorSettings.lastEngineeringUrl)) {
                profiles.selectStream(operatorSettings.lastSelectedIndex)
                controller.profileDirty = false
                controller.showMessage("Last engineering configuration restored.", false)
            } else {
                operatorSettings.lastEngineeringUrl = ""
                operatorSettings.lastSelectedIndex = 0
                controller.showMessage("The previous engineering file is no longer available; using the built-in 4I+4V profile.", false)
            }
        }
        smartSession.start()
    }

    Connections {
        target: controller
        function onPhasorDockVisibleChanged() { operatorSettings.phasorVisible = controller.phasorDockVisible }
        function onWaveformDockVisibleChanged() { operatorSettings.waveformVisible = controller.waveformDockVisible }
        function onTelemetryDockVisibleChanged() { operatorSettings.telemetryVisible = controller.telemetryDockVisible }
        function onPhasorDetachedChanged() { operatorSettings.phasorDetached = controller.phasorDetached }
        function onWaveformDetachedChanged() { operatorSettings.waveformDetached = controller.waveformDetached }
        function onTelemetryExpandedChanged() { operatorSettings.telemetryExpanded = controller.telemetryExpanded }
    }

    Connections {
        target: profiles
        function onSelectedIndexChanged() {
            if (profiles.selectedIndex >= 0)
                operatorSettings.lastSelectedIndex = profiles.selectedIndex
        }
    }

    Connections {
        target: smartSession
        function onReadyForLiveApply() {
            controller.profileDirty = false
            controller.applyAllSignals()
        }
        function onStateChanged() {
            if (ribbon.firmwareGateActive)
                controller.profileDirty = true

            if (smartSession.updatingFirmware || smartSession.updateNeedsBootloaderHelp) {
                if (installDialog.opened) installDialog.close()
                if (updateDialog.opened) updateDialog.close()
                if (!progressDialog.opened) Qt.callLater(progressDialog.open)
                return
            }
            if (smartSession.firmwareInstallRequired && !ribbon.installPromptDeferred &&
                    !installDialog.opened && !progressDialog.opened) {
                Qt.callLater(installDialog.open)
                return
            }
            if (smartSession.firmwareUpdateRequired && !ribbon.updatePromptDeferred &&
                    !updateDialog.opened && !progressDialog.opened) {
                Qt.callLater(updateDialog.open)
            }
        }
        function onFirmwareUpdateFinished(success) {
            if (progressDialog.opened) progressDialog.close()
            ribbon.firmwareResultSuccess = success
            if (success) {
                ribbon.updatePromptDeferred = false
                ribbon.installPromptDeferred = false
                ribbon.firmwareResultTitle = "Firmware installed successfully"
                ribbon.firmwareResultMessage = "ESP32-P4 restarted, reconnected, and ARStack firmware was verified. Studio is preparing the 4I+4V injector."
                controller.showMessage("Firmware installed and verified successfully.", false)
            } else {
                ribbon.firmwareResultTitle = "Firmware installation not completed"
                ribbon.firmwareResultMessage = FirmwareService.status.length > 0
                    ? FirmwareService.status
                    : "Studio could not verify the firmware installation. Keep the board connected and try again."
                controller.showMessage("Firmware installation was not verified.", true)
            }
            Qt.callLater(resultDialog.open)
        }
    }

    Connections {
        target: device
        function onDeviceVerifiedChanged() {
            if (!device.deviceVerified) ribbon.updatePromptDeferred = false
        }
        function onPortsChanged() {
            if (device.ports.length === 0) {
                ribbon.installPromptDeferred = false
                ribbon.updatePromptDeferred = false
            }
        }
        function onProfileStateChanged() {
            if (!ribbon.firmwareGateActive) return
            controller.profileDirty = true
            Qt.callLater(function() {
                if (ribbon.firmwareGateActive) controller.profileDirty = true
            })
        }
    }

    readonly property string displayState: {
        if (smartSession.state === "WAITING FOR DEVICE") return "Connect device"
        if (smartSession.state === "DEVICE FOUND" || smartSession.state === "CONNECTING") return "Connecting"
        if (smartSession.state === "CHECKING DEVICE") return "Checking device"
        if (smartSession.state === "FIRMWARE REQUIRED") return "Firmware required"
        if (smartSession.state === "FIRMWARE UPDATE") return "Firmware update available"
        if (smartSession.state === "UPDATING FIRMWARE") return "Installing firmware"
        if (smartSession.state === "UPDATE NEEDS BOOT") return "Download mode required"
        if (smartSession.state === "PREPARING 4I+4V") return "Preparing"
        if (smartSession.state === "READY") return "Ready to inject"
        if (smartSession.state === "RUNNING") return "Injection running"
        if (smartSession.state === "PROFILE BLOCKED") return "Profile unavailable"
        if (smartSession.state === "SETUP ERROR") return "Setup issue"
        return smartSession.state
    }

    readonly property color smartStateColor: {
        if (smartSession.state === "RUNNING" || smartSession.state === "READY") return theme.green
        if (smartSession.state === "CONNECTING" || smartSession.state === "CHECKING DEVICE" ||
            smartSession.state === "FIRMWARE REQUIRED" || smartSession.state === "PREPARING 4I+4V" ||
            smartSession.state === "FIRMWARE UPDATE" || smartSession.state === "UPDATING FIRMWARE" ||
            smartSession.state === "UPDATE NEEDS BOOT") return theme.amber
        if (smartSession.state === "DEVICE FOUND") return theme.accent
        if (smartSession.state === "PROFILE BLOCKED" || smartSession.state === "SETUP ERROR") return theme.red
        return theme.muted
    }

    function startReason() {
        if (device.running) return "SMV output is already running."
        if (smartSession.firmwareInstallRequired) return "Install ARStack firmware before starting injection."
        if (smartSession.firmwareUpdateRequired) return "Update firmware before starting injection."
        if (smartSession.updatingFirmware) return "Firmware installation is in progress."
        if (smartSession.updateNeedsBootloaderHelp) return "Complete Download mode recovery before starting injection."
        if (smartSession.state === "CONNECTING" || smartSession.state === "CHECKING DEVICE") return "Studio is identifying the device automatically."
        if (smartSession.state === "PREPARING 4I+4V") return "Studio is preparing the default 4I+4V injection automatically."
        if (smartSession.state === "WAITING FOR DEVICE") return "Connect ESP32-P4; Studio will detect it automatically."
        return smartSession.statusText
    }

    function requestStart() {
        if (device.running) return
        if (smartSession.firmwareInstallRequired) {
            ribbon.installPromptDeferred = false
            installDialog.open()
            return
        }
        if (smartSession.firmwareUpdateRequired) {
            ribbon.updatePromptDeferred = false
            updateDialog.open()
            return
        }
        if (smartSession.updatingFirmware || smartSession.updateNeedsBootloaderHelp) {
            controller.showMessage(ribbon.startReason(), false)
            return
        }
        if (!smartSession.startReady) {
            controller.showMessage(ribbon.startReason(), false)
            return
        }
        device.start()
    }

    function requestStop() {
        if (device.running) device.stop()
    }

    function openEngineeringDialog() {
        if (!device.running) engineeringFileDialog.open()
        else controller.showMessage("Stop injection before changing the engineering configuration.", true)
    }

    function loadEngineeringFile(url) {
        if (device.running) {
            controller.showMessage("Stop injection before changing the engineering configuration.", true)
            return false
        }
        if (!profiles.loadFile(url)) {
            controller.showMessage(profiles.fatalError || "Unable to load engineering file.", true)
            return false
        }
        operatorSettings.lastEngineeringUrl = url.toString()
        operatorSettings.lastSelectedIndex = profiles.selectedIndex >= 0 ? profiles.selectedIndex : 0
        controller.profileDirty = false
        controller.showMessage(profiles.documentStatus, false)
        return true
    }

    function useBuiltInProfile() {
        if (device.running) {
            controller.showMessage("Stop injection before changing the engineering configuration.", true)
            return
        }
        if (profiles.loadReferenceTemplate()) {
            operatorSettings.lastEngineeringUrl = ""
            operatorSettings.lastSelectedIndex = 0
            controller.profileDirty = false
            controller.showMessage("Built-in 4I+4V / 4000 fps reference profile selected.", false)
        } else {
            controller.showMessage(profiles.fatalError || "Unable to load the built-in profile.", true)
        }
    }

    function resetDockLayout() {
        controller.phasorDetached = false
        controller.waveformDetached = false
        controller.phasorDockVisible = true
        controller.waveformDockVisible = false
        controller.telemetryDockVisible = true
        controller.telemetryExpanded = false
        controller.showMessage("Dock layout reset.", false)
    }

    FileDialog {
        id: engineeringFileDialog
        title: "Open IEC 61850 engineering configuration"
        nameFilters: [
            "IEC 61850 SCL (*.scd *.cid *.icd *.iid *.ssd *.xml)",
            "All files (*)"
        ]
        onAccepted: ribbon.loadEngineeringFile(selectedFile)
    }

    Dialog {
        id: installDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 420
        title: "Set up ESP32-P4"
        closePolicy: Popup.NoAutoClose
        background: Rectangle { color: ribbon.theme.surface; radius: 10; border.width: 1; border.color: ribbon.theme.line }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: "ESP32-P4 detected"
                color: ribbon.theme.text
                font.family: ribbon.uiFont
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                text: "ARStack firmware is not installed or is not responding. Studio can install the correct firmware automatically."
                color: ribbon.theme.textSoft
                font.family: ribbon.uiFont
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: "No firmware file, ESP-IDF, Python, or command line is required. Keep USB connected until Studio confirms success."
                color: ribbon.theme.muted
                font.family: ribbon.uiFont
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                CalmButton {
                    theme: ribbon.theme; uiFont: ribbon.uiFont; text: "Later"
                    onClicked: { ribbon.installPromptDeferred = true; installDialog.close() }
                }
                CalmButton {
                    theme: ribbon.theme; uiFont: ribbon.uiFont; text: "Install firmware"; tone: "accent"; implicitWidth: 142
                    onClicked: {
                        if (smartSession.beginFirmwareInstall()) {
                            ribbon.installPromptDeferred = false
                            installDialog.close()
                        } else controller.showMessage("Firmware installation could not start. Open Tools > Advanced for recovery details.", true)
                    }
                }
            }
        }
    }

    Dialog {
        id: updateDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 410
        title: "Firmware update required"
        closePolicy: Popup.NoAutoClose
        background: Rectangle { color: ribbon.theme.surface; radius: 10; border.width: 1; border.color: ribbon.theme.line }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: "Update firmware to start injection"
                color: ribbon.theme.text
                font.family: ribbon.uiFont
                font.pixelSize: 14
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: "This ESP32-P4 is connected, but its ARStack firmware is older than this Studio build. Update now? Studio will flash, restart, reconnect, and verify it automatically."
                color: ribbon.theme.textSoft
                font.family: ribbon.uiFont
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                CalmButton {
                    theme: ribbon.theme; uiFont: ribbon.uiFont; text: "Later"
                    onClicked: { ribbon.updatePromptDeferred = true; updateDialog.close() }
                }
                CalmButton {
                    theme: ribbon.theme; uiFont: ribbon.uiFont; text: "Update firmware"; tone: "accent"; implicitWidth: 138
                    onClicked: {
                        if (smartSession.beginFirmwareUpdate()) {
                            ribbon.updatePromptDeferred = false
                            updateDialog.close()
                        } else controller.showMessage("Firmware update could not start. Open Tools > Advanced for recovery help.", true)
                    }
                }
            }
        }
    }

    Dialog {
        id: progressDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 450
        title: smartSession.updateNeedsBootloaderHelp ? "Device needs Download mode" : "Installing firmware"
        closePolicy: Popup.NoAutoClose
        background: Rectangle {
            color: ribbon.theme.surface
            radius: 10
            border.width: 1
            border.color: smartSession.updateNeedsBootloaderHelp ? ribbon.theme.amber : ribbon.theme.line
        }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: smartSession.updateNeedsBootloaderHelp
                    ? "Studio cannot reach the ESP32-P4 bootloader yet."
                    : "Keep USB connected. Studio verifies the board before reporting success."
                color: smartSession.updateNeedsBootloaderHelp ? ribbon.theme.amber : ribbon.theme.text
                font.family: ribbon.uiFont
                font.pixelSize: 13
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: smartSession.updateStatus.length > 0 ? smartSession.updateStatus : "Preparing firmware installation…"
                color: ribbon.theme.textSoft
                font.family: ribbon.uiFont
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
            ProgressBar {
                visible: !smartSession.updateNeedsBootloaderHelp
                Layout.fillWidth: true
                from: 0; to: 100
                indeterminate: smartSession.firmwareProgress < 0
                value: smartSession.firmwareProgress < 0 ? 0 : smartSession.firmwareProgress
            }
            Label {
                visible: !smartSession.updateNeedsBootloaderHelp && smartSession.firmwareProgress >= 0
                Layout.fillWidth: true
                text: smartSession.firmwareProgress + "%"
                color: ribbon.theme.text
                font.family: ribbon.monoFont
                font.pixelSize: 12
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
            }
            RowLayout {
                visible: smartSession.updateNeedsBootloaderHelp
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                CalmButton { theme: ribbon.theme; uiFont: ribbon.uiFont; text: "Retry"; tone: "accent"; implicitWidth: 110; onClicked: smartSession.retryFirmwareUpdate() }
            }
        }
    }

    Dialog {
        id: resultDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 430
        title: ribbon.firmwareResultSuccess ? "Success" : "Firmware result"
        closePolicy: Popup.NoAutoClose
        background: Rectangle {
            color: ribbon.theme.surface
            radius: 10
            border.width: 1
            border.color: ribbon.firmwareResultSuccess ? ribbon.theme.green : ribbon.theme.red
        }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: ribbon.firmwareResultSuccess ? "✓" : "!"
                color: ribbon.firmwareResultSuccess ? ribbon.theme.green : ribbon.theme.red
                font.family: ribbon.uiFont
                font.pixelSize: 30
                font.weight: Font.Bold
                horizontalAlignment: Text.AlignHCenter
            }
            Label {
                Layout.fillWidth: true
                text: ribbon.firmwareResultTitle
                color: ribbon.firmwareResultSuccess ? ribbon.theme.green : ribbon.theme.text
                font.family: ribbon.uiFont
                font.pixelSize: 17
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: ribbon.firmwareResultMessage
                color: ribbon.theme.textSoft
                font.family: ribbon.uiFont
                font.pixelSize: 11
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                CalmButton { theme: ribbon.theme; uiFont: ribbon.uiFont; text: "OK"; tone: ribbon.firmwareResultSuccess ? "success" : "normal"; implicitWidth: 100; onClicked: resultDialog.close() }
                Item { Layout.fillWidth: true }
            }
        }
    }

    implicitHeight: 72
    color: theme.surface2
    border.color: theme.line

    component MenuButton: CalmButton {
        theme: ribbon.theme
        uiFont: ribbon.uiFont
        implicitHeight: 24
        implicitWidth: 66
        font.pixelSize: 10
    }

    component RunButton: CalmButton {
        theme: ribbon.theme
        uiFont: ribbon.uiFont
        implicitHeight: 36
        implicitWidth: 112
        iconSize: 15
        font.pixelSize: 10
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.topMargin: 5
        anchors.bottomMargin: 5
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            spacing: 3

            MenuButton {
                text: "File"
                onClicked: fileMenu.open()
                Menu {
                    id: fileMenu
                    y: parent.height
                    MenuItem { text: "Open Engineering File…"; enabled: !device.running; onTriggered: ribbon.openEngineeringDialog() }
                    MenuItem {
                        text: "Reopen Last Configuration"
                        enabled: !device.running && operatorSettings.lastEngineeringUrl.length > 0
                        onTriggered: ribbon.loadEngineeringFile(operatorSettings.lastEngineeringUrl)
                    }
                    MenuSeparator {}
                    MenuItem { text: "Use Built-in 4I+4V Profile"; enabled: !device.running; onTriggered: ribbon.useBuiltInProfile() }
                    MenuSeparator {}
                    MenuItem { text: "Exit"; onTriggered: Qt.quit() }
                }
            }

            MenuButton {
                text: "Injection"
                implicitWidth: 82
                onClicked: injectionMenu.open()
                Menu {
                    id: injectionMenu
                    y: parent.height
                    MenuItem { text: "Start Injection    F5"; enabled: !device.running; onTriggered: ribbon.requestStart() }
                    MenuItem { text: "Stop Injection     F6"; enabled: device.running; onTriggered: ribbon.requestStop() }
                    MenuSeparator {}
                    MenuItem { text: "Balanced 3-Phase"; onTriggered: controller.balanced() }
                    MenuItem { text: "Zero All"; onTriggered: controller.zeroAll() }
                }
            }

            MenuButton {
                text: "View"
                onClicked: viewMenu.open()
                Menu {
                    id: viewMenu
                    y: parent.height
                    Menu {
                        title: "Docks"
                        MenuItem {
                            text: (controller.phasorDockVisible || controller.phasorDetached ? "✓  " : "    ") + "Phasor"
                            onTriggered: {
                                if (controller.phasorDetached) controller.phasorDetached = false
                                controller.phasorDockVisible = !controller.phasorDockVisible
                            }
                        }
                        MenuItem {
                            text: (controller.waveformDockVisible || controller.waveformDetached ? "✓  " : "    ") + "Waveform"
                            onTriggered: {
                                if (controller.waveformDetached) controller.waveformDetached = false
                                controller.waveformDockVisible = !controller.waveformDockVisible
                            }
                        }
                        MenuItem {
                            text: (controller.telemetryDockVisible ? "✓  " : "    ") + "Status Monitor"
                            onTriggered: controller.telemetryDockVisible = !controller.telemetryDockVisible
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: "Dock All Floating Views"
                            enabled: controller.phasorDetached || controller.waveformDetached
                            onTriggered: {
                                if (controller.phasorDetached) { controller.phasorDetached = false; controller.phasorDockVisible = true }
                                if (controller.waveformDetached) { controller.waveformDetached = false; controller.waveformDockVisible = true }
                            }
                        }
                        MenuItem { text: "Reset Dock Layout"; onTriggered: ribbon.resetDockLayout() }
                    }
                }
            }

            MenuButton {
                text: "Tools"
                onClicked: toolsMenu.open()
                Menu {
                    id: toolsMenu
                    y: parent.height
                    MenuItem { text: "Advanced…"; onTriggered: controller.openConfiguration() }
                    MenuItem { text: "Diagnostics…"; onTriggered: controller.openDiagnostics() }
                }
            }

            Item { Layout.fillWidth: true }

            Label {
                visible: !ribbon.compact
                text: operatorSettings.lastEngineeringUrl.length > 0 ? "Last configuration auto-opens" : "Built-in 4I+4V default"
                color: ribbon.theme.muted
                font.family: ribbon.uiFont
                font.pixelSize: 9
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            Rectangle { width: 8; height: 8; radius: 4; color: ribbon.smartStateColor }
            Label {
                text: ribbon.displayState
                color: ribbon.smartStateColor
                font.family: ribbon.uiFont
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                visible: !ribbon.compact
                text: smartSession.state === "READY" ? "4I + 4V · 4000 samples/s" : smartSession.statusText
                color: ribbon.theme.muted
                font.family: ribbon.uiFont
                font.pixelSize: 10
                elide: Text.ElideRight
            }

            CalmButton {
                visible: smartSession.firmwareUpdateRequired && !smartSession.updatingFirmware
                theme: ribbon.theme
                uiFont: ribbon.uiFont
                text: "Update firmware"
                tone: "accent"
                implicitHeight: 32
                implicitWidth: 128
                onClicked: { ribbon.updatePromptDeferred = false; updateDialog.open() }
            }
            CalmButton {
                visible: smartSession.firmwareInstallRequired && !smartSession.updatingFirmware
                theme: ribbon.theme
                uiFont: ribbon.uiFont
                text: "Install firmware"
                tone: "accent"
                implicitHeight: 32
                implicitWidth: 128
                onClicked: { ribbon.installPromptDeferred = false; installDialog.open() }
            }

            RunButton {
                text: "Start"
                iconSource: Qt.resolvedUrl("../assets/lucide/play.svg")
                tone: smartSession.startReady ? "success" : "neutral"
                enabled: !device.running && !smartSession.updatingFirmware && !smartSession.updateNeedsBootloaderHelp
                toolTipText: ribbon.startReason()
                onClicked: ribbon.requestStart()
            }
            RunButton {
                text: "Stop"
                iconSource: Qt.resolvedUrl("../assets/lucide/square.svg")
                tone: device.running ? "danger" : "neutral"
                enabled: device.running && !smartSession.updatingFirmware
                toolTipText: device.running ? "Stop Sampled Values output" : "Injection is not running"
                onClicked: ribbon.requestStop()
            }
        }
    }
}
