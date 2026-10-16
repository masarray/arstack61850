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
    preview = source("SignalPreview.qml")
    tray = source("StudioTimingTray.qml")
    expert = source("PtpExpertPanel.qml")
    config = source("ConfigurationHub.qml")
    matrix = source("SignalMatrix.qml")
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
            and "return profilePanel.startPtpRole(role)" in main_qml
            and "requestStartPtpConfigured(role, panel.ptpProfile())" in expert,
            "main PTP must reuse the existing verified Advanced profile")
    require("checked = Qt.binding(function()" in tray
            and "tray.session.requestStopPtp()" in tray
            and "tray.session.requestSmpSynch(choice)" in tray,
            "main PTP/smpSynch must restore device-owned state after user intent")
    require("SIMULATED metadata" in tray and "sample phase needs independent verification" in tray,
            "SV simulation must never masquerade as independently proven lock")
    require(matrix.count("Qt.Key_Menu") == 2 and matrix.count("Qt.Key_F10") == 2
            and matrix.count("signalRow.openFieldMenu(") == 4,
            "both magnitude and angle menus must support mouse and keyboard")
    require("1.75" in (STUDIO.parent.parent / ".github/workflows/arstack-studio-windows-lifecycle.yml").read_text(encoding="utf-8"),
            "reuse Windows executable for 100%, 150% and 175% DPI acceptance")
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
