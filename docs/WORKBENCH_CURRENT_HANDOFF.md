# Native Qt IEC 61850 Workbench — current cross-thread handoff

> **Updated:** 2026-10-16 (WIB), snapshot only; always refresh GitHub refs and PR state before editing.
> **Primary strategy and ownership contract:** [WORKBENCH_ARSAS_GUI_SSOT_STRATEGY.md](WORKBENCH_ARSAS_GUI_SSOT_STRATEGY.md).
> **Scope:** installed native ARStack Workbench in apps/ied_simulator. Not a web app, WPF clone, or the separate ESP32-P4/SMV Studio track.

## P0 exact RCB InformationReport authority — 2026-10-17 (WIB)

- **Implementation PR:** [#203](https://github.com/masarray/arstack61850/pull/203), native Qt Workbench + reusable C++ MMS engine; source changes present on PR, **merge and field acceptance pending CI** at this checkpoint. Refresh its exact head, 13 workflow results, review threads, and main before treating this as accepted.
- **Critical issue closed in source:** original subscription mapped every incoming InformationReport against the selected RCB DataSet before proving source ownership; Inspector filtered only by RptID. New authority boundary checks exact RptID *before* mapping and then validates optional DatSet/ConfRev (when present), ordered unique inclusion indexes and wire DataRef against exact directory identity before exposing process values. Foreign/stale reports are rejected, counted separately from decode failures, and never enter continuity stream.
- **Browser output:** each accepted frame carries its association's active RCB reference, and Inspector requires both exact RCB reference and RptID where known. Bounded lifecycle events include explicit GI sent/not sent/failure and a warning on rejected frames/decoder errors, not an invented successful subscription.
- **Static RCB safety:** discovery-chosen DataSet reference is pinned across the live activation probe and compared with the fresh RCB readback before any write; mismatch fails closed. GI is a Write trigger, removed from both targeted SCL attach Read set and subscription Read set. URCB reservation / BRCB separation, guarded TrgOps/OptFlds, RptEna-last and cleanup ownership remain one existing C++ runtime authority (no second decoder or polling association).
- **Regression:** native C++ tests for valid and foreign RptID, wrong DataSet, ConfRev change, out-of-order/duplicate indices, wrong/swap/empty DataRef, typed MMS DataRef, missing directory, valid minimal OptFlds; scripted MMS transport rejects foreign report then accepts good report without poisoning counters, and rejects a live RCB DataSet rebind before writes. Existing Qt GOOSE/Browser reports smoke, SCL, 3-platform MMS, interop and sanitizers are required.
- **Physical acceptance outstanding:** verify on an actual IED with exact selected static URCB/BRCB instance; verify RptEna/Resv/Owner readbacks, GI Write outcome (not merely request), optional-fields profile, first report's real RptID and DataRef, index/member order and quality/time, disconnect/reconnect and RCB cleanup. This is not physical manufacturer interop certification. It remains impossible to infer which of two identical wire RptIDs emitted a frame without additional uniqueness/association evidence.

## M2N native Browser GOOSE Subscribe + ARSAS/ARIEC parity — 2026-10-16 (WIB)

- **Integrated `main` at this checkpoint:** `71580659f28c49207db94efd7f7d302d5fd12901`, [PR #200](https://github.com/masarray/arstack61850/pull/200) MERGED after exact-head `4df620874bdab9c0bf280d31a342c12dfa6ab38a` **3/3 relevant workflows SUCCESS**: 55/55 native IED Simulator Qt job steps, SCL-Assisted Workbench CI and Product Naming Guard. Three P1 review findings fixed/answered/resolved. Exact post-merge main jobs (IED Simulator Qt, SCL-assisted, Release Hardening) were still running; verify separately.
- **Reference-backed root cause:** `BrowserGoosePane.qml` previously presented only canonical configured SCL/control-block data, while `GooseWorkspace` already received real Ethernet GOOSE via `GooseMonitorController`, bounded AF_PACKET/Npcap, native `GooseFrameCodec`, sequence/TAL supervision and PCAP export. Cross-checked ARSAS `docs/GOOSE_SUBSCRIBER.md`, `GooseSubscriberRuntime.cs`, `GooseSubscriberLiteView.xaml` and ARIEC `GooseFrameParser.cs`. This ports missing capability into the existing native owner; **not** a duplicate parser, second NIC socket, .NET app, timer, poller or cache.
- **User-visible capability:** Browser → GOOSE has explicit interface picker, Refresh NICs and GOOSE Subscribe/Stop for the *shared passive* monitor; never silently auto-select first NIC. The selected configured stream displays actual matching wire stNum/sqNum, packet/timeout state and decoded ordered `allData`. After capture stops, retained values are clearly historical, not live. The full Sniffer remains available.
- **Identity and safe engineering naming:** exact case-preserved `goCBRef` and available DataSet, APPID (SCL hexadecimal), destination MAC, goID and VLAN/PCP; typed live MMS `LD/LN.GO.gcb`/ `LD/LN.DataSet` → canonical `$GO$`/`$DataSet` wire conversion only (no global dot replacement). No APPID-only guesses; malformed/ambiguous/wrong-publisher evidence rejected. A model member is labeled with an observed value **only when the configured ConfRev AND DataSet member count agree**. Otherwise raw allData remains visible, explicitly unbound, with mismatch warning, no invented FCDA mapping.
- **Bounded performance:** one `GooseMonitorController` for the application, 256 streams/256 members/256 events/4096 retained packets/64 packets per poll; NIC enumeration cached behind its own `interfacesChanged`, refreshed only on command and never per incoming frame. Multiple IED tabs reuse the one monitor with strict stream binding, not multiple receivers.
- **Regression:** `ied_goose_workspace_qa` replays generated native GOOSE publisher PCAP; checks exact matching, allData order, canonical MMS references, wrong APPID/DataSet/MAC/goID/VLAN, malformed identity, case sensitivity, competing publishers with same ref/APPID, plus existing TTL/sequence/PCAP capacity. Qt QML smoke and rendered Browser routing passed on exact PR SHA. This is test evidence, **not** field IED capture or manufacturer interoperability certification.
- **Outstanding product gates:** test on physical publisher/relay and selected Windows Npcap NIC (including VLAN/offload/network path and physical frame receipt), validate ConfRev/FCDA values with real SCD, time-to-first-observation, Windows installed Workbench package, and independent reporting/RCB controls. No guessed GOOSE subscription acknowledgements or device state changes. Separate ESP32-P4/PTP/SMV Studio track remains independent.
- **Next milestone candidates:** Reporting static/URCB/BRCB real-frame mapping, GOOSE SCL-enriched per-member quality/metadata, and measurable repeatable discovery latency benchmark on same IED; require source-owner and CI checks to avoid duplicate architectures.

## P0 negotiated NameList wire pipeline — 2026-10-16 (WIB)

- **Actual main at integration:** `47f9966edc07d5ef391d149b0ac60bc259b5bd11` after [PR #197](https://github.com/masarray/arstack61850/pull/197), source head `88ba0419af3866938be7a69c57cdc6cea6c56457`. **13/13 exact-head workflows SUCCESS** including C++ GCC/Clang/MSVC, native IED Simulator Qt, Security and Evidence, SCL-assisted, MMS/RCB, interoperability, and Product Naming Guard. Automated review issue about strict GCC aggregate initialization was corrected and marked resolved. Post-merge `main` workflows were still active at the checkpoint; independently verify results on their actual SHA.
- **Implementation:** portable native C++ `MmsAssociationRuntime::exchange_confirmed_batch` sends whole confirmed P-DATA frames on one association owner, then receives through the existing authoritative receive primitive; exact invoke-ID correlation handles out-of-order responses, duplicates/unregistered IDs fail closed, and asynchronous InformationReport remains on the original bounded queue. Each complete send has a per-batch receipt even when later timeout/fault occurs. No concurrent `exchange_confirmed` threads, second decoder, worker pool, timers, or hidden MMS Write.
- **Safety contract:** strict positive decoded peer MMS Initiate-Response `maxOutstandingCalling` is a prerequisite for any batch window above 1. If unknown/tolerant, **fallback is serial** despite compatibility defaults. Effective GetNameList window = min(explicit request, verified peer calling window, internal hard cap 8). The batch call is synchronous and single-owner: it is not safe to invoke other receive/poll methods concurrently. Pagination on each chain stays sequential, bounded and fail-closed.
- **Discovery behavior:** independent per-domain NamedVariable and NamedVariableList chains are opt-in; existing Workbench/controller default remains serial to preserve field compatibility. Read-only bench CLI: `ariec61850_live_discover <ied-ip> --name-list-window 4` (range 1..8, default 1). Model/raw FC identity stays canonical; completed evidence only is published. Summary exposes completed confirmed-request wire budget and `nameListPeakOutstanding` without extra network traffic.
- **Regression evidence:** 48/48 CTest PASS on GCC and Clang. Focused tests verify VMD+two-domain model parity and equal six NameList sends between serial and four-chain operation; page continuation after the prior response; out-of-order invoke routing; interleaved InformationReport; bad continuation, rogue/duplicate IDs, over-window refusal, loss/timeout and reconnect/send-receipt semantics. No real IED timing speedup claimed from scripted transport.
- **Other observed CI issue (independent):** Workbench Release Hardening on `main bcbebc32` failed *before build* because Chocolatey/NSIS dependency download returned HTTP 503; Linux release-soak succeeded. Treat as packaging environment incident, not evidence of MMS/Qt regression. Re-verify installer separately.
- **Next highest-value work:** same-relay repeated serial/window=2/window=4 captures with SCL and model fingerprint, completeness, wire budget, `maxOutstandingCalling`, elapsed discovery latency, time-to-first-usable model, CPU/memory, reconnect and report/RCB behavior. Only after physical interoperability evidence consider exposing the opt-in to native Workbench. Long-term association-wide concurrent requester APIs still require an explicit shared lifecycle/receive-pump contract; the current batch API is intentionally single-owner.
- **Acceptance remaining:** exact post-merge `main` gates; physical IED and installed Windows Workbench bench runs; measurement of speedup; ESP32-P4 PTP/SV qualification is unrelated. Do not merge the separate Studio scope into the native Workbench.

## Latest accepted P0 checkpoint — 2026-10-16 (WIB)

- **Main HEAD after integration:** `aae37327d8e9c40314f4a86b6127a12d38248420` (merge of #187). This supersedes the older baseline in the historical section below; always re-query GitHub before further writes.
- **[PR #185](https://github.com/masarray/arstack61850/pull/185) MERGED** at `8fba05d19e02d94af333fae02313f7430a6696e1`, exact PR head `3903d88623a4dfe66320967d0da8bd8c512dc7e7`, **12/12 exact-head workflow SUCCESS** and no unresolved reviews. Native Workbench exposes immutable measured GetNameList/GVAA/DataSetDirectory/RCB Read confirmed-request counts on completed live discovery. No additional wire traffic, GUI polling, or protocol concurrency. Post-merge CI should be verified separately at the merge SHA; exact-PR evidence is not an installed-package or field IED test.
- **[PR #187](https://github.com/masarray/arstack61850/pull/187) MERGED** at `aae37327d8e9c40314f4a86b6127a12d38248420`, exact PR head `882dd307a2452ca3c0d62f9d3779e8588a5dc7db`, **13/13 exact-head workflow SUCCESS** (GCC, Clang, MSVC, native Qt, interoperability, security, naming guard), zero unresolved review threads. Fixes a confirmed-response integrity hazard: an out-of-order/unexpected invoke ID or an idle-orphan cached response must not acknowledge a future, unsent request. The serial runtime faults and closes the association instead of replaying stale evidence. Added two targeted MMS runtime negative tests; all 48 CTest cases passed on Linux GCC and Clang. No Write/Control/GI/RCB enable, extra Read, new polling, or naive concurrency.
- **Transport ownership audit:** `exchange_confirmed`, `poll_once`, and `try_poll_once_for` call a synchronous association-owned receive primitive. The thread-safe invoke queue is **not** a thread-safe transport dispatcher; no safe simultaneous confirmed exchange or receive route exists. The negotiated `maximum_outstanding_calling` is decoded and retained but not yet enforced as an in-flight window. InformationReport routing remains separately bounded; cancellation, timeout, reconnect, and per-exchange send receipt must retain their established semantics. Detailed source audit is recorded in [PR #187 discussion](https://github.com/masarray/arstack61850/pull/187).
- **Next P0 engineering slice:** Only after an explicit association-owned send/receive dispatcher, invoke lifecycle registration, bounded negotiated concurrency, frame-write serialization, cancellation/reconnect fences and deterministic model parity tests exist, consider opt-in concurrent GetNameList chains. Prefer a measured non-concurrent optimization or additional negative regression first. Compare exact LD/LN/DO/DA, ordered FCDA, RCB, type coverage, wire-budget, failure behavior, CPU/latency and time-to-first-model on the same device.
- **Acceptance still open:** post-merge exact main check completion, installed Windows Workbench verification, real relay/IED trials, and hardware ESP32-P4/SV/PTP qualification. Native Workbench `apps/ied_simulator` and separate Studio `apps/arstack_studio` remain distinct products. No physical interop or speedup claim has been established.

## P0 MMS Discovery interoperability and failure containment — 2026-10-16

- **Integrated baseline**: main `6cc64b384b643fe85b71b9e2082daa1e805766c8`. Source remains the portable IEC 61850 C++ engine under `src/mms/` and `include/ariec61850/mms/`, consumed by the native Qt Workbench; the ESP32-P4 SV Studio is a separate product.
- **[PR #182](https://github.com/masarray/arstack61850/pull/182) MERGED**: preserve case-sensitive MMS references and add bounded, opt-in live DataSet-backed ST/MX LN type-root closure. Do not duplicate this feature or claim it is universally enabled for the GUI.
- **[PR #183](https://github.com/masarray/arstack61850/pull/183) MERGED** at `0081a52d73036fb4ebeec7be63a2d6b2d88cb060`: per-service confirmed-request wire budget from a *per-exchange runtime send receipt*. The receipt flips only after full request transmission; unsuccessful responses still count as sent, while pre-send fault/disconnect attempts do not. Exact PR head `4c2dee88d718c25d9ca245f32a935b46842f2676`, **13/13 CI SUCCESS** including Qt, GCC, Clang, MSVC, naming guard and negative failure tests.
- **[PR #184](https://github.com/masarray/arstack61850/pull/184) MERGED** at `6cc64b384b643fe85b71b9e2082daa1e805766c8`: stop all non-productive optional discovery probes once the accepted MMS association becomes unusable; retain **all domains observed from VMD GetNameList** before stopping, plus successfully received LN evidence, but no fabricated values/children from unattempted probes. Exact PR head `2539da2f96966b048990b63b42c2c2633f46fede`, **12/12 CI SUCCESS** including [native Qt 38812887713](https://github.com/masarray/arstack61850/actions/runs/38812887713) 55 steps and [C++ CI 38812887750](https://github.com/masarray/arstack61850/actions/runs/38812887750) on GCC/Clang/MSVC; all review threads resolved.
- **Post-merge evidence**: Product Naming Guard on #184 main SUCCESS; other main workflows were still IN PROGRESS at this checkpoint. PR CI green does not prove actual field IED interoperability, Windows installed package equivalence, or parallel MMS transport performance.
- **Next performance/interop task**: record request budgets and semantic model fingerprints on the *same device/SCL* under repeatable conditions before comparing serial and optimized discovery. The existing `exchange_confirmed` performs synchronous send/receive; **do not** launch concurrent callers naïvely. A negotiated-outstanding pipeline requires one association-owned receive dispatcher, exact invoke-ID correlation, bounded in-flight window (never exceeding peer `maxOutstandingCalling`), sequential pages within each continuation chain, cancellation, failure containment, deterministic canonical publication, and a prove-no-regression baseline. Keep conservative mode when evidence is incomplete.
- **Next native user acceptance**: correctly expose/enable optional DataSet-backed closure where proven and explicitly requested, plus physical relay trials for truncated NameList, identity conflict, RCB/SG/Files, source generation, and rendering. Do not treat this source integration as an installed Workbench or physical IED certification.

## Latest verified native Workbench integration — 2026-10-15

- Main source: `1e6ff57be39badd8d1d297b4301060ed3835984e`, M2M [PR #180](https://github.com/masarray/arstack61850/pull/180) MERGED; exact-head Qt [38714259031](https://github.com/masarray/arstack61850/actions/runs/38714259031) SUCCESS (55 steps), Product Naming Guard [38714259041](https://github.com/masarray/arstack61850/actions/runs/38714259041) SUCCESS, zero unresolved reviews.
- M2M [PR #179](https://github.com/masarray/arstack61850/pull/179) MERGED at `ce643e81cd7640ed2b694c0914282177f871a9fa` with 14/14 exact-head workflows SUCCESS; selected canonical DataSet DA inspector and tracked-source naming guard.
- Documentation strategy [PR #181](https://github.com/masarray/arstack61850/pull/181) MERGED at `01188097c9551102a264b5d140597d1aea48d7b4`; older instructions to merge M2K/M2L are superseded.
- Canonical C++ owners remain the runtime SSOT. ARIEC61850 provides demonstrated engine semantics to port only when missing; ARSAS provides native Explorer interaction patterns, never a second WPF/.NET/web stack.
- NEXT: genuine selected-FCDA DataSets -> Data Model -> explicit Read / Watch DA click-through rendered Windows acceptance, reconnect and cross-IED isolation; then M2N Reporting/RCB effective-state and real frame mapping. Physical IED and Windows installed-package parity remain UNVERIFIED.
- Post-merge Qt and Windows Release Hardening on this main SHA were IN PROGRESS at this checkpoint; do not mark PASS without exact run evidence.

## Historical milestone notes (accepted or superseded; do not execute old PR integration instructions)
## M2M native DataSets -> Data Model handoff slice — 2026-10-15

- Branch `feat/workbench-m2m-dataset-da-actions-context-20261015` based on merged #179 main `ce643e81cd7640ed2b694c0914282177f871a9fa`. Keep this section IN PROGRESS until exact-head CI/merge checks.
- The selected DataSet attribute inspector gains **Data Model ›** (reuse existing canonical selection and explicit Read action) and **Watch DA** (reuse per-IED bounded Global Data addSelectedData), following ARSAS Explorer's short-path navigation without copying .NET/WPF or adding a protocol engine.
- Context replacement invalidates DataSets' expanded FCDA children and selected DA evidence even when the next IED reuses the same IEC reference. Any action is disabled without a readable selected DA.
- No extra MMS request or timer; no duplicate SCL/MMS tree. Existing stable indexed 64-child FC-matched DA projection and report-first behavior stay untouched.
- Native Qt QML smoke / 60k large model / Browser convergence and exact-head SCL-assisted gates required; selected FCDA physical evidence and standalone installed Windows UI parity remain open.

## Active native parity + repository naming work — 2026-10-15

- Work branch: feat/native-workbench-reference-hygiene-arsas-parity-20261015; base main c8aabf292c370993950b8682718cd56b77f2cabf. IN PROGRESS until exact-head CI and merge.
- Removed prohibited third-party names from source comments, tests and current docs; indexed RCB fixture tests now derive IED/domain identity from validated references instead of a hard-coded vendor label.
- A lightweight Product Naming Guard workflow checks all tracked source/path names on PR and push.
- Engine reference map: [ARIEC61850_ARSAS_NATIVE_PORT_MAP.md](ARIEC61850_ARSAS_NATIVE_PORT_MAP.md). ARIEC C# behavior is a reference for missing protocol capabilities; canonical C++/Qt is the runtime SSOT.
- M2M native UX: DataSets selected attribute inspector now shows ordinary ST/MX/CO DA value, read-state, FC, Quality, IED Timestamp, MMS type and provenance. No implicit MMS read.
- Post-M2L main [Qt 38701682484](https://github.com/masarray/arstack61850/actions/runs/38701682484), [SCL 38701682271](https://github.com/masarray/arstack61850/actions/runs/38701682271) and [Release Hardening 38701682258](https://github.com/masarray/arstack61850/actions/runs/38701682258): all SUCCESS.
- Remaining gate: PR exact-head Qt + source review and selected-state screenshot QA. Do not claim physical IED or Windows installer acceptance from synthetic CI.

## Verified current integration checkpoint — 2026-10-15 (WIB)

**Source baseline at acceptance: main `9cc9bda9c30804be343d49367e4dde6948789d95`.** Refresh live GitHub before further edits. Subsequent documentation-only commits may advance main without changing Workbench source.

| Milestone | Integrated status | Exact-head evidence |
| --- | --- | --- |
| **M2K — ARSAS native All-IED live grid** | **MERGED** [PR #176](https://github.com/masarray/arstack61850/pull/176), squash `0cc882022d20f416a33433d5f29a1f8950984d85`; no conflict with independently merged native Studio/PTP #175 | PR head `5c5e425c20acb1b849399f39bf62da0f68c0942b`: [Qt 38678320479](https://github.com/masarray/arstack61850/actions/runs/38678320479) and [SCL 38678320482](https://github.com/masarray/arstack61850/actions/runs/38678320482) SUCCESS. Post-M2K main [Qt 38698517002](https://github.com/masarray/arstack61850/actions/runs/38698517002), [SCL 38698516965](https://github.com/masarray/arstack61850/actions/runs/38698516965), [Release Hardening 38698517080](https://github.com/masarray/arstack61850/actions/runs/38698517080) **all SUCCESS** |
| **M2L — canonical measurement semantics & provenance** | **MERGED** [PR #178](https://github.com/masarray/arstack61850/pull/178), squash `9cc9bda9c30804be343d49367e4dde6948789d95`. Five changed paths, one canonical SSOT, native inspector and actual C++ regression | PR head `278271d7ca316d154113c1c8968339eea3a83e94`: [Qt 38700261642](https://github.com/masarray/arstack61850/actions/runs/38700261642) **SUCCESS (55 successful steps)**; [SCL 38700261577](https://github.com/masarray/arstack61850/actions/runs/38700261577) **SUCCESS**; no unresolved review. Post-M2L main [Qt 38701682484](https://github.com/masarray/arstack61850/actions/runs/38701682484), [SCL 38701682271](https://github.com/masarray/arstack61850/actions/runs/38701682271), [Release Hardening 38701682258](https://github.com/masarray/arstack61850/actions/runs/38701682258) **IN PROGRESS at this checkpoint**, not claimed PASS |

### Canonical M2L invariants accepted

- `MmsLiveTreeModel` is the only value/measurement owner; no additional MMS parser, q/t decoder, cache, or timer.
- Only exact-authority CDC `CMV` + received finite numeric MX `cVal.mag.f/i` and `cVal.ang.f/i` exposes vector and angle. Scalar `MV/SAV` `mag.f/i` or `instMag.f/i` has no fabricated angle; `Hz` and `TotW` stay scalar even with stray `ang.f`. Boolean remains non-measurement. No guessed unit, multiplier, or degree symbol.
- Uses existing `referenceIndex_` exact IEC aliases for constant-number measurement lookups rather than rescanning all DO siblings for each projected DA. No O(whole-tree) hot-path lookup.
- q/t companions require actual received state and matching FC. DO preview now carries real received-value and exact value-source reference, including empty-string successful MMS reads.
- Source replacement clears evidence. Malformed numeric angle, untrusted CDC, wrong-FC companion, and integer AnalogueValue alternatives have deterministic regression in the existing 60k QA.
- Historical #172/#164 and legacy Browser recovery PRs remain **unmerged candidates**. Never automatically merge them; #175 native Studio/PTP remains a distinct track and was preserved.

### Next engineering milestones (owners)

1. **M2M — Native Data Model/DataSets GUI/UX** on merged M2L canonical fields: selected attribute, FC/IEC aliasing, lazy DO→DA and DataSet member ordering; actual native-rendered selected-state QA; strict no implicit MMS Read on expanding tree.
2. **M2N — Reporting/RCB UX** stays with the existing `MmsReportController` and ordered-report-member projection; no cyclic polling in static report mode.
3. **M2O/M2P — SG, Files, GOOSE, acceptance** need independent service-owner testing, Windows installed Workbench lineage and physical IED interop. Synthetic Qt CI is **not** proof of physical interoperability or installer parity.
4. **Track D** must refresh and validate post-M2L main CI above before claiming fully qualified integrated CI. Subsequent work should not rewrite M2K/M2L canonical ownership without a regression-backed need.

## Historical checkpoint — before M2K/M2L (archived, superseded by verified section above)

| Item | Verified at this snapshot |
| --- | --- |
| Repo / production branch | [masarray/arstack61850](https://github.com/masarray/arstack61850), main |
| main before documentation-only update | [a328da13133a6473dc6a28a69f5d2aebab7a7198](https://github.com/masarray/arstack61850/commit/a328da13133a6473dc6a28a69f5d2aebab7a7198) |
| M2G | PR [#169](https://github.com/masarray/arstack61850/pull/169) **already superseded by merged later work**; old “fix PR #169” instructions are obsolete |
| M2H / M2I / M2J | PR [#171](https://github.com/masarray/arstack61850/pull/171), [#173](https://github.com/masarray/arstack61850/pull/173), [#174](https://github.com/masarray/arstack61850/pull/174) **merged** |
| M2J post-merge | Exact-main Qt, SCL-assisted Connect/Workbench and Release Hardening **success** |
| **M2K live work** | PR [#176](https://github.com/masarray/arstack61850/pull/176), head **5c5e425c20acb1b849399f39bf62da0f68c0942b** when checked |
| M2K SCL-assisted | [run 38678320482](https://github.com/masarray/arstack61850/actions/runs/38678320482) **success** on that head |
| M2K native Qt | [run 38678320479](https://github.com/masarray/arstack61850/actions/runs/38678320479) **in progress** when last checked; do not assume a merge or success |
| Parallel/legacy PRs | #172, #164, older Browser recovery; evaluate against current main, do not auto-merge duplicate work. PTP/SMV/Studio PRs are separate |

## No-regression source of truth

1. Open SCL and live discovery converge in the existing IedEngineeringContextController and canonical MmsLiveTreeModel; index IEC/MMS references instead of reparsing or duplicating trees.
2. MmsClientController is the only confirmed Read/write path; MmsReportController owns report/RCB states; MmsEngineeringEvidence.hpp is the shared q/t decoder.
3. DA readable does **not** imply a sample was received. Preserve actual empty-string sample vs NOT READ, unknown quality/time, and generation/association provenance. No stale evidence after reconnect/source replacement.
4. Report member reference and evidence must map to exact received frame ordinal and actual Report ID. Static reporting remains report-only without implicit periodic MMS Read.
5. Preserve verified bounds: 60k+ node large-model regression, 4,096 detail rows, 64 lazy FCDA DAs, 64 explicit Read targets, 256 per-IED watched objects, 2,048 All-IED rendered objects, 256-char All-IED preview, 256 visible report members, 1,024-char report preview, 96 initial typed q/t details. These are presentation bounds; do not drop authoritative frames.
6. Scalar Hz and total power (TotW) do not acquire an “angle” through a generic presentation shortcut; only display units/phasor components proven by typed metadata.
7. Never claim pixel-level reference IEC 61850 client/ARSAS parity, latest installer parity, or real physical IED interop on synthetic fixtures alone.

## Historical action and owners (superseded; do not execute stale PR merge instructions)

**Integration owner (first):** re-query main, PR #176 head, exact-head Qt result, review threads. Fix failures in **that PR**, not in a new duplicate. If green, squash-merge with expected-head lease, verify main integration and update this table.

**Canonical/SSOT owner (second):** M2L Measurement semantics and value/q/t/unit/angle provenance on the existing MMS model. Tests for MX vector, scalar Hz, TotW, malformed q/t, unset value, SCL-vs-live authority and source-generation mismatch first.

**QML/UX owner (parallel on disjoint files):** compare ARSAS Explorer live grid, selected-state reference IEC 61850 client screenshots and ARStack Qt Data Model/DataSets/Reports/SG. Build a screen-state matrix and deterministic native captures; do not change the canonical model or edit #176's All-IED files concurrently.

**Protocol/RCB owner:** preserve exact Reporting contract ([RCB_REPORTING_REFERENCE.md](RCB_REPORTING_REFERENCE.md)); no new polling, invented report values or uncontrolled writes.

**QA/CI owner:** source-contract, actual rendered selected-state screenshots, 60k-node stress, 8-IED isolation, oversized metadata, read error and reconnect. Coordinates shared test files with the feature owner; source token checks alone do not establish rendered semantics.

## One-turn resume checklist

1. Read [strategy](WORKBENCH_ARSAS_GUI_SSOT_STRATEGY.md), [AGENTS](../AGENTS.md), [AI_READ_FIRST](../AI_READ_FIRST.md) and [CI policy](CI_WORKFLOW_ROUTING.md).
2. Inspect live main, open PRs and run results, exact affected files and current integration owner. Record base SHA/head SHA before touching a file.
3. Reproduce observed wrong behavior, identify canonical state owner, set typed/value provenance requirements, then make **one small coherent vertical slice**, including failure/large-input regression.
4. Check applicable exact-head CI, review, squash merge with expected_head_sha and re-read main; never call in-progress/skipped CI PASS.
5. Update this handoff with accepted checkpoint and links. Keep separate lifecycle: code merged vs native artifact built vs actual Windows installer vs real IED tested.

## Useful commands (if native Qt toolchain available)

    python3 apps/ied_simulator/test_browser_convergence.py apps/ied_simulator/qml/IedBrowserWorkspace.qml apps/ied_simulator/qml/IedBrowserNavigation.qml apps/ied_simulator/qml/BrowserStatusConsole.qml apps/ied_simulator/qml/BrowserDataModelPane.qml
    cmake -S apps/ied_simulator -B build-ied-simulator-qt -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build-ied-simulator-qt --target arstack_ied_simulator ied_browser_large_model_qa ied_browser_session_qa ied_report_workspace_qa --parallel 2
    ./build-ied-simulator-qt/ied_browser_large_model_qa
    QT_QPA_PLATFORM=offscreen ./build-ied-simulator-qt/arstack_ied_simulator --smoke-test

The full workflow defines the remaining real MMS, P6E Multi-IED and native QA gates. Follow [CI policy](CI_WORKFLOW_ROUTING.md) for cost-efficient targeting.
