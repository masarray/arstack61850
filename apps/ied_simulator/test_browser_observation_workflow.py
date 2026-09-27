#!/usr/bin/env python3
"""Source contract for the canonical Browser observation workflow."""

from pathlib import Path
import sys


if len(sys.argv) != 4:
    raise SystemExit(
        "usage: test_browser_observation_workflow.py "
        "IedBrowserWorkspace.qml MmsClientWorkspace.qml BrowserDataSetPane.qml"
    )

workspace, inspector, datasets = (
    Path(path).read_text(encoding="utf-8") for path in sys.argv[1:]
)


def require(source: str, tokens: tuple[str, ...], label: str) -> None:
    missing = [token for token in tokens if token not in source]
    if missing:
        raise SystemExit(
            "BROWSER_OBSERVATION_FAIL " + label + " missing=" + ",".join(missing)
        )


require(
    inspector,
    (
        'text: "Model values"',
        'text: root.client.operationBusy ? "Reading…" : "Read visible"',
        "model: root.modelProvider.treeModel",
        "root.refreshCurrentVisible(root.firstVisibleRow(), root.lastVisibleRow())",
        'text: "OBSERVED VALUE"',
        'text: "ENGINEERING IDENTITY"',
        'text: root.client.operationBusy ? "Reading selected…" : "Read selected"',
        "root.modelProvider.treeModel.selectRow(index)",
        "SplitView.minimumWidth: 278",
    ),
    "model inspector",
)

require(
    datasets,
    (
        "context.treeModel.nodeForReference(reference)",
        "root.client.refreshEngineeringReferences(reports.selectedDataSetMembers)",
        "signal inspectRequested(string reference)",
        'text: "Ordered members and observed values"',
        'text: root.client.operationBusy ? "Reading…" : "Read members"',
    ),
    "dataset values",
)

require(
    workspace,
    (
        "context: root.context",
        "client: root.client",
        "onInspectRequested: function(reference)",
        "root.context.treeModel.selectReference(reference)",
        'root.selectSection(0, "Data Model")',
    ),
    "browser routing",
)

if "ListModel {" in inspector or "ListModel {" in datasets:
    raise SystemExit(
        "BROWSER_OBSERVATION_FAIL canonical projection duplicated into a QML ListModel"
    )

print(
    "BROWSER_OBSERVATION_PASS canonical_projection=true "
    "virtualized_table=true bounded_visible_read=64 dataset_values=true "
    "dataset_to_model_routing=true"
)
