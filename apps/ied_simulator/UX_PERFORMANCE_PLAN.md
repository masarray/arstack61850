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

### Stage C - incremental live updates — hot presentation path implemented

- Signal projection keeps a source-index -> visible-row routing table.
- Live refreshes are coalesced to ~16 ms and compare only source rows represented by the active projection.
- `dataChanged()` is emitted only for contiguous rows whose value/quality/writable/changed roles actually changed.
- Selected-row invalidation targets only the previous/new source rows.
- Active value-based search deliberately falls back to the existing coalesced rebuild because a live value can change filtered membership.
- `IedSignalModel` retains only compact typed rows for the active LD/LN; it does not retain a second complete point snapshot.
- On the async import path, signal rebuild uses the worker-built per-LN source-index vector, making scope changes proportional to the selected LN instead of the entire 20k/50k-point catalog.
- The 16 ms refresh timer is the latest-state presentation accumulator: multiple `valuesChanged` bursts collapse into one read of authoritative typed point state while protocol/runtime state remains ordered and lossless.

### Stage D - diagnostics and lifecycle hardening — implemented

- Activity presentation is a bounded/coalesced C++ model instead of QML-side list filtering.
- Child stdout/stderr framing has bounded buffers, bounded line length, and bounded per-turn drain work.
- Generation-safe delayed-kill semantics prevent an old stop timer from killing a restarted endpoint.
- Negative/lifecycle CI covers malformed SCL, child-output flooding, repeated start/stop/restart, fleet partial-start rollback, and non-blocking interactive clear.

### Stage E - performance evidence — implemented

- `benchmark_large_scl.py` generates deterministic engineering models that expand to about 5k, 20k, and 50k data attributes using reusable SCL type templates.
- The benchmark drives the same `loadFileAsync()` path used by interactive Open SCL, records end-to-end wall time and Linux peak RSS, and fails on crash/deadlock/non-completion.
- Import tracing separates parser time, worker-side profile/index preparation time, and GUI adoption time.
- CI enforces a **<=50 ms GUI-adoption budget** for 5k/20k/50k imports and every iteration of the repeated 20k reload soak.
- The one-process 20k reload soak guards against obvious ownership/memory-slope regressions.

### Stage H - Core Data Path Performance — implemented

- High-cardinality interactive import preparation stays on the one bounded worker: parse -> profile build -> source ordering -> typed point construction -> navigation/scope indexes.
- `IedPointStore` is the canonical high-cardinality point state: compact `PointRecord` storage plus a stable IED/reference index replaces canonical per-point `QVariantMap` containers.
- The QML-facing `values()` API is retained only as a compatibility boundary and materializes maps on demand; C++ navigation and signal hot paths do not call it.
- GUI-thread import completion is an ownership/adoption step instead of a second profile/index construction pass.
- Navigation consumes the worker-built compact LD/LN index; its synchronous fallback also reads typed records directly and no longer materializes a complete QVariant point list.
- Signal-table rebuild consumes the worker-built selected-LN index and reads `PointRecord` fields directly; filter/rebuild and 16 ms refresh paths avoid QVariant-map conversion.
- Signal presentation storage is typed and scoped; the previous duplicate complete presentation snapshot is removed.
- CI records `IEDSIM_IMPORT_PATH worker_ms=... parser_ms=... prepare_ms=... gui_apply_ms=... points=... scopes=... typed_store=1` and fails if direct GUI adoption exceeds 50 ms.

### Stage I - Runtime Scale & Responsiveness — implemented

- Interactive import prepares the full multi-IED runtime projection set on the bounded worker, so selecting or starting a previously non-selected IED does not synchronously construct its profile on the GUI thread.
- Per-IED prepared projections reuse one canonical typed point store and retain integer source indices plus compact navigation/scope indexes instead of duplicating full point maps.
- Runtime switching at 20k/50k scale is measured through `ied_simulator_responsiveness_qa`; CI requires every IED in the imported fleet to be prepared before normal interactive switching.
- Live presentation burst evidence proves thousands of source updates can collapse into a bounded GUI refresh rather than one GUI invalidation per update.
- GUI heartbeat/stall evidence is captured during large-model switch/search/live-update workloads.
- Fleet start is transactional from the operator perspective: if one endpoint cannot start, already-started peers are rolled back to a recoverable Ready state.
- Interactive Clear no longer waits synchronously for child-process shutdown; destructive runtime teardown is scheduled asynchronously with generation-safe completion.
- CI gate `RUNTIME_SCALE_RESPONSIVENESS_PASS` covers 20k, 50k, full multi-IED preparation, partial-start rollback, and non-blocking clear.

