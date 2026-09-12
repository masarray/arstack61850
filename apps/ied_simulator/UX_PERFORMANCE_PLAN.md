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

### Stage B - asynchronous import/indexing — implemented for interactive Open SCL

- Interactive Open SCL parses on one bounded `QThreadPool` worker instead of the GUI thread.
- Only one import runs at a time; repeated Open SCL requests are coalesced to one newest pending request instead of creating an unbounded worker queue.
- A monotonically increasing generation token plus source-path ownership prevents an older import, a cleared workspace, or an older request from replacing newer state.
- The same bounded worker now builds the selected IED simulator profile, source-ordered point projection, structural counts, LD/LN navigation index, and per-LN source-index lookup before handing ownership to the GUI thread.
- GUI completion adopts the prepared containers and creates the small per-IED runtime objects; it no longer calls `rebuildPresentation()` / `rebuildValues()` on the interactive import path.
- The synchronous `loadFile()` path remains for deterministic CLI/CI automation and compatibility; the desktop FileDialog uses `loadFileAsync()`.
- Import while a simulator endpoint is active is rejected instead of blocking the GUI on process shutdown.
- Remaining follow-up: cache/prebuild profiles for non-selected IEDs and remove legacy synchronous/start-path profile rebuilding where compatibility permits.

### Stage C - incremental live updates — hot presentation path implemented

- Signal projection keeps a source-index -> visible-row routing table.
- Live refreshes are coalesced to ~16 ms and compare only source rows represented by the active projection.
- `dataChanged()` is emitted only for contiguous rows whose value/quality/writable/changed roles actually changed.
- Selected-row invalidation targets only the previous/new source rows.
- Active value-based search deliberately falls back to the existing coalesced rebuild because a live value can change filtered membership.
- `IedSignalModel` retains only compact typed rows for the active LD/LN; it does not retain a second complete point snapshot.
- On the async import path, signal rebuild uses the worker-built per-LN source-index vector, making scope changes proportional to the selected LN instead of the entire 20k/50k-point catalog.
- The 16 ms refresh timer is the latest-state presentation accumulator: multiple `valuesChanged` bursts collapse into one read of authoritative typed point state while protocol/runtime state remains ordered and lossless.
- Remaining follow-up: add explicit live-update burst latency/coalescing-ratio evidence at scale.

### Stage D - diagnostics and lifecycle hardening — first production slice implemented

- Activity presentation is a bounded/coalesced C++ model instead of QML-side list filtering.
- Child stdout/stderr framing has bounded buffers, bounded line length, and bounded per-turn drain work.
- Generation-safe delayed-kill semantics prevent an old stop timer from killing a restarted endpoint.
- Negative/lifecycle CI covers malformed SCL, child-output flooding, and repeated start/stop/restart.
- Remaining follow-up: remove user-visible blocking waits from destructive/synchronous GUI lifecycle paths and broaden bind/fleet-partial-start failure tests.

### Stage E - performance evidence — large import and GUI-adoption gates implemented

- `benchmark_large_scl.py` generates deterministic engineering models that expand to about 5k, 20k, and 50k data attributes using reusable SCL type templates.
- The benchmark drives the same `loadFileAsync()` path used by interactive Open SCL, records end-to-end wall time and Linux peak RSS, and fails on crash/deadlock/non-completion.
- Import tracing separates parser time, worker-side profile/index preparation time, and GUI adoption time.
- CI enforces a **<=50 ms GUI-adoption budget** for 5k/20k/50k imports and every iteration of the repeated 20k reload soak. This proves the direct worker-result adoption boundary is bounded; it is not yet a full event-loop heartbeat measurement.
- The one-process 20k reload soak continues to guard against obvious ownership/memory-slope regressions.
- Remaining follow-up: full GUI event-loop stall heartbeat, delegate-instantiation count, search/filter latency at scale, and live-update burst latency/coalescing ratio.

### Stage H - Core Data Path Performance — implemented

- High-cardinality interactive import preparation stays on the one bounded worker: parse -> profile build -> source ordering -> typed point construction -> navigation/scope indexes.
- `IedPointStore` is now the canonical high-cardinality point state: compact `PointRecord` storage plus a stable IED/reference index replaces canonical per-point `QVariantMap` containers.
- The QML-facing `values()` API is retained only as a compatibility boundary and materializes maps on demand; C++ navigation and signal hot paths do not call it.
- GUI-thread import completion is an ownership/adoption step instead of a second profile/index construction pass.
- Navigation consumes the worker-built compact LD/LN index; its synchronous fallback also reads typed records directly and no longer materializes a complete QVariant point list.
- Signal-table rebuild consumes the worker-built selected-LN index and reads `PointRecord` fields directly; filter/rebuild and 16 ms refresh paths avoid QVariant-map conversion.
- Signal presentation storage is typed and scoped; the previous duplicate complete presentation snapshot is removed.
- Live UI notification keeps only the latest presentation state inside a ~16 ms window and emits targeted row changes while protocol/runtime state remains authoritative.
- CI records `IEDSIM_IMPORT_PATH worker_ms=... parser_ms=... prepare_ms=... gui_apply_ms=... points=... scopes=... typed_store=1` and fails if direct GUI adoption exceeds 50 ms.
- Remaining Milestone H evidence work is measurement rather than architecture: explicit burst/coalescing-ratio and GUI-heartbeat tests, plus optional metadata string interning only if measured RSS justifies the complexity.
- Multi-IED non-selected profiles remain lazily built by the compatibility path; prebuilding/caching those profiles is a follow-up so ordinary single-IED import does not pay unused fleet cost.

## Initial budgets

- No user interaction should block the GUI event loop for >50 ms under normal desktop load.
- Search typing should coalesce within ~80 ms and never allocate a second complete SCL tree.
- Large lists must instantiate approximately viewport-sized delegates, not one delegate per model row.
- Presentation/event queues are explicitly bounded.
- Repeated open/start/stop/close stress must show no monotonically growing owned worker/process/timer count.
- CI large-import guardrails: 5k <= 15 s / 512 MiB, 20k <= 30 s / 768 MiB, 50k <= 55 s / 1024 MiB. These are regression tripwires, not claimed product targets.
- Direct async-import GUI adoption is gated at <=50 ms for each 5k/20k/50k case and each repeated 20k reload iteration.

## Definition of done for the redesign

The redesign is complete only when the new workflow passes the existing simulator wire regressions, large-model UI tests, negative/failure tests, lifecycle stress, and measured performance checks. A visually improved screenshot alone is not completion evidence.
