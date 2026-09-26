# ARStack IEC 61850 Workbench
# IEDScout-Inspired GUI, Workflow, and Behaviour PRD

Status: Proposed
Owner: ARStack IEC 61850 Workbench
Source branch: `main-iedscout-workbench-prd`
Date: 2026-09-26

## 1. Executive summary

ARStack has a growing IEC 61850 protocol and engineering foundation, but its current Browser experience does not yet give an operator the coherent commissioning workflow expected from a mature IED tool. The product must become a dependable engineering workstation rather than a collection of disconnected feature panels.

This PRD adopts the functional mental model documented by IEDScout 4.20:

```text
File / Discover
    -> select one IED context
    -> browse a usable offline model
    -> connect or remain offline
    -> select an object
    -> read, write, control, subscribe, or author a service
    -> observe results in one Activity Monitor
    -> save evidence or move to the next service
```

The implementation must not copy OMICRON branding, icons, exact ribbon geometry, colors, illustrations, wording, or proprietary visual assets. ARStack should retain its own calm graphite and teal visual system while matching the useful workflow invariants: context continuity, progressive discovery, master-detail browsing, contextual commands, direct monitoring, visible operation feedback, and safe failure recovery.

## 2. Evidence and source basis

The requirements below were derived from the local installed documentation:

1. **Working with IEDScout 4.20 - Practical Example of Use**, 36 pages, OMICRON electronics, 2016.
   - Pages 6-7: screen model and Browser/Simulator pane responsibilities.
   - Pages 8-13: configuration, discovery, IED selection, navigation tree, offline/online browsing, descriptions, DataSet and quality expansion.
   - Pages 14-18: Details pane, Activity Monitor, subscription, drag-and-drop semantics, zoom and polling.
   - Pages 19-22: SCL file types, validation/status history, GOOSE, Reports, GI, and DataSets.
   - Pages 23-27: Sniffer filtering/export, Write, and guarded Control Select -> Operate workflow.
   - Pages 28-31: test/simulation indication, Save SCL, IED simulation, and GOOSE handoff.
   - Pages 33-35: File Transfer and Setting Groups.
2. **IEDScout What's New in Version 4.20**, 3 pages, OMICRON electronics, 2016.
   - Page 2: fast discovery with a usable partial model while background analysis continues, large-model SCL browsing, File Transfer, Setting Group insight, improved Activity Monitor path/zoom, intelligent IED naming, TimeQuality editing, simulator File Transfer, SGCB and substitution support, diagnostics, and legacy MMS compatibility.

The source PDFs remain installed at:

- `C:\Program Files\OMICRON\IEDScout 4\IEDScout_Application_Example.pdf`
- `C:\Program Files\OMICRON\IEDScout 4\Documentation\English\IEDScout_Whats_New.pdf`

The documents are treated as workflow evidence and product behaviour reference, not as permission to reproduce OMICRON's protected branding or assets.

## 3. Problem statement

The current ARStack Browser can expose many individual capabilities, but the operator can lose the active IED context, see an empty detail surface with no recovery action, encounter commands that are disabled without explanation, and fail to see operation history where the action occurs. The result is a screen that looks like an unfinished engineering shell even when the protocol/service implementation exists.

The key product failure is not the lack of one more feature. It is the absence of one stable task model that connects:

- source and IED identity;
- model navigation;
- offline versus online state;
- selection and operation eligibility;
- monitor/watch state;
- service-specific actions;
- progress, warnings, and evidence.

## 4. Goals

### 4.1 Primary goals

- Make Open SCL and Discover IED understandable, recoverable, and observable.
- Keep one explicit active IED context across File, Browser, Simulator, and service surfaces.
- Make the Browser a usable three-pane workstation: navigation, details/value table, and Activity Monitor.
- Preserve offline browsing while making Online a deliberate and visible association transition.
- Make Read, Write, Control, Reports, DataSets, GOOSE, Files, Setting Groups, Simulator, and Sniffer actions context-sensitive and explainable.
- Provide operation feedback and bounded history without blocking the UI thread.
- Make the first successful model load useful without requiring the operator to discover hidden controls.
- Support large SCL and live models with progressive rendering and explicit performance budgets.
- Establish screenshot-backed and end-to-end UX acceptance gates so source-token checks alone cannot declare the experience complete.