### Stage J - Live Runtime Data Plane & Hot Updates — implemented

- Runtime startup still emits one complete model manifest, but steady-state Set Value / Undo updates no longer rewrite that manifest.
- GUI/controller -> child runtime uses a bounded delta channel with explicit runtime generation and monotonic revision numbers.
- Pending and in-flight live-update tracking are each bounded at 256 entries; same-point bursts are coalesced so stale intermediate presentation values do not create an unbounded pipe backlog.
- The child server accepts only the active generation and a strictly newer revision, applies accepted deltas directly to the authoritative MMS runtime store, and rejects stale generation or duplicate/stale revision updates fail-closed.
- ACKs are parsed by the controller and recorded as `IEDSIM_LIVE_ACK generation=... revision=... latency_ms=... pending=... inflight=...`; raw child diagnostics do not need to leak into the GUI log contract.
- Hot-update burst regression sends 1,000 and 10,000 writes to one live MMS value, requires all writes to be accepted by the controller, heavily coalesced, bounded in pending/in-flight storage, visible over an independent MMS association, and leaves startup manifest revision/value unchanged.
- Negative regression explicitly rejects stale-generation and duplicate-revision deltas and verifies rejected state never reaches MMS.
- Existing GUI -> MMS value, control-model, URCB/BRCB, quality/timestamp refresh, and multi-IED same-port regressions remain required after the new data plane is enabled.
- CI gate `LIVE_RUNTIME_DATA_PLANE_PASS` requires `manifest_hot_rewrites=0`, `bounded_pending=256`, and `bounded_inflight=256`; `LIVE_DATA_PLANE_NEGATIVE_PASS` proves stale-state rejection.

### Stage K - Commissioning Feature Depth — implemented

- `CommissioningWorkspace` adds a dedicated right-edge commissioning explorer for the selected IED without replacing the normal live signal workspace.
- `IedCommissioningModel` projects DataSet, Report, GOOSE, and control topology from the already parsed canonical `SclDocument`; it does not parse XML again or retain a second complete FCDA tree.
- Service catalog and member views remain virtualized with delegate reuse and zero speculative cache; canonical FCDA member maps are materialized only for the visible/selected rows requested by QML or QA.
- Report inspection exposes URCB/BRCB mode, RptID, ConfRev, BufTm, IntgPd, binding status, canonical DataSet reference, and member list.
- GOOSE inspection exposes goID, canonical hexadecimal APPID text, destination MAC, VLAN ID/priority, Min/Max time, ConfRev, bound DataSet, and canonical members.
- Control inspection exposes configured CDC and `ctlModel` semantics while live Direct/SBO normal/enhanced execution remains on the existing runtime/control path.
- Report and GOOSE entries can navigate directly to their bound DataSet. Unresolved DataSet bindings remain visible as `Unresolved`, expose zero invented members, and reject the DataSet jump fail-closed.
- Commissioning search/filtering covers service type, name/reference, DataSet reference, APPID/goID, and control model without disturbing the simulator runtime state.
- CI target `ied_simulator_commissioning_qa` validates the positive inventory/metadata/member/navigation path and an unresolved-binding negative fixture. Gates: `COMMISSIONING_DEPTH_PASS` and `COMMISSIONING_NEGATIVE_PASS`.

## Initial budgets

- No ordinary user interaction should create sustained GUI event-loop stalls; expensive construction remains off the GUI thread.
- Search typing coalesces within ~80 ms and never allocates a second complete SCL tree.
- Large lists instantiate approximately viewport-sized delegates, not one delegate per model row.
- Presentation/event/diagnostic/live-delta queues are explicitly bounded.
- Repeated open/start/stop/close stress must show no monotonically growing owned worker/process/timer count.
- CI large-import guardrails: 5k <= 15 s / 512 MiB, 20k <= 30 s / 768 MiB, 50k <= 55 s / 1024 MiB. These are regression tripwires, not claimed product targets.
- Direct async-import GUI adoption is gated at <=50 ms for each 5k/20k/50k case and each repeated 20k reload iteration.
- Live ACK latency regression budget is <=1500 ms under the deterministic 1k/10k CI burst, with pending and in-flight tracking each <=256.
- Commissioning browsing must reuse parsed SCL ownership, virtualize lists, and fail closed on unresolved DataSet bindings rather than fabricating members or silently rebinding references.

## Definition of done for the redesign

The redesign is complete only when the new workflow passes the existing simulator wire regressions, large-model UI tests, negative/failure tests, lifecycle stress, runtime-scale responsiveness gates, commissioning-depth positive/negative gates, live-data-plane burst/negative gates, and measured performance checks. A visually improved screenshot alone is not completion evidence.
