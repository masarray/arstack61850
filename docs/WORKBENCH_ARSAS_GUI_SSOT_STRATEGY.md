# ARStack61850 native IED Browser — ARSAS-inspired, SSOT-first engineering strategy

**Status:** cross-thread execution contract; first written 2026-10-15 (WIB), against ARStack main a328da13133a and ARSAS main 45348eccf24e941dd185e5fb729217923846c610. **Re-query GitHub before acting.** This is a living strategy, not a claim of completed functionality.

**Product:** the installed **native Qt/QML ARStack Workbench** (apps/ied_simulator), **not** a web GUI, a new desktop app, a WPF port, or the separate ESP32-P4 timing/SV firmware track. The objective is a professional IEC 61850 engineering workflow while borrowing effective visual/interaction patterns from the ARSAS/ARIEC61850 Windows Explorer. ARStack's own IEC 61850 models and wire evidence remain authoritative.

## 0. Read order for every thread

1. [AGENTS.md](../AGENTS.md) and [AI_READ_FIRST.md](../AI_READ_FIRST.md) — hard rules.
2. [This strategy](WORKBENCH_ARSAS_GUI_SSOT_STRATEGY.md) — common direction, SSOT, handoffs, gates, ownership.
3. [WORKBENCH_CURRENT_HANDOFF.md](WORKBENCH_CURRENT_HANDOFF.md) — live checkpoint; update after each accepted milestone.
4. [CI_WORKFLOW_ROUTING.md](CI_WORKFLOW_ROUTING.md), [RCB_REPORTING_REFERENCE.md](RCB_REPORTING_REFERENCE.md), [ONLINE_MODEL_CONNECT_DECISION.md](ONLINE_MODEL_CONNECT_DECISION.md) as applicable.
5. Recheck GitHub main SHA, open PRs and their review threads, exact-head Actions, and current file content. This document's SHAs and status are **as-of-snapshot**, not a substitute for live checks.

**Reviewable design references (pinned to an actual ARSAS revision):**

