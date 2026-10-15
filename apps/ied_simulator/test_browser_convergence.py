#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 5:
    raise SystemExit("usage: test_browser_convergence.py IedBrowserWorkspace.qml IedBrowserNavigation.qml BrowserStatusConsole.qml BrowserDataModelPane.qml")

workspace_path = Path(sys.argv[1])
workspace = workspace_path.read_text(encoding="utf-8")
report_inspector = (workspace_path.parent / "BrowserReportPane.qml").read_text(encoding="utf-8")
data_set = (workspace_path.parent / "BrowserDataSetPane.qml").read_text(encoding="utf-8")
navigation = Path(sys.argv[2]).read_text(encoding="utf-8")
status = Path(sys.argv[3]).read_text(encoding="utf-8")
data_model = Path(sys.argv[4]).read_text(encoding="utf-8")
fleet_global = (workspace_path.parent / "FleetGlobalDataPane.qml").read_text(encoding="utf-8")
per_ied_global = (workspace_path.parent / "BrowserGlobalDataPane.qml").read_text(encoding="utf-8")
canonical_header = (workspace_path.parent.parent / "src" / "MmsLiveTreeModel.hpp").read_text(encoding="utf-8")
canonical_tree = (workspace_path.parent.parent / "src" / "MmsLiveTreeModel.cpp").read_text(encoding="utf-8")
large_model_qa = (workspace_path.parent.parent / "src" / "ied_browser_large_model_qa.cpp").read_text(encoding="utf-8")

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
    'BrowserEngineeringEvidence {',
    'root.currentNode.engineeringEvidence',
    'objectName: "browserDataModelSelectedAttributeInspector"',
    'root.detail.rootChildCount === 0 || root.currentNode.kind === "DA"',
    'function onValuesChanged() { root.valueRevision += 1 }',
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

data_set_required = [
    'objectName: "browserDataSetMembers"',
    'function memberParts(',
    'function toggleMember(',
    'dataSetChildrenForReference(',
    'root.expandedMemberIndex',
    'maximumExpandedChildren: 64',
    'required property var modelData',
    'root.showSelectedEvidence = selected',
    'BrowserEngineeringEvidence {',
    'objectName: "browserDataSetEvidenceInspector"',
    'visible: root.selectedAttributeNode.kind === "DA"',
    'root.selectedAttributeNode.hasReceivedValue === true',
    'root.selectedAttributeNode.valueSourceReference',
    '{label: "IED Timestamp"',
    'memberRow.resolved.hasReceivedValue !== true',
    'attributeRow.modelData.hasReceivedValue === true',
    'root.selectedAttributeNode.engineeringEvidence || ({})',
    'selected.reference === root.selectedAttributeReference',
    'root.selectedAttributeReference = selected',
    'memberRow.resolved.reference : ""',
    'root.showSelectedEvidence = selected',
]
missing_dataset = [term for term in data_set_required if term not in data_set]
if missing_dataset:
    raise SystemExit("BROWSER_DATASET_HIERARCHY_FAIL missing=" + ",".join(missing_dataset))
if 'root.memberNode(modelData)' in data_set:
    raise SystemExit("BROWSER_DATASET_HIERARCHY_FAIL unnormalized per-row linear lookup")
# The DataSets evidence inspector must be inside the selectable members page,
# never an unreachable third child in the overview/members StackLayout.
stack = data_set.split('StackLayout {', 1)[1]
members = stack.find('objectName: "browserDataSetMembers"')
evidence = stack.find('objectName: "browserDataSetEvidenceInspector"')
if members < 0 or evidence <= members or 'ColumnLayout {' not in stack[:members]:
    raise SystemExit("BROWSER_DATASET_EVIDENCE_FAIL inspector is not in member pane")
if data_set.count('BrowserEngineeringEvidence {') != 1:
    raise SystemExit("BROWSER_DATASET_EVIDENCE_FAIL duplicate/hidden evidence inspector")