### 4.2 Secondary goals

- Retain the existing canonical IEC 61850 model and per-IED service ownership.
- Preserve protocol safety, report evidence rules, guarded control semantics, and reconnect lifecycle guarantees.
- Make the ARStack workbench feel premium, calm, and information-dense without tiny text or decorative noise.

## 5. Non-goals and boundaries

- Do not clone IEDScout pixel-for-pixel or reuse OMICRON trademarks, icons, illustrations, screenshots, or proprietary copy.
- Do not create a second semantic SCL/IED model to make the UI easier. All UI views must project the canonical model.
- Do not make direct MMS reads look like report evidence. Report success still requires real InformationReport evidence.
- Do not claim universal IEC 61850 conformance from a visual similarity or a synthetic simulator result.
- Do not implement dynamic DataSet deletion, remote file deletion/rename, or unsupported Setting Group semantics merely to fill a screen. Unsupported operations must be visible and fail closed.
- Do not put parsing, network setup, file I/O, report persistence, or large model materialization on the GUI thread.
- Do not use polling as a substitute for verified report delivery or event-driven runtime state.

## 6. Target users and core jobs

### Commissioning engineer

Needs to discover an IED, confirm its identity and endpoint, inspect the model offline, connect, read values, enable reports/GI, and observe changes without losing context.

### Protection/control engineer

Needs to inspect controllable objects, understand the control model, perform a guarded Select -> Operate or direct action, and see the result and termination evidence.

### Test engineer

Needs to create or load SCL, simulate an IED, modify supported values, inspect GOOSE/report behaviour, and capture reproducible evidence.

### Service/diagnostics engineer

Needs to inspect Files, COMTRADE/event logs, Setting Groups, Sniffer traffic, protocol errors, and exportable evidence without navigating through unrelated screens.

## 7. Product principles

1. **Context before command.** Every command must show which IED, source, endpoint, selection, and connection state it applies to.
2. **Browse before connect.** A validated SCL model must be useful offline. Connection is an explicit transition, not a hidden side effect.
3. **One selection, many actions.** The selected model node drives Details, eligibility, commands, and monitor operations.
4. **Activity is a first-class surface.** Read, write, control, report, GOOSE, discovery, and errors must produce visible, filterable activity.
5. **Progressive disclosure.** Large trees and complex service metadata appear progressively without hiding the path or freezing the interface.
6. **Evidence over optimism.** State labels must distinguish configured, discovered, attempted, acknowledged, verified, and unsupported.
7. **Fail closed, explain why.** Disabled actions need an explanation; rejected actions need a recovery path.
8. **Stable layout, adaptive density.** Resizable panes and persisted layout are allowed, but the default layout must be immediately useful at 1280 x 768 and above.
9. **Own the visual identity.** Use ARStack's graphite/teal palette, Segoe UI/Inter-like typography, restrained borders, and functional icons.

## 8. Information architecture

The main shell exposes four workspaces, with the active IED context retained when applicable:

| Workspace | Primary job | Required top-level groups |
| --- | --- | --- |
| File | Start, reopen, configure, recover | Open SCL, recent SCL, recent IEDs, configuration, status |
| Browser | Inspect and operate a real or SCL-backed IED | Application, IED, Data, Services, Show |
| Simulator | Run an SCL-backed IED safely | Application, IED model, Data, Services, Activity |
| Sniffer | Capture, filter, inspect, export | Capture, Filters, Messages, Export, Activity/details |

The Browser navigation model is IED-rooted:

```text
IED context
  - identity, source, endpoint, association state
  - GOOSE
  - Reports
  - Setting Groups
  - Files
  - DataSets
  - Data Model
      - Logical Device
          - Logical Node
              - Data Object
                  - Data Attribute
  - Global Data / Watch
```

The navigation tree and Details table are separate projections of the same canonical model. They must never disagree about identity, reference, functional constraint, or source provenance.

## 9. Global state model

The UI must expose one state machine per IED workspace. The state must be machine-readable and human-readable.