- [ARSAS MainWindow.xaml — IEC 61850 Explorer / LIVE SIGNAL VALUES](https://github.com/masarray/arsas/blob/45348eccf24e941dd185e5fb729217923846c610/MainWindow.xaml): IED cards, Connect/Open SCL/Add IED workflow, dense live grid (Signal, IEC Telegram, Value, Quality, IED Timestamp, Acquisition), search, and guarded command panel.
- [ARSAS ExplorerSignalGrid](https://github.com/masarray/arsas/blob/45348eccf24e941dd185e5fb729217923846c610/MainWindow.ExplorerSignalGrid.cs): column sizing and timestamp detail.
- [ARSAS SclLiveModelAuthority](https://github.com/masarray/arsas/blob/45348eccf24e941dd185e5fb729217923846c610/MainWindow.SclLiveModelAuthority.cs) and [live projection](https://github.com/masarray/arsas/blob/45348eccf24e941dd185e5fb729217923846c610/SclLiveSignalModelProjection.Authority.cs): design/live authority reconciliation.
- [ARSAS shared static DataSet authority](https://github.com/masarray/arsas/blob/45348eccf24e941dd185e5fb729217923846c610/MainWindow.SharedStaticDataSetAuthority.cs): preserve static report-first behavior, without automatic cyclic MMS.
- [ARSAS IoList FAT workspace](https://github.com/masarray/arsas/blob/45348eccf24e941dd185e5fb729217923846c610/IoListTestingWindow.xaml): multi-IED selection and engineering context.

**Copy the *interaction principles*, not WPF/C# runtime, invented values, unsupported IEC semantics, or duplicated services.** ARSAS' IEC 61850 Explorer is a useful reference, but not proof that every ARSAS screen literally matches any single engineering tab.

## 1. Integration baseline and merge dependencies

Historical baseline and accepted merges (always refresh current GitHub status):
- ARStack main: [a328da13 (M2J merged)](https://github.com/masarray/arstack61850/commit/a328da13133a6473dc6a28a69f5d2aebab7a7198). M2H [#171](https://github.com/masarray/arstack61850/pull/171), M2I [#173](https://github.com/masarray/arstack61850/pull/173) and M2J [#174](https://github.com/masarray/arstack61850/pull/174) are merged.
- M2J post-merge Qt, release hardening, SCL-assisted connect and SCL-assisted Workbench workflows completed successfully at this main SHA.
- **Integrated M2K:** PR #176 merged into main at `0cc882022d20f416a33433d5f29a1f8950984d85` with exact-head native Qt and SCL verification.
- Unmerged PR [#172](https://github.com/masarray/arstack61850/pull/172) (another q/t visual QA branch), [#164](https://github.com/masarray/arstack61850/pull/164) (older q/t branch), and legacy Browser recovery PRs exist. They are **not automatically safe to merge**; review differences against main and accepted M2I/M2J first. Some PTP/SV and native Studio branches are distinct tracks (for example #175).
- **M2K accepted scope:** native All-IED grid, visible Quality / IED Timestamp, bounded search, canonical sample-received state; retain its single SSOT and no polling.
- Every future milestone lists both base SHA and exact PR head, a bounded change set, CI links, review resolution and merge SHA. Update the companion [current handoff](WORKBENCH_CURRENT_HANDOFF.md) at accepted checkpoints, not after every experimental commit.

## 2. Authoritative architecture — a single model, multiple views

The graph is a dependency contract, **not a request to add layers**:

    Open SCL / normalize ----------+
                                     |
    Online MMS discovery -----------+--> IedEngineeringContextController
                                     |         |
    Trusted-SCL assisted validation-+         +--> canonical MMS model / MmsLiveTreeModel
                                               |    indexed domain/item and IEC aliases
    per-IED MMS association (MmsClientController)-+--> actual read samples + typed evidence
    per-IED RCB association (MmsReportController)--+--> actual report frames + RCB lifecycle
                                               |
                                  QML navigation, Data Model, DataSets,
                                  Reports, Global Data, SG, Files, GOOSE
                                               |
                              All-IED is a **bounded presentation only**,
                              never a second protocol model or acquisition owner

Current code authority / edit boundaries:

| Concern / role | Owned source; do not clone |
| --- | --- |
| IED source identity, SCL vs online, context generation | apps/ied_simulator/src/IedEngineeringContextController.* |
| Canonical domain/LD/LN/DO/DA, indexed IEC/MMS aliases, value and selected state | apps/ied_simulator/src/MmsLiveTreeModel.hpp/.cpp |
| MMS confirmed request/read and per-IED session | apps/ied_simulator/src/MmsClientController.* and IedBrowserFleetController.* |
| IEC Quality and UTC TimeQuality type validation | apps/ied_simulator/src/MmsEngineeringEvidence.hpp (**one shared typed decoder**) |
| Trusted-SCL initial mapped Read | apps/ied_simulator/src/MmsSclClientProjection.hpp, with Qt conversion only in Workbench boundary |
| RCB activation, framed InformationReport, replay and cleanup | apps/ied_simulator/src/MmsReportController.* and BrowserReportMemberProjection.hpp |
| Navigation, viewport and inspectors | apps/ied_simulator/qml/IedBrowserWorkspace.qml, IedBrowserNavigation.qml, BrowserDataModelPane.qml, BrowserDataSetPane.qml, BrowserReportPane.qml |
| Per-IED watchlist (presentation only) | apps/ied_simulator/qml/BrowserGlobalDataPane.qml |
| Cross-IED watchlist (bounded, view only) | apps/ied_simulator/qml/FleetGlobalDataPane.qml and Main.qml |
| SG/File/GOOSE existing behavior | existing MmsFileSettingsController, BrowserGoosePane, service/controller paths; identify live owner before editing |

**No new protocol parser in QML, separate signal store, second SCL model, second quality decoder, UI-owned MMS polling, or WPF/.NET subsystem.** Only already-confirmed source values can become live evidence. All-IED aggregation must carry the original IED slot and never silently redirect an action to a different association.

### Provenance and lifecycle state contract

- A discovered/structurally **readable DA is not a received MMS value**. The canonical tree must distinguish: loaded/discovered, no sample (NOT READ), successful sample (including a legitimate empty scalar), failed/unknown, offline, and mismatched/stale generation. Do not mark LIVE from readable alone.
- Keep value, typed q/t, server timestamp, local receipt time (only if really captured), acquisition source (explicit Read versus confirmed RCB report versus design-only) and source-generation coherent. Extend metadata **only on the current authoritative event-ingress path**; do not invent second value caches to satisfy the grid.
- On a new SCL/IED identity, disconnect, generation replacement or report lifecycle reset: invalidate context-dependent selection, unverified live claims, stale q/t/SG evidence and delayed callbacks. A late callback from an obsolete generation must be dropped, not applied to the next IED.
- Distinguish a displayed '-' for absent evidence from valid IEC Boolean false, validity good, TimeQuality false bits, or a *received empty value*. 'Unknown' must not be silently converted into false.
- Typed q/t inspection must be sourced from actual MMS BIT STRING/UTC Time and correct leaf **only**. No guessed q/t bit positions, no timestamp synthesis, no q/t from arbitrary decimal strings.
- Angle exists only when source type/CDC/structured vector supports a phasor angle (for example an applicable complex measurement); **never append angle to frequency Hz, TotW, or generic scalar just because another row had an angle**. Units/multipliers require SCL or validated metadata, not a hardcoded unit-per-column shortcut. Do not project every measurement as a generic magnitude/angle tuple.

### Protocol/reporting constraints

- Report/DataSet members maintain confirmed ordered FCDA identity and **received frame ordinal**; Report ID and optional fields must be decoded from the real frame. Missing/invalid/segmented values cannot be shifted to a neighboring member.
- **Static DataSet/RCB must remain report-first/report-only** where configured; no silent periodic Read to fill missing rows. Initial bounded snapshot and user-triggered explicit Read are distinct authorities. URCB reservation and BRCB behavior cannot be collapsed.
- Controls/write/SG switching/file operations require effective server-state readback/guarded user intent and clear error presentation. A successful request submission alone is not proof of effect.

## 3. UX target and user workflow (ARSAS guidance, professional IEC 61850 engineering-grade outcome)

**Primary journey:** Open SCL or Add IED by endpoint -> resolve exact IED/AP/domain -> inspect offline hierarchy -> Connect/Discover/Validate -> browse Data Model/Measurement -> select DA / Read explicitly -> inspect typed Value/Quality/TimeQuality/unit/provenance -> choose bounded DataSet -> inspect ordered FCDA -> inspect report controls -> confirm/enable static RCB under correct guard -> receive evidence mapped to report member -> monitor All-IED grid -> disconnect/reopen without cross-context residue.

| Native workbench surface | Required contents and behavior | ARSAS idea adopted / exact acceptance |
| --- | --- | --- |
| Fleet header / IED cards | one card per IED; endpoint, loaded/connected state, action availability, explicit selected IED | ARSAS Explorer card-driven navigation; no global implicit Connect All |
| Data Model / Measurement | LD/LN/DO/DA indexed tree; FC and MMS/SCL type; current scalar/vector value; engineering unit if proven; q/t & read-state visible | dense selected inspector, lazy drilldown; scalar frequency/power never borrow phase angle |
| DataSets | searchable ordered FCD/FCDA members; one FC label, valid alias resolution, bounded lazy DO->DA expansion | selection changes inspector without MMS Read; no duplicate FC or phantom value |
| Reports | RCB list grouped by LLN0; selected RCB attributes / effective state; last actual InformationReport and aligned members | DataSet binding, RptID, state, GI, reason, q/t; static report-only path preserved |
| Setting Groups | actual SGCB state, active/edit SG, selected group parameter values with typed semantics | never claim write applied before effective readback; no phantom SG values |
| Files | file listing, safe names/sizes, guarded transfer/download progress, cancellation/errors | IO bounded, no blocking UI; user-visible provenance of local vs server |
| GOOSE | configured GOOSE attributes/selection/evidence, source identity | no auto-subscribe/transmit or fabricated live state on SCL-only source |
| Per-IED Global Data | canonical-selected DA, DataSet, Report, GOOSE watchlist | add/remove/refresh explicit; display read status and q/t as separate evidence |
| All-IED Global Data | IED, IEC reference, Value, Quality, IED Timestamp, acquisition/status; filter by IED/ref/value | 1:1 slot+reference identity, visible q/t, event-driven row patch, bounded presentation and no cross-IED requests |

**Presentation rules:** professional dense hierarchy and consistent affordances; show why action is disabled (offline, busy, no loaded model, incompatible trusted SCL). Do not hide primary QA fields in tooltips only. Prefer stable row identity and virtualization over resetting the list on every report. Filters change only what is visible, not the watchlist, subscription or report authority. Preserve selection/scroll state across value-only updates when possible. Test at realistic Windows scaling and narrow/wide sizes; do not claim pixel parity without actual captured selected-state screenshots and human review.

## 4. Bounded, optimized implementation contract

Existing enforceable or established bounds to **preserve** (verify against current source at every milestone):
- Canonical large-model fixture exercises **60,106 nodes**; continue lazy/virtualized tree projection and indexed MMS/IEC lookups; no full-tree QML rewalk per value.
- MmsLiveDetailModel caps its projected rows at **4,096**; per FCDA lazy expansion at **64 children**.
- Global Data explicit MMS refresh caps at **64 targets per action**; do not multiply requests by each visible delegate.
- Per-IED watchlist **256 entries**, All-IED presentation **2,048 rows**, per-presented text preview **256 chars** (M2K scope).
- RCB report GUI member projection **256 members**, display preview **1,024 chars**, initial typed q/t evidence budget **96 items**. These caps are presentation/initial projection limits, **not permission to drop or truncate authoritative source frames**.
- No unbounded search index, duplicate semantic cache, one Timer per row, eager QString formatting of attacker-sized MMS strings, blocking conversion on packet callback, or heap growth proportional to unlimited incoming events.

**Optimization sequence:** measure before changing data structure; use already-built indexes, model roles and stable key projections; patch changed rows and coalesce bursts on GUI event loop; keep conversion off the UI and protocol hot paths; validate bounds **before** string allocation. No speculative abstraction layers, broad refactors, more background workers, fixed sleeps or fake evidence merely to pass a test.

**Capacity and performance acceptance:** record node/visible count, model build and selection latency, filter latency, UI update/refresh time, result count, allocations or bounded-memory high water where instrumented; compare to **last known good CI under same runner/workload**. No increase of asymptotic complexity; any material regression needs a measured justification. For interactive views, test repeated value bursts, 8 independent IEDs, sparse/empty lists, 2,048 watched rows, large SCL and oversized untrusted values.

## 5. Staged delivery order and integration gates

Every milestone is a **small vertical slice** (one actual user action through the existing model to rendered evidence, plus negative/regression cases). Do not wait for one giant UI rewrite.

| Gate | Scope and dependencies | Merge acceptance |
| --- | --- | --- |
| **M2K — merged, PR #176** | Native All-IED grid + canonical hasReceivedValue | Preserve existing verified grid/provenance/large-model behavior |
| **M2L — merged, PR #178** | Canonical exact CDC + MX measurement semantics, authoritative q/t and read source | Preserve integer/float vector and scalar regression, no fictional angle/units, source replacement tests |
| **M2M** | Data Model and DataSets **rendered native workflow**: selection, FCDA aliases, tree expansion, inspector/read button, per-IED Global Data handoff | deterministic native screenshots with actual selected DA/FCDA (not overview only), no implicit Read, 64 child bound, multi-IED isolation |
| **M2N** | Reports/RCB and Global Data provenance: requested vs effective RCB; actual report q/t and ordered frame members; consolidated live grid acquisition | static report-only; GI/URCB/BRCB and cleanup, malformed/omitted/segmented report tests, actual selected native screenshot and no cross-IED leakage |
| **M2O** | Setting Groups / Files / GOOSE productivity and safety; one controller and explicit user workflow per feature | selected SG values and readback, negative/safe control cases, guarded transfer/cancel, configured-vs-live GOOSE state |
| **M2P** | End-to-end visual/interaction/scale polish, Windows executable delivery and physical IED trials | screenshot compare with reference by section/state, keyboard/mouse/resize, stress + cancel/reconnect, exact Windows installer artifact lineage, real IED evidence distinguished from simulation |

Milestone order may be rearranged **only for explicit blockers/dependencies**, and the current handoff must record the reason. A screenshot-only cosmetic PR cannot silently supersede a missing semantic/measurement test. Full GUI parity remains a **goal**, not a CI status.

## 6. Parallel-thread ownership and collaboration protocol

**Integration steward** (one thread at a time):
- Maintains this strategy and current handoff, checks PR queue, decides dependency order, owns final exact-head merge with expected SHA, reconciles conflicts and performs post-merge main check.
- Publishes a short checkpoint per accepted milestone (baseline SHA, PR number/HEAD, changed source, green exact-head runs, artifacts, unverified gaps).
- Does **not** auto-merge legacy/parallel branches #172, #164, #136/#137 or firmware PRs on name similarity alone. First compare with main and accepted work.

**Track A — canonical data/provenance** (serial owner): MmsLiveTreeModel, typed evidence, context/source generation, measurement semantics and hardening; ARSAS-inspired output **must** come from these existing models. Typical M2L. Any report decoder overlap requires an explicit handoff to Track C.

**Track B — native Qt UX** (parallel only on disjoint paths): IedBrowserNavigation, BrowserDataModelPane, BrowserDataSetPane, Global Data QML, selection/visual states and GUI QA. No decoder, new model, timer-driven polling, or another source of truth. Typical M2M.

**Track C — protocol-dependent panels**: MmsReportController/Report projection, SG/File/GOOSE controller APIs, actual RCB lifecycle/cleanup. Follow reporting profile; no network/polling behavioral changes as “UI polish”. Typical M2N–M2O.

**Track D — evidence/CI/release**: fixture matrix, native Qt smoke, screenshot capture of selected states, large-model/performance and Windows artifacts. Works concurrently on **test paths not already being modified**; coordinates test_browser_convergence.py and test_multi_ied_workspace.py with the feature owner (these files are shared cross-cutting conflict points). Never “fix” failing CI by relaxing a correct invariant.

**Ownership rule:** avoid two open PRs editing the same authoritative source file. If needed, designate one integration candidate and explicitly merge/rebase/reconcile prior changes **before** new work; compare diffs and check lease, never force-push another thread's branch. Prefer separate feature PRs based on merged main. Cross-thread coordination is visible **in the PR description and the handoff**, not assumed from chat context.

**Handoff contract for a thread opening work:** include base/ref and fresh main SHA; active dependency PRs with HEAD; precise canonical owner and target files; observed failure/UX gap with source/fixture; expected semantic invariants; acceptance tests and performance budget. On exit update current handoff with status (planned, in progress, exact-head green, merged, or blocked), logs/artifact links, review objections and next-step owner. Another thread must first reconcile these entries with live GitHub, not blindly reuse them.

## 7. CI, QA and production acceptance

**Before code**: read the relevant files and source contracts, reproduce/trace current behavior, enumerate model consumers, identify exact no-read/no-poll/no-cross-IED invariants, and inspect current GitHub PR state.

**Before pushing**: do local/source preflight for touched files, deterministic negative tests, and a single coherent batch. Avoid re-triggering heavy CI with one-line follow-up commits that were preventable by review. Keep all tests that assert semantically correct contracts; update only **stale coupling to implementation spelling** after verifying the new runtime route.

**Targeted CI** (select only relevant gates, see CI routing policy):
- Qt compile/QML smoke + Browser convergence + P6E Multi-IED authority + canonical engineering context/60k large model.
- SCL-assisted Connect/Workbench when changing SCL projection, source reconciliation or session ownership.
- RCB/Interop/Control/Files/Settings tests when changing those services.
- Native screenshot QA for *selected* q/t/DataSet/RCB states and malformed/unread variants; an overview screenshot alone proves only layout.
- Windows Qt lifecycle/release-hardening where changes touch actual Windows behavior; **Studio Windows installer is tag/manual release**, not every Browser source PR.

**Never equate:** source token tests with rendered UX; CI green with physical IED interoperability; release job complete with downloadable installer unless an exact artifact/release exists; synthetic test input with a physical IED capture; shown timestamp with a proven device-quality timestamp.

**Required negative matrix:** offline/disconnected; empty source; multiple IEDs with same LD name; failed or timed-out Read; late response after source generation changes; report member omissions/segmentation; q/t truncated/wrong MMS type; SG unsupported/readback mismatch; FCDA alias/FC mismatch; long untrusted metadata; exceeding each budget; repeated filter + selection + reconnect. Run high-impact negative cases in native QML/C++ integration tests, not just string checks.

**Merge gate:** review live diff/ownership, verify no unresolved review thread, require **completed/success, non-skipped, exact PR-head** relevant gates, squash merge with expected_head_sha, re-read main HEAD, then verify post-merge CI separately. A docs-only PR should use relevant cheap document/link checks, not dispatch unrelated firmware/Windows build storms.

## 8. Current integration order and cross-thread handoff

**Verified integration baseline (2026-10-15):**
- M2K [PR #176](https://github.com/masarray/arstack61850/pull/176) merged at `0cc882022d20f416a33433d5f29a1f8950984d85`.
- M2L [PR #178](https://github.com/masarray/arstack61850/pull/178) merged at `9cc9bda9c30804be343d49367e4dde6948789d95`.
- Native DataSets inspector + repository naming safeguard M2M [PR #179](https://github.com/masarray/arstack61850/pull/179) merged at `ce643e81cd7640ed2b694c0914282177f871a9fa`, its exact-head 14-workflow matrix **SUCCESS**.
- Native DataSets → Data Model / Global Data actions and source-generation selection reset [PR #180](https://github.com/masarray/arstack61850/pull/180) **MERGED** at `1e6ff57be39badd8d1d297b4301060ed3835984e`, exact PR head `cb7b7fb92447f6365c3c751abd3d0d7db922ffdc`; native Qt 55 steps and naming guard SUCCESS.

**Current Integrator:** #179 and #180 are merged. Verify post-merge Qt / Windows Workbench release evidence and run actual DataSets-to-Data Model selected-DA click-through; then pursue M2N Reporting/RCB UX and only demonstrated missing ARIEC61850 engine semantics. Keep [current handoff](WORKBENCH_CURRENT_HANDOFF.md) current. Do not resume obsolete #176/M2L instructions.

**Track A — C++ engine:** compare [ARIEC61850 port map](ARIEC61850_ARSAS_NATIVE_PORT_MAP.md) against existing canonical C++ owners. Port only a **measured, genuinely missing semantic capability** with protocol/negative tests; no second C# runtime, MMS client, SCL parser, or model.

**Track B — native GUI/UX:** continue the existing Qt `IedBrowserWorkspace`, Data Model/DataSets/Reports/Global Data views, borrowing dense Explorer navigation and feedback patterns from ARSAS. Demonstrate selected-state rendered screenshots, real actions, no automatic Read or per-row timers; do not convert WPF verbatim.

**Track C — reports/RCB and SG/Files:** verify effective state and actual received ordered report members, controls and life-cycle cleanup under existing service owners. Keep report-first static behavior and bound IED session generation.

**Track D — acceptance and Windows package:** capture genuine selected DA/FCDA/RCB states, 60k model and multi-IED regression, then verify installed **Workbench** executable artifacts separately from **Studio**. A synthetic screenshot is not proof of physical IED interoperability.

**Definition of progress:** merged source on the real native product path, exact-head and post-merge CI evidence, selected-state screenshots, truthful source/provenance, no product-naming regression, and updated handoff. A redundant PR, a second GUI stack or an unchecked screenshot is not progress.
