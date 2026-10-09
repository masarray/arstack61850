# HANDOFF — ARStack IEC 61850 Workbench: GUI/UX IEDScout-style recovery

**Snapshot:** 28 September 2026, ~05:42 WIB (GitHub status must be rechecked on resume).  
**Repository:** https://github.com/masarray/arstack61850  
**Audience:** next ChatGPT/Codex thread maintaining the Qt/QML desktop Workbench.  
**Mission:** finish the GUI recovery and operator workflow from Open SCL / Discover IED through canonical browsing, Dataset Signals, explicit static RCB reporting and Windows/real-device acceptance. **This is NOT finished.**

> START HERE: Read this document, then fetch live PR/issue/CI state. Fix the failing exact-head PR #137 Qt report-workspace test FIRST. Do not assume an earlier green run applies to the latest commit. Preserve tested discovery/SCL and static report-only behavior.

## 1. Non-negotiable product contract

The user wants an IEDScout-like *working* engineering workflow, not a screenshot imitation. The Browser must support Open SCL and live MMS Discovery as two ways to create ONE canonical model. After model creation, both entry paths must converge on the same Browser, ordered static Dataset Signals, and explicit static reporting. Keep independent per-IED session/context ownership; no active IED may show another IED's tree, values, association or modal.

Acceptance journey:
1. Open SCL/CID/SCD/IID/ICD OR Discover IED; if the SCL contains multiple IEDs, select the exact IED explicitly.
2. Browse IED → LD → LN → DO → DA, with readable FC, type, value, quality/timestamp and engineering provenance; offline model survives Disconnect.
3. Discover IED presents Signal Catalog only for the active IED; choices: Dataset Signals or Browse Data Model. Manual re-entry also works.
4. Dataset Signals shows the IED's exact ordered static member list with FC/MMS identity and observed values when available, not fabricated or dynamically polled members.
5. Static Reporting… carries the selected exact DataSet into live Reports. Resolve a matching selectable RCB with successful live probe; no row-0 fallback, silent rebinding, or wrong DataSet.
6. Operator explicitly enables the chosen RCB and requests GI when supported. Observe report count, values, quality/timestamp and diagnostics; no cyclic MMS polling fallback or mixed static/dynamic authority.
7. Save discovered SCL → disconnect → reopen → semantic round trip, with consistent Browser/reporting readiness.
8. Compact professional UI (no bulky cards/fonts), no clipping at 1024px, no QML ReferenceError/TypeError/binding loop, Windows installer/portable build and REAL device evidence.

Never introduce an MMS engine rewrite, static DataSet writes, automatic RCB enable/GI, or polling fallback as a cosmetic GUI fix. Dynamic authoring must stay advanced, separate and owned-only.

## 2. Verified repository and PR lineage (not a single mergeable branch)

| Item | Exact ref / relationship | Verified status at snapshot |
| --- | --- | --- |
| Production main | c7238d3a8ff86b30b397164c5fcbed820562b3e0 | Do not overwrite with old UX branch. |
| Old UX base | main-workbench-ux-prd at 009855c0594ecbe27f0ce16c12ef25f711a7b8cd | Diverged from main; a prior regression source. |
| PR #127, R0/R0.1 recovery | fix/workbench-browser-recovery-20260927 at 3a3ceef9149fc0dd31b27f4c9232418a68fae0e6; base main-workbench-ux-prd | Draft/open; exact-head Qt, C++, SCL-Assisted and Release Hardening green; Windows RC exists. |
| PR #136, R1A | feat/browser-dataset-signals-r1a at 1d3899e5266a588a2602a81c0ffe06f148f4e467; stacked on #127 | Draft/open; exact-head Qt/C++/Release Hardening green, rendered Signal Catalog pass, Windows RC exists; NOT real-IED verified. |
| PR #137, R1B CURRENT NEXT WORK | feat/static-report-route-r1b at b47ac165f4ae9a3927cb81641cefc88df3e477dc; stacked on #136 | Draft/open. Release Hardening green, but exact-head IED Simulator Qt **FAILED**. Do not merge or call accepted. |
| PR #130, alternative R1A | feat/ied-browser-r1a at 7d7cc5e9831df761cc61231a3f686b4a2e40a83a; also stacked on #127 | Separate competing/overlapping implementation; review/diff against #136 before deciding. DO NOT blindly merge both. |
| PR #133, unrelated security P0 | fix/reporting-bit-string-bounds-p0 at d12f5be70b54260287bf3ffae321b1d93db50a29; base main | Draft/open. Security blocker #132; must be handled in its own reviewed lineage before final release. |