```text
Empty
  -> ImportingScl / Discovering
  -> ModelReadyOffline
  -> IedSelectionRequired (multi-IED source)
  -> Connecting
  -> Online
  -> BusyOperation
  -> Online / ModelReadyOffline / Error
```

Required rules:

- A tab label alone cannot imply that a model is active.
- `ModelReadyOffline` must show an explorable tree and source evidence.
- `IedSelectionRequired` must show the candidate list, selected value, and one primary action.
- `Connecting` must show progress, endpoint, cancellation, and the last stable model.
- A disconnect must preserve the model for offline browsing while clearing association-scoped evidence.
- Closing an IED must retire its services only after QML bindings and model consumers are detached.
- Reconnect must rebuild association-scoped report/change plans and must not display stale report evidence.
- Every operation must expose `Idle`, `Running`, `Succeeded`, `Failed`, `Cancelled`, or `Unsupported`.

## 10. Layout and visual requirements

### 10.1 Default Browser layout

The Browser default is a three-pane workspace:

1. **Navigation pane**: 250-340 px default, resizable, IED identity at the top, hierarchy below.
2. **Details pane**: flexible center, selected path/breadcrumb, metadata sections, and value table.
3. **Activity Monitor**: 32-40% default width when enabled, resizable, filterable, and independently scrollable.

The layout must remain useful when the Activity Monitor is hidden. Hiding it must not destroy watch state.

### 10.2 Command surface

Use a compact ARStack command strip with groups equivalent to the documentation, but with ARStack naming and iconography:

- Application: Open SCL, Save SCL, Discover IED, Close workspace.
- IED: Online/Offline, IED properties, association details.
- Data: Read, Read visible, Write, Control, Clear selection evidence.
- Services: Enable report/GI, Subscribe GOOSE, Add DataSet, Setting Groups, Files, Simulate.
- Show: Navigation, Details, Activity Monitor, Descriptions, Restore layout.

Command enablement must be derived from current selection and service state. Disabled commands must provide a tooltip or inline reason such as `Select a writable FC=SP/CF/DC/SE scalar` or `Online association required`.

### 10.3 Typography and density

- Body text: 13-15 px, line height approximately 1.45.
- Labels/captions: 11-12 px, readable contrast.
- Headings: 20-28 px, semibold at most.
- Table values: tabular figures where useful.
- Avoid 7-10 px primary labels. Small text may be used only for secondary technical metadata.
- Status must use text plus icon/shape; color alone is insufficient.
- Minimum interactive target: 28 px high for dense engineering controls and 32-36 px for primary actions.

### 10.4 Empty and loading states

Every empty state must answer three questions:

1. What is empty?
2. Why is it empty?
3. What is the next useful action?

Examples:

- `No IED model loaded` -> `Open SCL` / `Discover IED`.
- `IED selection required` -> show candidate list and `Use selected IED`.
- `Offline model` -> show `Browse offline` and `Go Online`.
- `No watched items` -> explain drag/drop and provide `Browse Data Model`.
- `No report evidence` -> distinguish `Report not enabled`, `No event received`, and `Not verified`.

## 11. Functional requirements

### FR-001 - File start surface

The File workspace shall provide Open SCL, Discover IED, recent SCL sources, recent discovered endpoints, Configuration, Simulator, and Sniffer entry points. Recent endpoints restore address context only; they must not auto-connect.

Acceptance criteria:

- A new operator can reach Open SCL or Discover IED in one visible action.
- A failed recent source shows why it failed and offers retry/remove, without deleting the underlying file.
- The File screen never exposes a stale "active IED" label without a valid context.

### FR-002 - SCL import and selection

The workbench shall support SCD, SSD, ICD, IID, CID, and project-supported SCL variants through the canonical typed import path. Multi-IED files shall show the candidate IED list and require explicit selection or explicit open-in-new-workspace.

Acceptance criteria:

- The model is usable as soon as the safe partial model is available, while remaining analysis is visible as background progress.
- Parser warnings/errors appear in Status History with source path and actionable detail.
- The selected IED name is authoritative for the opened file and is not silently replaced by a discovery guess.

### FR-003 - Discovery workflow

