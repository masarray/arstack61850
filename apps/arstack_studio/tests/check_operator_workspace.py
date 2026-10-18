#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fast structural gates for the native operator GUI (before the Qt build).

These assert UI and state-ownership intent; headless QML launch/Windows
interactive review remain separate acceptance steps.
"""
from pathlib import Path

STUDIO = Path(__file__).resolve().parents[1]
QML = STUDIO / "qml"


def source(name: str) -> str:
    return (QML / name).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"Studio workspace contract failed: {message}")


def ordered(data: str, first: str, second: str, reason: str) -> None:
    require(first in data and second in data and data.index(first) < data.index(second), reason)


def main() -> None:
    main_qml = source("Main.qml")
    workflow = source("WorkflowBar.qml")
    preview = source("SignalPreview.qml")
    tray = source("StudioTimingTray.qml")
    expert = source("PtpExpertPanel.qml")
    config = source("ConfigurationHub.qml")
    require("Lab simulation controls" not in config
            and "Signals, firmware, PTP and device settings" in config,
            "Advanced must not imply this is only a simulated lab tool")
    require("Changes sent on Start" in main_qml
            and "root.pendingApply && !workflowBar.injectionRunning" in main_qml,
            "STOPPED waveform edits must distinguish draft from sent setpoints")
    controller = (STUDIO / "src/DeviceController.cpp").read_text(encoding="utf-8")
    require("logUpdateTimer_.setInterval(100)" in controller
            and "if (!logUpdateTimer_.isActive()) logUpdateTimer_.start();" in controller,
            "serial log QML notification must be coalesced to avoid GUI stutter")
    role = controller.split("match = kPtpRoleExpression.match(line);")[1].split(
        "match = kPtpMeasuredStateExpression.match(line);")[0]
    require("ptpRunning_ =" not in role and "ptpDiscipline_ =" not in role,
            "stale PROFILE PTPROLE must not override verified PTP runtime")
    require("ptpRestoreAfterFirmware" in main_qml
            and "root.ptpRestoreChecks > 120" in main_qml
            and "root.ptpDesiredForSession = false" in main_qml
            and "property bool ptpDesiredForSession: true" in main_qml
            and "controller.ptpRestoreAfterFirmware = true" in workflow
            and "panel.controller.requestStopPtp()" in expert,
            "default PTP SOURCE must be verified-only and OFF must cancel it in Main and Advanced")
    matrix = source("SignalMatrix.qml")
    menu = source("StudioActionMenu.qml")
    cmake = (STUDIO / "CMakeLists.txt").read_text(encoding="utf-8")

    ordered(main_qml, "id: voltageMatrix", "id: currentMatrix",
            "voltage must precede current in the injection table")
    require("ColumnLayout {\n                            Layout.fillWidth: true\n                            Layout.fillHeight: true\n                            Layout.minimumHeight: 398" in main_qml,
            "voltage/current engineering tables must be vertically stacked")
    workflow = source("WorkflowBar.qml")
    require("smartSession.requestStartWithScenario(\n            controller.startScenarioSnapshot())" in workflow
            and "smartSession.requestStart()" not in workflow,
            "every Start must send the current eight table rows, never stale board setpoints")
    brand = source("ModernRibbon.qml")
    require("font.pixelSize: 15" in brand and "width: 38" in brand,
            "SMV Injector icon and title must be legible")
    require('parent.hovered ? "#e9f2fd"' in config
            and 'parent.hovered ? "#142334"' in config,
            "Advanced tabs must retain dark text over light hover background")
    require("horizontalAlignment: Text.AlignHCenter" in source("CalmButton.qml")
            and "verticalAlignment: Text.AlignVCenter" in source("CalmButton.qml"),
            "button captions must be centered")
    require("control.hovered || control.activeFocus" in source("StudioDarkCheckBox.qml"),
            "checkbox should keep a single indicator through hover")
    ordered(preview, "id: voltagePhasorCanvas", "id: currentPhasorCanvas",
            "voltage must precede current in the phasor view")
    ordered(preview, "drawWaveGroup(preview.voltageModel, voltageY, false)",
            "drawWaveGroup(preview.currentModel, currentY, true)",
            "voltage waveform must be above current")
    require("lane === 0 ? voltageY : currentY" in preview,
            "waveform baselines must follow voltage/current lane order")
    require("StudioTimingTray {" in main_qml and "id: frequencyMenu" in main_qml,
            "main window must expose timing and frequency context controls")
    require("function startPtpRole(role) { return timingPanel.startRole(role) }" in config
            and "const accepted = profilePanel.startPtpRole(role)" in main_qml
            and "requestStartPtpConfigured(role, panel.ptpProfile())" in expert,
            "main PTP must reuse the existing verified Advanced profile")
    require("checked = Qt.binding(function()" in tray
            and "tray.controller.requestStopPtp()" in tray
            and "tray.session.requestSmpSynch([\"AUTO\", \"0\", \"1\", \"2\"][requested])" in tray
            and "if (!tray.boardOnline)" in tray,
            "main PTP/smpSynch must restore device-owned state after user intent")
    require("SIMULATED FLAG" in expert and "MEASURED SYNC FLAG" in expert
            and "sample-phase evidence independently" in expert,
            "Advanced must retain truthful SV timing evidence without noisy Main disclaimers")
    require(matrix.count("Qt.Key_Menu") == 2 and matrix.count("Qt.Key_F10") == 2
            and matrix.count("signalRow.openFieldMenu(") == 4,
            "both magnitude and angle menus must support mouse and keyboard")
    require("1.75" in (STUDIO.parent.parent / ".github/workflows/arstack-studio-windows-lifecycle.yml").read_text(encoding="utf-8"),
            "reuse Windows executable for 100%, 150% and 175% DPI acceptance")
    workflow = source("WorkflowBar.qml")
    require("Qt.callLater(installDialog.open)" not in workflow
            and "Qt.callLater(updateDialog.open)" not in workflow
            and "if (!success) Qt.callLater(resultDialog.open)" in workflow
            and "installDialog.open()" in workflow
            and "updateDialog.open()" in workflow,
            "connection must not pop setup/update dialogs; Start may offer firmware repair")
    require("provide lab PTP" not in expert
            and "lab simulation" not in expert
            and "Local flag (manual)" in expert
            and 'SV sample synchronization' in expert
            and '"OFFLINE LAB"' not in tray,
            "PTP operator controls must use direct understandable language")
    firmware_ui = source("FirmwarePanel.qml")
    timing_ui = source("StudioTimingTray.qml")
    ribbon = source("ModernRibbon.qml")
    cmake_icons = (STUDIO / "CMakeLists.txt").read_text(encoding="utf-8")
    require("scenarioSnapshot()" in main_qml and "restoreLocalScenario()" in main_qml
            and "startScenarioSnapshot()" in main_qml and "offlinePreviewRunning" in main_qml,
            "offline-first scenario must persist and support one-click Start")
    require("Apply to board" not in main_qml and "requestApplyDraft()" not in main_qml,
            "operator UI must not expose a redundant Apply-to-board action")
    require('saveScenarioTimer.stop()' in main_qml
            and 'scenarioSettings.lastScenario = JSON.stringify(root.scenarioSnapshot())' in main_qml,
            "closing immediately after an edit must flush the local draft before teardown")
    require('typeof data.hz !== "number"' in main_qml
            and 'typeof row[0] !== "number"' in main_qml
            and 'typeof row[1] !== "number"' in main_qml
            and 'typeof row[3] !== "number"' in main_qml,
            "saved scenario restoration must reject JSON null/string coercion and nonfinite values")
    require('id: voltageMatrix\n                                enabled: !workflowBar.startQueued' in main_qml
            and 'id: currentMatrix\n                                enabled: !workflowBar.startQueued' in main_qml
            and 'enabled: !workflowBar.startQueued; onActivated: root.balanced()' in main_qml
            and 'enabled: !workflowBar.startQueued; onActivated: root.zeroAll()' in main_qml,
            "scenario editors and shortcuts must be frozen during a pending START snapshot")
    require("controller.pendingApply = true" in workflow
            and "smartSession.requestStartWithScenario(\n            controller.startScenarioSnapshot())" in workflow
            and "controller.applyAllSignals()" not in workflow
            and "startWatchdog" in workflow and "onRunningChanged()" in workflow,
            "reconnect must not auto-transmit, but one-click Start must stage a bounded scenario")
    require("offline-ptp-lab" in tray and "if (!tray.boardOnline)" in tray
            and "PTP preview only. Connect ESP32-P4 to transmit." in tray
            and "offlineSettings.previewPtp = wantsRunning" in tray,
            "PTP/smpSynch offline controls must be plans only, not device writes")
    require('case "verify": return 0' in workflow
            and 'case "write": return 1' in workflow
            and 'case "reconnect": return 2' in workflow
            and workflow.count('{ title: "Prepare device" }') == 1
            and workflow.count('{ title: "Install firmware" }') == 1
            and workflow.count('{ title: "Verify restart" }') == 1,
            "firmware wizard must map real firmware states onto exactly three steps")
    require('smartSession.updateNeedsBootloaderHelp' in workflow
            and "smartSession.retryFirmwareUpdate()" in workflow
            and "smartSession.firmwareUpdateCanCancel" in workflow
            and "smartSession.diagnosticsText()" in workflow,
            "short wizard must retain bootloader recovery, cancel, retry, failure diagnostics")
    require("GUIDED ONLY" not in firmware_ui
            and "SESSION OWNED" not in firmware_ui
            and "PACKAGE READY" not in firmware_ui
            and "No firmware file, ESP-IDF" not in workflow,
            "firmware dialog must remove duplicate badges and instructional noise")
    require('toolTipText: "Stop"' not in ribbon
            and 'toolTipText: "Advanced"' not in ribbon
            and 'ToolTip.text: "smpSynch mode"' not in timing_ui,
            "captioned operator controls must not duplicate labels as tooltips")
    require('smartSession.requestStartWithScenario(\n            controller.startScenarioSnapshot())' in workflow
            and "Apply to board" not in main_qml,
            "Edit-to-Start must not regress to a manual Apply-to-board action")
    require("assets/material/smv_injector.svg" in cmake_icons
            and "assets/MATERIAL_ICONS_LICENSE.txt" in cmake_icons
            and "../assets/material/smv_injector.svg" in ribbon,
            "user-provided injector mark and licensed Material icon resources required")
    require("MAGNITUDE  ·" not in matrix and "ANGLE  ·" not in matrix
            and "Equal magnitudes A/B/C" in matrix
            and "Balance angles · 120°" in matrix,
            "context actions must stay compact, descriptive and grouped")
    assert "implicitHeight: 65" in source("RibbonAction.qml")
    assert 'hovered ? "#142334"' in source("RibbonAction.qml")
    require('title: ""' in menu and 'property string contextLabel: ""' in menu
            and 'contextLabel: "Magnitude · "' in matrix
            and 'contextLabel: "Angle · "' in matrix
            and 'title: "Magnitude · "' not in matrix and 'title: "Angle · "' not in matrix,
            "context menu must have no phantom native title/header blank space")
    require("width: 204" in menu and "commandRows * 30" in menu
            and "actionMenu.popup(x, edge.y + 3)" in matrix
            and "signalRow.mapFromItem(field, field.width, field.height)" in matrix,
            "context menu must align to the editor with compact deterministic geometry")
    assert "text: \"Generated setpoint · not independent on-wire proof\"" not in preview
    assert "No ESP32 board detected · Preview mode only" in main_qml
    require("Layout.preferredHeight: 57" in main_qml
            and "SplitView.maximumHeight: 265" in main_qml
            and "SplitView.fillHeight: false" in main_qml,
            "compact timing strip and bounded phasor panel must avoid dead space")
    require("TIME SYNC  /  SV SIMULATION" not in tray
            and "StateBadge {" not in tray,
            "device/PTP status must not be duplicated in the control tray")
    require("Offline waveform preview · no network SV output." not in ribbon,
            "offline Start should not duplicate preview status with a toast")
    assert "Layout.preferredHeight: 78" in main_qml
    device_src = (STUDIO / "src/DeviceController.cpp").read_text(encoding="utf-8")
    session_src = (STUDIO / "src/SmartSessionController.cpp").read_text(encoding="utf-8")
    require('commands << QStringLiteral("START")' in device_src
            and "sendCommandBatch(commands, false, true)" in device_src
            and "formatSignalCommand(ids.at(i)" in device_src
            and "requestStartWithScenario(const QVariantMap& scenario)" in session_src,
            "one-click physical Start must validate and exclusively enqueue all SETs before START")
    require("assets/material_dark/play_arrow.svg" in cmake,
            "light-hover Material icons must have bundled dark variants")
    require(matrix.count("StudioActionMenu {") == 2
            and "id: magnitudeMenu" in matrix and "id: phaseMenu" in matrix
            and "popupType: Popup.Item" in menu,
            "QML-styled wide context menus must replace platform-native renderers")
    for component in ("StudioActionMenu", "StudioMenuDivider", "StudioPtpSwitch", "StudioTimingTray"):
        require(f"qml/{component}.qml" in cmake,
                f"{component} must be registered in the Qt QML module")

    require("--check-operator-ui" in (STUDIO / "src/main.cpp").read_text(encoding="utf-8")
            and "studio-ui-evidence" in (STUDIO.parent.parent / ".github/workflows/arstack-studio-windows-lifecycle.yml").read_text(encoding="utf-8"),
            "Windows geometry/evidence gate must run from existing lifecycle build")
    print("Studio operator workspace contract: PASS")


if __name__ == "__main__":
    main()
