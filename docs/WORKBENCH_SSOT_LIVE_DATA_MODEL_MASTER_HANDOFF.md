# P0 MASTER HANDOFF — Native Workbench SSOT Live Data Model (entire application)

> **As-of:** 2026-10-17 WIB, GitHub checkpoint; **NOT a completion certificate**.
> **Scope:** `masarray/arstack61850/apps/ied_simulator` native C++/Qt/QML Workbench. Do **not** confuse with `apps/arstack_studio`, ESP32-P4 process-bus Studio, or separate `masarray/arsas` / `masarray/ARIEC61850` products.
> **Read first:** [AGENTS.md](../AGENTS.md), [AI_READ_FIRST.md](../AI_READ_FIRST.md), [Workbench SSOT strategy](WORKBENCH_ARSAS_GUI_SSOT_STRATEGY.md), [online-model decision](ONLINE_MODEL_CONNECT_DECISION.md), [field acceptance](WORKBENCH_IED_FIELD_GUI_ACCEPTANCE.md).
> **User's overriding priority:** **SSOT Live Data Model throughout the WHOLE Workbench, not another isolated Reports/IED Browser visual polish**. Professional, immediately understandable engineering data with evidence-backed DO/DA/value/q/t, no protocol fiction, no mock live values, no duplicated engine. Do the implementation, tests and integration; do not stop at auditing, proposals, or screenshots.

## 1. The correction in direction

Previous progress overconcentrated on `BrowserReportPane.qml`, reporting member detail rows, and historical GUI parity. This improved readability but **did not establish that every Workbench surface derives its current values, Quality/TimeQuality, freshness and source identity from one authoritative live-data model**.

**Current truthful status: whole-application SSOT Live Data Model is NOT demonstrated complete.** Specifically, source inspection of merged `main` as of this handoff shows:

- `MmsClientController.cpp` applies confirmed Read results through `MmsLiveTreeModel::applyReadValue(...)`.
- `MmsReportController.cpp` builds `ReportUi` snapshots/`valueDetails` from the native report runtime and publishes `reportsChanged`; source inspection did **not** find a corresponding `MmsLiveTreeModel::applyReadValue` ingress from accepted InformationReports. This is an architectural convergence **gap to investigate and fix**, not proof that every report should overwrite a Read value.
- The Data Model, DataSets, Reports, per-IED Global Data and All-IED Global Data use different **view projections**. Presentational projections are legitimate; independent stores of authoritative current values, invented acquisition states or independent semantic IEC interpretations are not.
- GOOSE has an existing, separate passive Ethernet runtime with its own actual wire provenance; do not merge its protocol lifecycle into MMS. A unified **display/provenance contract** may consume it, but must keep transport identity, verified publisher binding, ConfRev and received sample origin explicit.
- The integrated source has bounded and tested pieces; do **not** claim end-to-end value convergence, packaged visual parity, or physical IED validation from unrelated Qt and fixture successes.

The correct P0 is the whole vertical route: **canonical model + one generation-scoped value ingress per actual acquisition → coherent source/provenance states → all actual Workbench consumers → rendered evidence and negative regression**. Do not introduce a new competing semantic model or store just to satisfy this sentence.

## 2. GitHub checkpoint (refresh before work)