Discover IED shall accept an endpoint or a recent connection, show connection/discovery progress, permit cancellation, and publish a canonical model into the selected workspace.

Acceptance criteria:

- The operator sees endpoint, phase, elapsed time, and cancellation state.
- Discovery failure leaves the previous stable model intact or returns to an explicit Empty/Error state; it must not create a misleading partial active context.
- A usable tree can appear before all optional analysis finishes when the protocol/model contract permits it.

### FR-004 - IED context header

The Browser shall always show IED name, source authority (Live discovery or Open SCL), endpoint, online/offline state, model counts, and last operation state.

Acceptance criteria:

- The same identity appears in tab, navigation root, breadcrumb, and activity rows.
- A stale tab cannot claim Online after disconnect.
- Context information remains visible while opening service details or dialogs.

### FR-005 - Navigation tree

The tree shall support GOOSE, Reports, Setting Groups, Files, DataSets, Data Model, and Global Data. Data Model navigation shall preserve LD -> LN -> DO -> DA hierarchy, ordered DataSet membership, FC, and source reference.

Acceptance criteria:

- Expand/collapse does not rebuild the entire model or reset the selected path.
- Search filters the visible projection without creating a second semantic model.
- Selecting a node updates Details and contextual commands within the same event cycle.

### FR-006 - Details and value table

Details shall show breadcrumb, object metadata, descriptions when present, FC, MMS type, SCL type/evidence, value, quality, timestamp, and writable/control eligibility.

Acceptance criteria:

- A selected DO can show a concise overview and expandable DA rows.
- Quality and timestamp detail can be expanded without losing value context.
- Unknown, unsupported, and not-representable states are shown distinctly from false or empty values.

### FR-007 - Read and refresh

Read shall operate on the current selection and Read visible shall refresh a bounded visible range. Direct reads are labeled MMS evidence and do not become report evidence.

Acceptance criteria:

- Read progress and result appear in the Activity Monitor and selection details.
- A stale or disconnected request cannot repopulate a newer workspace.
- Refresh scope is bounded and does not walk the full model from QML.

### FR-008 - Write

Write shall be available only for exact supported scalar types and functional constraints. It shall show current value, proposed value, validation state, and verification read result.

Acceptance criteria:

- Unsupported FC/type/structure is rejected before transmission with a clear reason.
- A successful write is followed by a verification read on the same association.
- Write history identifies IED, reference, old value when available, new value, and result.

### FR-009 - Control

Control shall be a guarded dialog for supported SPC/DPC/IPC-style objects and their configured control model. The flow is `Select parameters -> validation/result -> Operate`.

Acceptance criteria:

- Control is disabled until the IED is online and a controllable object is selected.
- The dialog exposes relevant origin, test, interlock/synchrocheck, sequence, and command parameters supported by the canonical model.
- `Select` and `Operate` have separate results and cannot silently collapse into one write.
- Cancel/termination/failure state is preserved in the Activity Monitor.

### FR-010 - Activity Monitor

Activity Monitor shall collect selected Data Objects, Data Attributes, DataSets, GOOSE, Reports, discovery, read/write/control actions, and errors in one filterable surface.

Acceptance criteria:

- Dragging a Data Object or Data Attribute adds bounded polling/watch state.
- Dragging GOOSE subscribes it; dragging a Report enables it; dragging a DataSet visualizes and updates it permanently, subject to supported service policy.
- Activity rows show full path through Logical Device, operation, state, timestamp, IED, and evidence type.
- History is bounded and exportable; clear is non-blocking.
- Zoom/density and polling controls affect presentation only, not protocol correctness.

### FR-011 - Reports and GI

The Reports view shall expose URCB/BRCB inventory, DataSet reference, trigger options, optional fields, ConfRev, binding state, and enable/disable/GI actions.

Acceptance criteria:

- Enable flow requires explicit trigger/transmission selection and shows the preselected DataSet.
- GI is distinguishable from a spontaneous report event.
- A report is marked verified only after real InformationReport evidence is captured.
- Dynamic DataSet/report ownership and cleanup remain bounded and fail closed.

### FR-012 - DataSets

