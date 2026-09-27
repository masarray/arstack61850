#!/usr/bin/env python3
"""Regression contract for Browser Repeater service bindings."""

from pathlib import Path
import sys


if len(sys.argv) != 3:
    raise SystemExit(
        "usage: test_browser_delegate_bindings.py Main.qml IedBrowserWorkspace.qml"
    )

main = Path(sys.argv[1]).read_text(encoding="utf-8")
workspace = Path(sys.argv[2]).read_text(encoding="utf-8")

required_main = (
    "property var browserFleet: fleet",
    "property int slotIndex: index",
    "fleet: browserFleet",
    "session: browserFleet.sessionAt(slotIndex)",
    "client: browserFleet.clientAt(slotIndex)",
    "context: browserFleet.contextAt(slotIndex)",
    "reports: browserFleet.reportsAt(slotIndex)",
    "utilities: browserFleet.utilitiesAt(slotIndex)",
    "controls: browserFleet.controlsAt(slotIndex)",
    "engineering: browserFleet.engineeringAt(slotIndex)",
)
missing = [token for token in required_main if token not in main]
if missing:
    raise SystemExit("BROWSER_DELEGATE_FAIL main missing=" + ",".join(missing))

delegate_lines = [line.strip() for line in main.splitlines()]
if any(
    line == "fleet: fleet"
    or line.startswith("session: fleet.")
    or line.startswith("client: fleet.")
    for line in delegate_lines
):
    raise SystemExit("BROWSER_DELEGATE_FAIL self-binding fleet authority")

required_workspace = (
    "client: root.client",
    "modelProvider: root.context",
)
missing = [token for token in required_workspace if token not in workspace]
if missing:
    raise SystemExit("BROWSER_DELEGATE_FAIL workspace missing=" + ",".join(missing))

print(
    "BROWSER_DELEGATE_PASS outer_fleet_alias=true "
    "per_slot_services=true child_projection=true self_binding=false"
)