- **Latest `main` observed at this handoff:** `ecf42b1b2bcba5c06714b047fa4484f80eed075b` (Studio change on `main`, not new Workbench acceptance). Main may advance independently: query GitHub again.
- **Merged Workbench UX:** [#211](https://github.com/masarray/arstack61850/pull/211) RCB identity/GI evidence; [#212](https://github.com/masarray/arstack61850/pull/212) reporting UI coalescing; [#213](https://github.com/masarray/arstack61850/pull/213) BIT STRING unsigned underflow protection; [#215](https://github.com/masarray/arstack61850/pull/215) idle report snapshot fast path; [#216](https://github.com/masarray/arstack61850/pull/216) Windows source-SHA provenance; [#222](https://github.com/masarray/arstack61850/pull/222) bounded positional member values; [#225](https://github.com/masarray/arstack61850/pull/225) lazy inline report drill-down. PR #225 merged into `main` at `467e064a6cf68dcdca485b0cf87f867215e078ca`.
- **Open P0 attempt:** [PR #230](https://github.com/masarray/arstack61850/pull/230), branch `feat/workbench-semantic-report-model-binding-p0-20261017`, observed exact HEAD `b55452b85f29b59f0dc88e3be12aa5b8b9f264f1`. **Do not merge blindly.** The exact-head [IED Simulator Qt run #39030557345](https://github.com/masarray/arstack61850/actions/runs/39030557345) **FAILED** at step **Browser convergence contract**, with log `BROWSER_REPORT_IDLE_FAIL missing=buildReportUi(worker->report->snapshot())`. Product Naming Guard passed. The latest code review was recorded as **failed/has findings** on that head; review threads may be marked resolved but that is not equivalent to a clean review. Re-run exact-head tests and review after substantive changes.
- **P0 tracking:** [issue #226](https://github.com/masarray/arstack61850/issues/226). This does **not** supersede the app-wide SSOT requirement; it is only the nested typed-report slice.
- Some older Field Kit and release workflows may be open/green at **different SHAs**. Never infer a source-matching Windows package or field result for an unmerged PR. See [CI routing](CI_WORKFLOW_ROUTING.md), [release hardening](../.github/workflows/ied-simulator-release-hardening.yml), and [field acceptance](WORKBENCH_IED_FIELD_GUI_ACCEPTANCE.md).

### Important trap: CI failure vs runtime failure

The exact-head #230 Qt workflow compiled native code and exercised native report QA before stopping at a **stale static Browser convergence assertion** referring to an old `buildReportUi` signature. Fix the regression's expected worker/report snapshot contract **without deleting safety coverage**. After fixing, run the entire workflow; successful compilation is not a substitute for full Qt and UI evidence.

### Important trap: semantic binding is NOT complete SSOT

PR #230 introduces `BrowserReportSemanticBinding.hpp`, native SCL/FCDA guards, selected-leaf q/t projection, and some negative tests. It is useful experimental code **but not a proven end-to-end, nested, vendor-interoperable SSOT**. Verify these real implementation conditions:

1. Canonical Open SCL model has `model.source = "OpenedSCL"` and DA `attribute.source = "OpenedSCL"`; trusted online overlay has different provenance. A gate that only permits `TrustedSclInitialSnapshot` makes the ordinary Open SCL flow silently fall back to `Field N`. Conversely, merely declaring an SCL identity 'exact' does not prove online model identity. Require current endpoint/IED/AP/generation plus actual live validation.
2. DataSet may be sent in IEC dotted or MMS dollar form, or omitted by `OptFlds`. Use the **existing native DataSet parser and verified live RCB directory**, not string guessing. Frame `RptID`, ConfRev, inclusion ordinals, DataRef aliases and FC identity must remain guarded by `MmsReportFrameBindingValidator`.
3. **MMS wire kind is insufficient for ordering** where multiple attributes have the same kind, even if SCL basic-type spellings differ. Wrong/ambiguous cardinality or child order must show explicitly positional/unbound values, not false `stVal/q/t` labels.
4. Quality's IEC BIT STRING length/padding and UTC TimeQuality must be validated by the **existing** `MmsEngineeringEvidence.hpp`; do not add a second decoder. Malformed quality must remain missing/invalid evidence, never 'good'.
5. Projector limits of 32 detail-bearing members × 16 children are presentation limits, not an excuse to discard report frames or to trigger a full-model scan for every 100 ms tick. Cache *bounded indexes of canonical schema*, not duplicate mutable semantics.
6. A source verification that simply compares hostname strings can reject valid endpoint aliases or allow stale model generations; prove the server and engineering-source identity with the existing context lifecycle. **No cross-IED value leakage.**

## 3. Existing SSOT owners — use these, do not replace

| Authority or view | Existing source of truth / consumer | Hard rule |
| --- | --- | --- |
| Engineering model, SCL vs online provenance, selected IED/AP, generation | `IedEngineeringContextController.*`, native `MmsLiveModelDocument` in `include/ariec61850/mms/live_model.hpp` | One selected canonical schema. No new XML DOM-derived parallel signal tree. |
| Indexed LD/LN/DO/DA and current canonical **Read-acquired** samples | `MmsLiveTreeModel.*`, `MmsLiveDetailModel`; `applyReadValue`, `hasReceivedValue`, `engineeringEvidence` | Extend this owner/its existing event path deliberately for other accepted samples; don't fork a Report-only mutable value database. |
| MMS association and requested/confirmed Reads | `MmsClientController.*`, `IedBrowserFleetController.*` | Source-scoped associations, no QML per-row Read, no invented 'LIVE' sample from a readable attribute. |
| Verified RCB lifecycle and **received** InformationReports | `MmsReportController.*`, `MmsReportSubscriptionRuntime`, `MmsStaticReportSessionRuntime`, native `MmsReportFrameBindingValidator` | Existing Report runtime owns wire acceptance and GI/cleanup; accepted values need canonical semantic ingress **only when safely bindable**, without re-reading every FCDA. |
| SCL-assisted typed initial read | `MmsSclClientProjection.hpp`, `InitialFcReadPlanner` / online connect profile | Both SCL-assisted and bare discovery converge to the same model; no second discovery owner. |
| Typed IEC Quality/TimeQuality | `MmsEngineeringEvidence.hpp` | Reuse one decoder, only for wire-verified q/t leaves. |
| DO/DA/Report/DataSet UI | `BrowserDataModelPane.qml`, `BrowserDataSetPane.qml`, `BrowserReportPane.qml` | Views do not own IEC truth. Bound and lazy projections, stable selected member/reference. |
| Per-IED & fleet current-value presentation | `BrowserGlobalDataPane.qml`, `FleetGlobalDataPane.qml`, `Main.qml` | Must consume canonical source identity/state. One IED must not update another IED's row. |
| GOOSE publisher observations | `GooseMonitorController` and existing native Ethernet decoder | Independent transport authority; attach verified publisher provenance, not unsupported MMS-Report semantics. |
| Setting Groups / Files / controls | Existing `MmsFileSettingsController` and guarded native owners | Server reads/readback, source state and explicit writes; no synthetic value cache. |

**Architectural diagram** (ownership, not a call to add middleware):

```text
Open SCL / online discovery / trusted validation
                    |
                    v
   IedEngineeringContextController / MmsLiveModelDocument
                    |
                    +------> MmsLiveTreeModel (canonical typed LD/LN/DO/DA)
                    |                   ^
Confirmed MMS Read --- source/gen/FC ----+
Accepted RCB Report -- RCB/DS/frame ----+  [MISSING/UNPROVEN CONVERGENCE]
                    |                   |
                    v                   v
      verified transport events     current sample/status/q/t
                                          |
       Data Model / DataSets / Reports / Global Data / All-IED
          (bounded projections, no second acquisition truth)

GOOSE: independent wire source -> separately verified publisher observations
       -> presentation may share identity/status conventions, NOT MMS ownership
```

## 4. Non-negotiable observable sample contract

Before touching GUI styling, trace and test one **source-qualified** DA sample across the existing owners. Implement the smallest needed extension to the canonical model and current ingress; do not create yet another `LiveDataStore` as a shortcut.

The runtime/presentation must be able to distinguish at least:

- **Identity:** IED slot + resolved IED/AP/endpoint; canonical IEC reference; typed MMS domain/item and FC; selected DataSet/FCDA ordinal when report-acquired; model/source/generation.
- **Value and type:** original bounded typed MMS value/decoder evidence; actual scalar/vector/enum **only if authoritative**; Quality and IED TimeQuality/timestamp **only if present and valid**; optional units/multipliers from validated engineering source.
- **Acquisition:** `SCL_ONLY`, `DISCOVERED_NOT_READ`, `READ_OK`, `READ_FAILED`, `REPORT_ACCEPTED`, `REPORT_REJECTED`, `OFFLINE`, `STALE_SOURCE` or an equivalent existing explicit state vocabulary. **A valid empty or false value is not NOT READ.**
- **Freshness:** actual device timestamp vs local receipt timestamp vs source generation, without synthetic times; retained reports are historical if transport/subscription is inactive.
- **Failures/limits:** missing optional fields, Quality malformed, split/segmented report, foreign indexed RCB with same RptID, changed ConfRev, aborted Read, stale callback; the outcome must never silently overwrite the last qualified observation.

*Whether a Report updates the canonical latest sample is a policy to design and test*, not a blind `applyReadValue` call: accepted report identity, DataSet member, FC and typed leaf mapping must be verified; keep last acquisition source and avoid degrading a newer qualified sample with older buffered report events.

## 5. Mandatory implementation sequence — milestone that actually moves the product

Do these in order; each item requires code, negative tests, and a consumer-visible result rather than a proposal or only a new document.

### P0-A — Freeze, reproduce, then establish canonical convergence

1. Refresh main, PR #230 and existing owner source. **First fix #230's exact-head regression failure** if reusing that branch; require new clean review, do not waive checks or merge a broken branch. If PR has diverged too far, salvage proven pieces into a smaller new branch from current main and supersede/close #230 explicitly.
2. Instrument/test one opened-SCL IED and one online-discovered IED through actual confirmed Read into `MmsLiveTreeModel`, then one runtime-accepted InformationReport from a verified RCB/DataSet member into **the same canonical value/provenance owner**, with no new MMS Read.
3. Add explicit IED slot/model generation guards, stable canonical IEC↔MMS FCDA identity and states (NOT READ/READ OK/REPORT ACCEPTED/ERROR/OFFLINE). Verify stale callback/disconnect/reconnect/selected-IED switch negative cases. If an incoming Report cannot safely map to a canonical DA, preserve the frame **in Reporting only**, marked unbound; don't poll, drop, or fabricate its semantic value.
4. Establish a test that compares **the same signal** viewed in Data Model, DataSets, Reports, per-IED Global Data and All-IED. The sample value, Quality, device timestamp, source and freshness must agree when all views refer to that accepted ingress.

**P0-A exit:** one actual native sample propagates correctly across **all** consumers, source identity preserved, negative mismatch and offline cases pass. Without this, **do not call SSOT done**.

### P0-B — Semantic DO/DA tree with truthful values

1. Use existing `MmsLiveModelDocument`, `MmsLiveTreeModel` and verified DataSet ordinal. Borrow type/interaction algorithms from `masarray/ARIEC61850` / `masarray/arsas`, **not** their C# or WPF runtime. Existing port map: [ARIEC61850_ARSAS_NATIVE_PORT_MAP.md](ARIEC61850_ARSAS_NATIVE_PORT_MAP.md).
2. Project nested DAs and arrays lazily from authoritative SCL/MMS schema; guard unknown/reordered/duplicate wire kinds, flattening and depth. Label `stVal`, `q`, `t`, etc **only when type and positional binding are proven**; otherwise show a clear positional fallback and reason.
3. Validate enum mappings, CDC semantics, Quality bits, UTC TimeQuality, units and SI multiplier with real type metadata. An integer without enum domain is **not** a symbolic double-point state. Do not infer unit from the UI column, or frequency/power phase angle.
4. Avoid two independent Quality/TimeQuality implementations or uncontrolled QML delegate trees. Reuse existing `MmsEngineeringEvidence.hpp`, bounded worker-side projection and lazy details.

**P0-B exit:** test fixtures for flat, nested and vendor-specific structures, malformed q/t, omitted OptFlds DataSet, IEC/MMS aliases, wrong FC, enum mismatch, ambiguous shape and stale generation all pass.

### P0-C — Operator workflow and measured rendered GUI

Make **all** of Data Model, DataSets, Reports, per-IED and fleet Global Data reflect the same SSOT. Navigation should be comprehensible to an ordinary engineering user: clear LD/LN/DO/DA, value/status/q/t, current RCB, selection, no raw aggregate dump as the only answer; no decorative affordance that does nothing. Reuse proven Qt controls and avoid adding multiple modal or lab-only obstacles.

Compare the **same IED, same selected indexed RCB, same DataSet, same report frame/state**, at the same Windows scaling and resolution, against the project's authorized reference GUI (use generic descriptions in GitHub documentation). The prior screenshots had different selected RCBs, so they demonstrated poor UX but **could not prove wire-value equivalence**.

**P0-C exit:** saved before/after screenshots of *selected* states, keyboard/mouse/scroll/resize tests, engineer can find `stVal/q/t` without interpreting raw BER, and negative/no-data states stay truthful.

### P0-D — Scale, release, and physical evidence

- Retain 60,106-node fixture, 8 IED isolation, bounded model rows, 256-report-member UI budget, bounded monitor frames, coalescing and idle native polling. No full scan of 60k nodes for each report value or QML delegate, and no per-row timer/Read.
- Run exact-head native Qt, Browser regression, MMS/RCB/GOOSE/SCL tests and security/negative checks when touched. Review thread resolution **plus** clean reviewed HEAD before merge. After merge validate main SHA.
- Use `IED Simulator Release Hardening` Windows package from the **integrated main SHA**, installed/portable `BUILD_HEAD.txt`, then permitted real hardware if available. Build/loopback/PCAP replay ≠ confirmed vendor interoperability.
- Use [physical GUI acceptance](WORKBENCH_IED_FIELD_GUI_ACCEPTANCE.md) with recorded vendor/model/firmware, RCB readback and actual accepted reports, GOOSE NIC/VLAN and field screenshots. Mark blocked/not-run honestly.

## 6. Source traps and tests to inspect immediately

- `apps/ied_simulator/test_browser_convergence.py`: source-shape assertions lagged legitimate `buildReportUi` signature changes, causing #230 workflow fail. Keep the substantive guard while adapting syntax; avoid one-off literal checks that misrepresent behavior.
- `apps/ied_simulator/src/ied_report_workspace_qa.cpp`: compare acceptance of valid dotted and `$FC$` MMS DataRef, omitted DatSet, valid/malformed 13-bit Quality and wrong FCDA; ensure fixture truly matches canonical Open SCL provenance.
- `apps/ied_simulator/src/BrowserReportSemanticBinding.hpp`: review exact source forms (`OpenedSCL` vs trusted overlay), duplicate-type ambiguity, DataSet parser, RCB identity and candidate endpoint/generation. This is currently **unmerged experimental code**.
- `apps/ied_simulator/src/MmsReportController.cpp`: verify whether worker copies trusted schema only at connect and whether model replacement/endpoint/RCB change invalidates it; don't consume stale types.
- `apps/ied_simulator/src/MmsLiveTreeModel.*`: inspect `applyReadValue`/engineered evidence/selected value roles, index lookup and generation; add event ingestion here or upstream where models already converge instead of an isolated report inspector cache.
- `apps/ied_simulator/qml/BrowserDataModelPane.qml`, `BrowserDataSetPane.qml`, `BrowserReportPane.qml`, `BrowserGlobalDataPane.qml`, `FleetGlobalDataPane.qml`: test consistent sample identity/values/status—not just QML layout or visible column names.
- `docs/ONLINE_MODEL_CONNECT_DECISION.md`: Open SCL assisted connects should not do an unnecessary full discovery; both online and offline routes converge to the canonical model.
- `docs/CI_WORKFLOW_ROUTING.md`, `.github/workflows/ied-simulator-qt.yml`: always associate test result to **exact PR head** and source files actually tested.

## 7. Stop conditions / explicit handoff to next implementer

A claim of **P0 SSOT COMPLETE** is forbidden unless these independently pass: (1) same qualified sample across all Workbench views; (2) real source/FC/IED/generation binding; (3) correct typed/q/t and UNKNOWN semantics; (4) native negative, large-model and multi-IED tests; (5) rendered installed-GUI inspection; (6) hardware acceptance separately labeled as passed or still blocked.

**Next working action:** correct or replace PR #230 only as a **small part of P0-A**, then implement and prove **accepted Report → canonical live value + acquisition provenance → all consumers**. Do **not** spend further days polishing only the Reports inspector. Do not touch unrelated Studio firmware/SMV/PTP work. Update this handoff with exact SHAs, test links, review state and honest blockers after every genuinely integrated vertical slice.

**Documentation naming constraint:** do not insert the prohibited third-party comparison product name into repository code, issues, PR descriptions, documents, tests or comments. Refer to an 'authorized reference GUI' or the existing ARSAS/ARIEC engineering references instead.