The DataSets view shall show configured and runtime-owned DataSets, ordered members, persistent/non-persistent policy where known, and a guarded authoring flow.

Acceptance criteria:

- Add DataSet supports selection from the canonical model through a clear staging list.
- Member order and exact references are preserved.
- Delete is available only for policy-permitted runtime-owned DataSets.
- Unresolved members are visible as unresolved; the UI must not invent a replacement reference.

### FR-013 - GOOSE

GOOSE shall show configured and observed streams, destination/address metadata, bound DataSet, test/simulation flags, state, and subscription lifecycle.

Acceptance criteria:

- Subscribe/unsubscribe state is visible on the stream and in Activity Monitor.
- Unknown GOOSE from Sniffer can be promoted to a monitored Browser item through an explicit action.
- APPID, MAC, VLAN, ConfRev, goID, and DataSet references are shown from canonical evidence.

### FR-014 - File Transfer

Files shall show capability state, directory/listing, transfer progress, supported open action, and explicit unsupported state. COMTRADE component files shall be grouped when the server exposes the required set.

Acceptance criteria:

- No-files and unsupported-service states are explicit, not blank.
- Downloads are bounded, cancellable where possible, and recorded in Activity Monitor.
- COMTRADE grouping does not alter source bytes or evidence provenance.

### FR-015 - Setting Groups

Setting Groups shall provide a service overview, active group, selectable group, affected logical devices, filter, active-versus-candidate value comparison, changed-only view, and guarded activate/write actions.

Acceptance criteria:

- The operator can preview the target group's values before activation.
- Changed-only filtering is derived from canonical values and clearly labeled.
- Activation and writing are separate operations with distinct confirmation and evidence.

### FR-016 - Simulator

Simulator shall reuse the canonical SCL model and provide explicit server settings, listening address, port, GOOSE enable/simulation, mode/behavior, and supported value editing.

Acceptance criteria:

- The SCL-defined model is read-only as structure; runtime values remain within supported standard semantics.
- Start/stop/restart is transactional and non-blocking.
- Simulator and monitored values can be viewed in the same Activity Monitor model without a second canonical store.

### FR-017 - Sniffer

Sniffer shall provide start/stop capture, IED filter, GOOSE/Reports/C-S/Protocol Error filters, text filtering, message selection, Details inspection, PCAP export, and CSV export.

Acceptance criteria:

- Filters are composable and their active state is visible.
- Text filtering is case-insensitive and bounded.
- Export identifies selected messages and preserves timestamps/ordering.
- A captured unknown GOOSE can be explicitly subscribed or promoted to Browser without an implicit cross-workspace state mutation.

### FR-018 - Layout and persistence

Navigation, Details, Activity Monitor, Descriptions, pane widths, zoom, and polling defaults shall be persisted per product profile and safely resettable.

Acceptance criteria:

- Restore default layout returns to a known usable three-pane state.
- A narrow or corrupted persisted layout cannot collapse the primary details surface to zero width.
- Layout persistence does not persist stale online/report ownership state.

## 12. Behaviour and interaction contracts

### 12.1 Selection contract

Every selectable item has a stable identity, display label, source reference, parent path, type, and capability set. The selection is retained across tab/pane visibility changes unless the item is removed or the active IED closes.

### 12.2 Contextual command contract

Each command exposes:

- `visible`: whether the action is relevant in the current workspace;
- `enabled`: whether the preconditions are satisfied;
- `reason`: human-readable explanation when disabled;
- `busy`: progress/phase while executing;
- `result`: success, failure, cancellation, unsupported, or not-verified.

### 12.3 Evidence contract

The UI must distinguish:

- configured source evidence;
- parsed/discovered model evidence;
- current live MMS read;
- report InformationReport evidence;
- GOOSE subscription/packet evidence;
- attempted but failed action;
- unsupported or unresolved state.

No green success badge may be shown for an action that only reached an intermediate state.

### 12.4 Error and recovery contract

Errors are shown at the operation location and in bounded Status History. Recovery actions may include Retry, Reconnect, Open source, Select another IED, Cancel, Export diagnostics, or Restore layout. Errors must not silently clear a previously valid offline model.

## 13. Non-functional requirements

### Performance

