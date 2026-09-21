#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 7:
    raise SystemExit(
        "usage: test_browser_global_data.py "
        "IedBrowserWorkspace.qml IedBrowserNavigation.qml BrowserGlobalDataPane.qml "
        "MmsClientController.cpp MmsLiveTreeModel.hpp MmsLiveTreeModel.cpp"
    )

workspace = Path(sys.argv[1]).read_text(encoding="utf-8")
navigation = Path(sys.argv[2]).read_text(encoding="utf-8")
pane = Path(sys.argv[3]).read_text(encoding="utf-8")
client = Path(sys.argv[4]).read_text(encoding="utf-8")
tree_h = Path(sys.argv[5]).read_text(encoding="utf-8")
tree_cpp = Path(sys.argv[6]).read_text(encoding="utf-8")

def require(text: str, token: str, label: str) -> None:
    if token not in text:
        raise SystemExit(f"{label}: missing required token: {token}")

for token in (
    'BrowserGlobalDataPane {',
    'text: "Add to Global Data"',
    'globalDataPane.addSelectedData',
    'globalDataPane.addDataSet',
    'globalDataPane.addReport',
    'globalDataPane.addGoose',
    'globalDataPane.refreshWatched',
    'sequence: "Alt+7"',
):
    require(workspace, token, "Browser workspace")

for token in (
    'targetSection: 6',
    'title: "Global Data"',
    'globalDataCount',
):
    require(navigation, token, "Browser navigation")

for token in (
    'readonly property int maximumEntries: 256',
    'ListModel { id: watchModel }',
    'function addSelectedData',
    'function addDataSet',
    'function addReport',
    'function addGoose',
    'client.refreshEngineeringReferences(refs)',
    'context.treeModel.nodeForReference',
    'model: reports.events',
    'bounded to 64 MMS targets',
):
    require(pane, token, "Global Data pane")

if 'Timer {' in pane:
    raise SystemExit("Global Data pane: background Timer/polling is forbidden")
if 'client.treeModel' in pane:
    raise SystemExit("Global Data pane: must consume canonical context.treeModel, not client.treeModel")

for token in (
    'refreshEngineeringReferences',
    'readTargetsForReferences(references, 64)',
    'QStringLiteral("Refresh Global Data")',
):
    require(client, token, "MMS client bounded watch refresh")

for token in (
    'nodeForReference',
    'readTargetsForReferences',
):
    require(tree_h, token, "Canonical tree API")

for token in (
    'MmsLiveTreeModel::nodeForReference',
    'MmsLiveTreeModel::readTargetsForReferences',
    'result.size() >= maximumTargets',
):
    require(tree_cpp, token, "Canonical tree implementation")

print(
    "P6A_BROWSER_GLOBAL_DATA_PASS "
    "canonical_tree=true explicit_refresh=true max_targets=64 "
    "watch_limit=256 reports=true goose=true polling=false"
)
