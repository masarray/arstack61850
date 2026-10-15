# Native Qt IEC 61850 Workbench — current cross-thread handoff

> **Updated:** 2026-10-15 (WIB), snapshot only; always refresh GitHub refs and PR state before editing.
> **Primary strategy and ownership contract:** [WORKBENCH_ARSAS_GUI_SSOT_STRATEGY.md](WORKBENCH_ARSAS_GUI_SSOT_STRATEGY.md).
> **Scope:** installed native ARStack Workbench in apps/ied_simulator. Not a web app, WPF clone, or the separate ESP32-P4/SMV Studio track.

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
