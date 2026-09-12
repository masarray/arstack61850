# IED Simulator UX + performance plan

This plan applies the repository `AGENTS.md` production contract to the desktop IED Simulator redesign. The UX target is a familiar IEC 61850 commissioning workflow (Open SCL -> select IED -> Start -> browse -> edit values) without copying another vendor's branding or pixel geometry.

## Performance invariants

- Protocol/core behavior stays independent from QML and view state.
- Large SCL data must be exposed through typed C++ models, not copied/re-grouped in QML JavaScript on every change.
- Views must virtualize rows and reuse delegates; no hidden full-tree delegate creation.
- UI refreshes are coalesced. Stale intermediate presentation updates may be skipped; protocol state may not be skipped or reordered.
- No unbounded GUI/event/log queue. Diagnostic presentation remains bounded.
- Expensive parse/index/filter work must not become a steady-state GUI-thread loop.
- One data-value change should become a targeted model notification, not a full workspace rebuild.
- Every worker/process/timer/model has explicit ownership and a deterministic shutdown path.
- Failures in SCL input, server output, or a child process must not crash the GUI.

## UX architecture

### Stage A - model/view foundation — implemented

- `IedNavigationModel`: compact C++ navigation index for LD/LN selection.
- `IedSignalModel`: C++ signal table projection with static roles and coalesced filtering/refresh.
- `ListView.reuseItems=true`, fixed-height simple delegates, zero speculative cache.
- Replace the four-panel workspace with a two-pane navigation/details layout.
- Start is a deliberate server-settings dialog; editing is a deliberate Set Values dialog.

### Stage B - asynchronous import/indexing — parser path implemented, indexing follow-up pending

- Interactive Open SCL now parses on one bounded `QThreadPool` worker instead of the GUI thread.
- Only one parse runs at a time; repeated Open SCL requests are coalesced to one newest pending request instead of creating an unbounded worker queue.
- A monotonically increasing generation token plus source-path ownership prevents an older import, a cleared workspace, or an older request from replacing newer state.
- Parsed `SclDocument` ownership is transferred back to the GUI thread through queued delivery before presentation models are rebuilt.
- The synchronous `loadFile()` path remains for deterministic CLI/CI automation, while the desktop FileDialog uses `loadFileAsync()`.
- Import while a simulator endpoint is active is rejected instead of blocking the GUI on process shutdown.
- Still pending in Stage B: move profile construction / large value-index construction off the GUI thread and add explicit progress/cancel UI for very large files.

### Stage C - incremental live updates

- Replace remaining `QVariantList` backing stores for high-cardinality values with typed storage + `QAbstractItemModel` access.
- Emit `dataChanged()` only for affected source rows.
- Coalesce presentation updates to one UI-frame window while preserving the latest value per point.
- Keep report/control/MMS runtime state authoritative outside the GUI model.

### Stage D - diagnostics and lifecycle hardening

- Convert activity presentation to a bounded C++ model with duplicate-event coalescing and counters.
- Add hard cap and recovery policy for unterminated child-process output lines.
- Remove user-visible blocking waits from the GUI thread; blocking shutdown remains only at final process teardown when unavoidable.
- Add negative tests for malformed/oversized SCL, child-process crash, rapid start/stop, repeated model reload, and large update bursts.

### Stage E - performance evidence

Add deterministic synthetic fixtures and record:

- import/index wall time and GUI-thread stall time;
- model row count vs. instantiated delegates;
- filter latency for 5k/20k/50k points;
- steady-state memory and repeated-open/close memory slope;
- update burst latency and coalescing ratio;
- start/stop/reload lifecycle stress;
- CI smoke + existing MMS/report/control interoperability regressions.

## Initial budgets (targets, not current claims)

- No user interaction should block the GUI event loop for >50 ms under normal desktop load.
- Search typing should coalesce within ~80 ms and never allocate a second complete SCL tree.
- Large lists must instantiate approximately viewport-sized delegates, not one delegate per model row.
- Presentation/event queues are explicitly bounded.
- Repeated open/start/stop/close stress must show no monotonically growing owned worker/process/timer count.

## Definition of done for the redesign

The redesign is complete only when the new workflow passes the existing simulator wire regressions, large-model UI tests, negative/failure tests, lifecycle stress, and measured performance checks. A visually improved screenshot alone is not completion evidence.