if 'Timer {' in data_set:
    raise SystemExit("BROWSER_DATASET_PROVENANCE_FAIL implicit per-DA timer")
if 'readEngineeringSelected()' in data_set or 'readEngineeringSelected()' in data_model:
    raise SystemExit("BROWSER_TYPED_EVIDENCE_FAIL passive inspector initiated MMS Read")

# M2K — migrate ARSAS live-grid presentation while retaining one authoritative
# per-IED canonical MMS tree and rejecting "readable means LIVE" shortcuts.
for token in [
    'objectName: "fleetGlobalDataSearch"',
    'text: "QUALITY"',
    'text: "IED TIMESTAMP"',
    'function requestRefresh()',
    'Qt.callLater(root.refreshRows)',
    'rows.setProperty(i, role, next[i][role])',
    'readonly property int maximumPreviewLength: 256',
    'onDoubleClicked: root.iedRequested(slotIndex)',
]:
    if token not in fleet_global:
        raise SystemExit("BROWSER_ARSAS_LIVE_GRID_FAIL missing=" + token)
if ("node.hasReceivedValue === true ? \"LIVE\" : \"NOT READ\"" not in per_ied_global or
        "bool hasReceivedValue{};" not in canonical_header or
        'QStringLiteral("hasReceivedValue")' not in canonical_tree or
        "node.hasReceivedValue = true;" not in canonical_tree or
        "unread_sample_false_live" not in large_model_qa or
        "empty_sample_received" not in large_model_qa):
    raise SystemExit("BROWSER_ARSAS_SSOT_FAIL unread values fabricated as LIVE")
main_qml = (workspace_path.parent / "Main.qml").read_text(encoding="utf-8")
if 'onWatchedDataChanged: fleetGlobalData.requestRefresh()' not in main_qml:
    raise SystemExit("BROWSER_ARSAS_LIVE_GRID_FAIL uncoalesced cross-IED burst")

if "Timer {" in fleet_global or "readEngineeringSelected()" in fleet_global:
    raise SystemExit("BROWSER_ARSAS_SSOT_FAIL implicit MMS polling or read")

# Typed MMS evidence from the initial trusted-SCL snapshot must survive into
# both canonical trees. The large-model QA exercises valid/invalid wire cases.
snapshot_projection = (workspace_path.parent.parent / "src" / "MmsSclClientProjection.hpp").read_text(encoding="utf-8")
mms_client = (workspace_path.parent.parent / "src" / "MmsClientController.cpp").read_text(encoding="utf-8")
if ("isSclTypedEvidenceLeaf(item)" not in snapshot_projection or
        "item[item.size() - 2] == '$'" not in snapshot_projection or
        "kMaximumSclInitialTypedEvidence = 96U" not in snapshot_projection or
        "typedCount < kMaximumSclInitialTypedEvidence" not in snapshot_projection or
        "initial_qt_evidence_budget" not in large_model_qa or
        '"MmsEngineeringEvidence.hpp"' in snapshot_projection or
        "trusted_scl_initial_typed_evidence" not in large_model_qa or
        "failed_root_evidence" not in large_model_qa or
        "snapshotValues" not in mms_client or
        mms_client.count("value.key, value.display, value.engineeringEvidence);") < 2):
    raise SystemExit("BROWSER_INITIAL_SNAPSHOT_TYPED_EVIDENCE_FAIL missing typed provenance")

report_controller = (workspace_path.parent.parent / "src" / "MmsReportController.cpp").read_text(encoding="utf-8")
for token in [
    'kMaximumTypedReportEvidence = 96',
    'projectBrowserReportMembers(',
    'QStringLiteral("valueEvidence")',
    'QStringLiteral("valueEvidenceTruncated")',
]:
    if token not in report_controller:
        raise SystemExit("BROWSER_REPORT_TYPED_EVIDENCE_FAIL missing=" + token)