- The first useful tree must appear progressively for large models where safe.
- Interactive GUI adoption after worker preparation must remain within the existing <=50 ms budget.
- No ordinary action may synchronously parse a large SCL, flush an unbounded journal, or block on child-process shutdown.
- QML delegates must remain virtualized and reuse bounded models.
- Watch/activity history and live delta queues remain bounded with explicit coalescing/drop policy.

### Accessibility and readability

- Keyboard navigation must reach workspace tabs, tree, details, commands, dialogs, and Activity Monitor.
- Focus must be visible and survive state changes where safe.
- Status cannot rely on color alone.
- Normal text must meet a WCAG-like contrast target and must not be rendered as 7-10 px primary copy.

### Reliability and lifecycle

- Disconnect/reconnect invalidates stale association work.
- Closing one IED workspace cannot mutate another workspace's model, report state, or monitor rows.
- Clear, stop, restart, and application close must complete without worker/process leaks.
- Malformed SCL, malformed MMS, unsupported types, and missing services fail closed without crashing the GUI.

### Security and operational safety

- Raw Ethernet/GOOSE operations require explicit adapter and capability state.
- Control and writes must never be triggered by passive selection alone.
- Files and external applications are opened only after explicit operator action.
- Logs and exported evidence must not expose credentials or sensitive configuration unintentionally.

## 14. Reuse map in the current ARStack codebase

The PRD is a convergence plan, not a second architecture. The first implementation should deepen the existing boundaries:

- Browser shell: [IedBrowserWorkspace.qml](../apps/ied_simulator/qml/IedBrowserWorkspace.qml), [IedBrowserNavigation.qml](../apps/ied_simulator/qml/IedBrowserNavigation.qml), [Main.qml](../apps/ied_simulator/qml/Main.qml).
- Per-IED authority: [IedBrowserFleetController.hpp](../apps/ied_simulator/src/IedBrowserFleetController.hpp) and its controller implementation.
- Canonical context: [IedEngineeringContextController.hpp](../apps/ied_simulator/src/IedEngineeringContextController.hpp).
- Live model projection: [MmsLiveTreeModel.hpp](../apps/ied_simulator/src/MmsLiveTreeModel.hpp) and [IedNavigationModel.hpp](../apps/ied_simulator/src/IedNavigationModel.hpp).
- Value presentation: [IedSignalModel.hpp](../apps/ied_simulator/src/IedSignalModel.hpp) and [IedPointStore.hpp](../apps/ied_simulator/src/IedPointStore.hpp).
- Activity: [IedActivityModel.hpp](../apps/ied_simulator/src/IedActivityModel.hpp) and [ActivityMonitor.qml](../apps/ied_simulator/qml/ActivityMonitor.qml).
- Reports, control, files/settings, and SCL: the existing `MmsReportController`, `MmsControlController`, `MmsFileSettingsController`, and `SclWorkspaceController` boundaries.

Do not introduce an alternate QML-only tree, a second report state machine, or a separate Browser cache that can diverge from these canonical services.

## 15. Phased delivery plan

### P0 - Make the workstation coherent

- Add a visible build/version indicator for support and stale-build detection.
- Implement the explicit state model and eliminate tab/context/connection contradictions.
- Fix multi-IED selection empty states and make the primary recovery action obvious.
- Establish the default three-pane Browser layout with non-collapsing Details and Activity Monitor.
- Replace primary 7-10 px copy with theme tokens and readable typography.
- Make Open SCL and Discover IED end-to-end flows visible in the File workspace.
- Add screenshot-backed smoke flows for Empty, ModelReadyOffline, SelectionRequired, Connecting, Online, and Error.

### P1 - Make the core commissioning loop work

- Auto-focus the first useful LD/LN after successful model adoption.
- Complete Data Model tree -> Details table -> Read/Write/Control selection continuity.
- Add clear contextual command reasons and bounded operation feedback.
- Implement Activity Monitor watch semantics and drag/drop or an equivalent explicit Add to Monitor action.
- Add report enable/GI and real InformationReport evidence presentation.

### P2 - Service depth