Links: [#127](https://github.com/masarray/arstack61850/pull/127) · [#136](https://github.com/masarray/arstack61850/pull/136) · [#137](https://github.com/masarray/arstack61850/pull/137) · [alternative #130](https://github.com/masarray/arstack61850/pull/130) · [security #133](https://github.com/masarray/arstack61850/pull/133).

Issue gates: [R0 #128](https://github.com/masarray/arstack61850/issues/128), [R0.1 #135](https://github.com/masarray/arstack61850/issues/135), [R1 parent #129](https://github.com/masarray/arstack61850/issues/129), [alternative R1A #131](https://github.com/masarray/arstack61850/issues/131), [R1B #134](https://github.com/masarray/arstack61850/issues/134), [security #132](https://github.com/masarray/arstack61850/issues/132).

**Important:** the PR ancestry is #127 → #136 → #137, while #127 targets the divergent UX base, not main. A green stacked PR is not authorization to merge the old UX branch wholesale into main. Reconcile the tested delta onto the last verified production/discovery baseline with a reviewed merge/cherry-pick strategy, rerun exact-head CI, and obtain the user's GUI/device acceptance.

## 3. Evidence already achieved — do not discard

**R0/R0.1 at 3a3ceef9:** [Qt run 36317352047](https://github.com/masarray/arstack61850/actions/runs/36317352047) and [Release run 36317352005](https://github.com/masarray/arstack61850/actions/runs/36317352005) succeeded. Rendered output: BROWSER_FLEET_ROUTING_PASS with offline signal tree, nonblank Model Values, switching, reindex, cross-IED isolation and toolbar_1024=visible. Windows artifact arstack-iec61850-workbench-windows-rc, ID 10931054253, SHA256 61da6553edc18152637d3bfd116fce3d32c9310c9b08b8bb90d3644ce0afc979 (expiry 2026-10-11). Earlier blank GUI causes included shadowed fleet services, implicit QML model role after required delegate properties, wrong Repeater/StackLayout routing and QObject lifetime during slot closure/shutdown.

**R1A at 1d3899e5:** [Qt run 36354815175](https://github.com/masarray/arstack61850/actions/runs/36354815175) and [Release run 36354811630](https://github.com/masarray/arstack61850/actions/runs/36354811630) succeeded. Rendered output: BROWSER_SIGNAL_CATALOG_PASS discovery_modal=visible offline_dataset=visible canonical_members=ordered static_rcb=matched no_write=true no_gi=true background_tab=isolated. Exact-head Windows RC exists (artifact ID 10943852515, SHA256 3f6219b8b1ac8e02c8f0617271f4c9936cadeccfcf7350c055d6203ad60fcdc4). Canonical offline RCB binding is projected with bindingSource=CanonicalEngineeringContext, while probeOk=false remains honest until wire probing.

**R1B at b47ac165 — mixed, NOT green:** [Release run 36356784224](https://github.com/masarray/arstack61850/actions/runs/36356784224) succeeded and uploaded Windows RC artifact 10943824004. But [Qt run 36356784220](https://github.com/masarray/arstack61850/actions/runs/36356784220) FAILED; the presence of a Windows ZIP does not override this. C++, Security/Evidence, BRCB, Dynamic RCB, Embedded and Control Interop runs for this head reported success at snapshot. R1B still lacks real-device AA1E1F06R4 acceptance.

Do not confuse synthetic/offscreen green with an operator-tested desktop GUI or protocol interoperability on real IEDs.

## 4. Immediate blocker: PR #137 Qt failure (FIRST task in next thread)

Source: [Qt run 36356784220](https://github.com/masarray/arstack61850/actions/runs/36356784220), job 108725990328, step **Report / RCB commissioning workspace gates**, exit code 29.

Observed log:
~~~
BROWSER_SIGNAL_CATALOG_PASS discovery_modal=visible offline_dataset=visible canonical_members=ordered static_rcb=matched no_write=true no_gi=true background_tab=isolated
BROWSER_FLEET_ROUTING_PASS active_tab=QA_IED_B offline_signals=visible model_values=visible value_label=bound toolbar_1024=visible switching=pass reindex=pass cross_ied_panel=false
REPORTS_WORKBENCH_FAIL dataset_signals_static_rcb_pivot 0 0 "MU01LD0/LLN0.URCB01" "MU01LD0/LLN0.dsGO"
Process completed with exit code 29
~~~

The failing test is apps/ied_simulator/src/ied_report_workspace_qa.cpp, near the staticPivotValid / invalidPivotRejected / originalRcb / staticRoute assertions (around the first DATASET_SIGNALS_STATIC_PIVOT_PASS checkpoint). It runs against tests/fixtures/scl/minimal-station-brcb.scd. The selected canonical route shown in the log is MU01LD0/LLN0.dsGO and the initial RCB is MU01LD0/LLN0.URCB01.

Relevant implementation: apps/ied_simulator/src/MmsReportController.cpp, selectStaticRcbForDataSet, selectStaticRcbForReference, refreshSelection, adoptEngineeringInventory, connectToIed attach and pendingStaticDataSetReference_. Relevant header: MmsReportController.hpp.

**Diagnose each Boolean independently before changing policy or weakening tests:**
- Was the selected DataSet row still the intended row after selectStaticRcbForDataSet?
- Was the chosen RCB bound to the exact canonical reference, and did ordered membership remain correct?
- Did selectedRcbIndex change to another *valid* RCB for the same DataSet, while the test incorrectly insists it equal originalRcb? Or did the production code actually drift/clear a route? Treat this as an open diagnosis.
- Was staticRouteDataSet armed, retained across invalid-index calls, and protected by the correct authority key?
- Did an invalid pivot mutate any prior selection? Did an offline canonical RCB remain probeOk=false?
- At live attach, did the selected RCB have a successful probe and pool-selector selectability? No first-row fallback.

Do NOT delete the assertion, unconditionally force originalRcb, accept any RCB, change DatSet, or turn on polling merely to make CI green. If originalRcb equality is not a valid product invariant because several RCBs are bound to the same static DataSet, replace it only with stronger checks for canonical binding/reference/member order and permitted pool selection; add a fixture covering two RCBs for one DataSet and a negative wrong-bound RCB. Record the verified cause in PR #137.

Fast local repro (Qt 6.8.3, Ninja, Linux/offscreen; from repo root):
~~~sh
cmake -S apps/ied_simulator -B build-ied-simulator-qt -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-ied-simulator-qt --target arstack_ied_simulator ied_report_workspace_qa --parallel 2
QT_QPA_PLATFORM=offscreen ./build-ied-simulator-qt/ied_report_workspace_qa ./tests/fixtures/scl/minimal-station-brcb.scd
QT_QPA_PLATFORM=offscreen ./build-ied-simulator-qt/ied_report_workspace_qa ./tests/fixtures/scl/interop-indexed-reports.scd
QT_QPA_PLATFORM=offscreen ./build-ied-simulator-qt/arstack_ied_simulator --qa-browser-routing
~~~
Then run full Qt and Release Hardening exact-head CI. Expected markers: DATASET_SIGNALS_STATIC_PIVOT_PASS, STATIC_REPORT_ROUTE_RESTORE_PASS, BROWSER_SIGNAL_CATALOG_PASS, BROWSER_FLEET_ROUTING_PASS; no ReferenceError, TypeError or binding loop. Confirm screenshots/artifacts come from the same SHA.

## 5. Sequenced work until the GUI phase is actually complete

### Gate A — unblock and freeze R1B
1. Recheck PR #137 head and workflow runs first; if the head moved, discard stale-run conclusions.
2. Diagnose/fix the pivot failure described above. Add exact DataSet/RCB/FC and route diagnostics; preserve fail-closed semantics and manual-selection override.
3. Test reconnect/re-attach, no verified live RCB, wrong DatSet, dynamic/deletable/empty DataSet, invalid index, context/endpoint switch, RCB pool selection, explicit GI only, and multi-IED isolation.
4. Run exact-head Qt, C++, Security/Evidence and Release Hardening; inspect the actual rendered Browser screenshot and full runtime diagnostics. Keep PR Draft while any gate is red.
5. If a test is flaky or a run is cancelled by a newer commit, distinguish cancellation from failure; do not cite older green SHA for a newer head.

### Gate B — close functional GUI gaps, not cosmetic milestones
1. End-to-end Open SCL: File/Home entry, multi-IED selection, Browser model LD/LN/DO/DA, select/expand, FC/type/value/quality/timestamp and provenance. Confirm offline persistence after Disconnect.
2. End-to-end Discovery: endpoint/discovery -> one canonical model -> active-IED Signal Catalog -> ordered Dataset Signals. Background tab must not steal modal focus.
3. Same DataSet selected via Open SCL and Discovery must converge on the same static report-only controller, same canonical FC/MMS identity and member order. No unverified RCB and no cyclic MMS fallback.
4. Explicit Static Reporting… -> correct BRCB/URCB, reservation/read-back, operator Enable + GI, received reports and diagnostics (RptID, DatSet, ConfRev, BufTm, IntgPd, TrgOps, OptFlds, RptEna, Owner/ResvTms, entry/sequence, quality/timestamp).
5. Maintain separation: Browser, File and Global Data workflows remain coherent; multi-IED association/state stays isolated. Large-model navigation stays responsive.
6. Compact IEDScout-inspired density at 1024px and normal desktop width, plus Windows DPI 100% and 125%; no clipped toolbar/dialogs, oversized cards or illegible tiny text. Prefer actual screenshots over assertions that components exist.

### Gate C — REAL IED acceptance (must not be fabricated)
Target IED from prior user-supplied diagnostic: AA1E1F06R4. Historical test snapshot reported 32 LD, 119 LN, 895 DO, 6324 DA, 2 static DataSets with Analog 22 and Digital 36 members, and BRCB/URCB mappings. **These are historical user observations, not a guarantee of the current device/live firmware.** Re-read live evidence and compare actual discovered SCL with an IEDScout-exported reference or validated user evidence. Compare identity, exact ordered members, FC/MMS refs, RCB DatSet bindings and report values/quality/timestamp. Record screenshots and diagnostics for both entry paths and post-disconnect reopen. Any mismatch is a tracked issue with evidence, not an excuse to reintroduce polling.

If the hardware is not accessible in the execution environment, complete fixture CI and Windows RC, then explicitly leave the hardware acceptance checkbox open and request the operator's evidence. Do not claim full IEDScout parity.

### Gate D — security, integration and release
- Review independent reporting decoder issue [#132](https://github.com/masarray/arstack61850/issues/132) / [PR #133](https://github.com/masarray/arstack61850/pull/133). It records an ASan heap-buffer-overflow on malformed one-byte BIT STRING with no payload. Fix must be isolated, fail closed, and sanitizer corpus green; incorporate into release ancestry before declaring safe release. Never suppress Security and Evidence.
- Resolve the overlapping alternative [PR #130](https://github.com/masarray/arstack61850/pull/130) against #136; do not combine competing Signal Catalogs without per-file review.
- Integrate PRs in explicit dependency order #127 -> #136 -> #137 *after* exact-head acceptance, but because #127 is based on the divergent UX branch, construct/review a clean integration branch from latest main/last verified discovery base. Compare paths and semantics; do not blindly merge that old UX base over main. Preserve working engine/static reporting.
- Produce exact-head Windows portable ZIP and NSIS installer, test staged and installed behavior, and provide GitHub Actions artifact link and checksum. Operator tests actual UI before final sign-off.
- Close #128, #135, #129 / #134 only when their unchecked acceptance evidence is attached, not when the CI badge alone is green.

## 6. Evidence/checklist to attach before declaring DONE

| Gate | Proof |
| --- | --- |
| Routing and data visibility | Screenshot + BROWSER_FLEET_ROUTING_PASS; signal tree, Model Values, active tab, reindex, context/association pointers |
| Catalog and static members | BROWSER_SIGNAL_CATALOG_PASS; exact ordered members, FC, active-IED modal isolation |
| Pivot/route | DATASET_SIGNALS_STATIC_PIVOT_PASS and STATIC_REPORT_ROUTE_RESTORE_PASS, wrong-route negatives, no write/GI during navigation |
| Reporting | Explicit GI and reports; exact DatSet/RCB, no polling fallback, observed value/quality/timestamp and diagnostics |
| SCL parity | Open SCL vs Discovery semantic compare, save/disconnect/reopen, large model |
| GUI QA | 1024px and normal-width screenshot, Windows DPI checks, no runtime QML error |
| Build | Qt, C++, Security/Evidence, Release Hardening all success on the SAME final SHA; Windows portable/installer artifact and SHA256 |
| Hardware | AA1E1F06R4 with dated actual evidence; if unavailable mark pending |
| Merge | Reviewed lineage onto verified main, no old UX regression, final clean CI and operator approval |

Relevant workflow files: .github/workflows/ied-simulator-qt.yml and .github/workflows/ied-simulator-release-hardening.yml. Qt 6.8.3. Qt report-workspace test step runs both minimal-station-brcb.scd and interop-indexed-reports.scd. Rendered Browser screenshot is captured by --qa-browser-routing --screenshot ./ied-browser-workbench.png and uploaded as ied-browser-workbench (only if the test reaches that step). The Windows RC artifact name is arstack-iec61850-workbench-windows-rc.

## 7. Essential source map

- apps/ied_simulator/qml/Main.qml — File/Home/Browser tabs, per-IED delegate routing, fleet lifetime.
- apps/ied_simulator/qml/IedBrowserWorkspace.qml — model/connection bar, Signal Catalog, section routing, commands and reporting entry.
- apps/ied_simulator/qml/IedBrowserNavigation.qml — canonical tree, DataSets/Reports explorer.
- apps/ied_simulator/qml/MmsClientWorkspace.qml — Model Values and observations.
- apps/ied_simulator/qml/BrowserDataSetPane.qml — ordered members, read action, Static Reporting… entry.
- apps/ied_simulator/qml/ReportsWorkspace.qml — RCB details and explicit subscription.
- apps/ied_simulator/src/IedBrowserFleetController.cpp — independently owned sessions/contexts and teardown.
- apps/ied_simulator/src/IedEngineeringContextController.cpp — canonical model authority and SCL/discovery publication.
- apps/ied_simulator/src/MmsReportController.cpp/.hpp — DataSet/RCB inventory, route selection, live attach, report runtime.
- apps/ied_simulator/src/ied_report_workspace_qa.cpp — current red gate and static reporting behavioral regression.
- apps/ied_simulator/src/main.cpp — rendered Browser fixture, 1024px assertions and screenshot output.
- apps/ied_simulator/test_browser_report_authoring.py — GUI/source guardrails.

## 8. New-thread kickoff (copy/paste)

Continue masarray/arstack61850 from docs/handoffs/ARSTACK_IEC61850_GUI_IEDSCOUT_HANDOFF_2026-09-28.md. Read the document and live GitHub state, including PR #127, #136, #137 and issues #128, #129, #134, #135, #132. FIRST diagnose and fix the exact-head PR #137 Qt failure REPORTS_WORKBENCH_FAIL dataset_signals_static_rcb_pivot (run 36356784220, job 108725990328), with real invariants and added negative tests, not by weakening QA or altering static reporting authority. Then pass exact-head CI, verify rendered Browser screenshot and Windows artifact, finish Open SCL/Discovery -> Dataset Signals -> explicit report-only static RCB/GI UX, handle independent security #133, and plan a reviewed integration onto verified main. Report concrete commit SHA, tests, remaining blockers and next task every time you stop. Do not call it DONE without real AA1E1F06R4 acceptance evidence.

---
Snapshot document. Live PR/CI status and hardware evidence must be refreshed on resume; never infer a current green status from this snapshot.