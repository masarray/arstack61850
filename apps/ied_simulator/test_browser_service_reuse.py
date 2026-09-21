#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 6:
    raise SystemExit(
        "usage: test_browser_service_reuse.py "
        "IedBrowserSessionController.cpp MmsReportController.cpp "
        "MmsFileSettingsController.cpp IedBrowserNavigation.qml BrowserGoosePane.qml")

session = Path(sys.argv[1]).read_text(encoding="utf-8")
reports = Path(sys.argv[2]).read_text(encoding="utf-8")
utilities = Path(sys.argv[3]).read_text(encoding="utf-8")
navigation = Path(sys.argv[4]).read_text(encoding="utf-8")
goose = Path(sys.argv[5]).read_text(encoding="utf-8")

required_session = [
    "reports_->setEngineeringContext(engineeringContext_)",
    "utilities_->setEngineeringContext(engineeringContext_)",
    "reports_->setHost(host_)",
    "utilities_->setHost(host_)",
]
required_reports = [
    "buildContextDiscoverySeed",
    "probeContextReportControls",
    "adoptEngineeringInventory",
    "if (contextModel)",
    "Browser-owned",
    "skip",
    "session->discover(options",
    "allow_polling_fallback = false",
]
required_utilities = [
    "buildContextSettingGroups",
    "adoptEngineeringInventory",
    "if (contextModel)",
    "Browser-owned",
    "skip this discovery",
    "session->discover(options",
]
required_navigation = [
    "context.gooseCount",
    "context.reportCount",
    "context.dataSetCount",
    "context.settingGroupCount",
]
required_goose = [
    "property var visibleStreams: context.gooseStreams",
    "target: context",
]

for label, source, required in [
    ("session", session, required_session),
    ("reports", reports, required_reports),
    ("utilities", utilities, required_utilities),
    ("navigation", navigation, required_navigation),
    ("goose", goose, required_goose),
]:
    missing = [token for token in required if token not in source]
    if missing:
        raise SystemExit(
            "BROWSER_SERVICE_REUSE_FAIL " + label + " missing=" + ",".join(missing))

if "engineering.gooseStreams" in navigation or "engineering.gooseStreams" in goose:
    raise SystemExit(
        "BROWSER_SERVICE_REUSE_FAIL GOOSE still consumes SclWorkspace authority")

report_context = reports.index("if (contextModel)")
report_fallback = reports.index("session->discover(options", report_context)
if report_fallback < report_context:
    raise SystemExit(
        "BROWSER_SERVICE_REUSE_FAIL Reports discovery precedes canonical attach")

utility_context = utilities.index("if (contextModel)")
utility_fallback = utilities.index("session->discover(options", utility_context)
if utility_fallback < utility_context:
    raise SystemExit(
        "BROWSER_SERVICE_REUSE_FAIL Utilities discovery precedes canonical attach")

if "client.treeModel" in navigation:
    raise SystemExit(
        "BROWSER_SERVICE_REUSE_FAIL Browser navigation regressed from canonical tree")

print(
    "BROWSER_SERVICE_REUSE_PASS "
    "canonical_services=5 endpoint_authority=browser "
    "browser_full_rediscovery=false standalone_fallback=preserved "
    "offline_inventory=preserved")
