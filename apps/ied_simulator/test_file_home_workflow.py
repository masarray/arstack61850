#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: test_file_home_workflow.py FileWorkspace.qml Main.qml")

file_qml = Path(sys.argv[1]).read_text(encoding="utf-8")
main_qml = Path(sys.argv[2]).read_text(encoding="utf-8")

required = [
    "Open SCL",
    "Discover IED",
    "Simulate IED",
    "Sniffer",
    "Configuration",
    "Recently opened SCL files",
    "Recently discovered IEDs",
    "signal discoverRequested(string host, int port)",
    "recentDiscoveredIeds",
]
missing = [token for token in required if token not in file_qml]
if missing:
    raise SystemExit("FILE_HOME_WORKFLOW_FAIL missing=" + ",".join(missing))

if "SclWorkspace {" in file_qml:
    raise SystemExit("FILE_HOME_WORKFLOW_FAIL File tab still owns separate SCL workspace UI")

recent_start = file_qml.index("Recently discovered IEDs")
recent_end = file_qml.index("Selecting a recent IED", recent_start)
recent_section = file_qml[recent_start:recent_end]
if "endpointRequested(" not in recent_section:
    raise SystemExit("FILE_HOME_WORKFLOW_FAIL recent IED does not restore endpoint context")
if "discoverRequested(" in recent_section or "discoverAndConnect(" in recent_section:
    raise SystemExit("FILE_HOME_WORKFLOW_FAIL recent IED auto-connects")

if "onDiscoverRequested" not in main_qml or "browserSession.discoverAndConnect()" not in main_qml:
    raise SystemExit("FILE_HOME_WORKFLOW_FAIL explicit Discover IED routing missing")
if 'browserSession.trustedSclPath = ""' not in main_qml:
    raise SystemExit("FILE_HOME_WORKFLOW_FAIL discovery/recent IED does not bypass stale trusted SCL")
if "rememberDiscoveredIed(" not in main_qml:
    raise SystemExit("FILE_HOME_WORKFLOW_FAIL discovered identity is not persisted")

print("FILE_HOME_WORKFLOW_PASS primary_actions=5 recent_scl=pass recent_discovered_ied=pass explicit_discovery=pass auto_reconnect=false")
