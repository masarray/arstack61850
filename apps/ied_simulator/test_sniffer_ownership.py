#!/usr/bin/env python3
"""P3 ownership guard for Sniffer vs Simulator GOOSE responsibilities."""

from pathlib import Path
import sys


def fail(message: str) -> None:
    print(f"SNIFFER_OWNERSHIP_FAIL {message}", file=sys.stderr)
    raise SystemExit(1)


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        fail(f"{label} missing required token: {needle}")


def forbid(text: str, needle: str, label: str) -> None:
    if needle in text:
        fail(f"{label} contains forbidden token: {needle}")


def main() -> int:
    if len(sys.argv) != 4:
        print(
            "usage: test_sniffer_ownership.py <sniffer-qml> <goose-qml> <commissioning-qml>",
            file=sys.stderr,
        )
        return 2

    sniffer_path, goose_path, commissioning_path = map(Path, sys.argv[1:])
    sniffer = sniffer_path.read_text(encoding="utf-8")
    goose = goose_path.read_text(encoding="utf-8")
    commissioning = commissioning_path.read_text(encoding="utf-8")

    # Sniffer is passive observation only. It must not depend on the Simulator
    # backend or gain access to the GOOSE publisher runtime.
    for token in (
        "IedFleetController simulator",
        "IedCommissioningModel",
        "publisherModel",
        "startSelectedGoosePublication",
        "stopSelectedGoosePublication",
        "stopAllGoosePublication",
        "openSimulatorRequested",
    ):
        forbid(sniffer, token, "SnifferWorkspace")
        forbid(goose, token, "GooseWorkspace")

    require(sniffer, "GooseMonitorController monitor", "SnifferWorkspace")
    require(goose, "GooseMonitorController monitor", "GooseWorkspace")
    require(goose, "root.monitor.startCapture()", "GooseWorkspace")
    require(goose, "root.monitor.stopCapture()", "GooseWorkspace")
    require(goose, "root.monitor.exportPcap(selectedFile)", "GooseWorkspace")
    require(goose, 'text: "GOOSE Sniffer"', "GooseWorkspace")
    require(
        goose,
        "GOOSE publication and stimulation belong to IED Simulator commissioning.",
        "GooseWorkspace",
    )

    # Publisher/stimulation remains explicitly owned by Simulator commissioning.
    require(commissioning, "startSelectedGoosePublication()", "CommissioningWorkspace")
    require(commissioning, "stopSelectedGoosePublication()", "CommissioningWorkspace")
    require(commissioning, '"GOOSE"', "CommissioningWorkspace")
    require(commissioning, "commissioningModel.kindFilter = currentText", "CommissioningWorkspace")

    print(
        "SNIFFER_OWNERSHIP_PASS "
        "sniffer=passive goose_capture=true pcap=true "
        "publisher=simulator_commissioning mms_sniffer=false"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
