#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 5:
    raise SystemExit("usage: test_browser_convergence.py IedBrowserWorkspace.qml IedBrowserNavigation.qml BrowserStatusConsole.qml BrowserDataModelPane.qml")

workspace_path = Path(sys.argv[1])
workspace = workspace_path.read_text(encoding="utf-8")
report_inspector = (workspace_path.parent / "BrowserReportPane.qml").read_text(encoding="utf-8")
navigation = Path(sys.argv[2]).read_text(encoding="utf-8")
status = Path(sys.argv[3]).read_text(encoding="utf-8")
data_model = Path(sys.argv[4]).read_text(encoding="utf-8")

workspace_required = [
    'component RibbonAction: Button',
    'Layout.preferredHeight: 94',
    'text: "MODEL"',
    'text: "COMMANDS"',
    'text: "Online"',
    '"Discover IED"',
    'client.readEngineeringSelected()',
    'BrowserStatusConsole {',
    'modelProvider: root.context',
    'BrowserReportPane {',
]
navigation_required = [
    'color: theme.navigation',
    'text: "ENGINEERING EXPLORER"',
    'id: iedRoot',
    'title: "GOOSE"',
    'title: "Reports"',
    'title: "Setting Groups"',
    'title: "Files"',
    'title: "DataSets"',
    'title: "Data Model"',
    'signal gooseRequested(int index)',
    'context.gooseStreams',
    'context.treeModel',
]
status_required = [
    'readonly property int maximumEntries: 80',
    'while (historyModel.count > maximumEntries)',
    'function captureSession()',
    'function captureContext()',
    'Bounded Browser history · no background polling',
]
data_model_required = [
    'objectName: "browserDataModelChildren"',
    'model: root.detail',
    'root.detail.select(childRow.index)',
    'root.detail.toggle(childRow.index)',
    'root.detail.selectParent()',
    'root.detail.truncated',
    'onDoubleClicked: function(event)',
]

for label, source, required in [
    ("workspace", workspace, workspace_required),
    ("navigation", navigation, navigation_required),
    ("status", status, status_required),
    ("data-model", data_model, data_model_required),
]:
    missing = [token for token in required if token not in source]
    if missing:
        raise SystemExit("BROWSER_CONVERGENCE_FAIL " + label + " missing=" + ",".join(missing))

report_required = [
    'objectName: "browserReportUnifiedInspector"',
    'Control Block Attributes',
    'Information Received in Last Report',
    'heading: "Data"',
    'newestMatchingReport()',
    'root.attributeLabels[index]',
    'root.informationLabels[index]',
    'required property int index',
    'required property var modelData',
    'text: dataRow.reference',
    'text: dataRow.displayValue',
    'delegate: PropertyRow { lastReportSection: true }',
    'delegate: DataRow { received: false }',
    'reports.selectedRcbDataSetMembers',
    'String(frame.reportId) === wanted',
    'Offline values are not fabricated.',
]
missing_report = [term for term in report_required if term not in report_inspector]
if missing_report:
    raise SystemExit("BROWSER_REPORT_INSPECTOR_FAIL missing=" + ",".join(missing_report))
if 'title: modelData[0]' in report_inspector or 'rawValue: modelData[1]' in report_inspector:
    raise SystemExit("BROWSER_REPORT_INSPECTOR_FAIL nested-array attribute labels")
if "ReportsWorkspace {" in workspace:
    raise SystemExit("BROWSER_REPORT_INSPECTOR_FAIL legacy three-panel report view")
if 'implicitHeight: 25' not in navigation or "readonly property bool newGroup:" not in navigation:
    raise SystemExit("BROWSER_REPORT_INSPECTOR_FAIL navigation is not grouped and compact")

if "client.treeModel" in navigation:
    raise SystemExit("BROWSER_CONVERGENCE_FAIL navigation regressed to session-owned model")
if "Timer {" in status:
    raise SystemExit("BROWSER_CONVERGENCE_FAIL status console introduced polling timer")
if "maximumEntries: 80" not in status:
    raise SystemExit("BROWSER_CONVERGENCE_FAIL status history is not bounded")

print("BROWSER_CONVERGENCE_PASS ribbon=pass dense_explorer=pass goose_drilldown=pass data_model_drilldown=pass ied_root=pass contextual_commands=pass canonical_tree=pass bounded_history=80 polling=false")
