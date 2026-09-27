#!/usr/bin/env python3
"""R1A source contract: one canonical DataSet browsing path for SCL and Discovery."""

from pathlib import Path
import sys

if len(sys.argv) != 6:
    raise SystemExit(
        "usage: test_browser_signal_catalog.py "
        "IedBrowserWorkspace.qml IedBrowserNavigation.qml "
        "BrowserDataSetPane.qml BrowserSignalCatalog.qml main.cpp"
    )

workspace, navigation, pane, catalog, main = (
    Path(path).read_text(encoding="utf-8") for path in sys.argv[1:]
)

def require(source: str, tokens: tuple[str, ...], label: str) -> None:
    missing = [token for token in tokens if token not in source]
    if missing:
        raise SystemExit("R1A_SIGNAL_CATALOG_FAIL " + label + " missing=" + ",".join(missing))

require(workspace, (
    "readonly property var dataSetCatalog:",
    "context.loaded ? context.dataSets : []",
    "matching.directoryAvailable === true",
    "function chooseCatalogDataSet(reference)",
    "root.selectedCatalogReference = reference",
    "if (reports.connected)",
    "reports.selectDataSet(j)",
    'root.selectSection(1, "DataSets")',
    'context.authorityKey !== "live-discovery"',
    "root.lastPromptedCatalogGeneration = context.contextGeneration",
    "BrowserSignalCatalog {",
    "onDataSetRequested: function(reference)",
    "root.chooseCatalogDataSet(reference)",
    "onBrowseRequested: root.selectSection(0, \"Data Model\")",
    "dataSetCatalog: root.dataSetCatalog",
    "selectedDataSetReference: root.effectiveCatalogReference",
), "workspace")

require(navigation, (
    "required property var dataSetCatalog",
    "signal dataSetRequested(string reference)",
    "model: root.dataSetCatalog",
    "root.dataSetRequested(modelData.reference)",
), "navigation")

require(pane, (
    "required property var dataSetCatalog",
    "required property string selectedDataSetReference",
    "dataSetCatalog[i].reference === selectedDataSetReference",
    "model: root.selectedDataSetMembers",
    'objectName: "iedBrowserDataSetMembers"',
    "root.memberReference(reference)",
    "root.client.refreshEngineeringReferences(root.readableMemberReferences)",
    "memberRow.resolved.functionalConstraint || root.memberFc(modelData)",
), "DataSet pane")

require(catalog, (
    'objectName: "iedSignalCatalog"',
    "readonly property var canonicalDataSets: context.loaded ? context.dataSets : []",
    'text: "Dataset Signals"',
    'text: "Browse All Signals"',
    "signal dataSetRequested(string reference)",
    "signal browseRequested()",
    "root.dataSetRequested(modelData.reference)",
    'objectName: "iedSignalCatalogDataSets"',
), "catalog")

require(main, (
    'dataSet.name = "QASet"',
    "document.data_sets.push_back(std::move(dataSet))",
), "rendered fixture")

for label, source in (("catalog", catalog), ("pane", pane)):
    for forbidden in ("Timer {", "reports.enableSelected(", "reports.createDynamicDataSet(",
                      "reports.enableSelectedAuthored(", "client.connectToIed("):
        if forbidden in source:
            raise SystemExit("R1A_SIGNAL_CATALOG_FAIL " + label + " forbidden=" + forbidden)

if "model: reports.dataSets" in navigation or "model: reports.selectedDataSetMembers" in pane:
    raise SystemExit("R1A_SIGNAL_CATALOG_FAIL offline catalog bypassed")

print("R1A_SIGNAL_CATALOG_PASS canonical_scl=true discovery=true ordered_members=true "
      "explicit_read=true no_implicit_enable=true no_polling=true")
