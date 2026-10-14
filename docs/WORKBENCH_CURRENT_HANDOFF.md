# Native IEC 61850 Workbench — current engineering / AI-thread handoff

> **Scope:** native Qt IEC 61850 Workbench in `apps/ied_simulator` of `masarray/arstack61850`, not the legacy web application.
> **Verified snapshot:** 2026-10-15 (WIB). Always refresh GitHub refs before editing; this document records a specific checkpoint.
> **Mandatory context:** [AGENTS.md](../AGENTS.md), [AI_READ_FIRST.md](../AI_READ_FIRST.md), [PROJECT_HANDOFF.md](../PROJECT_HANDOFF.md), [CI policy](CI_WORKFLOW_ROUTING.md), [RCB reporting contract](../RCB_REPORTING.md).

## A. Live GitHub baseline at the snapshot

| Topic | Verifiable status |
| --- | --- |
| Repository/default branch | [masarray/arstack61850](https://github.com/masarray/arstack61850), `main` |
| Latest *Workbench code* baseline before this docs-only commit | `a4f0abe2536c318b24f2877b491107bca981cd45` |
| M2A Reports layout | PR [#159](https://github.com/masarray/arstack61850/pull/159), merged `6081df29` |
| M2B canonical indexed DataSets | PR [#160](https://github.com/masarray/arstack61850/pull/160), merged `d527c894` |
| M2C Global Data IEC/MMS read | PR [#161](https://github.com/masarray/arstack61850/pull/161), merged `d4374434` |
| M2D-1 typed q/t MMS Read | PR [#163](https://github.com/masarray/arstack61850/pull/163), merged `e749617f` |
| M2D-2 typed q/t RCB reports | PR [#165](https://github.com/masarray/arstack61850/pull/165), merged `af3ced5d` |
| M2E bounded report GUI projection | PR [#167](https://github.com/masarray/arstack61850/pull/167), merged `35eda54d` |
| M2F streaming value preview | PR [#168](https://github.com/masarray/arstack61850/pull/168), **merged** as `a4f0abe2`; Qt run [38644305224](https://github.com/masarray/arstack61850/actions/runs/38644305224) **success** |
| M2G bounding untrusted RCB metadata | PR [#169](https://github.com/masarray/arstack61850/pull/169), **OPEN, RED** at `c6cf52c2b8c37d1f5ca55c9e7399c3a5ad4035aa`; Qt run [38646373741](https://github.com/masarray/arstack61850/actions/runs/38646373741) **failure** |

Merging a docs-only handoff can advance `main` without changing Workbench code. Treat the SHA above as **code checkpoint at snapshot**, not the perpetual HEAD. Many open Draft PRs belong to separate PTP/SMV, earlier Browser recovery or parallel q/t work (#164); do not conflate them or merge obsolete branches casually.

## B. Immediate blocker: fix M2G PR #169, do not open a duplicate

**Exact observed failure:** `BROWSER_REPORT_BOUNDED_PROJECTION_FAIL missing=#include "BrowserBoundedMmsPreview.hpp"previewMmsValue(*member.value`.

GitHub Actions log confirms native Qt **Build passed**, then **Browser convergence source-contract** failed. Root cause is missing comma in `apps/ied_simulator/test_browser_convergence.py`: Python silently concatenates two adjacent string literals in the `projection_header` required-token list.

```python
'#include "BrowserBoundedMmsPreview.hpp"'
'previewMmsValue(*member.value',
```

Add the comma after the first string, **preserve both assertions**, run the targeted test and then the exact-head Qt workflow on PR #169. Do not revert the correct M2F/M2G performance work to appease the stale contract.

PR #169 changes only:
- `apps/ied_simulator/src/BrowserReportMemberProjection.hpp`
- `apps/ied_simulator/src/ied_browser_large_model_qa.cpp`
- `apps/ied_simulator/test_browser_convergence.py`

Its goal: cap huge untrusted DataSet reference strings and Reason-for-Inclusion before QString conversion, cap complete GUI preview including marker, flag truncated identities honestly, and never decode q/t evidence from a truncated reference. Regression includes ~1.5 MB reference/reason strings and many reason entries. **Not yet accepted / merged** at snapshot.

## C. User objective: engineering-grade IEDScout-like GUI/UX

The target is a **native Windows Qt Workbench** resembling reference IEDScout *Global Data, File, Browser, Reports and DataSets* workflow, backed by truthful IEC 61850 data. User values measurable improvements and rejects naive coding, noisy UI, mismatched screenshots and excessive CI.

- `Open SCL` and `Discovery IED` must converge onto **one canonical IED model** and same browsing / static Dataset / reporting pipeline. Do not fork semantic trees or repeat outdated discovery base code.
- Expose clear `Browse IED` and `Dataset Signals` choices. **Static RCB reporting must remain report-only** when selected; never silently mix in cyclic MMS polling.
- LD/LN/DO/DA hierarchy with clean LD labels (avoid repeated IED name in UI), correct per-node value, q/t detail and engineering unit only when source provides it.
- Reports inspector follows `Control Block Attributes → Information Received in Last Report → Data`. Match report frames by **Report ID**, and member evidence by exact **received-frame member ordinal**, not assumed DataSet index.
- DataSets: dense ordered FCDA list, exactly **one FC tag**, lazy FC-filtered DO→DA expansion, no automatic MMS read merely on navigation.
- File download/open, splitters, screen density and pixel-level visual parity still need separate explicit QA. **CI green ≠ screenshot parity or real-device validation.**

Do not confuse this track with the released **native ARStack Studio / ESP32-P4 SMV Injector** (`apps/arstack_studio`) or the separate ARSAS repository. No firmware/PTP/SMV changes are authorized by this GUI handoff.

## D. Ownership and key source paths

| Layer | Authoritative source |
| --- | --- |
| Canonical IEC/MMS DO/DA + indexed IEC/MMS aliases + current value/evidence | `apps/ied_simulator/src/MmsLiveTreeModel.hpp/.cpp` |
| SCL and discovered engineering model | `apps/ied_simulator/src/IedEngineeringContextController.*` |
| Successful typed MMS Read / post-write evidence | `apps/ied_simulator/src/MmsClientController.cpp` |
| **Single** IEC Quality 13-bit / UTC TimeQuality typed decoder | `apps/ied_simulator/src/MmsEngineeringEvidence.hpp` |
| Existing RCB runtime/snapshot projection, ReportID & members | `apps/ied_simulator/src/MmsReportController.cpp` |
| Bounded RCB member presentation, currently modified by M2G | `apps/ied_simulator/src/BrowserReportMemberProjection.hpp` |
| Presentation-only streaming MMS preview (M2F) | `apps/ied_simulator/src/BrowserBoundedMmsPreview.hpp` |
| Native Qt Browser layout | `apps/ied_simulator/qml/IedBrowserWorkspace.qml`; `IedBrowserNavigation.qml` |
| Qt inspectors | `BrowserDataModelPane.qml`, `BrowserDataSetPane.qml`, `BrowserGlobalDataPane.qml`, `BrowserReportPane.qml`, `BrowserEngineeringEvidence.qml` |
| QA | `apps/ied_simulator/test_browser_convergence.py`; `src/ied_browser_large_model_qa.cpp` |
| CI routing | `.github/workflows/ied-simulator-qt.yml` and `docs/CI_WORKFLOW_ROUTING.md` |

**Non-negotiable invariants:**

1. Never fabricate measurements, quality flags, timestamps or RCB state; absent or malformed evidence is `Unknown` / `—`, **not** assumed false.
2. One canonical model / typed decoder; never build second semantic cache, reporting association, implicit polling loop or new q/t parser in QML.
3. Keep full authoritative MMS report frame intact; cap **presentation only**. Established limits: **256 displayed report members**, **1024-character GUI preview**, **96 typed q/t evidence items per snapshot**, **64 DAs per lazy expanded DataSet row**. M2F streams large values before allocating huge intermediary strings; M2G bounds metadata before QString allocation.
4. Evidence from an RCB frame aligns to the actual received member ordinal and Report ID. Failed/untyped values must not show fabricated details. Do not misalign on omitted members.
5. No periodic MMS reads on expand, no protocol/firmware regression, no hidden blocking allocations on hot callbacks. Follow [AGENTS.md](../AGENTS.md).

## E. Mandatory optimized implementation & CI workflow

Use **RECON -> EXACT BASELINE -> ROOT CAUSE -> INVARIANTS -> SMALL COHERENT DESIGN -> IMPLEMENT -> NEGATIVE/LARGE INPUT QA -> ONE COMMIT -> TARGETED EXACT-SHA CI -> REVIEW/MERGE**.

For the current Browser source contract, run locally if code/toolchain is available:

```bash
python3 apps/ied_simulator/test_browser_convergence.py \
  apps/ied_simulator/qml/IedBrowserWorkspace.qml \
  apps/ied_simulator/qml/IedBrowserNavigation.qml \
  apps/ied_simulator/qml/BrowserStatusConsole.qml \
  apps/ied_simulator/qml/BrowserDataModelPane.qml

cmake -S apps/ied_simulator -B build-ied-simulator-qt -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-ied-simulator-qt \
  --target arstack_ied_simulator ied_browser_large_model_qa ied_browser_session_qa ied_report_workspace_qa \
  --parallel 2
./build-ied-simulator-qt/ied_browser_large_model_qa
QT_QPA_PLATFORM=offscreen ./build-ied-simulator-qt/arstack_ied_simulator --smoke-test
```

See the actual workflow for all required CI gates, including MMS data-plane/GUI checks. Do not claim success based solely on Build, skipped checks, or generic screenshots.

- Fix open PR **#169 first**. Inspect **current** PR head before pushing (other threads may advance it). Commit to the **same PR**, not a duplicate. Never force-push or overwrite parallel work.
- Prefer one coherent commit / at most necessary regression correction, with local preflight before expensive CI. Avoid build storms and unrelated ESP-IDF, Studio installer, C++ matrices.
- If CI red, inspect exact failed job logs. Fix real code/test cause without reverting correct architecture. Verify exact-head and required checks before squash merge.
- Keep Windows artifact lineage honest: earlier M2D-1 ZIP is **not** latest M2F/M2G. Physical IED q/t and full IEDScout GUI parity remain **unverified** until actually tested.

## F. Next-step acceptance sequence

1. Requery `main`, PR #169 status/head and latest Actions; do not assume snapshot still current.
2. If still red due missing comma, fix the Python token-list typo, run Browser contract, exact-head Qt CI; verify 1.5 MB metadata tests, no eager QString conversion, correct q/t provenance, preview bounds.
3. Once CI success, inspect PR #169 diff, squash-merge with `expected_head_sha` and verify new `main` integration.
4. Select **one** meaningful next milestone: exact-screen visual regression for DataSets/Data Model/Quality/TimeQuality versus provided IEDScout references; or measured value/report memory/latency profiling. Do not promise pixel parity without screenshots and Windows/IED evidence.
5. Separate known-good and unfinished behavior in progress reports. New thread must **perform repository actions and provide verifiable PR/commit/CI links**, not merely restate strategy.

**Status at snapshot:** M2A–M2F merged; M2G open/CI red; IEDScout GUI parity, full real-IED q/t interoperability and latest Windows installer not yet accepted.
