#!/usr/bin/env python3
"""P6E permanent source ownership / routing regression contract."""
from pathlib import Path
import sys

if len(sys.argv) != 10:
    raise SystemExit(
        "usage: test_multi_ied_workspace.py Fleet.cpp Fleet.hpp Main.qml "
        "IedBrowserWorkspace.qml FileWorkspace.qml FleetGlobalDataPane.qml "
        "BrowserGlobalDataPane.qml SclWorkspaceController_part_01.inc "
        "MmsReportController.cpp"
    )
(fleet_cpp, fleet_hpp, main, browser, home, all_data, global_data,
 scl_source, reports) = (Path(p).read_text(encoding="utf-8") for p in sys.argv[1:])

def require(haystack, words, label):
    for word in words:
        if word not in haystack:
            raise SystemExit(f"P6E_FAIL {label}: missing {word}")

require(fleet_cpp, (
    "maximumWorkspaces",
    "std::make_unique<IedEngineeringContextController>()",
    "std::make_unique<MmsClientController>()",
    "std::make_unique<MmsReportController>()",
    "std::make_unique<MmsControlController>()",
    "std::make_unique<MmsFileSettingsController>()",
    "std::make_unique<SclWorkspaceController>()",
    "std::make_unique<IedBrowserSessionController>()",
    "session->setClient(client.get())",
    "session->setEngineeringContext(context.get())",
    "session->setReports(reports.get())",
    "session->setControls(controls.get())",
    "session->setUtilities(utilities.get())",
    "bool IedBrowserFleetController::switchTo(",
    "bool IedBrowserFleetController::prepareForNewSource(",
    "bool IedBrowserFleetController::openSclIedInNewWorkspace(",
    "bool IedBrowserFleetController::closeWorkspace(",
    "entry->reports->cleanupRequired()",
    "activeEngineering()->adoptSourceFrom(*original->engineering)",
    "auto retired = std::move(entries_[static_cast<std::size_t>(index)])",
    "retired.reset();",
), "fleet authority")
require(fleet_hpp, (
    "QAbstractListModel",
    "maximumWorkspaces = 8",
    "Q_PROPERTY(IedEngineeringContextController* activeContext",
    "Q_PROPERTY(IedBrowserSessionController* activeSession",
    "Q_PROPERTY(MmsClientController* activeClient",
    "Q_PROPERTY(MmsReportController* activeReports",
    "Q_PROPERTY(MmsControlController* activeControls",
    "Q_PROPERTY(MmsFileSettingsController* activeUtilities",
    "Q_PROPERTY(SclWorkspaceController* activeEngineering",
), "fleet surface")
require(main, (
    "IedBrowserFleetController {",
    "property var iedContext: fleet.activeContext",
    "property var browserSession: fleet.activeSession",
    "property var sclWorkspace: fleet.activeEngineering",
    "model: fleet",
    "visible: index === root.browserFleet.activeIndex",
    "anchors.fill: parent",
    'objectName: "iedBrowserView_" + index',
    "fleet.contextAt(index)",
    "fleet.sessionAt(index)",
    "fleet.clientAt(index)",
    "fleet.reportsAt(index)",
    "fleet.controlsAt(index)",
    "fleet.utilitiesAt(index)",
    "fleet.engineeringAt(index)",
    "onOpenSourceRequested: function(fileUrl)",
    "fleet.prepareForNewSource()",
    "FleetGlobalDataPane {",
    "onWatchedDataChanged: fleetGlobalData.refreshRows()",
), "shell routing")
require(browser, (
    "required property var fleet",
    "fleet.prepareForNewSource()",
    "fleet.openSclIedInNewWorkspace(activeIedPicker.currentText)",
    "function watchedRows()",
    "function refreshWatchedGlobalData()",
), "Browser source routing")
require(home, (
    "signal openSourceRequested(url fileUrl)",
    "root.openSourceRequested(selectedFile)",
    "root.openSourceRequested(hardening.recentResourceUrl(index))",
), "File Home source routing")
require(all_data, (
    "browserViews.itemAt(index)",
    "fleet.contextAt(index)",
    "view.watchedRows()",
    "view.refreshWatchedGlobalData()",
    "maximumRows: 2048",
), "all-IED Global Data")
require(global_data, (
    "signal watchSnapshotChanged()",
    "function snapshotRows()",
    "root.watchSnapshotChanged()",
), "per-IED Global Data")
require(scl_source, (
    "SclWorkspaceController::adoptSourceFrom(",
    "sourceBytes_ = source.sourceBytes_",
    "engineeringContext_->sourcePath() != source.sourcePath_",
), "SCL provenance")
require(reports, (
    "options.selection.allow_polling_fallback = false",
    "options.subscription.write_data_set_reference = false",
    "MmsDynamicDataSetDeletePolicy::owned_only",
), "P6C static reporting safety")
for typename in (
    "IedEngineeringContextController",
    "IedBrowserSessionController",
    "MmsClientController",
    "MmsReportController",
    "MmsControlController",
    "MmsFileSettingsController",
    "SclWorkspaceController",
):
    if f"{typename} {{" in main:
        raise SystemExit(f"P6E_FAIL singleton {typename} remained in Main.qml")
# QML required property 'fleet' in IedBrowserWorkspace shadows Main's
# id 'fleet': `fleet: fleet` resolves to itself and leaves every Browser
# service/context undefined, rendering the exact blank signal screenshot.
require(main, (
    "property var browserFleet: fleet",
    "fleet: root.browserFleet",
    "session: root.browserFleet.sessionAt(index)",
    "client: root.browserFleet.clientAt(index)",
    "context: root.browserFleet.contextAt(index)",
    "reports: root.browserFleet.reportsAt(index)",
    "utilities: root.browserFleet.utilitiesAt(index)",
    "controls: root.browserFleet.controlsAt(index)",
    "engineering: root.browserFleet.engineeringAt(index)",
), "non-shadowed Browser context/service routing")
if "fleet: fleet" in main:
    raise SystemExit("P6E_FAIL QML fleet self-shadowing would empty the Browser")

# A StackLayout containing a Repeater has an extra layout child and can
# render slot N-1 while the active tab points at slot N.
if "currentIndex: fleet.activeIndex" in main or \
        "currentIndex: fleet.activeIndex + 1" in main:
    raise SystemExit("P6E_FAIL Repeater delegates must not use StackLayout indexing")
require(main, (
    'id: perIedBrowserHost',
    'id: iedBrowserRepeater',
    'visible: index === root.browserFleet.activeIndex',
    'objectName: "iedBrowserView_" + index',
), "exact active Browser panel routing")

if "Timer {" in all_data:
    raise SystemExit("P6E_FAIL aggregate Global Data timer/polling introduced")
if "setEngineeringContext(fleet.activeContext)" in main:
    raise SystemExit("P6E_FAIL one live service was rebound across IEDs")
print(
    "P6E_MULTI_IED_PASS isolated_contexts=true isolated_services=true "
    "persistent_tabs=true bounded_slots=8 cross_ied_monitor=event_driven "
    "source_provenance=true static_report_only=preserved"
)
