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
    'Layout.preferredHeight: 112',
    'width: 332',
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
    'objectName: "browserDataSetOpenInDataModel"',
    'objectName: "browserDataSetWatchSelectedDa"',
    'signal dataModelRequested()',
    'signal watchAttributeRequested(var node)',
    'onClicked: root.dataModelRequested()',
    'onClicked: root.watchAttributeRequested(root.selectedAttributeNode)',
    'function onContextChanged() {',
    'root.selectedAttributeReference = ""',
    'root.expandedMemberChildren = []',
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
if ('onDataModelRequested: root.selectSection(0, "Data Model")' not in workspace or
        'globalDataPane.addSelectedData(node)' not in workspace):
    raise SystemExit("BROWSER_DATASET_HANDOFF_FAIL use canonical Data Model and per-IED watchlist")
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
    'latestReport.rcbReference || ""',
    'BrowserEngineeringEvidence {',
    'root.latestReport.valuesTruncated === true',
    'root.latestReport.valueTextTruncated === true',
    'String(frame.rcbReference) !== expectedRcb',
    'String(frame.reportId) !== expectedRptId',
    'Offline values are not fabricated.',
    'heading: "Trigger Options (TrgOps)"',
    'selectedIsSubscribed: hasSelection && reports.active',
    'reports.subscriptionRcbReference === rcb.reference',
    'root.selectedIsSubscribed ? "SUBSCRIBED" : "OTHER RCB ACTIVE"',
    'The selected RCB is inspection-only; Stop active RCB acts on',

    'heading: "Optional Fields (OptFlds)"',
    'root.rcb.triggerOptionsReadStatus === "verified"',
    'root.rcb.optionalFieldsReadStatus === "verified"',
    'root.rcb.triggerOptionNames.indexOf(modelData.name)',
    'root.rcb.optionalFieldNames.indexOf(modelData.name)',
    'enabled: false // inspected bit, never an MMS Write',
    'no on/off bit state is inferred.',
    'RCB in use: enabled, reservation or Owner evidence is present.',

]
missing_report = [term for term in report_required if term not in report_inspector]
if missing_report:
    raise SystemExit("BROWSER_REPORT_INSPECTOR_FAIL missing=" + ",".join(missing_report))
if 'String(frame.reportId) === wanted' in report_inspector:
    raise SystemExit("BROWSER_REPORT_INSPECTOR_FAIL report ID alone is not exact RCB provenance")
if 'title: modelData[0]' in report_inspector or 'rawValue: modelData[1]' in report_inspector:
    raise SystemExit("BROWSER_REPORT_INSPECTOR_FAIL nested-array attribute labels")
if "ReportsWorkspace {" in workspace:
    raise SystemExit("BROWSER_REPORT_INSPECTOR_FAIL legacy three-panel report view")
if 'implicitHeight: 25' not in navigation or "readonly property bool newGroup:" not in navigation:
    raise SystemExit("BROWSER_REPORT_INSPECTOR_FAIL navigation is not grouped and compact")

# A 100 ms native receive/drain tick must not force QML to rebuild identical
# report inspectors or compare large QVariant histories on the GUI thread.
# The serial I/O worker owns the last published presentation (not a protocol
# model) and observes event-only and retained-frame changes as well as counts.
poll_start = report_controller.find("void MmsReportController::schedulePoll()")
poll_end = report_controller.find("} catch (const mms::MmsTransportCancelledError&)", poll_start)
if poll_start < 0 or poll_end < 0:
    raise SystemExit("BROWSER_REPORT_COALESCE_FAIL native poll lifecycle missing")
report_poll = report_controller[poll_start:poll_end]
for required in (
    "worker->lastPublishedReportUi",
    "const bool presentationChanged =",
    "previous->active != ui.active",
    "previous->cleanupRequired != ui.cleanupRequired",
    "previous->receivedCount != ui.receivedCount",
    "previous->subscriptionRcbReference != ui.subscriptionRcbReference",
    "previous->rejectedCount != ui.rejectedCount",
    "previous->decodeFailureCount != ui.decodeFailureCount",
    "previous->events != ui.events",
    "previous->reports != ui.reports",
    "if (!presentationChanged) {",
    "worker->lastPublishedReportUi = ui;",
):
    if required not in report_poll:
        raise SystemExit("BROWSER_REPORT_COALESCE_FAIL missing=" + required)
changed_dispatch = "QMetaObject::invokeMethod(self, [self, generation, ui] {"
if not (report_poll.index("const bool presentationChanged =")
        < report_poll.index("if (!presentationChanged) {")
        < report_poll.index("worker->lastPublishedReportUi = ui;")
        < report_poll.index(changed_dispatch)):
    raise SystemExit("BROWSER_REPORT_COALESCE_FAIL deep comparison leaked onto GUI thread")