# Protect the shared typed decoder from truncated/doubled source,
# not just presence of UI tokens.
typed_decoder = (workspace_path.parent.parent / "src" / "MmsEngineeringEvidence.hpp").read_text(encoding="utf-8")
if typed_decoder.count("decodeEngineeringEvidence(") != 1 or (
        "const QString leaf = mmsItem.mid(leafSeparator + 1);" not in typed_decoder):
    raise SystemExit("BROWSER_REPORT_TYPED_EVIDENCE_FAIL invalid shared decoder source")

projection_header = (workspace_path.parent.parent / "src" / "BrowserReportMemberProjection.hpp").read_text(encoding="utf-8")
for token in [
    'kBrowserReportVisibleMemberLimit = 256',
    'kBrowserReportTextLimit = 1024',
    'std::min(members.size(),',
    'projection.rowsTruncated = members.size() > visibleCount',
    'if (!member.value || fullReference.empty()) continue',
    'decodeEngineeringEvidence(',
    'boundedReportMetadata(',
    'const std::string_view fullReference(*rawReference)',
    'if (referenceTruncated) {',
    'hasReportTypedLeaf(',
    'text.left(kBrowserReportTextLimit - marker.size()) + marker',
    '#include "BrowserBoundedMmsPreview.hpp"',
    'previewMmsValue(*member.value',
]:
    if token not in projection_header:
        raise SystemExit("BROWSER_REPORT_BOUNDED_PROJECTION_FAIL missing=" + token)
if 'for (const auto& item : frame.values)' in report_controller:
    raise SystemExit("BROWSER_REPORT_BOUNDED_PROJECTION_FAIL full-frame QML string conversion")
if 'decodeEngineeringEvidence(' in report_controller:
    raise SystemExit("BROWSER_REPORT_BOUNDED_PROJECTION_FAIL duplicate inline decoding")
if 'MmsDataCodec::to_display_string(*member.value)' in projection_header:
    raise SystemExit("BROWSER_VALUE_BOUNDED_FAIL unbounded intermediary in report GUI")
if ('QString::fromStdString(member.data_reference)' in projection_header or
        'QString::fromStdString(reason)' in projection_header):
    raise SystemExit("BROWSER_REPORT_METADATA_BOUNDED_FAIL eager untrusted metadata conversion")

value_preview = (workspace_path.parent.parent / "src" / "BrowserBoundedMmsPreview.hpp").read_text(encoding="utf-8")
for token in [
    'BoundedMmsPreview',
    'previewMmsValue(',
    'writer.appendHex(value.raw_value())',
    'writer.appendUtf8(std::get<std::string>(value.value()))',
    'writer.result.truncated = true',
    'value.children()',
]:
    if token not in value_preview:
        raise SystemExit("BROWSER_VALUE_BOUNDED_FAIL missing=" + token)

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
    'selectedReportMemberEvidence()',
    'root.selectedReportMemberIndex = dataRow.index',
    'onLatestReportIdentityChanged: selectedReportMemberIndex = -1',
    'BrowserEngineeringEvidence {',
    'root.latestReport.valuesTruncated === true',
    'root.latestReport.valueTextTruncated === true',
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

main_cpp = (workspace_path.parent.parent / "src" / "main.cpp").read_text(encoding="utf-8")
for required in [
    'qa-browser-select-mms',
    'selectMmsItem(domain, item)',
    'BROWSER_EVIDENCE_QA_FAIL',
]:
    if required not in main_cpp:
        raise SystemExit("BROWSER_EVIDENCE_QA_FAIL missing=" + required)
if 'applyReadValue(' in main_cpp:
    raise SystemExit("BROWSER_EVIDENCE_QA_FAIL synthetic value injection in app CLI")

print("BROWSER_CONVERGENCE_PASS ribbon=pass dense_explorer=pass goose_drilldown=pass data_model_drilldown=pass ied_root=pass contextual_commands=pass canonical_tree=pass bounded_history=80 polling=false")
