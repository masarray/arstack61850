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
    # Lock the successful 120-second PTP/SV baseline in the operator CI.
    # A GUI-only change must not silently restore the old free-running bundle.
    project_root = STUDIO.parent.parent
    firmware_profile = (project_root / ".github/workflows/esp32p4-smv-injector.yml").read_text(encoding="utf-8")
    preview_profile = (project_root / ".github/workflows/arstack-studio-native-preview.yml").read_text(encoding="utf-8")
    require("CONFIG_AR_SMV_PTP_PHASE_SERVO_EXPERIMENTAL=y" in firmware_profile
            and "CONFIG_AR_SMV_HW_TX_EVIDENCE_EXPERIMENTAL=y" in firmware_profile
            and "# CONFIG_AR_SMV_BROADCAST_MIRROR is not set" in firmware_profile
            and "phase_sync_preview: true" in preview_profile,
            "Studio native preview must ship measured PTP-SV AUTO firmware with no diagnostic mirror")
    sv_gate = (project_root / "include/ariec61850/time_sync/sv_tx_egress_evidence.hpp").read_text(encoding="utf-8")
    sv_tests = (project_root / "tests/test_sv_tx_egress_evidence.cpp").read_text(encoding="utf-8")
    require("kMaxSoftwarePhaseBoundUs = 4U" in sv_gate
            and "kMaxCanonicalTxOffsetNs = 250000LL" in sv_gate
            and "check_auto_local_sync_gate_requires_both_clocks_and_fresh_mac" in sv_tests,
            "retain independently bounded PTP sample-phase and nonzero MAC publication latency")
    firmware_source = (project_root / "embedded/esp32p4_smv_injector/main/app_main.cpp").read_text(encoding="utf-8")
    require("kSvTxEvidenceEpochStride = 2U" in sv_gate
            and "expected_epoch_stride = kSvTxEvidenceEpochStride" in sv_gate
            and "kEvidenceStride = kSvTxEvidenceEpochStride" in sv_gate
            and "kEgressEvidenceEveryZeroEpochs =\n    SvLocalSyncEvidenceGate::kEvidenceStride" in firmware_source
            and "check_fast_source_auto_qualification_with_fresh_evidence()" in sv_tests,
            "SOURCE AUTO must qualify from two coherent descriptor pairs at 2/4/6s, not 48s")
    # Eliminate long initial phase slew caused by GPTimer driver enable
    # AFTER the EMAC/PTP epoch snapshot. All this work is outside the ISR.
    enable_clock = firmware_source.index("ESP_ERROR_CHECK(gptimer_enable(timer));")
    sample_ptp = firmware_source.index("const auto epoch_arm = plan_ptp_epoch_arm(eth_handle);")
    start_clock = firmware_source.index("ESP_ERROR_CHECK(gptimer_start(timer));")
    require(enable_clock < sample_ptp < start_clock
            and "SV/PTP AUTO phase=" in firmware_source
            and "MACpairs=%lu" in firmware_source
            and "measured_pairs() const noexcept" in sv_gate,
            "PTP startup must compensate GPTimer enable delay and expose early acquisition measurements")
    require("SvLocalRefAcquisition local_acquisition;" in firmware_source
            and "local_acquisition.observe_preflight(" in firmware_source
            and "local_acquisition.observe_live(" in firmware_source
            and "local_acquisition.reset();" in firmware_source
            and "live_clean=%lu/%lu warm_phase=%lu/%lu live_age=%lu" in firmware_source
            and "kRequiredCleanEpochs = 10U" in sv_gate
            and "kWarmRequiredLiveEpochs = 3U" in sv_gate
            and "kMaxWarmMacAcquisitionEpochs = 8U" in sv_gate
            and "if (had_mac_evidence_) reset();" in sv_gate
            and "check_no_intermitttent_local_at_start()" in sv_tests
            and "check_warm_phase_preflight_cannot_fake_canonical_mac()" in sv_tests
            and "check_warm_preflight_negative_session_and_clock_faults()" in sv_tests,
            "LOCAL REF: ten LIVE epochs for COLD, ten stable phase-only PREFLIGHT epochs "
            "plus three verified LIVE MAC epochs for WARM; invalidate on loss of evidence")
    phase_header = (project_root / "include/ariec61850/time_sync/sv_ptp_phase_authority.hpp").read_text(encoding="utf-8")
    phase_tests = (project_root / "tests/test_sv_ptp_phase_authority.cpp").read_text(encoding="utf-8")
    require("g_preflight_phase_clock_active" in firmware_source
            and "publisher_notifications_enabled" in firmware_source
            and "launch_on_next_epoch" in firmware_source
            and "preflight_only = false" in firmware_source
            and "SV/PTP AUTO preflight sample clock armed (SV TX OFF)" in firmware_source
            and "check_auto_preflight_does_not_invent_sync_or_start()" in phase_tests
            and "SvAutoPreflightPolicy::can_steer(" in firmware_source
            and "SvAutoPreflightPolicy::can_advertise_local(" in firmware_source
            and "if (injecting)" in firmware_source
            and "preflight_telemetry" in firmware_source
            and "SvAutoPreflightPolicy" in phase_header,
            "AUTO SOURCE preflight must discipline the sample clock before START without transmitting SV")
    # Field regression: no firmware-reinstall mandate after a slow serial
    # response, and previously verified COM gets priority without bypassing
    # semantic identification.
    io_worker = (STUDIO / "src/DeviceIoWorker.cpp").read_text(encoding="utf-8")
    io_header = (STUDIO / "src/DeviceIoWorker.hpp").read_text(encoding="utf-8")
    session_cpp = (STUDIO / "src/SmartSessionController.cpp").read_text(encoding="utf-8")
    ribbon_qml = source("ModernRibbon.qml")
    require('device-discovery/lastVerifiedPort' in io_worker
            and "orderedDiscoveryPorts(" in io_worker
            and "identityMaxAttempts() noexcept { return 5; }" in io_header
            and "manualRecoveryArmed_" in session_cpp
            and "FIRMWARE RECOVERY" in session_cpp
            and "Firmware recovery" in ribbon_qml,
            "never infer missing firmware from USB timeout; retry identification and prioritize semantic-verified ports")
    main_qml = source("Main.qml")
    preview = source("SignalPreview.qml")
    tray = source("StudioTimingTray.qml")
    expert = source("PtpExpertPanel.qml")
    config = source("ConfigurationHub.qml")
    require("Lab simulation controls" not in config
            and "Signals, firmware, PTP and device settings" in config,
            "Advanced must not imply this is only a simulated lab tool")
    require("Setpoints pending · Start to apply" in main_qml
            and "root.pendingApply" in main_qml
            and "pendingApply = true" in main_qml,
            "table edits must expose unsent setpoints without claiming wire readback")
    require("Layout.preferredHeight: Math.min(410, Math.max(215, root.height - 365))" in main_qml
            and "Layout.maximumHeight: 410" in main_qml
            and "ColumnLayout {" in main_qml,
            "voltage and current must stack vertically with bounded content height")
    require('property bool ptpDefaultStartAttempted: false' in main_qml
            and 'root.ptpDefaultStartAttempted = true' in main_qml
            and '!root.startPtpRole("SOURCE")' in main_qml,
            "default PTP start must be one-shot and deliberate OFF must cancel it")
    require("phaseLink && row < 3" in main_qml
            and "sendLinkedGroup(group) : sendSignal(group, row)" in main_qml,
            "edited 3-phase angles must be transmitted from the same table model")
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
            and "root.ptpDesiredForSession = false" in main_qml,
            "PTP user intent restoration must be bounded, explicit and cancellable")
    ribbon = source("ModernRibbon.qml")
    checkbox = source("StudioDarkCheckBox.qml")
    calm = source("CalmButton.qml")
    require("Layout.preferredWidth: bar.compact ? 232 : 286" in ribbon
            and "font.pixelSize: bar.compact ? 21 : 25" in ribbon,
            "operator brand/logo must stay legible")
    require("indicator: Rectangle {" in checkbox,
            "checkbox must keep one stable custom indicator on hover")
    require("anchors.centerIn: parent" in calm,
            "button caption needs horizontal centering")
    require("function sendSignal(group, row)" in main_qml
            and "Layout.fillHeight: false" in main_qml
            and "Layout.preferredHeight: 196" in main_qml,
            "tables and live sent phases must use explicit bounded layout without link-toggle replay")
    require('property bool ptpDefaultStartAttempted: false' in main_qml
            and 'root.startPtpRole("SOURCE")' in main_qml
            and "device.deviceVerified" in main_qml,
            "default PTP must reuse verified session ownership")
    device_cpp = (STUDIO / "src/DeviceController.cpp").read_text(encoding="utf-8")
    require('commands << QStringLiteral("CFG FREQ %1")' in device_cpp
            and "const double phase = row.at(1).toDouble(&phaseOk);" in device_cpp
            and "const double angleMdeg = phaseDegrees * 1000.0;" in device_cpp
            and 'commands << QStringLiteral("CFG COMMIT %1 %2")' in device_cpp
            and 'scenarioStage_ = ScenarioStage::awaiting_commit;' in device_cpp
            and 'scenarioStage_ = ScenarioStage::awaiting_readback;' in device_cpp
            and 'scenarioStage_ = ScenarioStage::awaiting_start_ack;' in device_cpp,
            "START requires signed millidegree setpoints, firmware commit ACK and readback")
    matrix = source("SignalMatrix.qml")
    link_toggle = source("StudioLinkSwitch.qml")
    require("StudioDarkCheckBox {" not in matrix
            and "requestSetEnabled(" not in matrix
            and "property bool channelEnabled:" not in matrix
            and "Item { Layout.preferredWidth: 25 }" not in matrix,
            "all eight injection table channel checkboxes and their header spacer must be gone")
    require("StudioLinkSwitch {" in main_qml
            and "Switch {" in link_toggle
            and "Behavior on x" in link_toggle
            and "Accessible.name" in link_toggle
            and "root.saveScenarioTimer.restart()" in main_qml
            and "Not all live phases were sent." not in main_qml,
            "3-phase link must be an accessible animated switch that sends no setpoints")
    require("wasDisabled ? 0 : rows[i][0]" in main_qml
            and 'model.setProperty(i, "enabled", true)' in main_qml
            and "migratedDisabled > 0" in main_qml
            and "Review before START." in main_qml
            and "Hidden disabled channel detected." in main_qml,
            "legacy disabled-channel presets must migrate visibly and START cannot hide OFF channels")
    require("qml/StudioLinkSwitch.qml" in (STUDIO / "CMakeLists.txt").read_text(encoding="utf-8"),
            "link control must be registered with the native QML module")
    require("Layout.fillHeight: true" in matrix
            and "Layout.minimumHeight: 34" in matrix
            and "Item { Layout.fillHeight: true }" not in matrix,
            "editable channel rows must consume vertical space without a blank filler")
    require('bar.workflow.injectionRunning || bar.workflow.startQueued || bar.controller.offlinePreviewRunning'
            in ribbon_qml
            and '!ribbon.injectionRunning && !ribbon.startQueued' in source("WorkflowBar.qml")
            and 'readonly property bool startQueued: Boolean(' in source("WorkflowBar.qml")
            and 'ribbon.device.scenarioApplying) || ribbon.startIntentWaiting' in source("WorkflowBar.qml")
            and 'if (startIntentWaiting)' in source("WorkflowBar.qml")
            and 'cancelStartIntent()' in source("WorkflowBar.qml")
            and 'startWatchdog' not in source("WorkflowBar.qml"),
            "SV STOP must remain actionable while the acknowledged scenario transaction is pending")
    require("function flushPendingEdits()" in matrix
            and "magnitudeApplyTimer.stop()" in matrix
            and "phaseApplyTimer.stop()" in matrix
            and "currentMatrix.flushPendingEdits()" in main_qml
            and "voltageMatrix.flushPendingEdits()" in main_qml
            and "if (!scenario) return" in source("WorkflowBar.qml"),
            "START must commit last visible I/V angle before ordered scenario serialization")
    require("Layout.fillHeight: true" in main_qml
            and "Layout.maximumHeight: root.compactLayout ? 352 : 384" not in main_qml
            and main_qml.index("id: voltageMatrix") < main_qml.index("id: currentMatrix"),
            "Voltage and Current need a vertical, flexible-height editor workspace")
    ribbon_qml = source("ModernRibbon.qml")
    require("Layout.preferredWidth: bar.compact ? 232 : 286" in ribbon_qml
            and "font.pixelSize: bar.compact ? 21 : 25" in ribbon_qml
            and "width: 53" in ribbon_qml,
            "SMV Injector branding must be readable at compact and normal widths")
    device_controller = (STUDIO / "src/DeviceController.cpp").read_text(encoding="utf-8")
    workflow = source("WorkflowBar.qml")
    require("const scenario = controller.startScenarioSnapshot()" in workflow
            and "smartSession.requestStartWithScenario(scenario)" in workflow
            and "smartSession.requestStart()" not in workflow,
            "every START must transmit all 8 visible setpoints, never reuse stale board angles")
    require('QStringLiteral("IA"), QStringLiteral("IB")' in device_controller
            and 'QStringLiteral("IC"), QStringLiteral("IN")' in device_controller
            and 'QStringLiteral("UA"), QStringLiteral("UB")' in device_controller
            and 'QStringLiteral("UC"), QStringLiteral("UN")' in device_controller
            and 'commands << QStringLiteral("CFG COMMIT %1 %2")' in device_controller
            and 'const auto phaseMdeg = std::llround(angleMdeg);' in device_controller
            and 'sv_scenario_hash(normalized)' in device_controller,
            "START transaction must retain every channel and signed millidegree with encoded hash")
    require("anchors.centerIn: parent" in source("CalmButton.qml")
            and "Text.AlignHCenter" in source("RibbonAction.qml"),
            "control captions must center on both axes")
    checkbox_qml = source("StudioDarkCheckBox.qml")
    require("width: 20" in checkbox_qml and "height: 20" in checkbox_qml,
            "checkbox geometry must remain unchanged by hover")
    require("hovered ? \"#edf5ff\"" in source("CalmButton.qml")
            and "hovered ? \"#142334\"" in source("CalmButton.qml")
            and "/assets/material_dark/" in source("CalmButton.qml"),
            "light hover background must pair with dark caption and icon")
    combo = source("StudioDarkComboBox.qml")
    require('choice.highlighted || choice.hovered ? "#142334"' in combo
            and 'choice.highlighted || choice.hovered ? "#edf5ff"' in combo,
            "Advanced dropdown options must be readable on white hover")
    dark_tool = source("DarkToolButton.qml")
    require('control.hovered ? "#142334"' in dark_tool
            and 'control.hovered ? "#edf5ff"' in dark_tool,
            "Advanced text/icon tool buttons must use dark captions on white hover")
    require("background: Item { implicitWidth: 1; implicitHeight: 1 }" in checkbox_qml,
            "checkbox hover must not render a second platform-native indicator")

    menu = source("StudioActionMenu.qml")
    cmake = (STUDIO / "CMakeLists.txt").read_text(encoding="utf-8")

    ordered(main_qml, "id: voltageMatrix", "id: currentMatrix",
            "voltage must precede current in the injection table")
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
            and "requestStartPtpConfigured(role, panel.ptpProfile(role))" in expert,
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
    require('const accepted = smartSession.requestStartWithScenario(' in workflow
            and 'controller.startScenarioSnapshot()' in workflow
            and 'smartSession.requestStart()' not in workflow,
            "every Start must send the visible 4I+4V table, never reuse stale device angles")
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
            and "smartSession.requestStartWithScenario(" in workflow
            and "const scenario = controller.startScenarioSnapshot()" in workflow
            and "if (!scenario) return" in workflow
            and "controller.applyAllSignals()" not in workflow
            and "readonly property bool startQueued:" in workflow
            and "scenarioStateChanged" in device_cpp
            and "onRunningChanged()" in workflow,
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
    require('smartSession.requestStartWithScenario(' in workflow
            and 'const scenario = controller.startScenarioSnapshot()' in workflow
            and 'if (!scenario) return' in workflow
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
    require('commands << QStringLiteral("CFG COMMIT %1 %2")' in device_src
            and "sendCommandBatch(commands, false, true)" in device_src
            and "formatSignalCommand(ids.at(i)" in device_src
            and 'sendCommand(QStringLiteral("CFG SHOW %1")' in device_src
            and 'sendCommand(QStringLiteral("START %1 %2 %3")' in device_src
            and "requestStartWithScenario(const QVariantMap& scenario)" in session_src,
            "physical START must follow exclusive staged SETs, commit ACK, and verified readback")
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
    # Capture is a first-class operator workflow, not a hidden Device-only
    # widget, a second lifetime-owner, or an external script requirement.
    hub = source("ConfigurationHub.qml")
    capture_ui = source("SvCapturePanel.qml")
    capture_cpp = (STUDIO / "src/SvCaptureController.cpp").read_text(encoding="utf-8")
    require('text: "Capture"' in ribbon and
            'onClicked: bar.controller.openCapture()' in ribbon and
            "function openCapture()" in main_qml and
            "profilePanel.showCapture()" in main_qml and
            "function showCapture()" in hub and
            'ExpertTab { text: "Capture" }' in hub and
            "active: hub.captureTabVisited" in hub and
            "sourceComponent: SvCapturePanel {" in hub and
            "SvCapturePanel {" not in source("DeviceExpertPanel.qml"),
            "Studio must expose ONE persistent Capture workspace from its main ribbon")
    require('"Capture SV + PTP"' in capture_ui and
            "capture.startCapture(" in capture_ui and
            "capture.summary.sv_frames" in capture_ui and
            "capture.summary.ptp_frames" in capture_ui and
            "capture.summary.missing_samples" in capture_ui and
            "capture.summary.sync_transitions" in capture_ui and
            "capture.openCaptureSupportHelp()" in capture_ui,
            "Studio capture must start in-app and display decoded SV/PTP evidence without terminal scripts")
    require("result.insert(QStringLiteral(\"summary\"), QVariantMap{" in capture_cpp and
            "result.insert(QStringLiteral(\"summary\"), QVariantMap{" in capture_cpp and
            "QDesktopServices::openUrl(" in capture_cpp and
            "QString findDumpcap()" in capture_cpp,
            "Studio capture results must derive from production wire decoder with click-only setup")
    ptp_src = source("PtpExpertPanel.qml")
    ptp_fw = (STUDIO.parent.parent / "embedded/esp32p4_smv_injector/main/live_control.cpp").read_text(encoding="utf-8")
    ptp_sync = (STUDIO.parent.parent / "embedded/esp32p4_smv_injector/main/smp_synch_lab.cpp").read_text(encoding="utf-8")
    dev_h = (STUDIO / "src/DeviceController.hpp").read_text(encoding="utf-8")
    dev_cpp = (STUDIO / "src/DeviceController.cpp").read_text(encoding="utf-8")
    session_h = (STUDIO / "src/SmartSessionController.hpp").read_text(encoding="utf-8")
    require('text: "Inspect SV lock"' in ptp_src and
            "panel.session.requestSyncDiagnostics()" in ptp_src and
            "panel.device.syncDiagnostics" in ptp_src and
            'std::strcmp(subcommand, "DIAG")' in ptp_fw and
            '"SVSYNC EVENT seq=' in ptp_fw and
            "record_transition(" in ptp_sync and
            "Q_PROPERTY(QString syncDiagnostics" in dev_h and
            "SVSYNC DIAG " in dev_cpp and
            "requestSyncDiagnostics()" in session_h and
            "onboard_sync_flight_UNVERIFIED" in capture_ui and
            "syncFlightLatest" in dev_h and
            "syncFlightLatest_ = flightLine" in dev_cpp,
            "Studio must expose the physical firmware's internal, reason-coded "
            "lock flight recorder in Time Sync and correlate it passively with Capture")
    firmware_panel = source("FirmwarePanel.qml")
    ptp_panel = source("PtpExpertPanel.qml")
    device_control_cpp = (STUDIO / "src/DeviceController.cpp").read_text(encoding="utf-8")
    require("sendCommand(QStringLiteral(\"PROFILE SHOW\"))" in device_control_cpp and
            "Could not request verified PTP role readback" in device_control_cpp and
            "Board rejected PTP %1 command: %2" in device_control_cpp and
            "awaiting_preflight" in device_control_cpp and
            "PTP status=(RUNNING|STOPPED)" in device_control_cpp,
            "PTP role ACK must trigger explicit firmware readback with meaningful failure")
    require("exactBuildMatch" in firmware_panel and
            "firmwareBuildId" in firmware_panel and
            "expectedFirmwareBuildId" in firmware_panel and
            "Installed build differs from this Studio package" in firmware_panel and
            'panel.device.ptpStatus.indexOf("PTP start failed")' in ptp_panel and
            'ptpLastStartFailure' in ptp_panel and
            'respondToPeerDelay: role === "SOURCE" ? true : pdelayCheck.checked' in ptp_panel and
            'announceIntervalMs: role === "SOURCE" ? 1000' in ptp_panel and
            'syncIntervalMs: role === "SOURCE" ? 1000' in ptp_panel and
            "SOURCE profile: 1 s Announce" in ptp_panel and
            "sourcePtpProfileError" in device_control_cpp and
            "ptpRuntimeRunningSeen_" in device_control_cpp and
            "busy: Boolean(panel.startPending)" in ptp_panel,
            "Firmware tab must show build provenance and SOURCE 9-3 preflight")
    ptp_firmware = (STUDIO.parent.parent / "embedded/esp32p4_smv_injector/main/ptp_lab_task.cpp").read_text(encoding="utf-8")
    eth_port = (STUDIO.parent.parent / "embedded/esp32p4_smv_injector/main/ethernet_port.c").read_text(encoding="utf-8")
    ptp_studio = (STUDIO / "src/DeviceController.cpp").read_text(encoding="utf-8")
    ptp_console = (STUDIO.parent.parent / "embedded/esp32p4_smv_injector/main/live_control.cpp").read_text(encoding="utf-8")
    require("ETHERNET_EVENT_START" in eth_port
            and "ETHERNET_EVENT_CONNECTED" in eth_port
            and "ETHERNET_EVENT_DISCONNECTED" in eth_port
            and "xEventGroupWaitBits(" in eth_port
            and "kTxReady" in eth_port
            and "ETHERNET_LINK_DOWN" in eth_port
            and "ETHERNET_DRIVER_NOT_STARTED" in eth_port
            and "void ar_esp32p4_ptp_print_link_status(void)" in eth_port
            and "ar_esp32p4_ptp_print_link_status();" in ptp_console
            and "ethernet_tx_invalid_state" in ptp_firmware
            and "ETHERNET_TX_INVALID_STATE" in ptp_firmware
            and "kStartReadyPolls = 350U" in ptp_firmware
            and "source_reason=ETHERNET_LINK_DOWN" in ptp_studio
            and "source_reason=ETHERNET_TX_INVALID_STATE" in ptp_studio
            and "kPtpEthernetExpression" in ptp_studio
            and "ptpEthernetLink" in source("PtpExpertPanel.qml")
            and "ptpEthernetDriver" in source("PtpExpertPanel.qml")
            and "PTP PHY link (board ETH_EVENT)" in
                (STUDIO / "src/SmartSessionController.cpp").read_text(encoding="utf-8")
            and "ptpPhysicalEthernetPreflightAndMacFailure()" in
                (STUDIO / "src/DeterministicSessionHarness.cpp").read_text(encoding="utf-8"),
            "PTP SOURCE must prove ETH driver and PHY before TX, classify MAC INVALID_STATE, "
            "and never mark a failed START as RUNNING")


    require('SOURCE_REQUIRES_1S_ANNOUNCE_1S_SYNC_AND_PDELAY' in ptp_firmware and
            'ar_ptp_lab_last_config_error()' in ptp_console and
            'PDELAY_RX_TIMESTAMP_UNAVAILABLE' in ptp_firmware and
            'SOURCE_TX_EVIDENCE_TIMEOUT' in ptp_firmware and
            'IEEE1588_CLOCK_UNAVAILABLE' in ptp_firmware and
            'PTP start rejected: source_reason=%s' in ptp_console and
            'ptpRuntimeRunningSeen_' in device_control_cpp and
            'if (!ptpRuntimeRoleSeen_ || !ptpRuntimeRunningSeen_' in device_control_cpp,
            "PTP errors must originate on board and two-order runtime proofs must be joined")
    # Control-session lease is a safety authority, not a QML watchdog or
    # clock qualification. One boot-lifetime periodic timer eliminates the
    # old racing one-shot stop/rearm from the ESP console and timer callback.
    life_header = (STUDIO / "src/DeviceController.hpp").read_text(encoding="utf-8")
    life_firmware = (STUDIO.parents[1] / "embedded/esp32p4_smv_injector/main/live_control.cpp").read_text(encoding="utf-8")
    life_qml = source("WorkflowBar.qml")
    require("esp_timer_start_periodic(" in life_firmware
            and "kControlWatchdogPeriodUs = 250000ULL" in life_firmware
            and "void enforce_control_lease() noexcept" in life_firmware
            and "g_control_lease_mux" in life_firmware
            and "esp_timer_start_once(" not in life_firmware
            and "SVLIFE state=%s stop_cause=%s" in life_firmware
            and "SvStopCause::lease_expired" in life_firmware
            and "SvStopCause::runtime_fault" in life_firmware
            and "Q_PROPERTY(QString svStopReason READ svStopReason NOTIFY svStopReasonChanged)" in life_header
            and "ribbon.unexpectedSvStop" in life_qml
            and "workflowBar.displayState" in source("Main.qml")
            and "function onSvStopReasonChanged()" in source("Main.qml"),
            "SV output must fail-closed on measured heartbeat loss, preserve board STOP cause, "
            "and never show misleading green READY after an unrequested stop")

    # Operator desktop layout: bounded injector column, space for plots,
    # no extra QML START authority and no abandoned first-click intent.
    layout = source("Main.qml")
    ribbon = source("ModernRibbon.qml")
    workflow = source("WorkflowBar.qml")
    require("SplitView.maximumWidth: 660" in layout
            and "SplitView.preferredWidth: root.compactLayout ? 505 : 610" in layout
            and "SplitView.preferredWidth: root.compactLayout ? 420 : 560" in layout
            and "run-bolt.svg" in ribbon and "stop-bolt.svg" in ribbon
            and "assets/material/run-bolt.svg" in (STUDIO / "CMakeLists.txt").read_text(encoding="utf-8")
            and "assets/material/stop-bolt.svg" in (STUDIO / "CMakeLists.txt").read_text(encoding="utf-8")
            and 'text: bar.device.running ? "RUN" : "STOP"' in ribbon
            and "property bool startIntentWaiting: false" in workflow
            and "interval: 8000" in workflow
            and "startIntentBootId" in workflow
            and "function cancelStartIntent()" in workflow
            and "function deliverReadyStart()" in workflow
            and "smartSession.state === \"PREPARING 4I+4V\"" in workflow
            and "if (startIntentWaiting)" in workflow
            and "smartSession.requestStartWithScenario(snapshot)" in workflow,
            "Studio must offer proportional engineering panels, verified bolt RUN/STOP "
            "and first-click START through a bounded same-boot cancellable intent")

    toast_layout = source("Main.qml")
    toast_view = source("StatusToast.qml")
    require("function explainFailure(message)" in toast_layout
            and "ETHERNET_LINK_DOWN" in toast_layout
            and "ETHERNET_TX_INVALID_STATE" in toast_layout
            and "function onLastErrorChanged()" in toast_layout
            and toast_layout.count("anchors.centerIn: parent") >= 2
            and "lastToastAt" in toast_layout
            and "messageTimer.interval = critical ? 4200 : 2300" in toast_layout
            and "wrapMode: Text.WordWrap" in toast_view
            and "Ethernet connected" in
                (STUDIO / "src/DeviceController.cpp").read_text(encoding="utf-8"),
            "PTP physical-link and board failures must generate bounded actionable center-screen toasts "
            "in both the main window and Advanced")

    clipboard_header = (STUDIO / "src/StudioClipboard.hpp").read_text(encoding="utf-8")
    clipboard_cpp = (STUDIO / "src/StudioClipboard.cpp").read_text(encoding="utf-8")
    main_cpp = (STUDIO / "src/main.cpp").read_text(encoding="utf-8")
    studio_main = source("Main.qml")
    require("import ARStack.Studio 1.0" in ptp_panel and
            "if (StudioClipboard.copyText(snapshot))" in ptp_panel and
            "panel.controller.openDiagnostics()" in ptp_panel and
            "Q_INVOKABLE bool copyText" in clipboard_header and
            "clipboard->text(QClipboard::Clipboard) == text" in clipboard_cpp and
            "--check-clipboard" in main_cpp and
            "selectByMouse: true" in studio_main,
            "Timing Copy diagnostics must use imported GUI singleton, verify native clipboard, "
            "and offer full selectable manual fallback")

    ptp_facade = (STUDIO.parents[1] / "embedded/esp32p4_smv_injector/main/ptp_mode_facade.cpp").read_text(encoding="utf-8")
    require("const bool source_clock_qualified = " in ptp_facade
            and "status.is_running = ok && ar_ptp_source_runtime_active();" in ptp_facade
            and "status.timing_authority_clock_usable = source_clock_qualified;" in ptp_facade,
            "PTP SOURCE physical RUNNING must survive unqualified clock freshness; "
            "canonical timing authority must remain independently fail-closed")

    print("Studio operator workspace contract: PASS")


if __name__ == "__main__":
    main()
