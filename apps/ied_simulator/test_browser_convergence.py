#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 4:
    raise SystemExit("usage: test_browser_convergence.py IedBrowserWorkspace.qml IedBrowserNavigation.qml BrowserStatusConsole.qml")

workspace = Path(sys.argv[1]).read_text(encoding="utf-8")
navigation = Path(sys.argv[2]).read_text(encoding="utf-8")
status = Path(sys.argv[3]).read_text(encoding="utf-8")

workspace_required = [
    'text: "MODEL"',
    'text: "COMMANDS"',
    'text: "Online"',
    '"Discover IED"',
    'client.readEngineeringSelected()',
    'BrowserStatusConsole {',
    'modelProvider: root.context',
]
navigation_required = [
    'text: "ENGINEERING EXPLORER"',
    'id: iedRoot',
    'title: "GOOSE"',
    'title: "Reports"',
    'title: "Setting Groups"',
    'title: "Files"',
    'title: "DataSets"',
    'title: "Data Model"',
    'context.treeModel',
]
status_required = [
    'readonly property int maximumEntries: 80',
    'while (historyModel.count > maximumEntries)',
    'function captureSession()',
    'function captureContext()',
    'Bounded Browser history · no background polling',
]

for label, source, required in [
    ("workspace", workspace, workspace_required),
    ("navigation", navigation, navigation_required),
    ("status", status, status_required),
]:
    missing = [token for token in required if token not in source]
    if missing:
        raise SystemExit("BROWSER_CONVERGENCE_FAIL " + label + " missing=" + ",".join(missing))

if "client.treeModel" in navigation:
    raise SystemExit("BROWSER_CONVERGENCE_FAIL navigation regressed to session-owned model")
if "Timer {" in status:
    raise SystemExit("BROWSER_CONVERGENCE_FAIL status console introduced polling timer")
if "maximumEntries: 80" not in status:
    raise SystemExit("BROWSER_CONVERGENCE_FAIL status history is not bounded")

print("BROWSER_CONVERGENCE_PASS ied_root=pass contextual_commands=pass canonical_tree=pass bounded_history=80 polling=false")