- DataSet authoring and guarded ownership/deletion.
- GOOSE inventory/subscription/unknown-stream promotion.
- Setting Group comparison/edit/activate workflow.
- File Transfer capability/listing/download/COMTRADE grouping.

### P3 - Lab and diagnostics

- Simulator settings, runtime value editing, test/simulation indication, and shared Activity Monitor.
- Sniffer filters, details, PCAP/CSV export, and unknown GOOSE handoff.
- Restore-layout, description column, keyboard, accessibility, and evidence export hardening.

## 16. Validation and acceptance gates

The PRD is complete only when all of the following are proven on the current release candidate:

### Visual gates

- 1280 x 768 and 1360 x 860 screenshots show no clipped command labels, anonymous disabled blocks, zero-width details, or unreadable primary text.
- Each state in the P0 state model has a named screenshot and a visible recovery action.
- Browser screenshots show identity, navigation, details, and activity state in one coherent context.

### Workflow gates

- Open SCL -> select IED -> browse offline -> save source evidence.
- Discover endpoint -> progressive model -> cancel/fail/retry -> stable context.
- Online -> select DA -> Read -> visible MMS evidence.
- Writable scalar -> Write -> verification Read -> activity result.
- Controllable object -> Select -> validation -> Operate -> termination result.
- Report -> select DataSet/triggers -> Enable/GI -> real InformationReport evidence.
- GOOSE -> subscribe -> monitored updates -> unsubscribe.
- DataSet -> stage ordered members -> create when supported -> guarded delete only when owned.
- Setting Group -> compare -> filter changed -> activate/write with separate evidence.
- Files -> unsupported/no-files/download/COMTRADE grouping paths.
- Simulator -> configure -> start -> modify supported runtime value -> stop/restore.
- Sniffer -> capture -> filter -> inspect -> export.

### Engineering gates

- Existing C++/protocol, SCL, report, control, lifecycle, and performance tests remain green.
- Add dynamic UI tests or an equivalent automation harness; source-string checks are supplementary only.
- Add negative tests for stale context, disconnected operation, unsupported types, unresolved DataSet members, cancelled discovery, malformed SCL, and close/reconnect races.
- Prove bounded Activity history, live queues, worker ownership, and shutdown.
- Validate at least one real or authorized lab IED path; simulator-only evidence must be labeled as such.

## 17. Success metrics

- A new operator can reach a usable model from File in <=3 deliberate actions after choosing Open SCL or Discover IED.
- No screenshot state shows conflicting IED identity, authority, or connection state.
- From a selected Data Attribute, Read/Write eligibility and reason are understandable without opening a separate help page.
- A watched object can be added and its latest value/status found in Activity Monitor without changing workspace.
- P0 screenshot review has zero critical findings for clipping, empty-state ambiguity, or unreadable text.
- All operation results are visible within the same workspace and remain exportable from bounded history.
- No end-to-end claim is marked complete solely from source-token tests.

## 18. Risks and decisions

### Risk: visual imitation becomes the goal

Decision: measure workflow outcomes and state clarity, not screenshot pixel distance. Use ARStack assets, palette, and terminology.

### Risk: duplicated model state for convenience

Decision: keep the canonical model/service boundaries and add projections/adapters only where a current view needs them.

### Risk: large models make the UI look empty while work continues

Decision: publish safe progressive model readiness and visible phase/progress; do not block the GUI or fabricate incomplete semantics.

### Risk: green checks conceal unusable rendered UX

Decision: make screenshots, dynamic UI interaction, and evidence-backed workflow gates part of release validation.

### Risk: direct reads are mistaken for report success

Decision: label evidence source in every details/activity row and require InformationReport evidence for report claims.

## 19. Immediate implementation slice

The first code slice after PRD approval should touch only the Browser shell and its state presentation:

1. Add `ProductBuildInfo`/visible version surface.
2. Normalize the per-IED state banner and tab label from the same authoritative context.
3. Repair multi-IED selection and empty states.
4. Establish layout tokens and readable typography in Browser QML.
5. Make Activity Monitor visible and useful by default for a loaded context.
6. Add screenshot-driven P0 acceptance coverage before adding new service features.

No protocol behaviour change is required for this slice. The existing canonical services remain the source of truth.
