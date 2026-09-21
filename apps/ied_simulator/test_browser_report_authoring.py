#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 7:
    raise SystemExit(
        "usage: test_browser_report_authoring.py "
        "MmsReportController.cpp IedBrowserWorkspace.qml "
        "BrowserDataSetAuthoringDialog.qml BrowserReportAuthoringDialog.qml "
        "BrowserDataSetPane.qml MmsReportController.hpp"
    )

controller = Path(sys.argv[1]).read_text(encoding="utf-8")
workspace = Path(sys.argv[2]).read_text(encoding="utf-8")
dataset_dialog = Path(sys.argv[3]).read_text(encoding="utf-8")
report_dialog = Path(sys.argv[4]).read_text(encoding="utf-8")
dataset_pane = Path(sys.argv[5]).read_text(encoding="utf-8")
header = Path(sys.argv[6]).read_text(encoding="utf-8")

def require(text: str, token: str, label: str) -> None:
    if token not in text:
        raise SystemExit(f"{label}: missing required token: {token}")

for token in (
    "MmsDynamicDataSetRuntime",
    "dynamicOptions.maximum_members = 64U",
    "dynamicOptions.verify_after_create = true",
    "dynamicDataSets->create(",
    "MmsDynamicDataSetDeletePolicy::owned_only",
    "engineeringContext_->treeModel()->nodeForReference(reference)",
    "canonicalMembers.size() > 64",
    "MmsReportSubscriptionRuntime",
    "options.write_data_set_reference = true",
    "options.write_trigger_options = writeTriggerOptions",
    "options.write_optional_fields = writeOptionalFields",
    "options.subscription.write_data_set_reference = false",
    "options.selection.allow_polling_fallback = false",
    "Enable + GI requires general-interrogation in authored TrgOps",
    "Static/non-owned DataSet binding is immutable",
):
    require(controller, token, "Report controller")

for forbidden in (
    "MmsNamedVariableListCodec::encode_define",
    "MmsNamedVariableListCodec::encode_delete",
):
    if forbidden in controller:
        raise SystemExit(
            f"Report controller: GUI-local dynamic DataSet wire encoding forbidden: {forbidden}"
        )

for token in (
    'text: "Add to DataSet draft…"',
    'text: "New dynamic…"',
    'text: "Delete owned…"',
    'text: "Author…"',
    "BrowserDataSetAuthoringDialog {",
    "BrowserReportAuthoringDialog {",
):
    require(workspace, token, "Browser workspace")

for token in (
    "readonly property int maximumMembers: 64",
    "function addSelected(node)",
    "memberModel.move(",
    "reports.createDynamicDataSet(",
    "reports.deleteDynamicDataSet(",
    "id: createConfirm",
    "id: deleteDialog",
    "association-owned",
):
    require(dataset_dialog, token, "DataSet authoring dialog")

for token in (
    "function triggerNames()",
    "function optionalNames()",
    "reports.enableSelectedAuthored(",
    "id: confirmEnable",
    "general-interrogation",
    "sequence-number",
    "reason-for-inclusion",
    "configuration-revision",
    "Static binding never rewrites DatSet",
    "No automatic retry",
):
    require(report_dialog, token, "Report authoring dialog")

for token in (
    "DYNAMIC OWNED",
    "STATIC / READ-ONLY",
    "Membership is immutable",
):
    require(dataset_pane, token, "DataSet pane")

for token in (
    "ownedDynamicDataSets",
    "enableSelectedAuthored",
    "createDynamicDataSet",
    "deleteDynamicDataSet",
):
    require(header, token, "Report controller header")

if "Timer {" in dataset_dialog or "Timer {" in report_dialog:
    raise SystemExit("P6C dialogs: background polling/timers are forbidden")

print(
    "P6C_BROWSER_REPORT_AUTHORING_PASS "
    "static_immutable=true dynamic_owned_only=true member_bound=64 "
    "create_verify=true canonical_members=true runtime_reuse=true "
    "static_poll_fallback=false explicit_enable=true qml_polling=false"
)
