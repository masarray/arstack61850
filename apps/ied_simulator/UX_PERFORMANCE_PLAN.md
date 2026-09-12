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

### Stage C - incremental live updates — first slice implemented

- Signal projection keeps a source-index -> visible-row routing table.
- Live refreshes are coalesced to ~16 ms and compare only source rows represented by the visible projection.
- `dataChanged()` is emitted only for contiguous rows whose value/quality/writable/changed roles actually changed.
- Selected-row invalidation targets only the previous/new source rows.
- Active value-based search deliberately falls back to the existing coalesced rebuild because a live value can change filtered membership.
- Still pending in Stage C: replace high-cardinality `QVariantList/QVariantMap` canonical backing storage with typed low/zero-copy storage and add a latest-value-per-point burst accumulator.

### Stage D - diagnostics and lifecycle hardening

- Convert activity presentation to a bounded C++ model with duplicate-event coalescing and counters.
- Add hard cap and recovery policy for unterminated child-process output lines.
- Remove user-visible blocking waits from the GUI thread; blocking shutdown remains only at final process teardown when unavoidable.
- Add negative tests for malformed/oversized SCL, child-process crash, rapid start/stop, repeated model reload, and large update bursts.

### Stage E - performance evidence — large-import gate first slice implemented

- `benchmark_large_scl.py` generates deterministic engineering models that expand to about 5k, 20k, and 50k data attributes using reusable SCL type templates.
- The benchmark drives the same `loadFileAsync()` path used by interactive Open SCL, records end-to-end wall time and Linux peak RSS, and fails on crash/deadlock/non-completion.
- CI now enforces deliberately generous wall-time/RSS ceilings so catastrophic O(N^2), runaway-memory, deadlock, and crash regressions become visible immediately without pretending CI is a laboratory micro-benchmark.
- Still pending in Stage E: GUI-thread stall heartbeat, delegate-instantiation count, search/filter latency at scale, repeated-open/close memory slope, update-burst coalescing ratio, and start/stop/reload soak evidence.

## Initial budgets

- No user interaction should block the GUI event loop for >50 ms under normal desktop load.
- Search typing should coalesce within ~80 ms and never allocate a second complete SCL tree.
- Large lists must instantiate approximately viewport-sized delegates, not one delegate per model row.
- Presentation/event queues are explicitly bounded.
- Repeated open/start/stop/close stress must show no monotonically growing owned worker/process/timer count.
- CI large-import guardrails: 5k <= 15 s / 512 MiB, 20k <= 30 s / 768 MiB, 50k <= 55 s / 1024 MiB. These are regression tripwires, not claimed product targets.

## Definition of done for the redesign

The redesign is complete only when the new workflow passes the existing simulator wire regressions, large-model UI tests, negative/failure tests, lifecycle stress, and measured performance checks. A visually improved screenshot alone is not completion evidence.