unchanged_branch = report_poll.split("if (!presentationChanged) {", 1)[1].split(
    "worker->lastPublishedReportUi = ui;", 1)[0]
if "self->pollPending_ = false;" not in unchanged_branch or "return;" not in unchanged_branch:
    raise SystemExit("BROWSER_REPORT_COALESCE_FAIL idle tick did not retire pending poll")
changed_branch = report_poll.split(changed_dispatch, 1)[1]
if "self->pollPending_ = false;" not in changed_branch or changed_branch.count(
        "emit self->reportsChanged();") != 1:
    raise SystemExit("BROWSER_REPORT_COALESCE_FAIL changed snapshot did not notify GUI exactly once")
if "self->receivedReports_ != ui.reports" in report_poll:
    raise SystemExit("BROWSER_REPORT_COALESCE_FAIL O(n) GUI snapshot comparison restored")
if "worker->lastPublishedReportUi.reset();" not in report_controller:
    raise SystemExit("BROWSER_REPORT_COALESCE_FAIL old association presentation cache retained")
disconnect_section = report_controller.split(
    "void MmsReportController::disconnectFromIed()", 1)[-1].split(
    "bool MmsReportController::reconnect()", 1)[0]
if "worker->lastPublishedReportUi.reset();" not in disconnect_section:
    raise SystemExit("BROWSER_REPORT_COALESCE_FAIL disconnect retained report history")

# Keep idle MMS receive polling cheap without hiding foreign/malformed reports.
# The fast path must run after the bounded queue drain, before building or
# copying the expensive stream history; never skip the first active snapshot.
if "void MmsReportController::schedulePoll()" not in report_controller:
    raise SystemExit("BROWSER_REPORT_IDLE_FAIL schedulePoll missing")
poll_start = report_controller.index("void MmsReportController::schedulePoll()")
poll_end = report_controller.index(
    "} catch (const mms::MmsTransportCancelledError&)", poll_start)
report_poll = report_controller[poll_start:poll_end]
for token in (
    "worker->session->association().try_poll_once_for(",
    "const auto drained = staticActive",
    "worker->report->drain_queued_reports()",
    "worker->authoredReport->drain_queued_reports()",
    "if (drained == 0U && worker->lastPublishedReportUi) {",
    "self->pollPending_ = false;",
    "buildReportUi(worker->report->snapshot())",
    "buildReportUi(worker->authoredReport->snapshot())",
    "previous->events != ui.events",
    "previous->reports != ui.reports",
):
    if token not in report_poll:
        raise SystemExit("BROWSER_REPORT_IDLE_FAIL missing=" + token)
if not (report_poll.index("try_poll_once_for(")
        < report_poll.index("const auto drained = staticActive")
        < report_poll.index("if (drained == 0U && worker->lastPublishedReportUi)")
        < report_poll.index("buildReportUi(worker->report->snapshot())")):
    raise SystemExit("BROWSER_REPORT_IDLE_FAIL snapshot built on unchanged receive tick")
unchanged = report_poll.split(
    "if (drained == 0U && worker->lastPublishedReportUi) {", 1)[1].split(
    "const ReportUi ui = staticActive", 1)[0]
if ("self->pollPending_ = false;" not in unchanged or
        "return;" not in unchanged or "emit self->reportsChanged();" in unchanged):
    raise SystemExit("BROWSER_REPORT_IDLE_FAIL idle tick leaked QML update or pending flag")
for method, next_method in (
    ("enableSelectedAuthored(", "bool MmsReportController::enableSelected("),
    ("bool MmsReportController::enableSelected(", "bool MmsReportController::disableSelected("),
    ("bool MmsReportController::disableSelected(", "bool MmsReportController::retryCleanup("),
    ("bool MmsReportController::retryCleanup(", "void MmsReportController::schedulePoll()"),
):
    begin = report_controller.find(method)
    end = report_controller.find(next_method, begin + len(method))
    if begin < 0 or end < 0 or "worker->lastPublishedReportUi.reset();" not in report_controller[begin:end]:
        raise SystemExit("BROWSER_REPORT_IDLE_FAIL stale RCB lifecycle cache: " + method)
if report_poll.count("emit self->reportsChanged();") != 1:
    raise SystemExit("BROWSER_REPORT_IDLE_FAIL dropped changed-state GUI notification")
poll_failure = report_controller[poll_end:]
if "Report association/poll failed" not in poll_failure or "emit self->reportsChanged();" not in poll_failure:
    raise SystemExit("BROWSER_REPORT_IDLE_FAIL dropped failure diagnostics")

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
